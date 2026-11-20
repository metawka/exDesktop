#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Extera::Md3 {

void ApplyBeforeStyles();

[[nodiscard]] bool Enabled();
[[nodiscard]] bool RoundedSelection();
[[nodiscard]] bool FloatingField();
[[nodiscard]] bool DrawerCorners();
[[nodiscard]] bool Expressive();

void ShowSettingsBox(not_null<Window::SessionController*> controller);
[[nodiscard]] bool ShowIntroIfNeeded(
	not_null<Window::SessionController*> controller);

} // namespace Extera::Md3
