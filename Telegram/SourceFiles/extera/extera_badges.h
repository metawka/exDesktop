#pragma once

class PeerData;

namespace Extera {

enum class BadgeKind {
	None,
	Porter,
	PorterChannel,
	Developer,
	OfficialChannel,
	Supporter,
	Custom,
	Bughunter,
	Helper,
	Donor,
};

void StartBadges();

[[nodiscard]] BadgeKind BadgeKindFor(not_null<PeerData*> peer);
[[nodiscard]] DocumentId BadgeEmoji(not_null<PeerData*> peer);
[[nodiscard]] QString BadgeCustomText(not_null<PeerData*> peer);

} // namespace Extera
