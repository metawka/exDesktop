#include "extera/extera_cross_send.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "core/application.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/image/image.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QDir>
#include <QtCore/QStandardPaths>
#include <QtCore/QUuid>

namespace Extera {
namespace {

constexpr auto kWaitControllerAttempts = 100;
constexpr auto kWaitControllerDelay = crl::time(100);

struct Part {
	TextWithEntities text;
	QString path;
	bool photo = false;
	DocumentData *document = nullptr;
	std::shared_ptr<Data::PhotoMedia> photoMedia;
};

struct State {
	std::vector<Part> parts;
	rpl::lifetime lifetime;
	bool finished = false;
};

[[nodiscard]] QString TempFolder() {
	const auto result = QDir::tempPath()
		+ u"/exDesktop/"_q
		+ QUuid::createUuid().toString(QUuid::WithoutBraces);
	QDir().mkpath(result);
	return result;
}

[[nodiscard]] TextWithTags ToTags(const TextWithEntities &text) {
	return TextWithTags{
		text.text,
		TextUtilities::ConvertEntitiesToTextTags(text.entities),
	};
}

void SendParts(
		const std::vector<Part> &parts,
		not_null<Data::Thread*> thread) {
	const auto session = &thread->session();
	const auto api = &session->api();
	for (const auto &part : parts) {
		if (part.path.isEmpty()) {
			if (part.text.text.trimmed().isEmpty()) {
				continue;
			}
			auto message = Api::MessageToSend(Api::SendAction(thread));
			message.textWithTags = ToTags(part.text);
			api->sendMessage(std::move(message));
			continue;
		}
		auto list = Storage::PrepareMediaList(
			QStringList{ part.path },
			st::sendMediaPreviewSize,
			session->premium());
		if (list.error != Ui::PreparedList::Error::None
			|| list.files.empty()) {
			continue;
		}
		list.files.front().caption = ToTags(part.text);
		api->sendFiles(
			std::move(list),
			part.photo ? SendMediaType::Photo : SendMediaType::File,
			nullptr,
			Api::SendAction(thread));
	}
}

void ChooseAndSend(
		std::shared_ptr<State> state,
		not_null<Main::Account*> to,
		int attempt) {
	const auto window = Core::App().activePrimaryWindow();
	const auto controller = window ? window->sessionController() : nullptr;
	if (!controller || (&controller->session().account() != to)) {
		if (attempt < kWaitControllerAttempts) {
			base::call_delayed(kWaitControllerDelay, [=] {
				ChooseAndSend(state, to, attempt + 1);
			});
		}
		return;
	}
	const auto parts = state->parts;
	Window::ShowChooseRecipientBox(controller, [=](
			not_null<Data::Thread*> thread) {
		SendParts(parts, thread);
		controller->showPeerHistory(thread->peer());
		return true;
	});
}

void Proceed(std::shared_ptr<State> state, not_null<Main::Account*> to) {
	if (state->finished) {
		return;
	}
	state->finished = true;
	state->lifetime.destroy();
	Core::App().domain().activate(to);
	ChooseAndSend(state, to, 0);
}

[[nodiscard]] bool CheckReady(const std::shared_ptr<State> &state) {
	auto ready = true;
	for (auto &part : state->parts) {
		if (part.document && part.path.isEmpty()) {
			const auto path = part.document->filepath(true);
			if (!path.isEmpty()) {
				part.path = path;
			} else {
				ready = false;
			}
		} else if (part.photoMedia && part.path.isEmpty()) {
			const auto image = part.photoMedia->image(
				Data::PhotoSize::Large);
			if (image) {
				const auto path = TempFolder() + u"/photo.jpg"_q;
				if (image->original().save(path, "JPG", 95)) {
					part.path = path;
				}
				part.photoMedia = nullptr;
			} else {
				ready = false;
			}
		}
	}
	return ready;
}

} // namespace

bool HasOtherAccounts(not_null<Main::Session*> from) {
	for (const auto &account : Core::App().domain().orderedAccounts()) {
		if (account != &from->account() && account->sessionExists()) {
			return true;
		}
	}
	return false;
}

void AddSendFromAccountActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<Main::Session*> from,
		MessageIdsList ids,
		Fn<void()> closeBox) {
	auto added = false;
	for (const auto &account : Core::App().domain().orderedAccounts()) {
		if (account == &from->account() || !account->sessionExists()) {
			continue;
		}
		if (!added) {
			if (!menu->empty()) {
				menu->addSeparator();
			}
			added = true;
		}
		const auto name = account->session().user()->name();
		menu->addAction(
			tr::lng_exd_send_from(tr::now, lt_name, name),
			[=] {
				closeBox();
				SendFromAccount(from, ids, account);
			},
			&st::menuIconForward);
	}
}

void SendFromAccount(
		not_null<Main::Session*> from,
		MessageIdsList ids,
		not_null<Main::Account*> to) {
	const auto state = std::make_shared<State>();
	const auto owner = &from->data();
	for (const auto &id : ids) {
		const auto item = owner->message(id);
		if (!item) {
			continue;
		}
		auto part = Part{ .text = item->originalText() };
		if (const auto media = item->media()) {
			if (const auto photo = media->photo()) {
				part.photo = true;
				part.photoMedia = photo->createMediaView();
				part.photoMedia->wanted(Data::PhotoSize::Large, id);
			} else if (const auto document = media->document()) {
				part.document = document;
				if (document->filepath(true).isEmpty()) {
					auto name = document->filename();
					if (name.isEmpty()) {
						name = u"file"_q;
					}
					document->save(id, TempFolder() + '/' + name);
				}
			}
		}
		state->parts.push_back(std::move(part));
	}
	if (state->parts.empty()) {
		return;
	}
	if (CheckReady(state)) {
		Proceed(state, to);
		return;
	}
	from->downloaderTaskFinished(
	) | rpl::on_next([=] {
		if (CheckReady(state)) {
			crl::on_main([=] {
				Proceed(state, to);
			});
		}
	}, state->lifetime);
}

} // namespace Extera
