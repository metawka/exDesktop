#include "extera/exd_port_bar.h"

#include "ui/widgets/buttons.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "lang/lang_keys.h"
#include "styles/style_dialogs.h"
#include "styles/style_settings.h"

namespace Exd {

PortBar::PortBar(
	not_null<QWidget*> parent,
	rpl::producer<Extera::Plugins::PortBarState> state)
: _button(
	parent,
	object_ptr<Ui::RippleButton>(parent, st::dialogsMenuToggle.ripple))
, _shadow(parent)
, _radial([=](crl::time now) { radialAnimationCallback(now); }) {
	_button.hide(anim::type::instant);
	_shadow.showOn(_button.shownValue());
	_button.setDirectionUp(false);
	_button.entity()->resize(0, st::downloadBarHeight);
	_button.entity()->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = Painter(_button.entity());
		paint(p, clip);
	}, lifetime());

	std::move(
		state
	) | rpl::on_next([=](const Extera::Plugins::PortBarState &state) {
		apply(state);
	}, lifetime());
}

PortBar::~PortBar() = default;

void PortBar::apply(const Extera::Plugins::PortBarState &state) {
	_button.toggle(state.active, anim::type::normal);
	if (!state.active) {
		return;
	}
	_state = state;
	if (!_radial.animating() && !state.finished) {
		_radial.start(state.progress);
	}
	_title.setMarkedText(st::defaultTextStyle, tr::bold(state.name));
	_info.setMarkedText(
		st::downloadInfoStyle,
		state.finished
			? Ui::Text::Link(state.status)
			: tr::marked(state.status));
	_button.entity()->update();
}

bool PortBar::isHidden() const {
	return _button.isHidden();
}

int PortBar::height() const {
	return _button.height();
}

rpl::producer<int> PortBar::heightValue() const {
	return _button.heightValue();
}

rpl::producer<bool> PortBar::shownValue() const {
	return _button.shownValue();
}

void PortBar::setGeometry(int left, int top, int width, int height) {
	_button.resizeToWidth(width);
	_button.moveToLeft(left, top);
	_shadow.setGeometry(left, top - st::lineWidth, width, st::lineWidth);
}

rpl::producer<> PortBar::clicks() const {
	return _button.entity()->clicks() | rpl::to_empty;
}

rpl::lifetime &PortBar::lifetime() {
	return _button.lifetime();
}

QRect PortBar::loadingRect() const {
	const auto size = st::downloadLoadingSize;
	const auto added = 3 * st::downloadLoadingLine;
	const auto skipy = (_button.entity()->height() - size) / 2;
	return QRect(
		st::downloadLoadingLeft - added,
		skipy - added,
		size + added * 2,
		size + added * 2);
}

void PortBar::paint(Painter &p, QRect clip) {
	const auto button = _button.entity();
	const auto outerw = button->width();
	const auto over = button->isOver() || button->isDown();
	const auto &arrow = over ? st::downloadArrowOver : st::downloadArrow;
	p.fillRect(clip, st::windowBg);
	button->paintRipple(p, 0, 0);

	const auto full = loadingRect();
	const auto added = 3 * st::downloadLoadingLine;
	if (full.intersects(clip)) {
		const auto loading = _radial.computeState();
		if (loading.shown > 0) {
			auto hq = PainterHighQualityEnabler(p);
			p.setOpacity(loading.shown);
			auto pen = st::windowBgActive->p;
			pen.setWidth(st::downloadLoadingLine);
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			const auto m = added / 2.;
			const auto rect = QRectF(full).marginsRemoved({ m, m, m, m });
			if (loading.arcLength < arc::kFullLength) {
				p.drawArc(rect, loading.arcFrom, loading.arcLength);
			} else {
				p.drawEllipse(rect);
			}
			p.setOpacity(1.);
		}
		const auto shift = qreal(_state.finished
			? st::downloadLoadingLine
			: added);
		const auto ellipse = QRectF(full).marginsRemoved(
			{ shift, shift, shift, shift });
		{
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(_state.failed
				? st::attentionButtonFg
				: st::windowBgActive);
			p.drawEllipse(ellipse);
		}
		const auto &icon = st::exteraInstallTranslate;
		icon.paint(
			p,
			full.x() + (full.width() - icon.width()) / 2,
			full.y() + (full.height() - icon.height()) / 2,
			outerw,
			st::windowFgActive->c);
	}

	const auto right = st::downloadArrowRight + arrow.width();
	const auto available = outerw - st::downloadTitleLeft - right;
	p.setPen(st::windowBoldFg);
	_title.drawLeftElided(
		p,
		st::downloadTitleLeft,
		st::downloadTitleTop,
		available,
		outerw);

	p.setPen(st::windowSubTextFg);
	p.setTextPalette(st::defaultTextPalette);
	_info.drawLeftElided(
		p,
		st::downloadInfoLeft,
		st::downloadInfoTop,
		available,
		outerw);

	const auto arrowTop = (st::downloadBarHeight - arrow.height()) / 2;
	arrow.paint(p, outerw - right, arrowTop, outerw);
}

void PortBar::radialAnimationCallback(crl::time now) {
	const auto updated = _radial.update(
		_state.progress,
		_state.finished,
		now);
	if (!anim::Disabled() || updated) {
		_button.entity()->update(loadingRect());
	}
}

} // namespace Exd
