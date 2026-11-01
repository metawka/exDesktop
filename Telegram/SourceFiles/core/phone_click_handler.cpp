/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/phone_click_handler.h"

#include "boxes/add_contact_box.h"
#include "core/click_handler_types.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "mtproto/sender.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_calls.h"
#include "styles/style_chat.h" // popupMenuExpandedSeparator.
#include "styles/style_menu_icons.h"

namespace {

constexpr auto kResolverBotId = 7424190611ULL;
const auto kResolverBot = u"tgdb_search_bot"_q;

[[nodiscard]] QString Trim(QString text) {
	return text
		.replace('+', QString())
		.replace(' ', QString())
		.replace('-', QString());
}

class ResolvePhoneAction final : public Ui::Menu::ItemBase {
public:
	ResolvePhoneAction(
		not_null<Ui::Menu::Menu*> parent,
		const style::PopupMenu &st,
		const QString &phone,
		not_null<Window::SessionController*> controller,
		bool byId = false);

	bool isEnabled() const override;
	not_null<QAction*> action() const override;

	void handleKeyPress(not_null<QKeyEvent*> e) override;

	[[nodiscard]] QString firstName() const;
	[[nodiscard]] QString lastName() const;

protected:
	QPoint prepareRippleStartPosition() const override;
	QImage prepareRippleMask() const override;

	int contentHeight() const override;

private:
	void prepare();
	void paint(Painter &p);
	void resolveById(const QString &id);
	void queryBot(not_null<UserData*> bot, const QString &id);
	void resolveUsername(const QString &username, uint64 bare);
	void finish(PeerData *peer);

	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	const not_null<Data::Session*> _owner;
	const bool _byId = false;
	rpl::variable<PeerData*> _peer;
	rpl::variable<bool> _loaded;
	Ui::PeerUserpicView _userpicView;

	MTP::Sender _api;

	Ui::Text::String _above;
	Ui::Text::String _below;
	int _aboveWidth = 0;
	int _belowWidth = 0;
	const int _height = 0;

};

ResolvePhoneAction::ResolvePhoneAction(
	not_null<Ui::Menu::Menu*> parent,
	const style::PopupMenu &st,
	const QString &phone,
	not_null<Window::SessionController*> controller,
	bool byId)
: ItemBase(parent, st.menu)
, _dummyAction(Ui::CreateChild<QAction>(parent))
, _st(st.menu)
, _owner(&controller->session().data())
, _byId(byId)
, _api(&controller->session().mtp())
, _height(rect::m::sum::v(st::groupCallJoinAsPadding)
	+ st::groupCallJoinAsPhotoSize) {
	setAcceptBoth(true);
	fitToMenuWidth();
	setActionTriggered([=] {
		if (const auto peer = _peer.current()) {
			controller->showPeerInfo(peer);
		}
	});

	const auto formattedPhone = Trim(phone);

	const auto owner = &controller->session().data();

	if (_byId) {
		resolveById(phone.trimmed());
	} else if (const auto peer = owner->userByPhone(formattedPhone)) {
		_peer = peer;
		_loaded.force_assign(true);
	} else {
		_api.request(MTPcontacts_ResolvePhone(
			MTP_string(phone)
		)).done([=](const MTPcontacts_ResolvedPeer &result) {
			result.match([&](const MTPDcontacts_resolvedPeer &data) {
				owner->processUsers(data.vusers());
				owner->processChats(data.vchats());
				if (const auto peerId = peerFromMTP(data.vpeer())) {
					_peer = owner->peer(peerId);
				}
				_loaded.force_assign(true);
			});
		}).fail([=](const MTP::Error &error) {
			if (error.code() == 400) {
				_peer.force_assign(nullptr);
				_loaded.force_assign(true);
			}
		}).send();
	}

	paintRequest(
	) | rpl::on_next([=] {
		Painter p(this);
		paint(p);
	}, lifetime());

	enableMouseSelecting();
	prepare();
}

void ResolvePhoneAction::finish(PeerData *peer) {
	_peer.force_assign(peer);
	_loaded.force_assign(true);
}

void ResolvePhoneAction::resolveById(const QString &id) {
	const auto bare = id.toULongLong();
	const auto ids = {
		peerFromUser(UserId(bare)),
		peerFromChannel(ChannelId(bare)),
		peerFromChat(ChatId(bare)),
	};
	for (const auto peerId : ids) {
		if (const auto peer = _owner->peerLoaded(peerId)) {
			finish(peer);
			return;
		}
	}
	if (const auto bot = _owner->userLoaded(UserId(kResolverBotId))) {
		queryBot(bot, id);
		return;
	}
	_api.request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(kResolverBot),
		MTP_string()
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		result.match([&](const MTPDcontacts_resolvedPeer &data) {
			_owner->processUsers(data.vusers());
			_owner->processChats(data.vchats());
		});
		if (const auto bot = _owner->userLoaded(UserId(kResolverBotId))) {
			queryBot(bot, id);
		} else {
			finish(nullptr);
		}
	}).fail([=] {
		finish(nullptr);
	}).send();
}

void ResolvePhoneAction::queryBot(
		not_null<UserData*> bot,
		const QString &id) {
	const auto bare = id.toULongLong();
	_api.request(MTPmessages_GetInlineBotResults(
		MTP_flags(0),
		bot->inputUser(),
		MTP_inputPeerEmpty(),
		MTPInputGeoPoint(),
		MTP_string(id),
		MTP_string()
	)).done([=](const MTPmessages_BotResults &result) {
		const auto &data = result.data();
		_owner->processUsers(data.vusers());
		const auto idMark = QString::fromUtf8("\xF0\x9F\x86\x94");
		const auto userMark = QString::fromUtf8("\xF0\x9F\x93\xA7");
		for (const auto &entry : data.vresults().v) {
			const auto &message = entry.match([](const auto &data)
					-> const MTPBotInlineMessage& {
				return data.vsend_message();
			});
			const auto text = message.match([](
					const MTPDbotInlineMessageText &data) {
				return qs(data.vmessage());
			}, [](const auto &) {
				return QString();
			});
			auto found = QString();
			auto username = QString();
			for (const auto &line : text.split('\n')) {
				const auto value = line.mid(line.indexOf(':') + 1).trimmed();
				if (line.startsWith(idMark)) {
					found = value.startsWith(u"-100"_q)
						? value.mid(4)
						: value.startsWith('-')
						? value.mid(1)
						: value;
				} else if (line.startsWith(userMark)) {
					username = value.startsWith('@') ? value.mid(1) : value;
				}
			}
			if (found.toULongLong() == bare && !username.isEmpty()) {
				resolveUsername(username, bare);
				return;
			}
		}
		finish(nullptr);
	}).fail([=] {
		finish(nullptr);
	}).send();
}

void ResolvePhoneAction::resolveUsername(
		const QString &username,
		uint64 bare) {
	_api.request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(username),
		MTP_string()
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		auto peer = (PeerData*)nullptr;
		result.match([&](const MTPDcontacts_resolvedPeer &data) {
			_owner->processUsers(data.vusers());
			_owner->processChats(data.vchats());
			peer = _owner->peerLoaded(peerFromMTP(data.vpeer()));
		});
		finish((peer && (peer->id.value & PeerId::kChatTypeMask) == bare) ? peer : nullptr);
	}).fail([=] {
		finish(nullptr);
	}).send();
}

QString ResolvePhoneAction::firstName() const {
	const auto peer = _peer.current();
	const auto user = peer ? peer->asUser() : nullptr;
	return user ? user->firstName : QString();
}

QString ResolvePhoneAction::lastName() const {
	const auto peer = _peer.current();
	const auto user = peer ? peer->asUser() : nullptr;
	return user ? user->lastName : QString();
}

void ResolvePhoneAction::paint(Painter &p) {
	const auto selected = isSelected() && _peer.current();
	const auto height = contentHeight();
	if (selected && _st.itemBgOver->c.alpha() < 255) {
		p.fillRect(0, 0, width(), height, _st.itemBg);
	}
	p.fillRect(0, 0, width(), height, selected ? _st.itemBgOver : _st.itemBg);
	if (isEnabled()) {
		paintRipple(p, 0, 0);
	}

	const auto &padding = st::groupCallJoinAsPadding;
	const auto textLeft = padding.left()
		+ st::groupCallJoinAsPhotoSize
		+ padding.left();
	if (const auto peer = _peer.current()) {
		peer->paintUserpic(
			p,
			_userpicView,
			padding.left(),
			padding.top(),
			st::groupCallJoinAsPhotoSize);
		p.setPen(selected ? _st.itemFgOver : _st.itemFg);
		_above.drawLeftElided(
			p,
			textLeft,
			st::groupCallJoinAsTextTop,
			width() - textLeft - padding.right(),
			width());
		p.setPen(selected ? _st.itemFgShortcutOver : _st.itemFgShortcut);
		_below.drawLeftElided(
			p,
			textLeft,
			st::groupCallJoinAsNameTop,
			_belowWidth,
			width());
	} else {
		p.setPen(selected ? _st.itemFgShortcutOver : _st.itemFgShortcut);
		const auto w = width() - rect::m::sum::h(padding);
		_below.draw(p, Ui::Text::PaintContext{
			.position = QPoint(
				padding.left(),
				(height - _below.countHeight(w)) / 2),
			.outerWidth = w,
			.availableWidth = w,
			.align = style::al_center,
			.elisionLines = 2,
		});
	}
}

void ResolvePhoneAction::prepare() {
	rpl::combine(
		(_byId
			? tr::lng_exd_open_profile_id()
			: tr::lng_context_view_profile()),
		_peer.value(
		) | rpl::map([](PeerData *peer) {
			return peer
				? Info::Profile::NameValue(peer)
				: rpl::single(QString());
		}) | rpl::flatten_latest(),
		(_byId
			? tr::lng_exd_profile_id_not_found()
			: tr::lng_menu_not_contact()),
		_loaded.value(
		) | rpl::map([](bool loaded) {
			return loaded
				? rpl::single(QString())
				: tr::lng_contacts_loading();
		}) | rpl::flatten_latest()
	) | rpl::on_next([=](
			QString text,
			QString name,
			QString no,
			QString loading) {
		const auto &padding = st::groupCallJoinAsPadding;
		QWidget::setAttribute(
			Qt::WA_TransparentForMouseEvents,
			!_peer.current());
		const auto above = name;
		const auto below = !loading.isEmpty()
			? loading
			: name.isEmpty()
			? no
			: text;
		const auto options = kDefaultTextOptions;
		const auto tempWidth = [&] {
			_below.setMarkedText(_st.itemStyle, { text }, options);
			return _below.maxWidth();
		}();
		const auto textLeft = padding.left()
			+ st::groupCallJoinAsPhotoSize
			+ padding.left();
		const auto w = std::clamp(
			(textLeft + tempWidth + padding.right()),
			_st.widthMin,
			_st.widthMax);
		if (!no.isEmpty()) {
			_below = Ui::Text::String(w - rect::m::sum::h(padding));
		}
		_above.setMarkedText(_st.itemStyle, { above }, options);
		_below.setMarkedText(_st.itemStyle, { below }, options);
		setMinWidth(w);
		_aboveWidth = w - textLeft - padding.right();
		_belowWidth = w
			- ((loading.isEmpty() && name.isEmpty()) ? 0 : textLeft)
			- padding.right();
		update();
	}, lifetime());
}

bool ResolvePhoneAction::isEnabled() const {
	return true;
}

not_null<QAction*> ResolvePhoneAction::action() const {
	return _dummyAction;
}

QPoint ResolvePhoneAction::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

QImage ResolvePhoneAction::prepareRippleMask() const {
	return Ui::RippleAnimation::RectMask(size());
}

int ResolvePhoneAction::contentHeight() const {
	return _height;
}

void ResolvePhoneAction::handleKeyPress(not_null<QKeyEvent*> e) {
	if (!isSelected() || !_peer.current()) {
		return;
	}
	const auto key = e->key();
	if (key == Qt::Key_Enter || key == Qt::Key_Return) {
		setClicked(Ui::Menu::TriggeredSource::Keyboard);
	}
}

} // namespace

PhoneClickHandler::PhoneClickHandler(
	not_null<Main::Session*> session,
	QString text)
: _session(session)
, _text(text) {
	setProperty(kPhoneNumberLinkProperty, _text);
}

void PhoneClickHandler::onClick(ClickContext context) const {
	if (context.button != Qt::LeftButton) {
		return;
	}
	const auto my = context.other.value<ClickHandlerContext>();
	const auto controller = my.sessionWindow.get();
	const auto pos = QCursor::pos();
	if (!controller) {
		return;
	}
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		controller->content(),
		st::popupMenuWithIcons);

	const auto phone = _text;

#if 0
	const auto maybeContact = [&]() -> PeerData* {
		const auto &chats = controller->session().data().contactsList();
		for (const auto &row : chats->all()) {
			if (const auto history = row->history()) {
				if (const auto user = history->peer->asUser()) {
					if (Trim(user->phone()) == Trim(phone)) {
						return user;
					}
				}
			}
		}
		return nullptr;
	}();
#endif

	const auto idText = phone.trimmed();
	auto idOk = false;
	const auto bareId = idText.toULongLong(&idOk);
	const auto byId = idOk
		&& bareId
		&& idText.size() >= 5
		&& idText.size() <= 16;

	menu->addAction(tr::lng_profile_copy_phone(tr::now), [=] {
		TextUtilities::SetClipboardText(
			TextForMimeData::Simple(phone.trimmed()));
	}, &st::menuIconCopy);

	auto resolvePhoneAction = base::make_unique_q<ResolvePhoneAction>(
		menu->menu(),
		menu->st(),
		phone,
		controller);

	if (Trim(phone) != Trim(controller->session().user()->phone())) {
		menu->addAction(
			tr::lng_info_add_as_contact(tr::now),
			[=, raw = base::make_weak(resolvePhoneAction.get())] {
				controller->show(
					Box<AddContactBox>(
						&controller->session(),
						raw ? raw->firstName() : QString(),
						raw ? raw->lastName() : QString(),
						Trim(phone)));
			},
			&st::menuIconInvite);
	}

	menu->addSeparator(&st::popupMenuExpandedSeparator.menu.separator);

	if (byId) {
		menu->addAction(base::make_unique_q<ResolvePhoneAction>(
			menu->menu(),
			menu->st(),
			phone,
			controller,
			true));
	}
	menu->addAction(std::move(resolvePhoneAction));

	menu->popup(pos);
}

auto PhoneClickHandler::getTextEntity() const -> TextEntity {
	return { EntityType::Phone };
}

QString PhoneClickHandler::tooltip() const {
	return _text;
}
