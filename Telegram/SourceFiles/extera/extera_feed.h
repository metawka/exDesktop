#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Extera {

void ShowChannelFeed(not_null<Window::SessionController*> controller);
void ShowFeedSettings(not_null<Window::SessionController*> controller);
[[nodiscard]] int FeedUnreadCount(not_null<Main::Session*> session);
[[nodiscard]] bool IsFeedList(const void *delegate);

} // namespace Extera
