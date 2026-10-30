#include "extera/extera_ai_chat.h"

#include "extera/extera_config.h"
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
	QString text;
};

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
	const auto limit = HistoryLimit();
	auto skip = std::max(int(chat.history.size()) - limit, 0);
	while (skip < int(chat.history.size()) && !chat.history[skip].user) {
		++skip;
	}
	auto index = 0;
	for (const auto &message : chat.history) {
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
					const auto obj = QJsonDocument::fromJson(line).object();
					const auto msg = obj.value(u"error"_q).toObject()
						.value(u"message"_q).toString();
					if (!msg.isEmpty()) {
						*error = msg;
					}
					continue;
				}
				const auto object = QJsonDocument::fromJson(
					line.mid(5).trimmed()).object();
				const auto message = object.value(u"error"_q).toObject()
					.value(u"message"_q).toString();
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
				const auto object = QJsonDocument::fromJson(
					*buffer).object();
				const auto message = object.value(u"error"_q).toObject()
					.value(u"message"_q).toString();
				done(message.isEmpty() ? reply->errorString() : message, false);
			}
		});
		return;
	}
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		const auto object = QJsonDocument::fromJson(
			reply->readAll()).object();
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
		const auto error = object.value(u"error"_q).toObject()
			.value(u"message"_q).toString();
		done(error.isEmpty() ? reply->errorString() : error, false);
	});
}

void FillMessages(not_null<Ui::VerticalLayout*> list) {
	list->clear();
	const auto &history = Chat().history;
	if (history.empty()) {
		list->add(
			object_ptr<Ui::FlatLabel>(
				list,
				tr::lng_extera_ai_empty(),
				st::exteraAbout),
			st::boxRowPadding);
	}
	for (const auto &message : history) {
		list->add(
			object_ptr<Ui::FlatLabel>(
				list,
				message.user
					? tr::lng_extera_ai_you()
					: rpl::single(ModelName()),
				st::exteraSubtitle),
			st::exteraAiAuthorPadding);
		const auto label = list->add(
			object_ptr<Ui::FlatLabel>(
				list,
				rpl::single(message.text),
				st::boxLabel),
			st::exteraAiMessagePadding);
		label->setSelectable(true);
	}
	if (Chat().waiting) {
		if (Chat().partial.isEmpty()) {
			list->add(
				object_ptr<Ui::FlatLabel>(
					list,
					tr::lng_extera_ai_thinking(),
					st::exteraSubtitle),
				st::exteraAiMessagePadding);
		} else {
			list->add(
				object_ptr<Ui::FlatLabel>(
					list,
					rpl::single(ModelName()),
					st::exteraSubtitle),
				st::exteraAiAuthorPadding);
			list->add(
				object_ptr<Ui::FlatLabel>(
					list,
					rpl::single(Chat().partial),
					st::boxLabel),
				st::exteraAiMessagePadding);
		}
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

void ShowAiChat(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_extera_ai_chat());
		box->setWidth(st::boxWideWidth);
		const auto key = box->addRow(
			object_ptr<Ui::SettingsButton>(
				box,
				tr::lng_extera_ai_key(),
				st::settingsButtonLightNoIcon),
			style::margins());
		key->setClickedCallback([=] { ShowKeyBox(controller); });
		const auto list = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			style::margins());
		FillMessages(list);
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			Ui::InputField::Mode::MultiLine,
			tr::lng_extera_ai_placeholder()));
		box->setFocusCallback([=] { field->setFocusFast(); });
		const auto refresh = [=] {
			FillMessages(list);
			list->resizeToWidth(list->width());
		};
		const auto send = [=] {
			const auto text = field->getLastText().trimmed();
			if (text.isEmpty() || Chat().waiting) {
				return;
			}
			if (String(u"ai_chat_key"_q).trimmed().isEmpty()) {
				ShowKeyBox(controller);
				return;
			}
			field->clear();
			Chat().history.push_back({ .user = true, .text = text });
			Chat().waiting = true;
			refresh();
			const auto weak = base::make_weak(box.get());
			Chat().partial = QString();
			Request([=](QString answer, bool ok) {
				Chat().waiting = false;
				Chat().partial = QString();
				if (ok) {
					Chat().history.push_back({ .text = answer });
				} else {
					if (!Chat().history.empty()) {
						Chat().history.pop_back();
					}
					controller->showToast(answer);
				}
				if (weak) {
					refresh();
				}
			}, [=](QString partial) {
				Chat().partial = partial;
				if (weak) {
					refresh();
				}
			});
		};
		field->submits() | rpl::on_next(send, field->lifetime());
		box->addButton(tr::lng_extera_ai_send(), send);
		box->addLeftButton(tr::lng_extera_ai_clear(), [=] {
			Chat().history.clear();
			refresh();
		});
	}));
}

} // namespace Extera
