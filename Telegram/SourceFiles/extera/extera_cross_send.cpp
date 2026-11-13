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
#include "data/data_histories.h"
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
	crl::on_main([=] {
		if (to->sessionExists()) {
			Core::App().domain().activate(to);
			ChooseAndSend(state, to, 0);
		}
	});
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

struct RelayState {
	std::vector<FullMsgId> received;
	int expected = 0;
	bool finished = false;
	rpl::lifetime lifetime;
};

void RelayChoose(
		std::shared_ptr<RelayState> state,
		not_null<Main::Account*> to,
		int attempt) {
	const auto window = Core::App().activePrimaryWindow();
	const auto controller = window ? window->sessionController() : nullptr;
	if (!controller || (&controller->session().account() != to)) {
		if (attempt < kWaitControllerAttempts) {
			base::call_delayed(kWaitControllerDelay, [=] {
				RelayChoose(state, to, attempt + 1);
			});
		}
		return;
	}
	const auto session = &controller->session();
	const auto ids = state->received;
	Window::ShowChooseRecipientBox(controller, [=](
			not_null<Data::Thread*> thread) {
		auto items = HistoryItemsList();
		for (const auto &id : ids) {
			if (const auto item = session->data().message(id)) {
				items.push_back(item);
			}
		}
		if (items.empty()) {
			return true;
		}
		const auto relay = MessageIdsList(ids.begin(), ids.end());
		session->api().forwardMessages(
			Data::ResolvedForwardDraft{ .items = std::move(items) },
			Api::SendAction(thread),
			[=] {
				session->data().histories().deleteMessages(relay, true);
				session->data().sendHistoryChangeNotifications();
			});
		controller->showPeerHistory(thread->peer());
		return true;
	});
}

void RelayFinish(
		std::shared_ptr<RelayState> state,
		not_null<Main::Account*> to) {
	if (state->finished) {
		return;
	}
	state->finished = true;
	state->lifetime.destroy();
	if (state->received.empty() || !to->sessionExists()) {
		return;
	}
	Core::App().domain().activate(to);
	RelayChoose(state, to, 0);
}

void RelayTo(
		not_null<Main::Session*> from,
		HistoryItemsList items,
		not_null<UserData*> target,
		not_null<Main::Account*> to) {
	const auto session = &to->session();
	const auto fromPeerId = from->userPeerId();
	const auto state = std::make_shared<RelayState>();
	state->expected = int(items.size());
	session->data().newItemAdded(
	) | rpl::filter([=](not_null<HistoryItem*> item) {
		return (item->history()->peer->id == fromPeerId) && !item->out();
	}) | rpl::on_next([=](not_null<HistoryItem*> item) {
		state->received.push_back(item->fullId());
		if (int(state->received.size()) >= state->expected) {
			crl::on_main([=] { RelayFinish(state, to); });
		}
	}, state->lifetime);
	base::call_delayed(crl::time(15000), [=] {
		RelayFinish(state, to);
	});
	from->api().forwardMessages(
		Data::ResolvedForwardDraft{ .items = std::move(items) },
		Api::SendAction(from->data().history(target)));
}

void CopyFromAccount(
	not_null<Main::Session*> from,
	MessageIdsList ids,
	not_null<Main::Account*> to);

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
				const auto source = &from->account();
				base::call_delayed(crl::time(50), [=] {
					if (account->sessionExists() && source->sessionExists()) {
						SendFromAccount(&source->session(), ids, account);
					}
				});
			},
			&st::menuIconForward);
	}
}

void SendFromAccount(
		not_null<Main::Session*> from,
		MessageIdsList ids,
		not_null<Main::Account*> to) {
	auto items = HistoryItemsList();
	for (const auto &id : ids) {
		const auto item = from->data().message(id);
		if (item && item->allowsForward()) {
			items.push_back(item);
		}
	}
	if (items.size() != ids.size() || !to->sessionExists()) {
		CopyFromAccount(from, ids, to);
		return;
	}
	const auto targetSelf = to->session().user();
	const auto targetId = targetSelf->id;
	const auto known = from->data().userLoaded(peerToUser(targetId));
	if (known && known->accessHash()) {
		RelayTo(from, std::move(items), known, to);
		return;
	}
	const auto owner = &from->data();
	const auto done = [=](const MTPcontacts_ResolvedPeer &result) {
		result.match([&](const MTPDcontacts_resolvedPeer &data) {
			owner->processUsers(data.vusers());
			owner->processChats(data.vchats());
		});
		const auto user = owner->userLoaded(peerToUser(targetId));
		if (user && user->accessHash()) {
			RelayTo(from, items, user, to);
		} else {
			CopyFromAccount(from, ids, to);
		}
	};
	const auto fail = [=] { CopyFromAccount(from, ids, to); };
	const auto username = targetSelf->username();
	if (!username.isEmpty()) {
		from->api().request(MTPcontacts_ResolveUsername(
			MTP_flags(0),
			MTP_string(username),
			MTP_string()
		)).done(done).fail(fail).send();
	} else if (!targetSelf->phone().isEmpty()) {
		from->api().request(MTPcontacts_ResolvePhone(
			MTP_string(targetSelf->phone())
		)).done(done).fail(fail).send();
	} else {
		CopyFromAccount(from, ids, to);
	}
}

namespace {

void CopyFromAccount(
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

} // namespace

} // namespace Extera
