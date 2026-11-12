/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_qr.h"

#include "boxes/abstract_box.h"
#include "data/components/passkeys.h"
#include "data/data_passkey_deserialize.h"
#include "intro/intro_phone.h"
#include "intro/intro_expressive.h"
#include <QtGui/QPainterPath>
#include "intro/intro_widget.h"
#include "intro/intro_password_check.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/effects/radial_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "main/main_app_config.h"
#include "main/main_account.h"
#include "ui/boxes/confirm_box.h"
#include "core/application.h"
#include "core/core_cloud_password.h"
#include "core/update_checker.h"
#include "base/unixtime.h"
#include "qr/qr_generate.h"
#include "platform/platform_webauthn.h"
#include "styles/style_intro.h"

namespace Intro {
namespace details {
namespace {

// exDesktop / Material 3 Expressive QR: round modules, rounded finder squares, and a clear
// middle for the badge. Drawn in the palette's colours on a transparent background.
[[nodiscard]] QImage TelegramQr(const Qr::Data &data, int pixel, int max = 0) {
	Expects(data.size > 0);

	if (max > 0 && data.size * pixel > max) {
		pixel = std::max(max / data.size, 1);
	}
	const auto ratio = style::DevicePixelRatio();
	const auto unit = double(pixel * ratio);
	const auto n = data.size;
	auto result = QImage(
		QSize(n * pixel, n * pixel) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	const auto fg = st::windowFgActive->c;
	auto p = QPainter(&result);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(fg);

	const auto finder = [&](int x, int y) {
		return (x < 7 && y < 7) || (x >= n - 7 && y < 7) || (x < 7 && y >= n - 7);
	};
	const auto middle = std::max(5, n / 4);
	const auto inMiddle = [&](int x, int y) {
		const auto c = n / 2.;
		return std::abs(x + 0.5 - c) < middle / 2. && std::abs(y + 0.5 - c) < middle / 2.;
	};
	for (auto y = 0; y != n; ++y) {
		for (auto x = 0; x != n; ++x) {
			if (!data.values[y * n + x] || finder(x, y) || inMiddle(x, y)) {
				continue;
			}
			const auto d = unit * 0.86;
			p.drawEllipse(QRectF(
				x * unit + (unit - d) / 2.,
				y * unit + (unit - d) / 2.,
				d,
				d));
		}
	}
	const auto drawFinder = [&](int fx, int fy) {
		const auto outer = QRectF(fx * unit, fy * unit, 7 * unit, 7 * unit);
		auto ring = QPainterPath();
		ring.addRoundedRect(outer, unit * 2.2, unit * 2.2);
		auto hole = QPainterPath();
		hole.addRoundedRect(outer.marginsRemoved(QMarginsF(unit, unit, unit, unit)), unit * 1.5, unit * 1.5);
		p.drawPath(ring.subtracted(hole));
		p.drawRoundedRect(
			outer.marginsRemoved(QMarginsF(unit * 2, unit * 2, unit * 2, unit * 2)),
			unit * 1.1,
			unit * 1.1);
	};
	drawFinder(0, 0);
	drawFinder(n - 7, 0);
	drawFinder(0, n - 7);
	p.end();
	return result;
}

[[nodiscard]] QColor QrActiveColor() {
	return QColor(0x40, 0xA7, 0xE3); // Default windowBgActive.
}

[[nodiscard]] not_null<Ui::RpWidget*> PrepareQrWidget(
		not_null<QWidget*> parent,
		rpl::producer<QByteArray> codes,
		rpl::producer<bool> active) {
	struct State {
		explicit State(Fn<void()> callback)
		: waiting(callback, st::defaultInfiniteRadialAnimation) {
		}

		QImage previous;
		QImage qr;
		QImage center;
		Ui::Animations::Simple shown;
		Ui::InfiniteRadialAnimation waiting;
		Ui::Animations::Basic clock;
		double angle = 0.;
		crl::time last = 0;
	};
	auto qrs = std::move(
		codes
	) | rpl::map([](const QByteArray &code) {
		return Qr::Encode(code, Qr::Redundancy::Quartile);
	});
	auto palettes = rpl::single(rpl::empty) | rpl::then(
		style::PaletteChanged()
	);
	class QrWidget final : public Ui::RpWidget {
	public:
		using RpWidget::RpWidget;

		QAccessible::Role accessibilityRole() override {
			return QAccessible::Role::Graphic;
		}
		QString accessibilityName() override {
			return tr::lng_intro_qr_title(tr::now);
		}

	};
	auto result = Ui::CreateChild<QrWidget>(parent.get());
	const auto state = result->lifetime().make_state<State>(
		[=] { result->update(); });
	state->waiting.start();
	state->clock.init([=](crl::time now) {
		if (state->last) {
			state->angle += std::min<crl::time>(now - state->last, 100) / 1000. * 0.32;
		}
		state->last = now;
		result->update();
		return true;
	});
	state->clock.start();
	const auto outline = st::introQrOutlineSpace;
	const auto size = st::introQrMaxSize + 2 * st::introQrBackgroundSkip + 2 * outline;
	result->resize(size, size);
	rpl::combine(
		std::move(qrs),
		rpl::duplicate(palettes)
	) | rpl::map([](const Qr::Data &code, const auto &) {
		return TelegramQr(code, st::introQrPixel, st::introQrMaxSize);
	}) | rpl::on_next([=](QImage &&image) {
		state->previous = std::move(state->qr);
		state->qr = std::move(image);
		state->waiting.stop();
		state->shown.stop();
		state->shown.start(
			[=] { result->update(); },
			0.,
			1.,
			st::fadeWrapDuration);
	}, result->lifetime());
	std::move(
		palettes
	) | rpl::map([] {
		return TelegramLogoImage();
	}) | rpl::on_next([=](QImage &&image) {
		state->center = std::move(image);
	}, result->lifetime());
	std::move(
		active
	) | rpl::on_next([=](bool active) {
		if (active) {
			state->previous = QImage();
			state->qr = QImage();
			state->shown.stop();
			state->waiting.start();
		} else {
			state->waiting.stop(anim::type::instant);
		}
		result->update();
	}, result->lifetime());
	result->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = QPainter(result);
		const auto has = !state->qr.isNull();
		const auto shown = has ? state->shown.value(1.) : 0.;
		const auto usualSize = 41;
		const auto pixel = std::clamp(
			st::introQrMaxSize / usualSize,
			1,
			st::introQrPixel);
		const auto size = has
			? (state->qr.size() / style::DevicePixelRatio())
			: QSize(usualSize * pixel, usualSize * pixel);
		const auto qr = QRect(
			(result->width() - size.width()) / 2,
			(result->height() - size.height()) / 2,
			size.width(),
			size.height());
		const auto radius = st::introQrBackgroundRadius;
		const auto skip = st::introQrBackgroundSkip;
		const auto seconds = crl::now() / 1000.;
		auto hq = PainterHighQualityEnabler(p);
		const auto card = QRectF(qr.marginsAdded({ skip, skip, skip, skip }));

		// a slow wavy outline around the card
		{
			const auto gap = style::ConvertScaleExact(6.);
			auto base = QPainterPath();
			base.addRoundedRect(card.marginsAdded({ gap, gap, gap, gap }), radius + gap, radius + gap);
			const auto length = base.length();
			const auto steps = std::max(120, int(length / 2.));
			const auto waves = std::max(10., std::round(length / 30.));
			const auto amplitude = style::ConvertScaleExact(1.6);
			auto wavy = QPainterPath();
			for (auto i = 0; i <= steps; ++i) {
				const auto t = double(i) / steps;
				const auto at = base.pointAtPercent(t);
				const auto ahead = base.pointAtPercent(std::fmod(t + 0.001, 1.));
				auto nx = ahead.y() - at.y(), ny = -(ahead.x() - at.x());
				const auto len = std::hypot(nx, ny);
				if (len > 0.) {
					nx /= len;
					ny /= len;
				}
				const auto push = amplitude * std::sin(t * waves * 2. * M_PI - seconds * 1.4);
				const auto point = QPointF(at.x() + nx * push, at.y() + ny * push);
				(i == 0) ? wavy.moveTo(point) : wavy.lineTo(point);
			}
			wavy.closeSubpath();
			auto color = st::windowBgActive->c;
			color.setAlphaF(color.alphaF() * 0.45);
			p.setPen(QPen(color, style::ConvertScaleExact(1.6)));
			p.setBrush(Qt::NoBrush);
			p.drawPath(wavy);
		}

		// the tonal card (light enough for any phone camera)
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgActive);
		p.drawRoundedRect(card, radius, radius);
		if (!state->qr.isNull()) {
			if (shown == 1.) {
				state->previous = QImage();
			} else if (!state->previous.isNull()) {
				p.drawImage(qr, state->previous);
			}
			p.setOpacity(shown);
			p.drawImage(qr, state->qr);
			p.setOpacity(1.);
		}
		const auto rect = QRect(
			(result->width() - st::introQrCenterSize) / 2,
			(result->height() - st::introQrCenterSize) / 2,
			st::introQrCenterSize,
			st::introQrCenterSize);
		Expressive::PaintSunBadge(
			p,
			QPointF(rect.center()) + QPointF(0.5, 0.5),
			st::introQrCenterSize * 0.92,
			st::windowFgActive->c,
			st::windowBgActive->c,
			anim::color(st::windowBgActive, st::windowFgActive, 0.35),
			state->angle,
			seconds);
		if (!anim::Disabled() && state->waiting.animating()) {
			auto hq = PainterHighQualityEnabler(p);
			const auto line = st::radialLine;
			const auto radial = state->waiting.computeState();
			auto pen = QPen(st::windowFgActive->c);
			pen.setWidth(line);
			pen.setCapStyle(Qt::RoundCap);
			p.setOpacity(radial.shown * (1. - shown));
			p.setPen(pen);
			p.drawArc(
				rect.marginsAdded({ line, line, line, line }),
				radial.arcFrom,
				radial.arcLength);
			p.setOpacity(1.);
		}
	}, result->lifetime());
	return result;
}

} // namespace

QrWidget::QrWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data)
, _refreshTimer([=] { refreshCode(); }) {
	setTitleText(rpl::single(QString()));
	setDescriptionText(rpl::single(QString()));
	setErrorCentered(true);

	cancelNearestDcRequest();

	account->mtpUpdates(
	) | rpl::on_next([=](const MTPUpdates &updates) {
		checkForTokenUpdate(updates);
	}, lifetime());

	setupControls();
	account->mtp().mainDcIdValue(
	) | rpl::on_next([=] {
		api().request(base::take(_requestId)).cancel();
		refreshCode();
	}, lifetime());

	account->appConfig().value(
	) | rpl::filter([=] {
		return !_passkey;
	}) | rpl::on_next([=] {
		setupPasskeyLink();
	}, lifetime());
}

QString QrWidget::accessibilityName() {
	return tr::lng_intro_qr_title(tr::now);
}

QString QrWidget::accessibilityDescription() {
	const auto phrases = {
		tr::lng_intro_qr_step1,
		tr::lng_intro_qr_step2,
		tr::lng_intro_qr_step3,
	};
	auto result = QString();
	auto index = 0;
	for (const auto &phrase : phrases) {
		result.append(QString::number(++index)).append(". ").append(phrase(tr::now)).append('\n');
	}
	return result;
}

int QrWidget::errorTop() const {
	return contentTop() + st::introQrErrorTop;
}

void QrWidget::checkForTokenUpdate(const MTPUpdates &updates) {
	updates.match([&](const MTPDupdateShort &data) {
		checkForTokenUpdate(data.vupdate());
	}, [&](const MTPDupdates &data) {
		for (const auto &update : data.vupdates().v) {
			checkForTokenUpdate(update);
		}
	}, [&](const MTPDupdatesCombined &data) {
		for (const auto &update : data.vupdates().v) {
			checkForTokenUpdate(update);
		}
	}, [](const auto &) {});
}

void QrWidget::checkForTokenUpdate(const MTPUpdate &update) {
	update.match([&](const MTPDupdateLoginToken &data) {
		if (_requestId) {
			_forceRefresh = true;
		} else {
			_refreshTimer.cancel();
			refreshCode();
		}
	}, [](const auto &) {});
}

void QrWidget::submit() {
	goNextOrBack<PhoneWidget>();
}

rpl::producer<QString> QrWidget::nextButtonText() const {
	return rpl::single(QString());
}

void QrWidget::setupControls() {
	const auto code = PrepareQrWidget(
		this,
		_qrCodes.events(),
		_qrActive.events());
	rpl::combine(
		sizeValue(),
		code->widthValue()
	) | rpl::on_next([=](QSize size, int codeWidth) {
		code->moveToLeft(
			(size.width() - codeWidth) / 2,
			contentTop() + st::introQrTop);
	}, code->lifetime());

	const auto title = Ui::CreateChild<Ui::FlatLabel>(
		this,
		tr::lng_intro_qr_title(),
		st::introQrTitle);
	rpl::combine(
		sizeValue(),
		title->widthValue()
	) | rpl::on_next([=](QSize size, int titleWidth) {
		title->resizeToWidth(st::introQrTitleWidth);
		const auto oneLine = st::introQrTitle.style.font->height;
		const auto topDelta = (title->height() - oneLine);
		title->moveToLeft(
			(size.width() - title->width()) / 2,
			contentTop() + st::introQrTitleTop - topDelta);
	}, title->lifetime());

	const auto steps = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto texts = {
		tr::lng_intro_qr_step1,
		tr::lng_intro_qr_step2,
		tr::lng_intro_qr_step3,
	};
	auto index = 0;
	for (const auto &text : texts) {
		const auto label = steps->add(
			object_ptr<Ui::FlatLabel>(
				steps,
				text(tr::rich),
				st::introQrStep),
			st::introQrStepMargins);
		// Material 3: numbered steps as small tonal circles
		const auto number = Ui::CreateChild<Ui::RpWidget>(steps);
		const auto digit = QString::number(++index);
		const auto badge = st::introQrStepBadge;
		number->resize(badge, badge);
		number->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(number);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::lightButtonBg);
			p.drawEllipse(QRectF(0, 0, badge, badge));
			p.setPen(st::lightButtonFg);
			p.setFont(st::semiboldFont);
			p.drawText(QRect(0, 0, badge, badge), Qt::AlignCenter, digit);
		}, number->lifetime());
		rpl::combine(
			label->positionValue(),
			label->heightValue()
		) | rpl::on_next([=](QPoint position, int) {
			const auto line = st::introQrStep.style.font->height;
			number->moveToLeft(
				position.x() - badge - st::introQrStepBadgeSkip,
				position.y() + (line - badge) / 2);
		}, number->lifetime());
	}
	// leave room under the last step for its round number badge
	steps->add(object_ptr<Ui::FixedHeightWidget>(steps, st::introQrStepBadge / 2));
	steps->resizeToWidth(st::introQrLabelsWidth);
	rpl::combine(
		sizeValue(),
		steps->widthValue()
	) | rpl::on_next([=](QSize size, int stepsWidth) {
		steps->moveToLeft(
			(size.width() - stepsWidth) / 2,
			contentTop() + st::introQrStepsTop);
	}, steps->lifetime());

	_skip = Ui::CreateChild<Ui::RoundButton>(
		this,
		tr::lng_intro_qr_phone(),
		st::introQrPhoneButton);
	_skip->setWavy(true);
	rpl::combine(
		sizeValue(),
		_skip->widthValue()
	) | rpl::on_next([=](QSize size, int skipWidth) {
		_skip->moveToLeft(
			(size.width() - skipWidth) / 2,
			contentTop() + st::introQrSkipTop);
	}, _skip->lifetime());

	_skip->setClickedCallback([=] { submit(); });
}

void QrWidget::setupPasskeyLink() {
	Expects(!_passkey);

	if (!account().appConfig().settingsDisplayPasskeys()
		|| !Platform::WebAuthn::IsSupported()) {
		return;
	}
	_passkey = Ui::CreateChild<Ui::RoundButton>(
		this,
		tr::lng_intro_qr_passkey(),
		st::introQrPasskeyButton);
	_passkey->setWavy(true);
	_passkey->show();
	rpl::combine(
		sizeValue(),
		_passkey->widthValue()
	) | rpl::on_next([=](QSize size, int passkeyWidth) {
		_passkey->moveToLeft(
			(size.width() - passkeyWidth) / 2,
			(contentTop()
				+ st::introQrSkipTop
				+ st::introQrPhoneButton.height
				+ st::introQrButtonsSkip));
	}, _passkey->lifetime());

	_passkey->setClickedCallback([=] {
		const auto attempt = [=](
				const ::Data::Passkey::LoginData &loginData) {
			const auto initialDc = _passkeyLoginDc;
			Platform::WebAuthn::Login(loginData, crl::guard(this, [=](
					Platform::WebAuthn::LoginResult result) {
				if (result.userHandle.isEmpty()) {
					using Error = Platform::WebAuthn::Error;
					if (result.error == Error::UnsignedBuild) {
						showError(
							tr::lng_settings_passkeys_unsigned_error());
					}
					return;
				}
				::Data::FinishPasskeyLogin(
					api(),
					initialDc,
					result,
					[=](const MTPauth_Authorization &auth) { done(auth); },
					[=](QString error) {
						_passkeyLoginData = std::nullopt;
						if (error == u"SESSION_PASSWORD_NEEDED"_q) {
							sendCheckPasswordRequest();
						} else {
							showError(rpl::single(error));
						}
					});
			}));
		};
		if (_passkeyLoginData
			&& (crl::now() - _passkeyLoginTime
				< crl::time(_passkeyLoginData->timeout))) {
			attempt(*_passkeyLoginData);
		} else {
			_passkeyLoginData = std::nullopt;
			const auto initedDc = api().instance().mainDcId();
			::Data::InitPasskeyLogin(api(), [=](
				const ::Data::Passkey::LoginData &loginData) {
				_passkeyLoginData = loginData;
				_passkeyLoginTime = crl::now();
				_passkeyLoginDc = initedDc;
				attempt(loginData);
			});
		}
	});
}

void QrWidget::refreshCode() {
	if (_requestId || _stopped) {
		return;
	}
	_requestId = api().request(MTPauth_ExportLoginToken(
		MTP_int(ApiId),
		MTP_string(ApiHash),
		MTP_vector<MTPlong>(0)
	)).done([=](const MTPauth_LoginToken &result) {
		handleTokenResult(result);
	}).fail([=](const MTP::Error &error) {
		showTokenError(error);
	}).send();
}

void QrWidget::handleTokenResult(const MTPauth_LoginToken &result) {
	result.match([&](const MTPDauth_loginToken &data) {
		_requestId = 0;
		showToken(data.vtoken().v);

		if (base::take(_forceRefresh)) {
			refreshCode();
		} else {
			const auto left = data.vexpires().v - base::unixtime::now();
			_refreshTimer.callOnce(std::max(left, 1) * crl::time(1000));
		}
	}, [&](const MTPDauth_loginTokenMigrateTo &data) {
		importTo(data.vdc_id().v, data.vtoken().v);
	}, [&](const MTPDauth_loginTokenSuccess &data) {
		done(data.vauthorization());
	});
}

void QrWidget::showTokenError(const MTP::Error &error) {
	_requestId = 0;
	if (error.type() == u"SESSION_PASSWORD_NEEDED"_q) {
		sendCheckPasswordRequest();
	} else if (base::take(_forceRefresh)) {
		refreshCode();
	} else {
		showError(rpl::single(error.type()));
	}
}

void QrWidget::showToken(const QByteArray &token) {
	const auto encoded = token.toBase64(QByteArray::Base64UrlEncoding);
	_qrCodes.fire_copy("tg://login?token=" + encoded);
}

void QrWidget::importTo(MTP::DcId dcId, const QByteArray &token) {
	Expects(_requestId != 0);

	api().instance().setMainDcId(dcId);
	_requestId = api().request(MTPauth_ImportLoginToken(
		MTP_bytes(token)
	)).done([=](const MTPauth_LoginToken &result) {
		handleTokenResult(result);
	}).fail([=](const MTP::Error &error) {
		showTokenError(error);
	}).toDC(dcId).send();
}

void QrWidget::done(const MTPauth_Authorization &authorization) {
	finish(authorization);
}

void QrWidget::sendCheckPasswordRequest() {
	_requestId = api().request(MTPaccount_GetPassword(
	)).done([=](const MTPaccount_Password &result) {
		result.match([&](const MTPDaccount_password &data) {
			getData()->pwdState = Core::ParseCloudPasswordState(data);
			if (!data.vcurrent_algo() || !data.vsrp_id() || !data.vsrp_B()) {
				LOG(("API Error: No current password received on login."));
				goReplace<QrWidget>(Animate::Forward);
				return;
			} else if (!getData()->pwdState.hasPassword) {
				const auto callback = [=](Fn<void()> &&close) {
					Core::UpdateApplication();
					close();
				};
				Ui::show(Ui::MakeConfirmBox({
					.text = tr::lng_passport_app_out_of_date(),
					.confirmed = callback,
					.confirmText = tr::lng_menu_update(),
				}));
				return;
			}
			goReplace<PasswordCheckWidget>(Animate::Forward);
		});
	}).fail([=](const MTP::Error &error) {
		showTokenError(error);
	}).send();
}

void QrWidget::activate() {
	Step::activate();
	showChildren();

	if (base::take(_stopped)) {
		_qrActive.fire(true);
		refreshCode();
	}
	if (_skip) {
		_skip->setFocus(Qt::OtherFocusReason);
	}
}

void QrWidget::finished() {
	Step::finished();
	_stopped = true;
	_forceRefresh = false;
	_qrActive.fire(false);
	hideError();
	_refreshTimer.cancel();
	apiClear();
	cancelled();
}

void QrWidget::cancelled() {
	api().request(base::take(_requestId)).cancel();
}

QImage TelegramLogoImage() {
	const auto size = QSize(st::introQrCenterSize, st::introQrCenterSize);
	auto result = QImage(
		size * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	{
		auto p = QPainter(&result);
		auto hq = PainterHighQualityEnabler(p);
		p.setBrush(QrActiveColor());
		p.setPen(Qt::NoPen);
		p.drawEllipse(QRect(QPoint(), size));
		st::introQrPlane.paintInCenter(p, QRect(QPoint(), size));
	}
	return result;
}

} // namespace details
} // namespace Intro
