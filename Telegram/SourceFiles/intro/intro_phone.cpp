/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_phone.h"

#include <QtGui/QCursor>

#include "lang/lang_keys.h"
#include "intro/intro_code.h"
#include "intro/intro_signup.h"
#include "intro/intro_password_check.h"
#include "intro/intro_email.h"
#include "intro/intro_qr.h"
#include "styles/style_intro.h"
#include "styles/palette.h"
#include "base/event_filter.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/widgets/fields/special_fields.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "data/data_user.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/abstract_box.h"
#include "boxes/phone_banned_box.h"
#include "core/application.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "countries/countries_instance.h" // Countries::Groups

namespace Intro {
namespace details {
namespace {

[[nodiscard]] bool AllowPhoneAttempt(const QString &phone) {
	const auto digits = ranges::count_if(
		phone,
		[](QChar ch) { return ch.isNumber(); });
	return (digits > 1);
}

[[nodiscard]] QString DigitsOnly(QString value) {
	static const auto RegExp = QRegularExpression("[^0-9]");
	return value.replace(RegExp, QString());
}

} // namespace

namespace {

// exDesktop / Material 3 Expressive phone entry: a tonal pill behind the country selector and
// one rounded outlined field around the code and the number, which turns to the accent colour
// and thickens when focused. Painted behind the inputs, which keep their own logic.
class PhoneDecor final : public Ui::RpWidget {
public:
	PhoneDecor(
		QWidget *parent,
		not_null<QWidget*> country,
		not_null<QWidget*> code,
		not_null<QWidget*> phone)
	: RpWidget(parent)
	, _country(country)
	, _code(code)
	, _phone(phone) {
		setAttribute(Qt::WA_TransparentForMouseEvents);
		const auto watchFocus = [=](not_null<QWidget*> w) {
			base::install_event_filter(this, w, [=](not_null<QEvent*> e) {
				if (e->type() == QEvent::FocusIn || e->type() == QEvent::FocusOut) {
					crl::on_main(this, [=] { refreshFocus(); });
				}
				return base::EventFilterResult::Continue;
			});
		};
		watchFocus(code);
		watchFocus(phone);
		// The country field has child widgets: moving onto one of them sends the
		// field a Leave while the cursor is still over the pill. So the hover is
		// decided by where the cursor is, on any mouse event of the field or of
		// its children, and only a real change starts the animation.
		const auto watchHover = [=](not_null<QWidget*> w) {
			base::install_event_filter(this, w, [=](not_null<QEvent*> e) {
				const auto type = e->type();
				if (type == QEvent::Enter
					|| type == QEvent::Leave
					|| type == QEvent::MouseMove
					|| type == QEvent::HoverMove) {
					crl::on_main(this, [=] { refreshHover(); });
				}
				return base::EventFilterResult::Continue;
			});
		};
		watchHover(country);
		for (const auto child : country->findChildren<QWidget*>()) {
			watchHover(child);
		}
		refreshFocus();
	}

private:
	[[nodiscard]] QRectF countryPill() const {
		const auto shift = style::ConvertScaleExact(15.);
		return QRectF(_country->geometry()).adjusted(0, shift, 0, 0);
	}

	void refreshHover() {
		const auto over = _country->isVisible()
			&& countryPill().contains(mapFromGlobal(QCursor::pos()));
		if (over == _over) {
			return;
		}
		_over = over;
		_hover.start(
			[=] { update(); },
			_hover.value(over ? 0. : 1.),
			over ? 1. : 0.,
			over ? 220 : 260,
			anim::easeOutCubic);
	}

	void refreshFocus() {
		const auto focused = _code->hasFocus() || _phone->hasFocus();
		if (focused == _focused) {
			return;
		}
		_focused = focused;
		_focus.start(
			[=] { update(); },
			_focus.value(focused ? 0. : 1.),
			focused ? 1. : 0.,
			focused ? 320 : 260,
			focused ? anim::easeOutBack : anim::easeOutCubic);
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		PainterHighQualityEnabler hq(p);
		const auto shift = style::ConvertScaleExact(15.);

		// the country selector as a tonal pill
		const auto country = countryPill();
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(country, country.height() / 2., country.height() / 2.);
		if (const auto hover = _hover.value(_over ? 1. : 0.); hover > 0.) {
			auto layer = st::windowFg->c;
			layer.setAlphaF(0.08 * hover);
			p.setBrush(layer);
			p.drawRoundedRect(country, country.height() / 2., country.height() / 2.);
		}

		// code + number in one outlined field
		const auto focus = std::max(0., _focus.value(_focused ? 1. : 0.));
		const auto box = QRectF(_code->geometry().united(_phone->geometry())).adjusted(0, shift, 0, 0);
		const auto width = style::ConvertScaleExact(1.) + style::ConvertScaleExact(1.) * std::min(1., focus);
		auto pen = QPen(anim::color(st::inputBorderFg, st::windowBgActive, std::min(1., focus)), width);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		const auto half = width / 2.;
		const auto radius = style::ConvertScaleExact(16.);
		p.drawRoundedRect(box.marginsRemoved({ half, half, half, half }), radius, radius);

		// a hairline between the code and the number
		const auto x = _phone->x() + 0.5;
		const auto gap = style::ConvertScaleExact(12.);
		p.setPen(QPen(st::inputBorderFg, style::ConvertScaleExact(1.)));
		p.drawLine(QPointF(x, box.top() + gap), QPointF(x, box.bottom() - gap));
	}

	const not_null<QWidget*> _country;
	const not_null<QWidget*> _code;
	const not_null<QWidget*> _phone;
	Ui::Animations::Simple _focus;
	Ui::Animations::Simple _hover;
	bool _focused = false;
	bool _over = false;

};

} // namespace

PhoneWidget::PhoneWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data)
, _country(
	this,
	getData()->controller->uiShow(),
	st::introCountrySelect)
, _code(this, st::introCountryCode)
, _phone(
	this,
	st::introPhone,
	[](const QString &s) { return Countries::Groups(s); })
, _checkRequestTimer([=] { checkRequest(); }) {
	_code->setAccessibleName(tr::lng_country_code(tr::now));
	_phone->setAccessibleName(tr::lng_phone_number(tr::now));
	_decor = Ui::CreateChild<PhoneDecor>(
		this,
		_country.data(),
		_code.data(),
		_phone.data());
	_decor->lower();
	_decor->show();
	_phone->frontBackspaceEvent(
	) | rpl::on_next([=](not_null<QKeyEvent*> e) {
		_code->startErasing(e);
	}, _code->lifetime());

	_country->codeChanged(
	) | rpl::on_next([=](const QString &code) {
		_code->codeSelected(code);
		_phone->chooseCode(code);
	}, _country->lifetime());
	_code->codeChanged(
	) | rpl::on_next([=](const QString &code) {
		_country->onChooseCode(code);
		_phone->chooseCode(code);
	}, _code->lifetime());
	_code->addedToNumber(
	) | rpl::on_next([=](const QString &added) {
		_phone->addedToNumber(added);
	}, _phone->lifetime());
	_code->spacePressed() | rpl::on_next([=] {
		submit();
	}, _code->lifetime());
	connect(_phone, &Ui::PhonePartInput::changed, [=] { phoneChanged(); });
	connect(_code, &Ui::CountryCodeInput::changed, [=] { phoneChanged(); });

	setTitleText(tr::lng_phone_title());
	setDescriptionText(tr::lng_phone_desc());
	getData()->updated.events(
	) | rpl::on_next([=] {
		countryChanged();
	}, lifetime());
	setErrorCentered(false);
	setErrorStyle(&st::introPhoneError);
	setupQrLogin();

	if (!_country->chooseCountry(getData()->country)) {
		_country->chooseCountry(u"US"_q);
	}
	_changed = false;
}

QString PhoneWidget::accessibilityName() {
	return tr::lng_phone_title(tr::now);
}

void PhoneWidget::setupQrLogin() {
	const auto qrLogin = Ui::CreateChild<Ui::RoundButton>(
		this,
		tr::lng_phone_to_qr(),
		st::introQrPasskeyButton);
	qrLogin->setWavy(true);
	qrLogin->show();

	DEBUG_LOG(("PhoneWidget.qrLogin link created and shown."));

	rpl::combine(
		sizeValue(),
		qrLogin->widthValue()
	) | rpl::on_next([=](QSize size, int qrLoginWidth) {
		qrLogin->moveToLeft(
			(size.width() - qrLoginWidth) / 2,
			contentTop() + st::introQrLoginLinkTop + st::introPhoneNextShift);
	}, qrLogin->lifetime());

	qrLogin->setClickedCallback([=] {
		goNextOrBack<QrWidget>();
	});
}

void PhoneWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	_country->moveToLeft(contentLeft(), contentTop() + st::introStepFieldTop);
	auto phoneTop = _country->y() + _country->height() + st::introPhoneTop;
	_code->moveToLeft(contentLeft(), phoneTop);
	_phone->moveToLeft(contentLeft() + _country->width() - st::introPhone.width, phoneTop);
	if (_decor) {
		_decor->setGeometry(rect());
		_decor->lower();
	}
}

int PhoneWidget::errorTop() const {
	return _phone->y() + _phone->height() + st::introPhoneErrorSkip;
}

int PhoneWidget::nextTopShift() const {
	return st::introPhoneNextShift;
}

void PhoneWidget::showPhoneError(rpl::producer<QString> text) {
	_phone->showError();
	showError(std::move(text));
}

void PhoneWidget::hidePhoneError() {
	hideError();
}

void PhoneWidget::countryChanged() {
	if (!_changed) {
		selectCountry(getData()->country);
	}
}

void PhoneWidget::phoneChanged() {
	_changed = true;
	hidePhoneError();
}

void PhoneWidget::submit() {
	if (_sentRequest || isHidden()) {
		return;
	}

	{
		const auto hasCodeButWaitingPhone = _code->hasFocus()
			&& (_code->getLastText().size() > 1)
			&& _phone->getLastText().isEmpty();
		if (hasCodeButWaitingPhone) {
			_phone->hideError();
			_phone->setFocus();
			return;
		}
	}
	const auto phone = fullNumber();
	if (!AllowPhoneAttempt(phone)) {
		showPhoneError(tr::lng_bad_phone());
		_phone->setFocus();
		return;
	}

	cancelNearestDcRequest();

	// Check if such account is authorized already.
	const auto phoneDigits = DigitsOnly(phone);
	for (const auto &[index, existing] : Core::App().domain().accounts()) {
		const auto raw = existing.get();
		if (const auto session = raw->maybeSession()) {
			if (raw->mtp().environment() == account().mtp().environment()
				&& DigitsOnly(session->user()->phone()) == phoneDigits) {
				crl::on_main(raw, [=] {
					Core::App().domain().activate(raw);
				});
				return;
			}
		}
	}

	hidePhoneError();

	_checkRequestTimer.callEach(1000);

	_sentPhone = phone;
	api().instance().setUserPhone(_sentPhone);
	_sentRequest = api().request(MTPauth_SendCode(
		MTP_string(_sentPhone),
		MTP_int(ApiId),
		MTP_string(ApiHash),
		MTP_codeSettings(
			MTP_flags(0),
			MTPVector<MTPbytes>(),
			MTPstring(),
			MTPBool())
	)).done([=](const MTPauth_SentCode &result) {
		phoneSubmitDone(result);
	}).fail([=](const MTP::Error &error) {
		phoneSubmitFail(error);
	}).handleFloodErrors().send();
}

void PhoneWidget::stopCheck() {
	_checkRequestTimer.cancel();
}

void PhoneWidget::checkRequest() {
	auto status = api().instance().state(_sentRequest);
	if (status < 0) {
		auto leftms = -status;
		if (leftms >= 1000) {
			api().request(base::take(_sentRequest)).cancel();
		}
	}
	if (!_sentRequest && status == MTP::RequestSent) {
		stopCheck();
	}
}

void PhoneWidget::phoneSubmitDone(const MTPauth_SentCode &result) {
	stopCheck();
	_sentRequest = 0;

	result.match([&](const MTPDauth_sentCode &data) {
		fillSentCodeData(data);
		getData()->phone = DigitsOnly(_sentPhone);
		getData()->phoneHash = qba(data.vphone_code_hash());
		if (getData()->emailStatus == EmailStatus::SetupRequired) {
			return goNext<EmailWidget>();
		}
		const auto next = data.vnext_type();
		if (next && next->type() == mtpc_auth_codeTypeCall) {
			getData()->callStatus = CallStatus::Waiting;
			getData()->callTimeout = data.vtimeout().value_or(60);
		} else {
			getData()->callStatus = CallStatus::Disabled;
			getData()->callTimeout = 0;
		}
		goNext<CodeWidget>();
	}, [&](const MTPDauth_sentCodeSuccess &data) {
		finish(data.vauthorization());
	}, [](const MTPDauth_sentCodePaymentRequired &) {
		LOG(("API Error: Unexpected auth.sentCodePaymentRequired "
			"(PhoneWidget::phoneSubmitDone)."));
	});
}

void PhoneWidget::phoneSubmitFail(const MTP::Error &error) {
	if (MTP::IsFloodError(error)) {
		stopCheck();
		_sentRequest = 0;
		showPhoneError(tr::lng_flood_error());
		return;
	}

	stopCheck();
	_sentRequest = 0;
	const auto &err = error.type();
	if (err == u"PHONE_NUMBER_FLOOD"_q) {
		Ui::show(Ui::MakeInformBox(tr::lng_error_phone_flood()));
	} else if (err == u"PHONE_NUMBER_INVALID"_q) { // show error
		showPhoneError(tr::lng_bad_phone());
	} else if (err == u"PHONE_NUMBER_BANNED"_q) {
		Ui::ShowPhoneBannedError(getData()->controller, _sentPhone);
	} else if (!MTP::IgnoreError(error)) {
		showPhoneError(rpl::single(err));
	}
}

QString PhoneWidget::fullNumber() const {
	return _code->getLastText() + _phone->getLastText();
}

void PhoneWidget::selectCountry(const QString &country) {
	_country->chooseCountry(country);
}

void PhoneWidget::setInnerFocus() {
	_phone->setFocusFast();
}

void PhoneWidget::activate() {
	Step::activate();
	showChildren();
	setInnerFocus();
}

void PhoneWidget::finished() {
	Step::finished();
	_checkRequestTimer.cancel();
	apiClear();

	cancelled();
}

void PhoneWidget::cancelled() {
	api().request(base::take(_sentRequest)).cancel();
}

} // namespace details
} // namespace Intro
