#include "ui/widgets/scroll_area.h"
#include "extera/exd_md3.h"

#include "core/application.h"
#include "extera/extera_ai_chat.h"
#include "extera/extera_feed.h"
#include "extera/extera_full_player.h"
#include "extera/extera_pdf_viewer.h"
#include "window/window_controller.h"
#include "core/core_settings.h"
#include "extera/extera_config.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/animation_value.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/style/style_core.h"
#include "ui/style/style_core_font.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtGui/QPainterPath>
#include <QtWidgets/QApplication>
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/menu/menu.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

namespace Extera::Md3 {
namespace {

constexpr auto kGoogleSans = "Google Sans";

struct PreviewState {
	bool enabled = true;
	bool grouped = true;
	bool rounded = true;
	bool floating = true;
};

[[nodiscard]] PreviewState ReadState() {
	return {
		.enabled = Enabled(),
		.grouped = Bool(u"md3_grouped"_q),
		.rounded = Bool(u"md3_rounded_selection"_q),
		.floating = Bool(u"md3_floating_field"_q),
	};
}

[[nodiscard]] QColor Mix(QColor a, QColor b, float64 amount) {
	return QColor(
		int(a.red() + (b.red() - a.red()) * amount),
		int(a.green() + (b.green() - a.green()) * amount),
		int(a.blue() + (b.blue() - a.blue()) * amount));
}

void RequestRestart(not_null<Window::SessionController*> controller) {
	controller->show(Ui::MakeConfirmBox({
		.text = tr::lng_exd_md3_restart(),
		.confirmed = [] { Core::Restart(); },
		.confirmText = tr::lng_extera_restart(),
	}));
}

void RepaintAll() {
	for (const auto widget : QApplication::topLevelWidgets()) {
		widget->update();
	}
}

void PaintBar(QPainter &p, QRectF rect, const QColor &color) {
	p.setBrush(color);
	p.drawRoundedRect(rect, rect.height() / 2., rect.height() / 2.);
}

void PaintPreview(QPainter &p, QRect rect, const PreviewState &state) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	const auto md3 = state.enabled;
	const auto outer = md3 ? style::ConvertScale(18.) : 6.;
	const auto bg = st::windowBg->c;
	const auto fg = st::windowFg->c;
	const auto sub = st::windowSubTextFg->c;
	const auto card = Mix(bg, fg, 0.09);
	const auto frame = Mix(bg, fg, 0.03);

	p.setBrush(frame);
	p.drawRoundedRect(rect, outer, outer);

	const auto pad = 12;
	const auto inner = rect.marginsRemoved({ pad, pad, pad, pad });
	const auto listWidth = inner.width() * 4 / 10;
	const auto list = QRect(inner.x(), inner.y(), listWidth, inner.height());
	const auto chat = QRect(
		list.x() + list.width() + pad,
		inner.y(),
		inner.width() - listWidth - pad,
		inner.height());

	p.setBrush(bg);
	p.drawRoundedRect(list, md3 ? 12. : 3., md3 ? 12. : 3.);
	p.drawRoundedRect(chat, md3 ? 12. : 3., md3 ? 12. : 3.);

	const auto rowHeight = list.height() / 4;
	const auto colors = std::array<QColor, 4>{
		QColor(0xF4, 0x8F, 0xB1),
		QColor(0x81, 0xC7, 0x84),
		QColor(0x64, 0xB5, 0xF6),
		QColor(0xFF, 0xB7, 0x4D),
	};
	for (auto i = 0; i != 4; ++i) {
		const auto row = QRect(
			list.x(),
			list.y() + i * rowHeight,
			list.width(),
			rowHeight);
		if (i == 1) {
			p.setBrush(st::dialogsBgActive->c);
			if (md3 && state.rounded) {
				p.drawRoundedRect(
					QRectF(row).marginsRemoved({ 4., 2., 4., 2. }),
					10.,
					10.);
			} else {
				p.drawRect(row);
			}
		}
		const auto active = (i == 1);
		const auto size = rowHeight - 12;
		p.setBrush(colors[i]);
		p.drawEllipse(QRect(
			row.x() + 8,
			row.y() + (rowHeight - size) / 2,
			size,
			size));
		const auto textLeft = row.x() + 16 + size;
		const auto textWidth = row.width() - (textLeft - row.x()) - 10;
		PaintBar(p, QRectF(
			textLeft,
			row.y() + rowHeight * 0.3,
			textWidth * 0.7,
			6), active ? st::dialogsNameFgActive->c : fg);
		PaintBar(p, QRectF(
			textLeft,
			row.y() + rowHeight * 0.58,
			textWidth,
			5), active ? st::dialogsTextFgActive->c : sub);
	}

	const auto groupLeft = chat.x() + 10;
	const auto groupWidth = chat.width() - 20;
	const auto groupTop = chat.y() + 10;
	const auto itemHeight = 26;
	for (auto i = 0; i != 3; ++i) {
		const auto item = QRectF(
			groupLeft,
			groupTop + i * itemHeight,
			groupWidth,
			itemHeight);
		if (md3 && state.grouped) {
			const auto shape = item.marginsRemoved({
				0.,
				(i > 0) ? 1. : 0.,
				0.,
				(i < 2) ? 1. : 0. });
			const auto big = 12.;
			const auto small = 4.;
			const auto top = (i == 0) ? big : small;
			const auto bottom = (i == 2) ? big : small;
			auto path = QPainterPath();
			path.moveTo(shape.left() + top, shape.top());
			path.lineTo(shape.right() - top, shape.top());
			path.quadTo(shape.right(), shape.top(), shape.right(), shape.top() + top);
			path.lineTo(shape.right(), shape.bottom() - bottom);
			path.quadTo(shape.right(), shape.bottom(), shape.right() - bottom, shape.bottom());
			path.lineTo(shape.left() + bottom, shape.bottom());
			path.quadTo(shape.left(), shape.bottom(), shape.left(), shape.bottom() - bottom);
			path.lineTo(shape.left(), shape.top() + top);
			path.quadTo(shape.left(), shape.top(), shape.left() + top, shape.top());
			p.fillPath(path, card);
		} else if (i > 0) {
			p.setBrush(Mix(bg, fg, 0.1));
			p.drawRect(QRectF(item.left(), item.top(), item.width(), 1));
		}
		PaintBar(p, QRectF(
			item.left() + 12,
			item.top() + itemHeight / 2. - 3,
			item.width() * 0.45,
			6), fg);
		p.setBrush(md3 ? st::windowBgActive->c : sub);
		const auto toggle = QRectF(
			item.right() - 34,
			item.top() + itemHeight / 2. - 7,
			24,
			14);
		p.drawRoundedRect(toggle, 7., 7.);
	}

	const auto fieldHeight = 30.;
	const auto fieldTop = chat.bottom() - fieldHeight - 8.;
	const auto sendSize = fieldHeight;
	if (md3 && state.floating) {
		p.setBrush(card);
		const auto field = QRectF(
			chat.x() + 8,
			fieldTop,
			chat.width() - 24 - sendSize,
			fieldHeight);
		p.drawRoundedRect(field, fieldHeight / 2, fieldHeight / 2);
		p.setBrush(st::windowBgActive->c);
		p.drawEllipse(QRectF(
			field.right() + 8,
			fieldTop,
			sendSize,
			sendSize));
		PaintBar(p, QRectF(field.left() + 14, fieldTop + 12, field.width() * 0.4, 6), sub);
	} else {
		p.setBrush(card);
		const auto bar = QRectF(
			chat.x(),
			fieldTop - 4,
			chat.width(),
			chat.bottom() - fieldTop + 5);
		p.drawRect(bar);
		PaintBar(p, QRectF(bar.left() + 14, fieldTop + 12, bar.width() * 0.4, 6), sub);
	}
}

not_null<Ui::SettingsButton*> AddOption(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> title,
		const QString &key,
		Fn<void(bool)> changed) {
	const auto button = container->add(object_ptr<Ui::SettingsButton>(
		container,
		std::move(title),
		st::settingsButtonNoIcon));
	button->toggleOn(rpl::single(Bool(key)));
	button->toggledChanges(
	) | rpl::filter([=](bool value) {
		return value != Bool(key);
	}) | rpl::on_next([=](bool value) {
		Config::Instance().setBool(key, value);
		changed(value);
	}, button->lifetime());
	return button;
}

} // namespace

void ApplyBeforeStyles() {
	const auto on = Enabled();
	style::SetMd3(on, on ? 1 : 0, false);
	style::SetMd3Grouped(on && Bool(u"md3_grouped"_q));
	style::SetMd3Floating(on && Bool(u"md3_floating_field"_q));
	anim::SetExpressive(on && Bool(u"md3_expressive"_q));
	if (on
		&& Bool(u"md3_google_sans"_q)
		&& Core::App().settings().customFontFamily().isEmpty()) {
		style::SetCustomFont(QString::fromLatin1(kGoogleSans));
	}
}

bool Enabled() {
	return Bool(u"md3"_q);
}

bool RoundedSelection() {
	return Enabled() && Bool(u"md3_rounded_selection"_q);
}

bool FloatingField() {
	return Enabled() && Bool(u"md3_floating_field"_q);
}

bool Expressive() {
	return Enabled() && Bool(u"md3_expressive"_q);
}

bool DrawerCorners() {
	return Enabled() && Bool(u"md3_drawer_corners"_q);
}

void ShowSettingsBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_exd_md3_title());
		box->setWidth(st::boxWideWidth);
		const auto content = box->verticalLayout();
		const auto restart = box->lifetime().make_state<bool>(false);
		const auto state = box->lifetime().make_state<PreviewState>(
			ReadState());

		Ui::AddSkip(content);
		const auto preview = content->add(
			object_ptr<Ui::FixedHeightWidget>(
				content,
				style::ConvertScale(150)),
			style::Md3Grouped()
				? style::margins(
					style::ConvertScale(10),
					0,
					style::ConvertScale(10),
					0)
				: st::boxRowPadding);
		preview->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(preview);
			PaintPreview(p, preview->rect(), *state);
		}, preview->lifetime());
		const auto refresh = [=] {
			*state = ReadState();
			preview->update();
		};

		Ui::AddSkip(content);
		AddOption(content, tr::lng_exd_md3_title(), u"md3"_q, [=](bool) {
			*restart = true;
			refresh();
		});
		Ui::AddSkip(content);
		Ui::AddDividerText(content, tr::lng_exd_md3_about());

		Ui::AddSkip(content);
		Ui::AddSubsectionTitle(content, tr::lng_exd_md3_view());
		const auto fontName = [] {
			return Bool(u"md3_google_sans"_q)
				? QString::fromLatin1(kGoogleSans)
				: tr::lng_exd_md3_font_telegram(tr::now);
		};
		const auto fontLabel = content->lifetime().make_state<
			rpl::variable<QString>>(fontName());
		Settings::AddButtonWithLabel(
			content,
			tr::lng_exd_md3_font(),
			fontLabel->value(),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			const auto options = std::vector<QString>{
				QString::fromLatin1(kGoogleSans),
				tr::lng_exd_md3_font_telegram(tr::now),
			};
			controller->show(Box([=](not_null<Ui::GenericBox*> choice) {
				SingleChoiceBox(choice, {
					.title = tr::lng_exd_md3_font(),
					.options = options,
					.initialSelection = Bool(u"md3_google_sans"_q) ? 0 : 1,
					.callback = [=](int index) {
						const auto google = (index == 0);
						if (google != Bool(u"md3_google_sans"_q)) {
							Config::Instance().setBool(
								u"md3_google_sans"_q,
								google);
							*fontLabel = fontName();
							*restart = true;
						}
					},
				});
			}));
		});
		AddOption(
			content,
			tr::lng_exd_md3_grouped(),
			u"md3_grouped"_q,
			[=](bool value) {
				style::SetMd3Grouped(Enabled() && value);
				refresh();
				RepaintAll();
			});
		AddOption(
			content,
			tr::lng_exd_md3_rounded_selection(),
			u"md3_rounded_selection"_q,
			[=](bool) {
				refresh();
				RepaintAll();
			});
		AddOption(
			content,
			tr::lng_exd_md3_floating_field(),
			u"md3_floating_field"_q,
			[=](bool value) {
				style::SetMd3Floating(Enabled() && value);
				refresh();
				RepaintAll();
			});
		AddOption(
			content,
			tr::lng_exd_md3_drawer_corners(),
			u"md3_drawer_corners"_q,
			[=](bool) { refresh(); });
		AddOption(
			content,
			tr::lng_exd_md3_expressive(),
			u"md3_expressive"_q,
			[=](bool value) { anim::SetExpressive(Enabled() && value); });
		Ui::AddSkip(content);
		Ui::AddDividerText(content, tr::lng_exd_md3_view_about());

		Ui::AddSkip(content);
		Ui::AddSubsectionTitle(content, tr::lng_exd_md3_controls());
		AddOption(
			content,
			tr::lng_extera_md3_switches(),
			u"md3_switches"_q,
			[=](bool) { RepaintAll(); });
		AddOption(
			content,
			tr::lng_exd_md3_switches_muted(),
			u"md3_switches_muted"_q,
			[=](bool) { RepaintAll(); });
		AddOption(
			content,
			tr::lng_extera_md3_sliders(),
			u"md3_sliders"_q,
			[=](bool) { RepaintAll(); });
		Ui::AddSkip(content);

		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		box->boxClosing() | rpl::on_next([=] {
			if (*restart) {
				RequestRestart(controller);
			}
		}, box->lifetime());
	}));
}

bool ShowIntroIfNeeded(not_null<Window::SessionController*> controller) {
	if (Bool(u"md3_intro_shown"_q)) {
		return false;
	}
	Config::Instance().setBool(u"md3_intro_shown"_q, true);
	controller->show(Ui::MakeConfirmBox({
		.text = tr::lng_exd_md3_intro_text(),
		.confirmed = [](Fn<void()> close) { close(); },
		.cancelled = [](Fn<void()> close) {
			close();
			if (Enabled()) {
				Config::Instance().setBool(u"md3"_q, false);
				Core::Restart();
			}
		},
		.confirmText = tr::lng_exd_md3_intro_keep(),
		.cancelText = tr::lng_exd_md3_intro_revert(),
		.title = tr::lng_exd_md3_intro_title(),
		.strictCancel = true,
	}));
	return true;
}

} // namespace Extera::Md3
