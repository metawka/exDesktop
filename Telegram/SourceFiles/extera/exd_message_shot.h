#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Exd::MessageShot {

enum class Part {
	Date,
	Reactions,
	Badges,
	Spoilers,
};

[[nodiscard]] bool Hidden(
	not_null<const HistoryView::Element*> view,
	Part part);

void Show(
	not_null<Window::SessionController*> controller,
	MessageIdsList ids);

} // namespace Exd::MessageShot
