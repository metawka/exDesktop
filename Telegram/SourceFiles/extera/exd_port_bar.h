#pragma once

#include "ui/wrap/slide_wrap.h"
#include "ui/effects/radial_animation.h"
#include "ui/widgets/shadow.h"
#include "ui/text/text.h"
#include "extera/extera_plugins_ui.h"

namespace Ui {
class RippleButton;
} // namespace Ui

namespace Exd {

class PortBar final {
public:
	PortBar(
		not_null<QWidget*> parent,
		rpl::producer<Extera::Plugins::PortBarState> state);
	~PortBar();

	[[nodiscard]] bool isHidden() const;
	[[nodiscard]] int height() const;
	[[nodiscard]] rpl::producer<int> heightValue() const;
	[[nodiscard]] rpl::producer<bool> shownValue() const;
	void setGeometry(int left, int top, int width, int height);

	[[nodiscard]] rpl::producer<> clicks() const;
	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void apply(const Extera::Plugins::PortBarState &state);
	void paint(Painter &p, QRect clip);
	void radialAnimationCallback(crl::time now);
	[[nodiscard]] QRect loadingRect() const;

	Ui::SlideWrap<Ui::RippleButton> _button;
	Ui::PlainShadow _shadow;
	Extera::Plugins::PortBarState _state;
	Ui::Text::String _title;
	Ui::Text::String _info;
	Ui::RadialAnimation _radial;

};

} // namespace Exd
