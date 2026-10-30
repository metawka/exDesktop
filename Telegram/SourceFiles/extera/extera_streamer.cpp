#include "extera/extera_streamer.h"

#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "extera/extera_config.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "settings/settings_common.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QFileDialog>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace Extera::Streamer {
namespace {

[[nodiscard]] bool Full() {
	return Active() && Bool(u"streamer_full"_q);
}

[[nodiscard]] bool Option(const QString &key) {
	return Active() && (Full() || Bool(key));
}

[[nodiscard]] int Choice(const QString &key, int shown, int full) {
	return !Active() ? shown : Full() ? full : Int(key);
}

class CaptureFilter final : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject *object, QEvent *event) override;

};

[[nodiscard]] int CaptureMode() {
	return Active() ? Int(u"streamer_capture"_q) : 0;
}

void ApplyCapture(QWidget *widget) {
#ifdef Q_OS_WIN
	if (!widget
		|| !widget->isWindow()
		|| !widget->internalWinId()
		|| widget->testAttribute(Qt::WA_DontShowOnScreen)
		|| qobject_cast<QFileDialog*>(widget)) {
		return;
	}
	const auto mode = CaptureMode();
	const auto applied = widget->property("exd_capture").toInt();
	if (mode == applied) {
		return;
	}
	widget->setProperty("exd_capture", mode);
	const auto affinity = (mode == 1)
		? DWORD(0x00000001)
		: (mode == 2)
		? DWORD(0x00000011)
		: DWORD(0x00000000);
	SetWindowDisplayAffinity(HWND(widget->internalWinId()), affinity);
#endif
}

void ApplyCaptureAll() {
	for (const auto widget : QApplication::topLevelWidgets()) {
		ApplyCapture(widget);
	}
}

bool CaptureFilter::eventFilter(QObject *object, QEvent *event) {
	if (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange) {
		if (const auto widget = qobject_cast<QWidget*>(object)) {
			if (widget->isWindow()) {
				ApplyCapture(widget);
			}
		}
	}
	return false;
}

[[nodiscard]] QString TypeKey(ChatType type) {
	switch (type) {
	case ChatType::Private: return u"private"_q;
	case ChatType::Bot: return u"bots"_q;
	case ChatType::Group: return u"groups"_q;
	case ChatType::Channel: return u"channels"_q;
	}
	return u"private"_q;
}

[[nodiscard]] QString PartKey(ChatType type, const QString &part) {
	return u"streamer_"_q + TypeKey(type) + '_' + part;
}

[[nodiscard]] ChatHide HideFor(ChatType type) {
	if (Full()) {
		return { .avatar = true, .name = true, .text = true };
	}
	return {
		.avatar = Bool(PartKey(type, u"avatar"_q)),
		.name = Bool(PartKey(type, u"name"_q)),
		.text = Bool(PartKey(type, u"text"_q)),
	};
}

[[nodiscard]] QStringList ExceptionList(const QString &key) {
	return String(key).split(',', Qt::SkipEmptyParts);
}

void SaveExceptionList(const QString &key, const QStringList &list) {
	Config::Instance().setString(key, list.join(','));
}

void Apply() {
	using Flag = Data::PeerUpdate::Flag;
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (const auto session = account->maybeSession()) {
			session->changes().peerUpdated(
				session->user(),
				Flag::Name
					| Flag::Photo
					| Flag::EmojiStatus
					| Flag::Username
					| Flag::Usernames
					| Flag::PhoneNumber
					| Flag::About
					| Flag::Birthday
					| Flag::BusinessDetails
					| Flag::PersonalChannel);
		}
	}
	for (const auto widget : QApplication::topLevelWidgets()) {
		widget->update();
	}
	ApplyCaptureAll();
}

[[nodiscard]] const QImage &PreviewAvatar() {
	static const auto result = QImage(u":/gui/art/devpfpprew.jpg"_q);
	return result;
}

void PaintBar(QPainter &p, int x, int y, int width, int height) {
	p.drawRoundedRect(QRect(x, y, width, height), height / 2., height / 2.);
}

void PaintCircleImage(QPainter &p, const QImage &image, QRect rect) {
	auto path = QPainterPath();
	path.addEllipse(QRectF(rect));
	p.save();
	p.setClipPath(path);
	p.drawImage(rect, image);
	p.restore();
}

void PaintPreview(QPainter &p, QRect rect) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBgOver);
	p.drawRoundedRect(rect, st::exteraStreamerRadius, st::exteraStreamerRadius);

	const auto padding = st::exteraStreamerPadding;
	const auto photo = st::exteraStreamerPhoto;
	const auto line = st::normalFont->height + st::exteraStreamerLineSkip;
	const auto muted = anim::with_alpha(st::windowSubTextFg->c, 0.35);
	const auto left = rect.x() + padding;
	const auto textLeft = left + photo + padding;
	const auto textWidth = rect.x() + rect.width() - padding - textLeft;

	const auto drawText = [&](int y, const QString &text, bool sub) {
		p.setPen(sub ? st::windowSubTextFg : st::windowBoldFg);
		p.setFont(sub ? st::normalFont : st::semiboldFont);
		p.drawText(
			textLeft,
			y + st::normalFont->ascent,
			st::normalFont->elided(text, textWidth));
	};
	const auto drawBar = [&](int y, int width) {
		p.setPen(Qt::NoPen);
		p.setBrush(muted);
		PaintBar(
			p,
			textLeft,
			y + (st::normalFont->height - st::exteraStreamerBar) / 2,
			width,
			st::exteraStreamerBar);
	};
	const auto drawLetter = [&](int y, QColor color, QString letter) {
		const auto r = QRect(left, y, photo, photo);
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawEllipse(r);
		p.setPen(st::windowFgActive);
		p.setFont(st::semiboldFont);
		p.drawText(r, Qt::AlignCenter, letter);
	};
	const auto drawHiddenAvatar = [&](int y) {
		p.setPen(Qt::NoPen);
		p.setBrush(muted);
		p.drawEllipse(QRect(left, y, photo, photo));
	};

	auto top = rect.y() + padding;
	PaintCircleImage(
		p,
		HideAvatar() ? Avatar() : PreviewAvatar(),
		QRect(left, top, photo, photo));
	drawText(top, HideName() ? Name() : u"метавка"_q, false);
	auto y = top + line;
	switch (Phone()) {
	case PhoneMode::Unknown:
		drawText(y, tr::lng_extera_unknown_number(tr::now), true);
		break;
	case PhoneMode::Spoiler:
		drawBar(y, textWidth / 3);
		break;
	case PhoneMode::Shown:
		drawText(y, u"+888 888 8888"_q, true);
		break;
	}
	y += line;
	switch (Username()) {
	case UsernameMode::Spoiler:
		drawBar(y, textWidth / 4);
		y += line;
		break;
	case UsernameMode::Hidden:
		break;
	case UsernameMode::Shown:
		drawText(y, u"@metawka"_q, true);
		y += line;
		break;
	}
	drawText(y, u"ID: "_q + MaskId(u"8888800"_q), true);
	y += line;
	if (!HideProfile()) {
		drawText(y, tr::lng_extera_streamer_preview_bio(tr::now), true);
		y += line;
	}

	top = std::max(y, top + photo) + padding;
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBgRipple);
	p.drawRect(
		rect.x() + padding,
		top - padding / 2,
		rect.width() - 2 * padding,
		st::lineWidth);

	const auto drawRow = [&](
			int y,
			ChatHide hide,
			Fn<void()> avatar,
			const QString &name,
			const QString &text) {
		if (Active() && hide.avatar) {
			drawHiddenAvatar(y);
		} else {
			avatar();
		}
		if (Active() && hide.name) {
			drawBar(y, textWidth * 2 / 5);
		} else {
			drawText(y, name, false);
		}
		if (Active() && hide.text) {
			drawBar(y + line, textWidth * 2 / 3);
		} else {
			drawText(y + line, text, true);
		}
	};
	const auto privateTop = top;
	drawRow(
		privateTop,
		HideFor(ChatType::Private),
		[&] { drawLetter(privateTop, QColor(0x5b, 0x8d, 0xef), u"W"_q); },
		u"Wave"_q,
		tr::lng_extera_streamer_preview_private(tr::now));
	const auto channelTop = top + photo + padding;
	drawRow(
		channelTop,
		HideFor(ChatType::Channel),
		[&] {
			PaintCircleImage(
				p,
				Avatar(),
				QRect(left, channelTop, photo, photo));
		},
		u"@exDesktop"_q,
		tr::lng_extera_streamer_preview_channel(tr::now));
}

void AddToggleRow(
		not_null<Ui::VerticalLayout*> container,
		const QString &key,
		rpl::producer<QString> title,
		Fn<void()> changed) {
	const auto button = container->add(
		object_ptr<Ui::SettingsButton>(
			container,
			std::move(title),
			st::settingsButtonNoIcon));
	button->toggleOn(rpl::single(Bool(key)));
	button->toggledChanges(
	) | rpl::on_next([=](bool value) {
		Config::Instance().setBool(key, value);
		changed();
	}, button->lifetime());
}

void AddChoiceRow(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		const QString &key,
		rpl::producer<QString> title,
		std::vector<QString> options,
		Fn<void()> changed) {
	const auto value = container->lifetime().make_state<
		rpl::variable<int>>(Int(key));
	const auto button = container->add(
		object_ptr<Ui::SettingsButton>(
			container,
			rpl::duplicate(title),
			st::settingsButtonNoIcon));
	Settings::CreateRightLabel(
		button,
		value->value() | rpl::map([=](int index) {
			return (index >= 0 && index < int(options.size()))
				? options[index]
				: QString();
		}),
		st::settingsButtonNoIcon,
		rpl::duplicate(title));
	const auto shared = std::make_shared<rpl::producer<QString>>(
		std::move(title));
	button->setClickedCallback([=] {
		controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			SingleChoiceBox(box, {
				.title = rpl::duplicate(*shared),
				.options = options,
				.initialSelection = value->current(),
				.callback = [=](int index) {
					Config::Instance().setInt(key, index);
					*value = index;
					changed();
				},
			});
		}));
	});
}

void AddTypeSection(
		not_null<Ui::VerticalLayout*> container,
		ChatType type,
		rpl::producer<QString> title,
		Fn<void()> changed) {
	Ui::AddDivider(container);
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, std::move(title));
	AddToggleRow(
		container,
		PartKey(type, u"avatar"_q),
		tr::lng_extera_streamer_part_avatar(),
		changed);
	AddToggleRow(
		container,
		PartKey(type, u"name"_q),
		tr::lng_extera_streamer_part_name(),
		changed);
	AddToggleRow(
		container,
		PartKey(type, u"text"_q),
		tr::lng_extera_streamer_part_text(),
		changed);
	Ui::AddSkip(container);
}

void ShowExceptionsBox(
		not_null<Window::SessionController*> controller,
		const QString &key,
		std::shared_ptr<rpl::producer<QString>> title,
		Fn<void()> changed) {
	const auto session = &controller->session();
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::duplicate(*title));
		const auto content = box->verticalLayout();
		const auto list = ExceptionList(key);
		if (list.isEmpty()) {
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_extera_streamer_exceptions_empty(),
				st::boxLabel));
		}
		for (const auto &value : list) {
			const auto peerId = PeerId(value.toULongLong());
			const auto peer = session->data().peerLoaded(peerId);
			const auto name = peer ? peer->name() : value;
			const auto button = content->add(
				object_ptr<Ui::SettingsButton>(
					content,
					rpl::single(name),
					st::settingsButtonNoIcon));
			Settings::CreateRightLabel(
				button,
				tr::lng_extera_streamer_exceptions_remove(),
				st::settingsButtonNoIcon,
				rpl::single(name));
			button->setClickedCallback([=] {
				auto list = ExceptionList(key);
				list.removeAll(value);
				SaveExceptionList(key, list);
				changed();
				delete button;
			});
		}
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

} // namespace

bool Active() {
	return Bool(u"streamer_mode"_q);
}

bool HideName() {
	return Option(u"streamer_name"_q);
}

bool HideAvatar() {
	return Option(u"streamer_avatar"_q);
}

bool HideProfile() {
	return Option(u"streamer_profile"_q);
}

PhoneMode Phone() {
	return PhoneMode(Choice(
		u"streamer_phone"_q,
		int(PhoneMode::Shown),
		int(PhoneMode::Unknown)));
}

UsernameMode Username() {
	return UsernameMode(Choice(
		u"streamer_username"_q,
		int(UsernameMode::Shown),
		int(UsernameMode::Hidden)));
}

IdMode Id() {
	return IdMode(Choice(
		u"streamer_id"_q,
		int(IdMode::Shown),
		int(IdMode::Full)));
}

ChatType TypeOf(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return user->isBot() ? ChatType::Bot : ChatType::Private;
	} else if (peer->isBroadcast()) {
		return ChatType::Channel;
	}
	return ChatType::Group;
}

Exception ExceptionFor(not_null<PeerData*> peer) {
	const auto id = QString::number(peer->id.value);
	if (ExceptionList(u"streamer_shown"_q).contains(id)) {
		return Exception::Shown;
	} else if (ExceptionList(u"streamer_hidden"_q).contains(id)) {
		return Exception::Hidden;
	}
	return Exception::None;
}

void SetException(not_null<PeerData*> peer, Exception exception) {
	const auto id = QString::number(peer->id.value);
	auto shown = ExceptionList(u"streamer_shown"_q);
	auto hidden = ExceptionList(u"streamer_hidden"_q);
	shown.removeAll(id);
	hidden.removeAll(id);
	if (exception == Exception::Shown) {
		shown.push_back(id);
	} else if (exception == Exception::Hidden) {
		hidden.push_back(id);
	}
	SaveExceptionList(u"streamer_shown"_q, shown);
	SaveExceptionList(u"streamer_hidden"_q, hidden);
	Apply();
}

ChatHide ForPeer(not_null<PeerData*> peer) {
	if (!Active()) {
		return {};
	}
	switch (ExceptionFor(peer)) {
	case Exception::Shown: return {};
	case Exception::Hidden: return { .avatar = true, .name = true, .text = true };
	case Exception::None: break;
	}
	return HideFor(TypeOf(peer));
}

QString Name() {
	return u"exDesktop"_q;
}

const QImage &Avatar() {
	static const auto result = QImage(u":/gui/art/exteradesktop.png"_q);
	return result;
}

QString MaskId(const QString &id) {
	const auto bullet = QChar(0x2022);
	switch (Id()) {
	case IdMode::Middle:
		return (id.size() > 4)
			? (id.left(2) + QString(id.size() - 4, bullet) + id.right(2))
			: QString(id.size(), bullet);
	case IdMode::Full:
		return QString(id.size(), bullet);
	case IdMode::Shown:
		break;
	}
	return id;
}

void Refresh() {
	Apply();
}

void Start() {
	static auto started = false;
	if (started) {
		return;
	}
	started = true;
	qApp->installEventFilter(new CaptureFilter(qApp));
	ApplyCaptureAll();
}

void ShowSettings(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_extera_streamer_settings());
		box->setWidth(st::boxWideWidth);
		const auto content = box->verticalLayout();
		const auto preview = content->add(
			object_ptr<Ui::RpWidget>(content),
			st::boxRowPadding + QMargins(0, 8, 0, 8));
		preview->resize(preview->width(), st::exteraStreamerPreviewHeight);
		preview->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(preview);
			PaintPreview(p, preview->rect());
		}, preview->lifetime());
		const auto dirty = std::make_shared<bool>(false);
		const auto changed = [=] {
			*dirty = true;
			preview->update();
		};
		box->boxClosing() | rpl::on_next([=] {
			if (*dirty) {
				Apply();
			}
		}, box->lifetime());

		AddToggleRow(
			content,
			u"streamer_mode"_q,
			tr::lng_extera_streamer_mode(),
			changed);
		AddToggleRow(
			content,
			u"streamer_full"_q,
			tr::lng_extera_streamer_full_mode(),
			changed);
		AddChoiceRow(content, controller, u"streamer_capture"_q,
			tr::lng_extera_streamer_capture(),
			{
				tr::lng_extera_streamer_capture_shown(tr::now),
				tr::lng_extera_streamer_capture_black(tr::now),
				tr::lng_extera_streamer_capture_hidden(tr::now),
			},
			changed);
		Ui::AddDividerText(content, tr::lng_extera_streamer_capture_about());

		Ui::AddDivider(content);
		Ui::AddSkip(content);
		Ui::AddSubsectionTitle(
			content,
			tr::lng_extera_streamer_profile_title());
		AddToggleRow(
			content,
			u"streamer_name"_q,
			tr::lng_extera_streamer_name(),
			changed);
		AddToggleRow(
			content,
			u"streamer_avatar"_q,
			tr::lng_extera_streamer_avatar(),
			changed);
		AddChoiceRow(content, controller, u"streamer_phone"_q,
			tr::lng_extera_streamer_phone(),
			{
				tr::lng_extera_streamer_unknown(tr::now),
				tr::lng_extera_streamer_spoiler(tr::now),
				tr::lng_extera_streamer_show(tr::now),
			},
			changed);
		AddChoiceRow(content, controller, u"streamer_username"_q,
			tr::lng_extera_streamer_username(),
			{
				tr::lng_extera_streamer_spoiler(tr::now),
				tr::lng_extera_streamer_hidden(tr::now),
				tr::lng_extera_streamer_show(tr::now),
			},
			changed);
		AddChoiceRow(content, controller, u"streamer_id"_q,
			tr::lng_extera_streamer_id(),
			{
				tr::lng_extera_streamer_middle(tr::now),
				tr::lng_extera_streamer_full(tr::now),
				tr::lng_extera_streamer_show(tr::now),
			},
			changed);
		AddToggleRow(
			content,
			u"streamer_profile"_q,
			tr::lng_extera_streamer_profile(),
			changed);
		Ui::AddDividerText(
			content,
			tr::lng_extera_streamer_profile_about());

		Ui::AddSkip(content);
		Ui::AddSubsectionTitle(
			content,
			tr::lng_extera_streamer_private_title());
		AddToggleRow(
			content,
			PartKey(ChatType::Private, u"avatar"_q),
			tr::lng_extera_streamer_part_avatar(),
			changed);
		AddToggleRow(
			content,
			PartKey(ChatType::Private, u"name"_q),
			tr::lng_extera_streamer_part_name(),
			changed);
		AddToggleRow(
			content,
			PartKey(ChatType::Private, u"text"_q),
			tr::lng_extera_streamer_part_text(),
			changed);
		Ui::AddSkip(content);
		AddTypeSection(
			content,
			ChatType::Bot,
			tr::lng_extera_streamer_bots_title(),
			changed);
		AddTypeSection(
			content,
			ChatType::Group,
			tr::lng_extera_streamer_groups_title(),
			changed);
		AddTypeSection(
			content,
			ChatType::Channel,
			tr::lng_extera_streamer_channels_title(),
			changed);

		Ui::AddDivider(content);
		Ui::AddSkip(content);
		Ui::AddSubsectionTitle(
			content,
			tr::lng_extera_streamer_exceptions());
		const auto addExceptions = [&](
				const QString &key,
				rpl::producer<QString> title) {
			const auto shared = std::make_shared<rpl::producer<QString>>(
				rpl::duplicate(title));
			const auto button = content->add(
				object_ptr<Ui::SettingsButton>(
					content,
					std::move(title),
					st::settingsButtonNoIcon));
			Settings::CreateRightLabel(
				button,
				Config::Instance().stringChanges(
					key
				) | rpl::map([=](const QString &value) {
					return QString::number(
						value.split(',', Qt::SkipEmptyParts).size());
				}),
				st::settingsButtonNoIcon,
				rpl::duplicate(*shared));
			button->setClickedCallback([=] {
				ShowExceptionsBox(controller, key, shared, changed);
			});
		};
		addExceptions(
			u"streamer_shown"_q,
			tr::lng_extera_streamer_always_shown());
		addExceptions(
			u"streamer_hidden"_q,
			tr::lng_extera_streamer_always_hidden());
		Ui::AddDividerText(
			content,
			tr::lng_extera_streamer_exceptions_about());
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

} // namespace Extera::Streamer
