/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "webauthn/cable_box.h"

#include "core/application.h"
#include "window/window_controller.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/painter.h"
#include "ui/effects/animations.h"
#include "intro/material_shapes.h"
#include "ui/expressive_fonts.h"
#include "lottie/lottie_icon.h"
#include "qr/qr_generate.h"
#include "base/unique_qptr.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtGui/QPainter>
#include <QtSvg/QSvgRenderer>

#include <rpl/flatten_latest.h>
#include <rpl/map.h>

namespace Platform::WebAuthn::Cable {
namespace {

[[nodiscard]] Window::Controller *ActiveController() {
	if (const auto active = Core::App().activeWindow()) {
		return active;
	}
	return Core::App().activePrimaryWindow();
}

[[nodiscard]] std::shared_ptr<Ui::Show> ActiveShow() {
	const auto controller = ActiveController();
	return controller ? controller->uiShow() : nullptr;
}

[[nodiscard]] QImage QrCenterImage() {
	const auto ratio = style::DevicePixelRatio();
	const auto side = st::passkeyCableQrCenter.width() * ratio;
	auto result = QImage(side, side, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(Qt::white);
	const auto radius = side / 5.;
	p.drawRoundedRect(0, 0, side, side, radius, radius);
	auto svg = QSvgRenderer(u":/gui/passkey_qr_center.svg"_q);
	if (svg.isValid()) {
		const auto target = (side * 94) / 100;
		const auto shift = (side - target) / 2.;
		svg.render(&p, QRectF(shift, shift, target, target));
	}
	return result;
}

[[nodiscard]] QImage RenderQr(const QString &text) {
	const auto data = Qr::Encode(text, Qr::Redundancy::Quartile);
	const auto ratio = style::DevicePixelRatio();
	const auto pixel = std::max(
		st::passkeyCableQrSize / std::max(data.size, 1),
		1);
	auto image = Qr::Generate(data, pixel * ratio, Qt::black, Qt::white);

	auto center = QrCenterImage();
	image = Qr::ReplaceCenter(std::move(image), center);
	image.setDevicePixelRatio(ratio);
	return image;
}

using Ui::ExpressiveFonts::Symbols;
constexpr auto kGlyphPasskey = Ui::ExpressiveFonts::kPasskey;
constexpr auto kGlyphBluetooth = Ui::ExpressiveFonts::kBluetooth;
constexpr auto kGlyphBluetoothOff = Ui::ExpressiveFonts::kBluetoothOff;
constexpr auto kGlyphError = Ui::ExpressiveFonts::kError;

[[nodiscard]] QColor WithAlpha(QColor color, double alpha) {
	color.setAlphaF(color.alphaF() * alpha);
	return color;
}

[[nodiscard]] double EaseOutBack(double t) {
	constexpr auto c1 = 1.70158;
	constexpr auto c3 = c1 + 1.;
	const auto u = t - 1.;
	return 1. + c3 * u * u * u + c1 * u * u;
}

// A slowly turning Material cookie with a glyph inside; pops in with a spring.
void AddHero(not_null<Ui::VerticalLayout*> content, char16_t glyph) {
	const auto side = st::passkeyCableHeroSize;
	// full width, painted in the middle: the layout keeps it centred at any width
	const auto hero = content->add(
		object_ptr<Ui::RpWidget>(content),
		st::passkeyCableHeroMargin);
	hero->resize(side, side);
	const auto started = crl::now();
	const auto animation = hero->lifetime().make_state<Ui::Animations::Basic>(
		[=] { hero->update(); });
	if (!anim::Disabled()) {
		animation->start();
	}
	hero->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(hero);
		auto hq = PainterHighQualityEnabler(p);
		const auto seconds = anim::Disabled()
			? 10.
			: (crl::now() - started) / 1000.;
		const auto pop = std::clamp(seconds / 0.5, 0., 1.);
		const auto scale = 0.6 + 0.4 * EaseOutBack(pop);
		const auto r = side / 2.;
		p.translate(hero->width() / 2., r);
		p.scale(scale, scale);
		p.save();
		p.rotate(seconds * 9.);
		const auto shape = Ui::MaterialShapes::Path(
			Ui::MaterialShapes::Shape::Cookie9,
			QRectF(-r, -r, 2 * r, 2 * r));
		p.fillPath(shape, WithAlpha(st::windowBgActive->c, 0.22));
		p.restore();
		p.setPen(st::windowActiveTextFg);
		p.setFont(Symbols(side * 0.44, true));
		p.drawText(
			QRectF(-r, -r, 2 * r, 2 * r),
			Qt::AlignCenter,
			QString(QChar(glyph)));
	}, hero->lifetime());
}

// A tonal card with a round glyph badge and text, for status lines and errors.
void AddStatusCard(
		not_null<Ui::VerticalLayout*> content,
		rpl::producer<QString> text,
		char16_t glyph,
		bool alert) {
	const auto card = content->add(
		object_ptr<Ui::RpWidget>(content),
		st::passkeyCableStatusMargin);
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		card,
		std::move(text),
		st::passkeyCableStatusLabel);
	const auto padding = st::passkeyCableStatusPadding;
	const auto badge = st::passkeyCableStatusBadge;
	const auto skip = st::passkeyCableStatusSkip;
	const auto layout = [=](int width) {
		label->resizeToWidth(std::max(
			width - padding.left() - badge - skip - padding.right(),
			1));
		const auto inner = std::max(label->height(), badge);
		label->moveToLeft(
			padding.left() + badge + skip,
			padding.top() + (inner - label->height()) / 2);
		card->resize(width, padding.top() + inner + padding.bottom());
	};
	card->widthValue(
	) | rpl::on_next(layout, card->lifetime());
	label->heightValue(
	) | rpl::on_next([=] {
		layout(card->width());
	}, card->lifetime());
	card->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(card);
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = st::passkeyCableStatusRadius;
		p.setPen(Qt::NoPen);
		p.setBrush(st::boxSearchBg);
		p.drawRoundedRect(card->rect(), radius, radius);
		const auto color = alert
			? st::attentionButtonFg->c
			: st::windowActiveTextFg->c;
		const auto circle = QRectF(
			padding.left(),
			(card->height() - badge) / 2.,
			badge,
			badge);
		p.setBrush(WithAlpha(color, 0.16));
		p.drawEllipse(circle);
		p.setPen(color);
		p.setFont(Symbols(badge * 0.55, true));
		p.drawText(circle, Qt::AlignCenter, QString(QChar(glyph)));
	}, card->lifetime());
}

} // namespace

void ShowCableToast(const QString &text) {
	if (const auto controller = ActiveController()) {
		controller->showToast(text);
	}
}

bool ShowCableBox(BoxContent &&content) {
	const auto show = ActiveShow();
	if (!show) {
		return false;
	}
	const auto state = content.state;
	const auto isRegister = content.isRegister;
	const auto bluetooth = content.bluetoothAvailable;
	const auto qrImage = bluetooth
		? std::make_shared<QImage>(RenderQr(content.qrText))
		: std::shared_ptr<QImage>();
	const auto securityKeyChosen = content.securityKeyChosen;
	const auto cancelled = content.cancelled;
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setStyle(st::passkeyCableBox);
		auto titleText = (state->sheet.value(
		) | rpl::map([=](Sheet sheet) -> rpl::producer<QString> {
			switch (sheet) {
			case Sheet::Qr:
				return isRegister
					? tr::lng_passkey_cable_register_title()
					: tr::lng_passkey_cable_login_title();
			case Sheet::Connecting:
				return tr::lng_passkey_cable_connecting();
			case Sheet::Continue:
				return tr::lng_passkey_cable_continue();
			case Sheet::Error:
				return tr::lng_passkey_cable_error_title();
			}
			Unexpected("Sheet value in cable box title.");
		}) | rpl::flatten_latest());
		const auto titleValue = box->lifetime().make_state<
			rpl::variable<QString>>(std::move(titleText));

		state->closeRequests.events() | rpl::on_next([=] {
			box->closeBox();
		}, box->lifetime());

		const auto current = box->lifetime().make_state<
			base::unique_qptr<Ui::VerticalLayout>>();
		const auto rebuild = [=](Sheet sheet) {
			current->reset();
			const auto outer = box->verticalLayout();
			const auto content = outer->add(
				object_ptr<Ui::VerticalLayout>(outer));
			current->reset(content);

			// Material 3 dialog with a hero icon: shape on top, centred headline.
			if (sheet == Sheet::Qr || sheet == Sheet::Error) {
				AddHero(content, (sheet == Sheet::Error)
					? kGlyphError
					: kGlyphPasskey);
			} else {
				content->add(object_ptr<Ui::FixedHeightWidget>(
					content,
					st::passkeyCableHeroMargin.top()));
			}
			content->add(
				object_ptr<Ui::FlatLabel>(
					content,
					titleValue->value(),
					st::passkeyCableTitle),
				st::passkeyCableTitleMargin,
				style::al_top);

			const auto addDescription = [&](rpl::producer<QString> text) {
				content->add(
					object_ptr<Ui::FlatLabel>(
						content,
						std::move(text),
						st::boxLabel),
					st::boxRowPadding);
			};
			const auto addPill = [&](
					rpl::producer<QString> text,
					Fn<void()> callback) {
				const auto button = content->add(
					object_ptr<Ui::RoundButton>(
						content,
						std::move(text),
						st::passkeyCablePrimaryButton),
					style::margins(
						st::boxRowPadding.left(),
						2 * st::boxLittleSkip,
						st::boxRowPadding.right(),
						2 * st::boxLittleSkip),
					style::al_justify);
				button->setFullRadius(true);
				button->setWavy(true);
				button->setClickedCallback(std::move(callback));
			};
			box->clearButtons();
			{
				auto bottom = object_ptr<Ui::RoundButton>(
					box,
					((sheet == Sheet::Error)
						? tr::lng_close()
						: tr::lng_cancel()),
					st::defaultLightButton);
				bottom->setFullRadius(true);
				bottom->setWavy(true);
				bottom->resizeToWidth(st::boxWidth
					- st::passkeyCableBox.buttonPadding.left()
					- st::passkeyCableBox.buttonPadding.right());
				bottom->setClickedCallback([=] {
					box->closeBox();
				});
				box->addButton(std::move(bottom));
			}

			switch (sheet) {
			case Sheet::Qr: {
				if (bluetooth) {
					addDescription(isRegister
						? tr::lng_passkey_cable_register_about()
						: tr::lng_passkey_cable_login_about());

					const auto size = qrImage->width()
						/ style::DevicePixelRatio();
					const auto skip = st::introQrBackgroundSkip;
					const auto radius = st::passkeyCableQrRadius;
					const auto full = size + 2 * skip;
					const auto qr = content->add(
						object_ptr<Ui::RpWidget>(content),
						style::margins(
							0,
							st::boxLittleSkip,
							0,
							st::boxLittleSkip));
					qr->resize(full, full);
					qr->paintRequest(
					) | rpl::on_next([=] {
						auto p = QPainter(qr);
						auto hq = PainterHighQualityEnabler(p);
						const auto x = (qr->width() - full) / 2;
						p.setPen(Qt::NoPen);
						p.setBrush(Qt::white);
						p.drawRoundedRect(
							QRect(x, 0, full, full),
							radius,
							radius);
						p.drawImage(x + skip, skip, *qrImage);
					}, qr->lifetime());

					AddStatusCard(
						content,
						tr::lng_passkey_cable_bluetooth(),
						kGlyphBluetooth,
						false);
				} else {
					AddStatusCard(
						content,
						tr::lng_passkey_cable_no_bluetooth(),
						kGlyphBluetoothOff,
						true);
				}

				addPill(tr::lng_passkey_method_key(), [=] {
					if (securityKeyChosen) {
						securityKeyChosen();
					}
				});
				content->add(
					object_ptr<Ui::FlatLabel>(
						content,
						tr::lng_passkey_cable_key_about(),
						st::passkeyCableHintLabel),
					style::margins(
						st::boxRowPadding.left(),
						0,
						st::boxRowPadding.right(),
						st::boxLittleSkip),
					style::al_top);
			} break;

			case Sheet::Connecting:
			case Sheet::Continue: {
				const auto side = (st::settingsCloudPasswordIconSize * 7)
					/ 10;
				const auto padding = st::settingLocalPasscodeIconPadding;
				auto owned = Lottie::MakeIcon({
					.name = u"passkeys"_q,
					.sizeOverride = { side, side },
					.limitFps = true,
				});
				const auto icon = owned.get();
				const auto widget = content->add(
					object_ptr<Ui::RpWidget>(content),
					style::margins(),
					style::al_top);
				widget->resize(
					side,
					padding.top() + side + padding.bottom());
				widget->lifetime().add([kept = std::move(owned)] {});
				widget->paintRequest(
				) | rpl::on_next([=] {
					if (!icon->valid()) {
						widget->update();
						return;
					}
					auto p = QPainter(widget);
					icon->paint(
						p,
						(widget->width() - side) / 2,
						padding.top());
					const auto last = icon->framesCount() - 1;
					if (anim::Disabled()) {
						if (last > 0 && icon->frameIndex() != last) {
							icon->jumpTo(
								last,
								[=] { widget->update(); });
						}
					} else if (!icon->animating()) {
						icon->animate(
							[=] { widget->update(); },
							0,
							last);
					}
				}, widget->lifetime());

				addDescription(state->sheet.value(
				) | rpl::map([](Sheet sheet) -> rpl::producer<QString> {
					return (sheet == Sheet::Continue)
						? tr::lng_passkey_cable_follow()
						: rpl::single(QString());
				}) | rpl::flatten_latest());
			} break;

			case Sheet::Error: {
				AddStatusCard(content, state->error.value(), kGlyphError, true);
			} break;
			}

			outer->update();
		};

		const auto sameContent = [](Sheet a, Sheet b) {
			const auto progress = [](Sheet sheet) {
				return (sheet == Sheet::Connecting)
					|| (sheet == Sheet::Continue);
			};
			return (a == b) || (progress(a) && progress(b));
		};
		const auto shown = box->lifetime().make_state<std::optional<Sheet>>();
		state->sheet.value() | rpl::on_next([=](Sheet sheet) {
			if (*shown && sameContent(**shown, sheet)) {
				*shown = sheet;
				return;
			}
			*shown = sheet;
			rebuild(sheet);
		}, box->lifetime());

		box->boxClosing() | rpl::on_next([=] {
			if (cancelled) {
				crl::on_main(cancelled);
			}
		}, box->lifetime());
	}));
	return true;
}

} // namespace Platform::WebAuthn::Cable
