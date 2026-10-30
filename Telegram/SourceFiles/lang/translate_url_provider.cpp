/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lang/translate_url_provider.h"

#include "spellcheck/platform/platform_language.h"
#include "ui/text/text_html_tags.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Ui {
namespace {

[[nodiscard]] bool SkipJsonKey(const QString &key) {
	return (key.compare(u"code"_q, Qt::CaseInsensitive) == 0);
}

[[nodiscard]] QString DetectFromLanguage(const QString &text) {
#ifndef TDESKTOP_DISABLE_SPELLCHECK
	const auto result = Platform::Language::Recognize(text);
	return result.known() ? result.twoLetterCode() : u"auto"_q;
#else // TDESKTOP_DISABLE_SPELLCHECK
	return u"auto"_q;
#endif // !TDESKTOP_DISABLE_SPELLCHECK
}

[[nodiscard]] QString JsonValueToText(const QJsonValue &v) {
	switch (v.type()) {
	case QJsonValue::Null: return u"null"_q;
	case QJsonValue::Bool: return v.toBool()
		? u"true"_q
		: u"false"_q;
	case QJsonValue::Double: return QString::number(v.toDouble(), 'g', 15);
	case QJsonValue::String: return v.toString().trimmed();
	case QJsonValue::Array: return QString::fromUtf8(
		QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
	case QJsonValue::Object: return QString::fromUtf8(
		QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
	case QJsonValue::Undefined: return QString();
	}
	return QString();
}

[[nodiscard]] std::optional<QString> ParseSegmentedArrayResponse(
		const QJsonDocument &parsed) {
	if (!parsed.isArray()) {
		return std::nullopt;
	}
	const auto root = parsed.array();
	if (root.isEmpty() || !root[0].isArray()) {
		return std::nullopt;
	}
	const auto segments = root[0].toArray();
	auto translated = QString();
	for (const auto &segmentValue : segments) {
		if (!segmentValue.isArray()) {
			return std::nullopt;
		}
		const auto segment = segmentValue.toArray();
		if (segment.isEmpty()) {
			return std::nullopt;
		}
		if (!segment[0].isString()) {
			return std::nullopt;
		}
		translated += segment[0].toString();
	}
	if (translated.trimmed().isEmpty()) {
		return std::nullopt;
	}
	return translated;
}

struct JsonLine {
	QString name;
	QString value;
	int length = 0;
};

void CollectJsonLines(
		const QString &name,
		const QJsonValue &value,
		std::vector<JsonLine> &lines) {
	if (value.isObject()) {
		const auto object = value.toObject();
		for (auto i = object.constBegin(); i != object.constEnd(); ++i) {
			if (SkipJsonKey(i.key())) {
				continue;
			}
			CollectJsonLines(
				name.isEmpty() ? i.key() : (name + '.' + i.key()),
				i.value(),
				lines);
		}
		return;
	}
	if (value.isArray()) {
		const auto array = value.toArray();
		for (auto i = 0; i != array.size(); ++i) {
			CollectJsonLines(
				u"%1[%2]"_q.arg(name).arg(i),
				array.at(i),
				lines);
		}
		return;
	}
	const auto text = JsonValueToText(value);
	if (text.isEmpty()) {
		return;
	}
	lines.push_back(JsonLine{
		.name = name,
		.value = text,
		.length = int(text.size()),
	});
}

[[nodiscard]] std::optional<QString> FormatJsonResponse(
		const QByteArray &body) {
	auto error = QJsonParseError();
	const auto parsed = QJsonDocument::fromJson(body, &error);
	if (error.error != QJsonParseError::NoError) {
		return std::nullopt;
	}
	if (const auto parsedArray = ParseSegmentedArrayResponse(parsed)) {
		return parsedArray;
	}
	auto lines = std::vector<JsonLine>();
	if (parsed.isObject()) {
		const auto object = parsed.object();
		for (auto i = object.constBegin(); i != object.constEnd(); ++i) {
			if (SkipJsonKey(i.key())) {
				continue;
			}
			CollectJsonLines(i.key(), i.value(), lines);
		}
	} else if (parsed.isArray()) {
		const auto array = parsed.array();
		for (auto i = 0; i != array.size(); ++i) {
			CollectJsonLines(u"[%1]"_q.arg(i), array.at(i), lines);
		}
	}
	if (lines.empty()) {
		return QString::fromUtf8(body);
	}
	ranges::sort(lines, [](const JsonLine &a, const JsonLine &b) {
		return (a.length != b.length)
			? (a.length > b.length)
			: (a.name < b.name);
	});
	auto result = QString();
	result.reserve(lines.size() * 16);
	for (auto i = 0; i != int(lines.size()); ++i) {
		const auto &line = lines[i];
		if (!line.name.isEmpty()) {
			result += line.name;
			result += '\n';
		}
		result += line.value;
		if (i + 1 != int(lines.size())) {
			result += "\n\n";
		}
	}
	return result;
}

class UrlTranslateProvider final : public TranslateProvider {
public:
	explicit UrlTranslateProvider(QString urlTemplate)
	: _urlTemplate(std::move(urlTemplate)) {
	}

	[[nodiscard]] bool supportsMessageId() const override {
		return false;
	}

	void request(
			TranslateProviderRequest request,
			LanguageId to,
			Fn<void(TranslateProviderResult)> done) override {
		if (request.text.text.isEmpty()) {
			done(TranslateProviderResult{
				.error = TranslateProviderError::Unknown,
			});
			return;
		}
		if (request.text.entities.empty()) {
			requestPlain(request.text.text, to, [=](
					std::optional<QString> text) {
				auto result = TranslateProviderResult();
				if (text) {
					result.text = TextWithEntities{ *text };
				} else {
					result.error = TranslateProviderError::Unknown;
				}
				done(std::move(result));
			});
			return;
		}
		requestFormatted(std::move(request.text), to, std::move(done));
	}

private:
	[[nodiscard]] static bool KeepAsIs(EntityType type) {
		switch (type) {
		case EntityType::CustomEmoji:
		case EntityType::Code:
		case EntityType::Pre:
		case EntityType::Url:
		case EntityType::Email:
		case EntityType::Phone:
		case EntityType::Mention:
		case EntityType::MentionName:
		case EntityType::Hashtag:
		case EntityType::Cashtag:
		case EntityType::BotCommand:
			return true;
		default:
			return false;
		}
	}

	void requestFormatted(
			TextWithEntities text,
			LanguageId to,
			Fn<void(TranslateProviderResult)> done) {
		const auto size = int(text.text.size());
		auto bounds = std::vector<int>{ 0, size };
		for (const auto &entity : text.entities) {
			bounds.push_back(std::clamp(entity.offset(), 0, size));
			bounds.push_back(std::clamp(
				entity.offset() + entity.length(),
				0,
				size));
		}
		ranges::sort(bounds);
		bounds.erase(ranges::unique(bounds), bounds.end());

		struct Segment {
			int from = 0;
			int till = 0;
			QString result;
			bool translate = false;
		};
		struct State {
			TextWithEntities original;
			std::vector<int> bounds;
			std::vector<Segment> segments;
			int left = 0;
			bool failed = false;
			Fn<void(TranslateProviderResult)> done;
		};
		const auto state = std::make_shared<State>();
		state->original = std::move(text);
		state->bounds = bounds;
		state->done = std::move(done);
		for (auto i = 0; i + 1 < int(bounds.size()); ++i) {
			auto segment = Segment{ bounds[i], bounds[i + 1] };
			const auto part = state->original.text.mid(
				segment.from,
				segment.till - segment.from);
			segment.result = part;
			auto keep = false;
			for (const auto &entity : state->original.entities) {
				if (KeepAsIs(entity.type())
					&& entity.offset() <= segment.from
					&& entity.offset() + entity.length() >= segment.till) {
					keep = true;
					break;
				}
			}
			segment.translate = !keep && ranges::any_of(part, [](QChar c) {
				return c.isLetter();
			});
			state->segments.push_back(std::move(segment));
		}

		const auto finish = [=] {
			if (state->failed) {
				requestPlain(state->original.text, to, [=](
						std::optional<QString> text) {
					auto result = TranslateProviderResult();
					if (text) {
						result.text = TextWithEntities{ *text };
					} else {
						result.error = TranslateProviderError::Unknown;
					}
					state->done(std::move(result));
				});
				return;
			}
			auto positions = base::flat_map<int, int>();
			auto result = TextWithEntities();
			for (const auto &segment : state->segments) {
				positions.emplace(segment.from, int(result.text.size()));
				result.text.append(segment.result);
			}
			positions.emplace(
				int(state->original.text.size()),
				int(result.text.size()));
			const auto map = [&](int position) {
				const auto i = positions.find(position);
				return (i != end(positions)) ? i->second : 0;
			};
			for (const auto &entity : state->original.entities) {
				const auto from = map(entity.offset());
				const auto till = map(entity.offset() + entity.length());
				if (till > from) {
					result.entities.push_back(EntityInText(
						entity.type(),
						from,
						till - from,
						entity.data()));
				}
			}
			state->done(TranslateProviderResult{ .text = std::move(result) });
		};

		for (auto &segment : state->segments) {
			if (segment.translate) {
				++state->left;
			}
		}
		if (!state->left) {
			finish();
			return;
		}
		for (auto i = 0; i != int(state->segments.size()); ++i) {
			auto &segment = state->segments[i];
			if (!segment.translate) {
				continue;
			}
			const auto &part = segment.result;
			auto lead = 0;
			while (lead < part.size() && part[lead].isSpace()) {
				++lead;
			}
			auto trail = 0;
			while (trail < part.size() - lead
				&& part[part.size() - 1 - trail].isSpace()) {
				++trail;
			}
			const auto prefix = part.left(lead);
			const auto suffix = part.right(trail);
			const auto core = part.mid(lead, part.size() - lead - trail);
			requestPlain(core, to, [=](std::optional<QString> text) {
				if (text) {
					state->segments[i].result = prefix
						+ text->trimmed()
						+ suffix;
				} else {
					state->failed = true;
				}
				if (!--state->left) {
					finish();
				}
			});
		}
	}

	void requestPlain(
			const QString &text,
			LanguageId to,
			Fn<void(std::optional<QString>)> done) {
		auto url = _urlTemplate;
		const auto from = DetectFromLanguage(text);
		const auto toCode = to.twoLetterCode();
		url.replace(
			u"%q"_q,
			QString::fromLatin1(
				QUrl::toPercentEncoding(
					TextUtilities::EscapeForHtml(text))));
		url.replace(
			u"%f"_q,
			QString::fromLatin1(QUrl::toPercentEncoding(from)));
		url.replace(
			u"%t"_q,
			QString::fromLatin1(QUrl::toPercentEncoding(toCode)));
		const auto requestUrl = QUrl(url);
		if (!requestUrl.isValid()) {
			done(std::nullopt);
			return;
		}
		auto networkRequest = QNetworkRequest(requestUrl);
		const auto reply = _network.get(networkRequest);
		QObject::connect(reply, &QNetworkReply::finished, [=] {
			if (reply->error() != QNetworkReply::NoError) {
				done(std::nullopt);
			} else {
				const auto body = reply->readAll();
				done(FormatJsonResponse(body).value_or(
					QString::fromUtf8(body)));
			}
			reply->deleteLater();
		});
	}

	const QString _urlTemplate;
	QNetworkAccessManager _network;

};

} // namespace

std::unique_ptr<TranslateProvider> CreateUrlTranslateProvider(
		QString urlTemplate) {
	return std::make_unique<UrlTranslateProvider>(std::move(urlTemplate));
}

} // namespace Ui
