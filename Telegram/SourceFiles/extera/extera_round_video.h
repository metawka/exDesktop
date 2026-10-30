#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Api {
struct SendAction;
} // namespace Api

namespace Extera::RoundVideo {

void ChooseAndSend(
	not_null<Window::SessionController*> controller,
	Api::SendAction action);
void ShowForPath(
	not_null<Window::SessionController*> controller,
	const QString &path,
	Api::SendAction action);

} // namespace Extera::RoundVideo
