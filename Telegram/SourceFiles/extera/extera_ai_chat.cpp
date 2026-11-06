#include "extera/extera_ai_chat.h"

#include "extera/extera_config.h"
#include "base/unixtime.h"
#include "extera/exd_ai_chat_section.h"
#include "settings.h"
#include <QtCore/QFile>
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Extera {
namespace {

const auto kModels = std::array{
	u"gemini-2.5-flash"_q,
	u"gemini-2.5-pro"_q,
	u"gemini-2.5-flash-lite"_q,
};

struct Message {
	bool user = false;
	bool error = false;
	QString text;
	TimeId date = 0;
};

[[nodiscard]] QString ErrorFrom(const QByteArray &data) {
	const auto document = QJsonDocument::fromJson(data);
	const auto object = document.isArray()
		? document.array().first().toObject()
		: document.object();
	const auto message = object.value(u"error"_q).toObject()
		.value(u"message"_q).toString();
	if (!message.isEmpty()) {
		return message;
	}
	const auto blocked = object.value(u"promptFeedback"_q).toObject()
		.value(u"blockReason"_q).toString();
	if (!blocked.isEmpty()) {
		return u"Blocked: "_q + blocked;
	}
	const auto candidates = object.value(u"candidates"_q).toArray();
	if (!candidates.isEmpty()) {
		const auto reason = candidates[0].toObject()
			.value(u"finishReason"_q).toString();
		if (!reason.isEmpty() && reason != u"STOP"_q) {
			return u"Finish reason: "_q + reason;
		}
	}
	return QString();
}

struct State {
	std::vector<Message> history;
	QNetworkAccessManager *manager = nullptr;
	QString partial;
	bool waiting = false;
};

State &Chat() {
	static auto result = State();
	return result;
}


[[nodiscard]] QString ModelName() {
	const auto index = std::clamp(
		Int(u"ai_chat_model"_q),
		0,
		int(kModels.size()) - 1);
	return kModels[index];
}

[[nodiscard]] QString RolePrompt() {
	switch (Int(u"ai_chat_role"_q)) {
	case 1: return u"You are a translator. Translate the user's text into Russian if it is in another language, otherwise into English. Reply with the translation only."_q;
	case 2: return u"You are an editor. Fix spelling, grammar and style of the user's text and reply with the corrected text only."_q;
	case 3: return u"You summarize the user's text in a few short sentences."_q;
	}
	return u"You are a helpful assistant."_q;
}

[[nodiscard]] int HistoryLimit() {
	switch (Int(u"ai_chat_history"_q)) {
	case 0: return 1;
	case 1: return 10;
	case 2: return 30;
	}
	return 1000;
}

void Request(
		Fn<void(QString answer, bool ok)> done,
		Fn<void(QString partial)> progress) {
	auto &chat = Chat();
	if (!chat.manager) {
		chat.manager = new QNetworkAccessManager(qApp);
	}
	auto contents = QJsonArray();
	auto messages = std::vector<Message>();
	for (const auto &message : chat.history) {
		if (!message.error) {
			messages.push_back(message);
		}
	}
	const auto limit = HistoryLimit();
	auto skip = std::max(int(messages.size()) - limit, 0);
	while (skip < int(messages.size()) && !messages[skip].user) {
		++skip;
	}
	auto index = 0;
	for (const auto &message : messages) {
		if (index++ < skip) {
			continue;
		}
		contents.push_back(QJsonObject{
			{ u"role"_q, message.user ? u"user"_q : u"model"_q },
			{ u"parts"_q, QJsonArray{ QJsonObject{
				{ u"text"_q, message.text },
			} } },
		});
	}
	const auto streaming = Bool(u"ai_chat_streaming"_q);
	auto request = QNetworkRequest(QUrl(
		u"https://generativelanguage.googleapis.com/v1beta/models/"_q
		+ ModelName()
		+ (streaming
			? u":streamGenerateContent?alt=sse"_q
			: u":generateContent"_q)));
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	request.setRawHeader(
		"x-goog-api-key",
		String(u"ai_chat_key"_q).trimmed().toUtf8());
	const auto body = QJsonDocument(QJsonObject{
		{ u"contents"_q, contents },
		{ u"systemInstruction"_q, QJsonObject{
			{ u"parts"_q, QJsonArray{ QJsonObject{
				{ u"text"_q, RolePrompt() },
			} } },
		} },
		{ u"generationConfig"_q, QJsonObject{
			{ u"temperature"_q, Int(u"ai_chat_temperature"_q) / 10. },
		} },
	}).toJson(QJsonDocument::Compact);
	const auto reply = chat.manager->post(request, body);
	if (streaming) {
		const auto buffer = std::make_shared<QByteArray>();
		const auto text = std::make_shared<QString>();
		const auto error = std::make_shared<QString>();
		const auto parse = [=] {
			while (true) {
				const auto end = buffer->indexOf('\n');
				if (end < 0) {
					break;
				}
				const auto line = buffer->left(end).trimmed();
				buffer->remove(0, end + 1);
				if (!line.startsWith("data:")) {
					continue;
				}
				const auto object = QJsonDocument::fromJson(
					line.mid(5).trimmed()).object();
				const auto message = ErrorFrom(line.mid(5).trimmed());
				if (!message.isEmpty()) {
					*error = message;
				}
				const auto candidates = object.value(
					u"candidates"_q).toArray();
				if (candidates.isEmpty()) {
					continue;
				}
				const auto parts = candidates[0].toObject()
					.value(u"content"_q).toObject()
					.value(u"parts"_q).toArray();
				for (const auto &part : parts) {
					*text += part.toObject().value(u"text"_q).toString();
				}
				progress(*text);
			}
		};
		QObject::connect(reply, &QNetworkReply::readyRead, [=] {
			buffer->append(reply->readAll());
			parse();
		});
		QObject::connect(reply, &QNetworkReply::finished, [=] {
			reply->deleteLater();
			buffer->append(reply->readAll());
			buffer->append('\n');
			parse();
			if (!text->isEmpty()) {
				done(text->trimmed(), true);
			} else if (!error->isEmpty()) {
				done(*error, false);
			} else {
				const auto message = ErrorFrom(*buffer);
				done(message.isEmpty() ? reply->errorString() : message, false);
			}
		});
		return;
	}
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		const auto data = reply->readAll();
		const auto object = QJsonDocument::fromJson(data).object();
		const auto candidates = object.value(u"candidates"_q).toArray();
		auto text = QString();
		if (!candidates.isEmpty()) {
			const auto parts = candidates[0].toObject()
				.value(u"content"_q).toObject()
				.value(u"parts"_q).toArray();
			for (const auto &part : parts) {
				text += part.toObject().value(u"text"_q).toString();
			}
		}
		if (!text.isEmpty()) {
			done(text.trimmed(), true);
			return;
		}
		const auto error = ErrorFrom(data);
		done(error.isEmpty() ? reply->errorString() : error, false);
	});
}

[[nodiscard]] rpl::event_stream<> &Updates() {
	static auto result = rpl::event_stream<>();
	return result;
}

[[nodiscard]] QString HistoryPath() {
	return cWorkingDir() + u"tdata/exd_ai_chat.json"_q;
}

void SaveHistory() {
	auto list = QJsonArray();
	for (const auto &message : Chat().history) {
		list.push_back(QJsonObject{
			{ u"user"_q, message.user },
			{ u"error"_q, message.error },
			{ u"text"_q, message.text },
			{ u"date"_q, qint64(message.date) },
		});
	}
	auto file = QFile(HistoryPath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(list).toJson(QJsonDocument::Compact));
	}
}

void LoadHistory() {
	static auto loaded = false;
	if (std::exchange(loaded, true)) {
		return;
	}
	auto file = QFile(HistoryPath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	for (const auto &entry : QJsonDocument::fromJson(file.readAll()).array()) {
		const auto object = entry.toObject();
		Chat().history.push_back({
			.user = object.value(u"user"_q).toBool(),
			.error = object.value(u"error"_q).toBool(),
			.text = object.value(u"text"_q).toString(),
			.date = TimeId(object.value(u"date"_q).toInteger()),
		});
	}
}

} // namespace

void ShowKeyBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_extera_ai_key());
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_extera_ai_key_about(),
			st::boxLabel));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			tr::lng_extera_ai_key(),
			String(u"ai_chat_key"_q)));
		box->setFocusCallback([=] { field->setFocusFast(); });
		box->addButton(tr::lng_settings_save(), [=] {
			Config::Instance().setString(
				u"ai_chat_key"_q,
				field->getLastText().trimmed());
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

bool CanTranscribe() {
	return (Int(u"transcribe_provider"_q) == 1)
		&& !String(u"ai_chat_key"_q).trimmed().isEmpty();
}

void Transcribe(
		const QByteArray &data,
		const QString &mime,
		Fn<void(QString text, bool ok)> done) {
	auto &chat = Chat();
	if (!chat.manager) {
		chat.manager = new QNetworkAccessManager(qApp);
	}
	auto request = QNetworkRequest(QUrl(
		u"https://generativelanguage.googleapis.com/v1beta/models/"_q
		+ ModelName()
		+ u":generateContent"_q));
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	request.setRawHeader(
		"x-goog-api-key",
		String(u"ai_chat_key"_q).trimmed().toUtf8());
	const auto body = QJsonDocument(QJsonObject{
		{ u"contents"_q, QJsonArray{ QJsonObject{
			{ u"role"_q, u"user"_q },
			{ u"parts"_q, QJsonArray{
				QJsonObject{
					{ u"inline_data"_q, QJsonObject{
						{ u"mime_type"_q, mime },
						{ u"data"_q, QString::fromLatin1(data.toBase64()) },
					} },
				},
				QJsonObject{
					{ u"text"_q, u"Transcribe the speech in this recording "
						"verbatim in its original language. Return only the "
						"transcription text without comments."_q },
				},
			} },
		} } },
		{ u"generationConfig"_q, QJsonObject{
			{ u"temperature"_q, 0. },
		} },
	}).toJson(QJsonDocument::Compact);
	const auto reply = chat.manager->post(request, body);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		const auto bytes = reply->readAll();
		const auto object = QJsonDocument::fromJson(bytes).object();
		const auto candidates = object.value(u"candidates"_q).toArray();
		auto text = QString();
		if (!candidates.isEmpty()) {
			const auto parts = candidates[0].toObject()
				.value(u"content"_q).toObject()
				.value(u"parts"_q).toArray();
			for (const auto &part : parts) {
				text += part.toObject().value(u"text"_q).toString();
			}
		}
		if (!text.trimmed().isEmpty()) {
			done(text.trimmed(), true);
		} else {
			const auto error = ErrorFrom(bytes);
			done(error.isEmpty() ? reply->errorString() : error, false);
		}
	});
}

std::vector<AiChatMessage> AiChatHistory() {
	LoadHistory();
	auto result = std::vector<AiChatMessage>();
	for (const auto &message : Chat().history) {
		result.push_back({
			message.user,
			message.error,
			message.text,
			message.date,
		});
	}
	return result;
}

QString AiChatPartial() {
	return Chat().partial;
}

bool AiChatWaiting() {
	return Chat().waiting;
}

QString AiChatModel() {
	return ModelName();
}

rpl::producer<> AiChatUpdates() {
	return Updates().events();
}

void AiChatClear() {
	LoadHistory();
	if (Chat().waiting) {
		return;
	}
	Chat().history.clear();
	SaveHistory();
	Updates().fire({});
}

bool AiChatSend(
		not_null<Window::SessionController*> controller,
		const QString &text) {
	LoadHistory();
	if (text.trimmed().isEmpty() || Chat().waiting) {
		return false;
	} else if (String(u"ai_chat_key"_q).trimmed().isEmpty()) {
		ShowKeyBox(controller);
		return false;
	}
	Chat().history.push_back({
		.user = true,
		.text = text.trimmed(),
		.date = base::unixtime::now(),
	});
	Chat().waiting = true;
	Chat().partial = QString();
	SaveHistory();
	Updates().fire({});
	Request([=](QString answer, bool ok) {
		Chat().waiting = false;
		Chat().partial = QString();
		Chat().history.push_back({
			.error = !ok,
			.text = answer,
			.date = base::unixtime::now(),
		});
		SaveHistory();
		Updates().fire({});
	}, [=](QString partial) {
		Chat().partial = partial;
		Updates().fire({});
	});
	return true;
}

void ShowAiChat(not_null<Window::SessionController*> controller) {
	Exd::ShowAiChatSection(controller);
}

} // namespace Extera
