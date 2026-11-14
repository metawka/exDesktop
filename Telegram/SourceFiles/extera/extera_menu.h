#pragma once

class HistoryItem;

namespace style {
namespace internal {
class Icon;
} // namespace internal
using icon = internal::Icon;
} // namespace style

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Extera {

void AddMessageMenuActions(
	not_null<Ui::PopupMenu*> menu,
	not_null<HistoryItem*> item,
	not_null<Window::SessionController*> controller);

void ApplyQuickActions(not_null<Ui::PopupMenu*> menu);
[[nodiscard]] QStringList QuickActionIds();
[[nodiscard]] QString QuickActionTitle(const QString &id);
[[nodiscard]] const style::icon *QuickActionIcon(const QString &id);

// Tristegram: remembers when a message arrived on this device (with ms).
void RememberReceivedTime(not_null<HistoryItem*> item);

} // namespace Extera
