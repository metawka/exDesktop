#pragma once

#include "settings/settings_type.h"

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

[[nodiscard]] Type ExteraId();
[[nodiscard]] Type ExteraGeneralId();
[[nodiscard]] Type ExteraAppearanceId();
[[nodiscard]] Type ExteraChatsId();
[[nodiscard]] Type ExteraPluginsId();
[[nodiscard]] Type ExteraOtherId();
[[nodiscard]] Type ExteraPluginsEngineId();
[[nodiscard]] Type ExteraNavigationId();
[[nodiscard]] Type ExteraAiChatId();
[[nodiscard]] Type ExteraPrivacyId();
[[nodiscard]] Type ExteraNetworkId();
[[nodiscard]] Type ExteraUpdatesId();
[[nodiscard]] Type ExteraSupportId();

void ShowExteraSupportBox(not_null<Window::SessionController*> controller);
void ShowExteraBadgeBox(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	const QString &text,
	bool supporter);
void ShowExteraAppIconBox(not_null<Window::SessionController*> controller);

} // namespace Settings
