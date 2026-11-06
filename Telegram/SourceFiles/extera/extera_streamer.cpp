#include "extera/extera_streamer.h"

#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
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

#include "extera/extera_app_icon.h"
#include "base/call_delayed.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"

#include <QtCore/QAbstractNativeEventFilter>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtGui/QKeyEvent>
#include <QtGui/QKeySequence>
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

[[nodiscard]] QKeySequence Hotkey(const QString &key) {
	return QKeySequence(String(key), QKeySequence::PortableText);
}

void ApplyNow();

void ToggleByHotkey() {
	Config::Instance().setBool(u"streamer_mode"_q, !Active());
}

void TriggerShot() {
	static auto active = false;
	if (active) {
		return;
	}
	active = true;
	const auto mode = Bool(u"streamer_mode"_q);
	Config::Instance().setBool(u"streamer_mode"_q, true);
	ApplyNow();
	base::call_delayed(crl::time(1000), [=] {
		Config::Instance().setBool(u"streamer_mode"_q, mode);
		ApplyNow();
		active = false;
	});
}

[[nodiscard]] base::flat_set<QString> &GlobalHotkeys() {
	static auto result = base::flat_set<QString>();
	return result;
}

[[nodiscard]] bool MatchHotkey(QKeyEvent *e, const QString &key) {
	if (GlobalHotkeys().contains(key)) {
		return false;
	}
	const auto sequence = Hotkey(key);
	return !sequence.isEmpty()
		&& sequence.count() == 1
		&& sequence[0] == e->keyCombination();
}

#ifdef Q_OS_WIN

[[nodiscard]] UINT VirtualKey(Qt::Key key) {
	if (key >= Qt::Key_A && key <= Qt::Key_Z) {
		return UINT('A' + (key - Qt::Key_A));
	} else if (key >= Qt::Key_0 && key <= Qt::Key_9) {
		return UINT('0' + (key - Qt::Key_0));
	} else if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
		return UINT(VK_F1 + (key - Qt::Key_F1));
	}
	switch (key) {
	case Qt::Key_Print: return VK_SNAPSHOT;
	case Qt::Key_Pause: return VK_PAUSE;
	case Qt::Key_Insert: return VK_INSERT;
	case Qt::Key_Home: return VK_HOME;
	case Qt::Key_End: return VK_END;
	case Qt::Key_PageUp: return VK_PRIOR;
	case Qt::Key_PageDown: return VK_NEXT;
	case Qt::Key_ScrollLock: return VK_SCROLL;
	default: return 0;
	}
}

struct LowHotkey {
	UINT vk = 0;
	Qt::KeyboardModifiers modifiers;
	bool down = false;
	bool swallowUp = false;
};

void InjectKey(UINT vk, Qt::KeyboardModifiers modifiers) {
	auto inputs = std::vector<INPUT>();
	const auto add = [&](UINT key, bool up) {
		auto input = INPUT();
		input.type = INPUT_KEYBOARD;
		input.ki.wVk = WORD(key);
		input.ki.wScan = WORD(MapVirtualKeyW(key, MAPVK_VK_TO_VSC));
		input.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0)
			| ((key == VK_SNAPSHOT
				|| key == VK_INSERT
				|| key == VK_HOME
				|| key == VK_END
				|| key == VK_PRIOR
				|| key == VK_NEXT)
				? KEYEVENTF_EXTENDEDKEY
				: 0);
		inputs.push_back(input);
	};
	auto pressed = std::vector<UINT>();
	const auto modifier = [&](Qt::KeyboardModifier flag, UINT key) {
		if ((modifiers & flag) && !(GetAsyncKeyState(key) & 0x8000)) {
			add(key, false);
			pressed.push_back(key);
		}
	};
	modifier(Qt::ControlModifier, VK_CONTROL);
	modifier(Qt::AltModifier, VK_MENU);
	modifier(Qt::ShiftModifier, VK_SHIFT);
	modifier(Qt::MetaModifier, VK_LWIN);
	add(vk, false);
	add(vk, true);
	for (auto i = pressed.rbegin(); i != pressed.rend(); ++i) {
		add(*i, true);
	}
	SendInput(UINT(inputs.size()), inputs.data(), sizeof(INPUT));
}

[[nodiscard]] LowHotkey &ToggleKey() {
	static auto result = LowHotkey();
	return result;
}

[[nodiscard]] LowHotkey &ShotKey() {
	static auto result = LowHotkey();
	return result;
}

[[nodiscard]] Fn<bool(QString)> &CaptureCallback() {
	static auto result = Fn<bool(QString)>();
	return result;
}

[[nodiscard]] QString KeyName(UINT vk) {
	if (vk >= 'A' && vk <= 'Z') {
		return QString(QChar(char16_t(vk)));
	} else if (vk >= '0' && vk <= '9') {
		return QString(QChar(char16_t(vk)));
	} else if (vk >= VK_F1 && vk <= VK_F24) {
		return u"F"_q + QString::number(vk - VK_F1 + 1);
	}
	switch (vk) {
	case VK_SNAPSHOT: return u"Print"_q;
	case VK_PAUSE: return u"Pause"_q;
	case VK_INSERT: return u"Ins"_q;
	case VK_HOME: return u"Home"_q;
	case VK_END: return u"End"_q;
	case VK_PRIOR: return u"PgUp"_q;
	case VK_NEXT: return u"PgDown"_q;
	case VK_SCROLL: return u"ScrollLock"_q;
	default: return QString();
	}
}

[[nodiscard]] Qt::KeyboardModifiers CurrentModifiers() {
	auto result = Qt::KeyboardModifiers();
	if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
		result |= Qt::ControlModifier;
	}
	if (GetAsyncKeyState(VK_MENU) & 0x8000) {
		result |= Qt::AltModifier;
	}
	if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
		result |= Qt::ShiftModifier;
	}
	if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) {
		result |= Qt::MetaModifier;
	}
	return result;
}

[[nodiscard]] QString ModifiersText(Qt::KeyboardModifiers modifiers) {
	auto result = QString();
	if (modifiers & Qt::ControlModifier) {
		result += u"Ctrl+"_q;
	}
	if (modifiers & Qt::AltModifier) {
		result += u"Alt+"_q;
	}
	if (modifiers & Qt::ShiftModifier) {
		result += u"Shift+"_q;
	}
	if (modifiers & Qt::MetaModifier) {
		result += u"Meta+"_q;
	}
	return result;
}

LRESULT CALLBACK LowLevelKeyboard(int code, WPARAM wParam, LPARAM lParam) {
	if (code == HC_ACTION) {
		const auto data = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
		if (data->flags & LLKHF_INJECTED) {
			return CallNextHookEx(nullptr, code, wParam, lParam);
		}
		const auto vk = UINT(data->vkCode);
		const auto pressed = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
		const auto released = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
		if (CaptureCallback()) {
			const auto name = KeyName(vk);
			if (!name.isEmpty() && (pressed || (released && vk == VK_SNAPSHOT))) {
				const auto text = ModifiersText(CurrentModifiers()) + name;
				const auto callback = CaptureCallback();
				if (callback(text)) {
					return 1;
				}
			}
		} else {
			for (auto key : { &ToggleKey(), &ShotKey() }) {
				if (!key->vk || key->vk != vk) {
					continue;
				}
				if (released) {
					key->down = false;
					if (std::exchange(key->swallowUp, false)) {
						return 1;
					}
					continue;
				}
				if (!pressed || key->down) {
					continue;
				}
				if (CurrentModifiers() != key->modifiers) {
					continue;
				}
				key->down = true;
				if (key == &ShotKey()) {
					key->swallowUp = true;
					const auto modifiers = key->modifiers;
					crl::on_main([=] {
						TriggerShot();
						base::call_delayed(crl::time(150), [=] {
							InjectKey(vk, modifiers);
						});
					});
				} else {
					crl::on_main([] { ToggleByHotkey(); });
				}
				return 1;
			}
		}
	}
	return CallNextHookEx(nullptr, code, wParam, lParam);
}

void EnsureHook() {
	static auto hook = HHOOK();
	if (!hook) {
		hook = SetWindowsHookExW(
			WH_KEYBOARD_LL,
			LowLevelKeyboard,
			GetModuleHandleW(nullptr),
			0);
	}
}

void RegisterOne(LowHotkey &slot, const QString &config) {
	GlobalHotkeys().remove(config);
	slot = LowHotkey();
	const auto sequence = Hotkey(config);
	if (sequence.isEmpty()) {
		return;
	}
	const auto combination = sequence[0];
	const auto vk = VirtualKey(combination.key());
	if (!vk) {
		return;
	}
	slot.vk = vk;
	slot.modifiers = combination.keyboardModifiers()
		& (Qt::ControlModifier
			| Qt::AltModifier
			| Qt::ShiftModifier
			| Qt::MetaModifier);
	EnsureHook();
	GlobalHotkeys().emplace(config);
}

class HotkeyFilter final : public QAbstractNativeEventFilter {
public:
	bool nativeEventFilter(
			const QByteArray &eventType,
			void *message,
			qintptr *result) override {
		return false;
	}
};
#endif

void RegisterHotkeys() {
#ifdef Q_OS_WIN
	RegisterOne(ToggleKey(), u"streamer_hotkey"_q);
	RegisterOne(ShotKey(), u"streamer_hotkey_shot"_q);
#endif
}

bool CaptureFilter::eventFilter(QObject *object, QEvent *event) {
	const auto print = (event->type() == QEvent::KeyRelease)
		&& (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Print);
	if (event->type() == QEvent::KeyPress || print) {
		const auto key = static_cast<QKeyEvent*>(event);
		if (!key->isAutoRepeat()) {
			if (MatchHotkey(key, u"streamer_hotkey_shot"_q)) {
				TriggerShot();
				return true;
			} else if (MatchHotkey(key, u"streamer_hotkey"_q)) {
				ToggleByHotkey();
				return true;
			}
		}
	}
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
			const auto changes = &session->changes();
			const auto notify = [&](not_null<PeerData*> peer) {
				changes->peerUpdated(peer, Flag::Name | Flag::Photo);
			};
			session->data().enumerateUsers(notify);
			session->data().enumerateGroups(notify);
			session->data().enumerateBroadcasts([&](
					not_null<ChannelData*> channel) {
				notify(channel);
			});
			session->data().exteraRefreshAllViews();
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

void ApplyNow() {
	Apply();
	for (const auto widget : QApplication::topLevelWidgets()) {
		if (widget->isVisible()) {
			widget->repaint();
		}
	}
}

[[nodiscard]] const QStringList &PresetKeys() {
	static const auto result = [] {
		auto list = QStringList{
			u"streamer_full"_q,
			u"streamer_capture"_q,
			u"streamer_name"_q,
			u"streamer_avatar"_q,
			u"streamer_profile"_q,
			u"streamer_phone"_q,
			u"streamer_username"_q,
			u"streamer_id"_q,
			u"streamer_shown"_q,
			u"streamer_hidden"_q,
			u"streamer_notifications_off"_q,
			u"streamer_everywhere"_q,
		};
		for (const auto &type : { u"private"_q, u"bots"_q, u"groups"_q, u"channels"_q }) {
			for (const auto &part : { u"avatar"_q, u"name"_q, u"text"_q }) {
				list.push_back(u"streamer_"_q + type + '_' + part);
			}
		}
		return list;
	}();
	return result;
}

[[nodiscard]] QJsonObject Presets() {
	return QJsonDocument::fromJson(
		String(u"streamer_presets"_q).toUtf8()).object();
}

void SavePresets(const QJsonObject &presets) {
	Config::Instance().setString(
		u"streamer_presets"_q,
		QString::fromUtf8(
			QJsonDocument(presets).toJson(QJsonDocument::Compact)));
}

void SavePreset(const QString &name) {
	auto values = QJsonObject();
	for (const auto &key : PresetKeys()) {
		values.insert(key, Config::Instance().valueOf(key));
	}
	auto presets = Presets();
	presets.insert(name, values);
	SavePresets(presets);
}

void ApplyPreset(const QString &name) {
	const auto values = Presets().value(name).toObject();
	for (auto i = values.begin(); i != values.end(); ++i) {
		if (PresetKeys().contains(i.key())) {
			Config::Instance().setValue(i.key(), i.value());
		}
	}
	Apply();
}

void RemovePreset(const QString &name) {
	auto presets = Presets();
	presets.remove(name);
	SavePresets(presets);
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
				box->closeBox();
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

PhoneMode SelfPhone() {
	const auto streamer = Phone();
	if (streamer != PhoneMode::Shown) {
		return streamer;
	} else if (Bool(u"hide_phone"_q)) {
		return (Int(u"hide_phone_mode"_q) == 1)
			? PhoneMode::Unknown
			: PhoneMode::Spoiler;
	}
	return PhoneMode::Shown;
}

QString SelfPhoneText(const QString &formatted) {
	switch (SelfPhone()) {
	case PhoneMode::Unknown:
		return tr::lng_extera_unknown_number(tr::now);
	case PhoneMode::Spoiler: {
		auto result = formatted;
		auto digits = 0;
		for (auto &ch : result) {
			if (ch.isDigit() && ++digits > 1) {
				ch = QChar(0x2022);
			}
		}
		return result;
	}
	case PhoneMode::Shown:
		break;
	}
	return formatted;
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
	static auto result = QImage();
	static auto key = QString();
	const auto now = String(u"app_icon"_q)
		+ (Bool(u"app_icon_square"_q) ? u"/s"_q : u"/c"_q);
	if (result.isNull() || key != now) {
		key = now;
		result = Extera::AppIcon::Current(256);
	}
	return result;
}

bool NotificationsMuted() {
	return Active() && Bool(u"streamer_notifications_off"_q);
}

[[nodiscard]] bool Everywhere() {
	return Active() && (Full() || Bool(u"streamer_everywhere"_q));
}

bool HideNameOf(not_null<const PeerData*> peer) {
	return Everywhere()
		&& !peer->isSelf()
		&& ForPeer(const_cast<PeerData*>(peer.get())).name;
}

bool HideAvatarOf(not_null<const PeerData*> peer) {
	return Everywhere()
		&& !peer->isSelf()
		&& ForPeer(const_cast<PeerData*>(peer.get())).avatar;
}

bool HideTextOf(not_null<const PeerData*> peer) {
	return Everywhere() && ForPeer(const_cast<PeerData*>(peer.get())).text;
}

const QString &PlaceholderName() {
	static const auto result = QString(3, QChar(0x2022));
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
#ifdef Q_OS_WIN
	qApp->installNativeEventFilter(new HotkeyFilter());
#endif
	RegisterHotkeys();
	Config::Instance().changes(
	) | rpl::filter([](const QString &key) {
		return key.startsWith(u"streamer_hotkey"_q);
	}) | rpl::on_next([] {
		RegisterHotkeys();
	}, *(new rpl::lifetime()));
}

void ShowMenu(not_null<Window::SessionController*> controller) {
	static auto menu = base::unique_qptr<Ui::PopupMenu>();
	menu = base::make_unique_q<Ui::PopupMenu>(
		QApplication::activeWindow(),
		st::popupMenuWithIcons);
	menu->addAction(tr::lng_extera_streamer_settings(tr::now), [=] {
		ShowSettings(controller);
	}, &st::menuIconSettings);
	const auto full = Active() && Bool(u"streamer_full"_q);
	menu->addAction(full
		? tr::lng_exd_streamer_full_off(tr::now)
		: tr::lng_exd_streamer_full_on(tr::now), [=] {
		Config::Instance().setBool(u"streamer_full"_q, !full);
		if (!full) {
			Config::Instance().setBool(u"streamer_mode"_q, true);
		}
		Apply();
	}, &st::menuIconUserHide);
	menu->popup(QCursor::pos());
}

[[nodiscard]] QString ParseHotkey(const QString &text) {
	static const auto aliases = QMap<QString, QString>{
		{ u"ctrl"_q, u"Ctrl"_q },
		{ u"control"_q, u"Ctrl"_q },
		{ u"shift"_q, u"Shift"_q },
		{ u"alt"_q, u"Alt"_q },
		{ u"win"_q, u"Meta"_q },
		{ u"meta"_q, u"Meta"_q },
		{ u"super"_q, u"Meta"_q },
		{ u"cmd"_q, u"Meta"_q },
		{ u"prtsc"_q, u"Print"_q },
		{ u"prtscr"_q, u"Print"_q },
		{ u"prtscn"_q, u"Print"_q },
		{ u"printscreen"_q, u"Print"_q },
		{ u"print"_q, u"Print"_q },
		{ u"esc"_q, u"Esc"_q },
		{ u"escape"_q, u"Esc"_q },
		{ u"del"_q, u"Del"_q },
		{ u"delete"_q, u"Del"_q },
		{ u"ins"_q, u"Ins"_q },
		{ u"insert"_q, u"Ins"_q },
		{ u"pgup"_q, u"PgUp"_q },
		{ u"pageup"_q, u"PgUp"_q },
		{ u"pgdn"_q, u"PgDown"_q },
		{ u"pgdown"_q, u"PgDown"_q },
		{ u"pagedown"_q, u"PgDown"_q },
		{ u"enter"_q, u"Return"_q },
		{ u"space"_q, u"Space"_q },
	};
	auto parts = QStringList();
	for (auto part : text.split('+', Qt::SkipEmptyParts)) {
		part = part.trimmed();
		const auto normalized = part.toLower().remove(' ');
		parts.push_back(aliases.value(normalized, part));
	}
	if (parts.isEmpty()) {
		return QString();
	}
	const auto sequence = QKeySequence(
		parts.join('+'),
		QKeySequence::PortableText);
	if (sequence.count() != 1
		|| sequence[0].key() == Qt::Key_unknown
		|| !sequence[0].key()) {
		return QString();
	}
	return sequence.toString(QKeySequence::PortableText);
}

void ShowHotkeyBox(
		not_null<Window::SessionController*> controller,
		const QString &key,
		rpl::producer<QString> title,
		Fn<void()> changed) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(std::move(title));
		const auto label = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_exd_streamer_hotkey_press(),
			st::boxLabel));
		const auto current = std::make_shared<QString>(String(key));
		const auto shown = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			current->isEmpty()
				? tr::lng_exd_streamer_hotkey_none(tr::now)
				: QKeySequence(*current, QKeySequence::PortableText)
					.toString(QKeySequence::NativeText),
			st::boxTitle));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			tr::lng_exd_streamer_hotkey_manual()));
		const auto filter = Ui::CreateChild<QObject>(box.get());
		class KeyFilter final : public QObject {
		public:
			KeyFilter(QObject *parent, Fn<bool(QKeyEvent*)> callback)
			: QObject(parent)
			, _callback(std::move(callback)) {
			}
			bool eventFilter(QObject *object, QEvent *event) override {
				if (event->type() == QEvent::KeyPress) {
					return _callback(static_cast<QKeyEvent*>(event));
				} else if (event->type() == QEvent::KeyRelease) {
					const auto key = static_cast<QKeyEvent*>(event);
					return (key->key() == Qt::Key_Print) && _callback(key);
				}
				return false;
			}
		private:
			Fn<bool(QKeyEvent*)> _callback;
		};
#ifdef Q_OS_WIN
		EnsureHook();
		CaptureCallback() = [=](QString text) {
			const auto focused = QApplication::focusWidget();
			if (focused && field->isAncestorOf(focused)) {
				return false;
			}
			*current = QKeySequence(text, QKeySequence::PortableText)
				.toString(QKeySequence::PortableText);
			shown->setText(QKeySequence(*current, QKeySequence::PortableText)
				.toString(QKeySequence::NativeText));
			return true;
		};
		box->boxClosing() | rpl::on_next([] {
			CaptureCallback() = nullptr;
		}, box->lifetime());
		box->lifetime().add([] { CaptureCallback() = nullptr; });
#endif
		qApp->installEventFilter(new KeyFilter(filter, [=](QKeyEvent *e) {
			const auto focused = QApplication::focusWidget();
			if (focused && field->isAncestorOf(focused)) {
				return false;
			}
			const auto code = e->key();
			if (code == Qt::Key_Control
				|| code == Qt::Key_Shift
				|| code == Qt::Key_Alt
				|| code == Qt::Key_Meta
				|| code == Qt::Key_unknown) {
				return true;
			} else if (code == Qt::Key_Escape && !e->modifiers()) {
				return false;
			}
			*current = QKeySequence(e->keyCombination()).toString(
				QKeySequence::PortableText);
			shown->setText(QKeySequence(e->keyCombination()).toString(
				QKeySequence::NativeText));
			return true;
		}));
		(void)label;
		box->addButton(tr::lng_settings_save(), [=] {
			const auto typed = field->getLastText().trimmed();
			if (!typed.isEmpty()) {
				const auto parsed = ParseHotkey(typed);
				if (parsed.isEmpty()) {
					field->showError();
					return;
				}
				*current = parsed;
			}
			Config::Instance().setString(key, *current);
			changed();
			box->closeBox();
		});
		box->addLeftButton(tr::lng_exd_streamer_hotkey_clear(), [=] {
			Config::Instance().setString(key, QString());
			changed();
			box->closeBox();
		});
	}));
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

		Ui::AddSkip(content);
		AddToggleRow(
			content,
			u"streamer_notifications_off"_q,
			tr::lng_exd_streamer_notifications(),
			changed);
		AddToggleRow(
			content,
			u"streamer_everywhere"_q,
			tr::lng_exd_streamer_everywhere(),
			changed);
		const auto addHotkey = [&](
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
				) | rpl::map([](const QString &value) {
					return value.isEmpty()
						? tr::lng_exd_streamer_hotkey_none(tr::now)
						: QKeySequence(value, QKeySequence::PortableText)
							.toString(QKeySequence::NativeText);
				}),
				st::settingsButtonNoIcon,
				rpl::duplicate(*shared));
			button->setClickedCallback([=] {
				ShowHotkeyBox(
					controller,
					key,
					rpl::duplicate(*shared),
					changed);
			});
		};
		addHotkey(
			u"streamer_hotkey"_q,
			tr::lng_exd_streamer_hotkey());
		addHotkey(
			u"streamer_hotkey_shot"_q,
			tr::lng_exd_streamer_hotkey_shot());
		Ui::AddDividerText(content, tr::lng_exd_streamer_hotkey_about());

		Ui::AddSkip(content);
		Ui::AddSubsectionTitle(content, tr::lng_exd_streamer_presets());
		const auto presets = Presets();
		for (auto i = presets.begin(); i != presets.end(); ++i) {
			const auto name = i.key();
			const auto button = content->add(
				object_ptr<Ui::SettingsButton>(
					content,
					rpl::single(name),
					st::settingsButtonNoIcon));
			const auto remove = Ui::CreateChild<Ui::IconButton>(
				button,
				st::exteraPluginAction);
			remove->setIconOverride(
				&st::menuIconDeleteAttention,
				&st::menuIconDeleteAttention);
			button->widthValue() | rpl::on_next([=](int width) {
				remove->moveToRight(
					st::settingsButtonNoIcon.padding.right() / 2,
					(button->height() - remove->height()) / 2,
					width);
			}, remove->lifetime());
			remove->setClickedCallback([=] {
				RemovePreset(name);
				box->closeBox();
				ShowSettings(controller);
			});
			button->setClickedCallback([=] {
				ApplyPreset(name);
				box->closeBox();
				ShowSettings(controller);
				controller->showToast(
					tr::lng_exd_streamer_preset_applied(
						tr::now,
						lt_name,
						name));
			});
		}
		const auto save = content->add(
			object_ptr<Ui::SettingsButton>(
				content,
				tr::lng_exd_streamer_preset_save(),
				st::settingsButtonLightNoIcon));
		save->setClickedCallback([=] {
			controller->show(Box([=](not_null<Ui::GenericBox*> inner) {
				inner->setTitle(tr::lng_exd_streamer_preset_save());
				const auto field = inner->addRow(
					object_ptr<Ui::InputField>(
						inner,
						st::defaultInputField,
						tr::lng_exd_streamer_preset_name()));
				inner->setFocusCallback([=] { field->setFocusFast(); });
				inner->addButton(tr::lng_settings_save(), [=] {
					const auto name = field->getLastText().trimmed();
					if (name.isEmpty()) {
						field->showError();
						return;
					}
					SavePreset(name);
					inner->closeBox();
					box->closeBox();
					ShowSettings(controller);
				});
				inner->addButton(tr::lng_cancel(), [=] {
					inner->closeBox();
				});
			}));
		});
		Ui::AddSkip(content);

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
