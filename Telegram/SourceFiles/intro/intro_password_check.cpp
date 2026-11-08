/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_password_check.h"
#include "intro/material_shapes.h"
#include <cmath>
#include <algorithm>
#include <QtGui/QPainterPath>
#include "styles/palette.h"
#include "base/timer.h"
#include "base/event_filter.h"
#include "ui/painter.h"

#include "intro/intro_widget.h"
#include "core/core_cloud_password.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/abstract_box.h"
#include "boxes/passcode_box.h"
#include "lang/lang_keys.h"
#include "intro/intro_signup.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "main/main_account.h"
#include "base/random.h"
#include "styles/style_intro.h"
#include "styles/style_widgets.h"

namespace Intro {
namespace details {

namespace {

// exDesktop: every typed character of the cloud password is a Material 3 Expressive shape, popping
// in with a spring and a turn (like the PIN entry on Pixel phones); deleting shrinks it away.
// The field keeps its text and caret logic; its text is transparent and this layer draws.
class PasswordShapes final : public Ui::RpWidget {
public:
	PasswordShapes(QWidget *parent, not_null<Ui::PasswordInput*> field)
	: RpWidget(parent)
	, _field(field)
	, _blink([=] { _caretShown = !_caretShown; update(); }) {
		setAttribute(Qt::WA_TransparentForMouseEvents);
		QObject::connect(field, &QLineEdit::textChanged, this, [=](const QString &text) {
			setCount(text.size());
		});
		base::install_event_filter(this, field, [=](not_null<QEvent*> e) {
			const auto type = e->type();
			if (type == QEvent::Move || type == QEvent::Resize) {
				reposition();
			} else if (type == QEvent::Show || type == QEvent::ShowToParent) {
				reposition();
				show();
			} else if (type == QEvent::Hide || type == QEvent::HideToParent) {
				hide();
			} else if (type == QEvent::FocusIn || type == QEvent::FocusOut) {
				const auto focused = _field->hasFocus();
				_caretShown = true;
				focused ? _blink.callEach(530) : _blink.cancel();
				_focus.start(
					[=] { update(); },
					_focus.value(focused ? 0. : 1.),
					focused ? 1. : 0.,
					focused ? 320 : 260,
					focused ? anim::easeOutBack : anim::easeOutCubic);
				_focused = focused;
				update();
			}
			return base::EventFilterResult::Continue;
		});
		reposition();
		setVisible(field->isVisible());
		setCount(field->getLastText().size());
	}

private:
	struct Shape {
		Ui::Animations::Simple appear;
		Ui::MaterialShapes::Shape kind = Ui::MaterialShapes::Shape::Circle;
		bool removing = false;
	};

	void reposition() {
		setGeometry(_field->geometry());
		raise();
	}

	void setCount(int count) {
		const auto alive = int(std::count_if(_shapes.begin(), _shapes.end(), [](const auto &s) {
			return !s->removing;
		}));
		_caretShown = true;
		if (count > alive) {
			for (auto i = alive; i != count; ++i) {
				auto shape = std::make_unique<Shape>();
				shape->kind = Ui::MaterialShapes::RandomSoft(_shapes.empty()
					? Ui::MaterialShapes::Shape::Count
					: _shapes.back()->kind);
				shape->appear.start([=] { update(); }, 0., 1., 440, anim::easeOutBack);
				_shapes.push_back(std::move(shape));
			}
		} else if (count < alive) {
			auto toRemove = alive - count;
			for (auto i = int(_shapes.size()) - 1; i >= 0 && toRemove > 0; --i) {
				if (_shapes[i]->removing) {
					continue;
				}
				--toRemove;
				const auto raw = _shapes[i].get();
				raw->removing = true;
				raw->appear.start([=] {
					update();
					if (!raw->appear.animating()) {
						crl::on_main(this, [=] {
							const auto j = std::find_if(_shapes.begin(), _shapes.end(), [&](const auto &s) {
								return s.get() == raw;
							});
							if (j != _shapes.end()) {
								_shapes.erase(j);
								update();
							}
						});
					}
				}, raw->appear.value(1.), 0., 200, anim::easeOutCubic);
			}
		}
		update();
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		PainterHighQualityEnabler hq(p);
		{
			// Material 3 outlined field, accent and thicker when focused
			const auto focus = std::max(0., _focus.value(_focused ? 1. : 0.));
			const auto width = style::ConvertScaleExact(1.) + style::ConvertScaleExact(1.) * std::min(1., focus);
			const auto half = width / 2.;
			p.setPen(QPen(anim::color(st::inputBorderFg, st::windowBgActive, std::min(1., focus)), width));
			p.setBrush(Qt::NoBrush);
			const auto radius = style::ConvertScaleExact(16.);
			p.drawRoundedRect(QRectF(rect()).marginsRemoved(QMarginsF(half, half, half, half)), radius, radius);
		}
		const auto &margins = st::introPasswordShapes.textMargins;
		const auto r = style::ConvertScaleExact(8.);
		const auto step = style::ConvertScaleExact(22.);
		const auto cy = margins.top() + (height() - margins.top() - margins.bottom()) / 2.;
		const auto available = width() - margins.left() - margins.right() - step;
		const auto total = step * _shapes.size();
		const auto scroll = std::max(0., total - available);
		auto x = margins.left() + r - scroll;
		p.setPen(Qt::NoPen);
		auto index = 0;
		for (const auto &shape : _shapes) {
			const auto t = shape->appear.value(shape->removing ? 0. : 1.);
			if (t > 0.01 && x > margins.left() - r) {
				p.save();
				p.translate(x, cy + (1. - std::min(1., t)) * style::ConvertScaleExact(6.));
				p.rotate((1. - t) * -70.);
				p.scale(t, t);
				p.setBrush(st::windowBgActive);
				p.drawPath(Ui::MaterialShapes::Path(shape->kind, QRectF(-r, -r, 2 * r, 2 * r)));
				p.restore();
			}
			x += step;
			++index;
		}
		if (_field->hasFocus() && _caretShown) {
			const auto cx = std::max(double(margins.left()), x - step + r + style::ConvertScaleExact(6.)) - (_shapes.empty() ? r : 0.);
			p.setPen(QPen(st::windowBgActive, style::ConvertScaleExact(2.), Qt::SolidLine, Qt::RoundCap));
			p.drawLine(QPointF(cx, cy - r * 1.3), QPointF(cx, cy + r * 1.3));
		}
	}

	const not_null<Ui::PasswordInput*> _field;
	std::vector<std::unique_ptr<Shape>> _shapes;
	Ui::Animations::Simple _focus;
	bool _focused = false;
	base::Timer _blink;
	bool _caretShown = true;

};

} // namespace

PasswordCheckWidget::PasswordCheckWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data)
, _passwordState(getData()->pwdState)
, _pwdField(this, st::introPasswordShapes, tr::lng_signin_password())
, _pwdHint(this, st::introPasswordHint)
, _codeField(this, st::introPassword, tr::lng_signin_code())
, _toRecover(this, tr::lng_signin_recover(tr::now))
, _toPassword(this, tr::lng_signin_try_password(tr::now)) {
	Expects(_passwordState.hasPassword);

	Lang::Updated(
	) | rpl::on_next([=] {
		refreshLang();
	}, lifetime());

	_toRecover->addClickHandler([=] { toRecover(); });
	_toPassword->addClickHandler([=] { toPassword(); });
	connect(_pwdField, &Ui::PasswordInput::changed, [=] { hideError(); });
	Ui::CreateChild<PasswordShapes>(this, _pwdField.data())->show();
	_codeField->changes(
	) | rpl::on_next([=] {
		hideError();
	}, _codeField->lifetime());
	_codeField->submits(
	) | rpl::on_next([=] {
		submit();
	}, _codeField->lifetime());

	setTitleText(tr::lng_signin_title());
	updateDescriptionText();

	if (_passwordState.hint.isEmpty()) {
		_pwdHint->hide();
	} else {
		_pwdHint->setText(tr::lng_signin_hint(
			tr::now,
			lt_password_hint,
			_passwordState.hint));
	}
	_codeField->hide();
	_toPassword->hide();

	setMouseTracking(true);
}

void PasswordCheckWidget::refreshLang() {
	if (_toRecover) {
		_toRecover->setText(tr::lng_signin_recover(tr::now));
	}
	if (_toPassword) {
		_toPassword->setText(
			tr::lng_signin_try_password(tr::now));
	}
	if (!_passwordState.hint.isEmpty()) {
		_pwdHint->setText(tr::lng_signin_hint(
			tr::now,
			lt_password_hint,
			_passwordState.hint));
	}
	updateControlsGeometry();
}

int PasswordCheckWidget::errorTop() const {
	return contentTop() + st::introErrorBelowLinkTop;
}

void PasswordCheckWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	updateControlsGeometry();
}

void PasswordCheckWidget::updateControlsGeometry() {
	_pwdField->moveToLeft(contentLeft(), contentTop() + st::introPasswordTop);
	_pwdHint->moveToLeft(contentLeft() + st::introTextInset, contentTop() + st::introPasswordHintTop);
	_codeField->moveToLeft(contentLeft(), contentTop() + st::introStepFieldTop);
	auto linkTop = _codeField->y() + _codeField->height() + st::introLinkTop;
	_toRecover->moveToLeft(contentLeft() + st::introTextInset, linkTop);
	_toPassword->moveToLeft(contentLeft() + st::introTextInset, linkTop);
}

void PasswordCheckWidget::setInnerFocus() {
	if (_pwdField->isHidden()) {
		_codeField->setFocusFast();
	} else {
		_pwdField->setFocusFast();
	}
}

void PasswordCheckWidget::activate() {
	if (_pwdField->isHidden() && _codeField->isHidden()) {
		Step::activate();
		_pwdField->show();
		_pwdHint->show();
		_toRecover->show();
	}
	setInnerFocus();
}

void PasswordCheckWidget::cancelled() {
	api().request(base::take(_sentRequest)).cancel();
}

void PasswordCheckWidget::pwdSubmitDone(
		bool recover,
		const MTPauth_Authorization &result) {
	_sentRequest = 0;
	if (recover) {
		cSetPasswordRecovered(true);
	}
	finish(result);
}

void PasswordCheckWidget::pwdSubmitFail(const MTP::Error &error) {
	if (MTP::IsFloodError(error)) {
		_sentRequest = 0;
		showError(tr::lng_flood_error());
		_pwdField->showError();
		return;
	}

	_sentRequest = 0;
	const auto &type = error.type();
	if (type == u"PASSWORD_HASH_INVALID"_q
		|| type == u"SRP_PASSWORD_CHANGED"_q) {
		showError(tr::lng_signin_bad_password());
		_pwdField->selectAll();
		_pwdField->showError();
	} else if (type == u"PASSWORD_EMPTY"_q
		|| type == u"AUTH_KEY_UNREGISTERED"_q) {
		goBack();
	} else if (type == u"SRP_ID_INVALID"_q) {
		handleSrpIdInvalid();
	} else if (!MTP::IgnoreError(error)) {
		showError(rpl::single(type));
		_pwdField->setFocus();
	}
}

void PasswordCheckWidget::handleSrpIdInvalid() {
	const auto now = crl::now();
	if (_lastSrpIdInvalidTime > 0
		&& now - _lastSrpIdInvalidTime < Core::kHandleSrpIdInvalidTimeout) {
		_passwordState.mtp.request.id = 0;
		showError(rpl::single(Lang::Hard::ServerError()));
	} else {
		_lastSrpIdInvalidTime = now;
		requestPasswordData();
	}
}

void PasswordCheckWidget::checkPasswordHash() {
	if (_passwordState.mtp.request.id) {
		passwordChecked();
	} else {
		requestPasswordData();
	}
}

void PasswordCheckWidget::requestPasswordData() {
	api().request(base::take(_sentRequest)).cancel();
	_sentRequest = api().request(
		MTPaccount_GetPassword()
	).done([=](const MTPaccount_Password &result) {
		_sentRequest = 0;
		result.match([&](const MTPDaccount_password &data) {
			base::RandomAddSeed(bytes::make_span(data.vsecure_random().v));
			_passwordState = Core::ParseCloudPasswordState(data);
			passwordChecked();
		});
	}).send();
}

void PasswordCheckWidget::passwordChecked() {
	if (!_passwordState.mtp.request) {
		if (_passwordState.mtp.unknownAlgorithm) {
			// Switched to an algorithm this build cannot compute.
			return serverError();
		}
		// The password was removed after this step was created, which is the
		// same situation the PASSWORD_EMPTY error reports.
		return goBack();
	}
	const auto check = Core::ComputeCloudPasswordCheck(
		_passwordState.mtp.request,
		_passwordHash);
	if (!check) {
		return serverError();
	}
	_passwordState.mtp.request.id = 0;
	_sentRequest = api().request(
		MTPauth_CheckPassword(check.result)
	).done([=](const MTPauth_Authorization &result) {
		pwdSubmitDone(false, result);
	}).fail([=](const MTP::Error &error) {
		pwdSubmitFail(error);
	}).handleFloodErrors().send();
}

void PasswordCheckWidget::serverError() {
	showError(rpl::single(Lang::Hard::ServerError()));
}

void PasswordCheckWidget::codeSubmitDone(
		const QString &code,
		const MTPBool &result) {
	auto fields = PasscodeBox::CloudFields::From(_passwordState);
	fields.fromRecoveryCode = code;
	fields.hasRecovery = false;
	fields.mtp.curRequest = {};
	fields.hasPassword = false;
	auto box = Box<PasscodeBox>(&api().instance(), nullptr, fields);
	const auto boxShared = std::make_shared<base::weak_qptr<PasscodeBox>>();

	box->newAuthorization(
	) | rpl::on_next([=](const MTPauth_Authorization &result) {
		if (boxShared) {
			(*boxShared)->closeBox();
		}
		pwdSubmitDone(true, result);
	}, lifetime());

	*boxShared = Ui::show(std::move(box));
}

void PasswordCheckWidget::codeSubmitFail(const MTP::Error &error) {
	if (MTP::IsFloodError(error)) {
		showError(tr::lng_flood_error());
		_codeField->showError();
		return;
	}

	_sentRequest = 0;
	const auto &type = error.type();
	if (type == u"PASSWORD_EMPTY"_q
		|| type == u"AUTH_KEY_UNREGISTERED"_q) {
		goBack();
	} else if (type == u"PASSWORD_RECOVERY_NA"_q) {
		recoverStartFail(error);
	} else if (type == u"PASSWORD_RECOVERY_EXPIRED"_q) {
		_emailPattern = QString();
		toPassword();
	} else if (type == u"CODE_INVALID"_q) {
		showError(tr::lng_signin_wrong_code());
		_codeField->selectAll();
		_codeField->showError();
	} else if (!MTP::IgnoreError(error)) {
		showError(rpl::single(type));
		_codeField->setFocus();
	}
}

void PasswordCheckWidget::recoverStarted(const MTPauth_PasswordRecovery &result) {
	_emailPattern = qs(result.c_auth_passwordRecovery().vemail_pattern());
	updateDescriptionText();
}

void PasswordCheckWidget::recoverStartFail(const MTP::Error &error) {
	_pwdField->show();
	_pwdHint->show();
	_codeField->hide();
	_pwdField->setFocus();
	updateDescriptionText();
	update();
	hideError();
}

void PasswordCheckWidget::toRecover() {
	if (_passwordState.hasRecovery) {
		if (_sentRequest) {
			api().request(base::take(_sentRequest)).cancel();
		}
		hideError();
		_toRecover->hide();
		_toPassword->show();
		_pwdField->hide();
		_pwdHint->hide();
		_pwdField->setText(QString());
		_codeField->show();
		_codeField->setFocus();
		updateDescriptionText();
		if (_emailPattern.isEmpty()) {
			api().request(
				MTPauth_RequestPasswordRecovery()
			).done([=](const MTPauth_PasswordRecovery &result) {
				recoverStarted(result);
			}).fail([=](const MTP::Error &error) {
				recoverStartFail(error);
			}).send();
		}
	} else {
		const auto box = Ui::show(
			Ui::MakeInformBox(tr::lng_signin_no_email_forgot()));
		box->boxClosing(
		) | rpl::on_next([=] {
			showReset();
		}, box->lifetime());
	}
}

void PasswordCheckWidget::toPassword() {
	const auto box = Ui::show(
		Ui::MakeInformBox(tr::lng_signin_cant_email_forgot()));
	box->boxClosing(
	) | rpl::on_next([=] {
		showReset();
	}, box->lifetime());
}

void PasswordCheckWidget::showReset() {
	if (_sentRequest) {
		api().request(base::take(_sentRequest)).cancel();
	}
	_toRecover->show();
	_toPassword->hide();
	_pwdField->show();
	_pwdHint->show();
	_codeField->hide();
	_codeField->setText(QString());
	_pwdField->setFocus();
	showResetButton();
	updateDescriptionText();
	update();
}

void PasswordCheckWidget::updateDescriptionText() {
	auto pwdHidden = _pwdField->isHidden();
	auto emailPattern = _emailPattern;
	setDescriptionText(pwdHidden
		? tr::lng_signin_recover_desc(
			lt_email,
			rpl::single(Ui::Text::WrapEmailPattern(emailPattern)),
			tr::marked)
		: tr::lng_signin_desc(tr::marked));
}

void PasswordCheckWidget::submit() {
	if (_sentRequest) {
		return;
	}
	if (_pwdField->isHidden()) {
		auto code = _codeField->getLastText().trimmed();
		if (code.isEmpty()) {
			_codeField->showError();
			return;
		}
		const auto send = crl::guard(this, [=] {
			_sentRequest = api().request(MTPauth_CheckRecoveryPassword(
				MTP_string(code)
			)).done([=](const MTPBool &result) {
				codeSubmitDone(code, result);
			}).fail([=](const MTP::Error &error) {
				codeSubmitFail(error);
			}).handleFloodErrors().send();
		});

		if (_passwordState.notEmptyPassport) {
			const auto confirmed = [=](Fn<void()> &&close) {
				send();
				close();
			};
			Ui::show(Ui::MakeConfirmBox({
				.text = tr::lng_cloud_password_passport_losing(),
				.confirmed = confirmed,
				.confirmText = tr::lng_continue(),
			}));
		} else {
			send();
		}
	} else {
		hideError();

		if (!_passwordState.mtp.request) {
			return serverError();
		}
		const auto password = _pwdField->getLastText().toUtf8();
		_passwordHash = Core::ComputeCloudPasswordHash(
			_passwordState.mtp.request.algo,
			bytes::make_span(password));
		checkPasswordHash();
	}
}

rpl::producer<QString> PasswordCheckWidget::nextButtonText() const {
	return tr::lng_intro_submit();
}

} // namespace details
} // namespace Intro
