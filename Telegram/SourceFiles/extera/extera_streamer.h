#pragma once

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace Extera::Streamer {

enum class PhoneMode {
	Unknown,
	Spoiler,
	Shown,
};

enum class UsernameMode {
	Spoiler,
	Hidden,
	Shown,
};

enum class IdMode {
	Middle,
	Full,
	Shown,
};

enum class ChatType {
	Private,
	Bot,
	Group,
	Channel,
};

struct ChatHide {
	bool avatar = false;
	bool name = false;
	bool text = false;

	[[nodiscard]] bool any() const {
		return avatar || name || text;
	}
	[[nodiscard]] bool all() const {
		return avatar && name && text;
	}
};

enum class Exception {
	None,
	Shown,
	Hidden,
};

[[nodiscard]] bool Active();
[[nodiscard]] bool HideName();
[[nodiscard]] bool HideAvatar();
[[nodiscard]] bool HideProfile();
[[nodiscard]] PhoneMode Phone();
[[nodiscard]] PhoneMode SelfPhone();
[[nodiscard]] QString SelfPhoneText(const QString &formatted);
[[nodiscard]] UsernameMode Username();
[[nodiscard]] IdMode Id();

[[nodiscard]] ChatType TypeOf(not_null<PeerData*> peer);
[[nodiscard]] ChatHide ForPeer(not_null<PeerData*> peer);
[[nodiscard]] Exception ExceptionFor(not_null<PeerData*> peer);
void SetException(not_null<PeerData*> peer, Exception exception);

[[nodiscard]] bool NotificationsMuted();
[[nodiscard]] bool HideNameOf(not_null<const PeerData*> peer);
[[nodiscard]] bool HideAvatarOf(not_null<const PeerData*> peer);
[[nodiscard]] bool HideTextOf(not_null<const PeerData*> peer);
[[nodiscard]] const QString &PlaceholderName();
void ShowMenu(not_null<Window::SessionController*> controller);

[[nodiscard]] QString Name();
[[nodiscard]] const QImage &Avatar();
[[nodiscard]] QString MaskId(const QString &id);

void Refresh();
void Start();
void ShowSettings(not_null<Window::SessionController*> controller);

} // namespace Extera::Streamer
