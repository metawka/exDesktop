#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Extera::Changelog {

void Start();
void Show(not_null<Window::SessionController*> controller);

} // namespace Extera::Changelog
