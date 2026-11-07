#pragma once

class PeerData;

namespace Ui::Text {
class CustomEmoji;
struct CustomEmojiPaintContext;
} // namespace Ui::Text

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

struct TrustedSource {
	uint64 id = 0;
	QString username;
};

void StartBadges();
void RefreshBadges();
[[nodiscard]] rpl::producer<> BadgesChanges();
void PaintBadgeEmoji(
	QPainter &p,
	not_null<Ui::Text::CustomEmoji*> emoji,
	const Ui::Text::CustomEmojiPaintContext &context);

[[nodiscard]] BadgeKind BadgeKindFor(not_null<PeerData*> peer);
[[nodiscard]] DocumentId BadgeEmoji(not_null<PeerData*> peer);
[[nodiscard]] int BadgeNumber(not_null<PeerData*> peer);
[[nodiscard]] int BadgeHolders(not_null<PeerData*> peer);
[[nodiscard]] bool BadgeIsCustom(not_null<PeerData*> peer);
[[nodiscard]] QString BadgeCustomText(not_null<PeerData*> peer);
[[nodiscard]] DocumentId BadgeTypeEmoji(const QString &type);
[[nodiscard]] bool IsBadgeEmoji(DocumentId id);
[[nodiscard]] std::optional<std::vector<TrustedSource>> TrustedSources();
[[nodiscard]] bool IsTrustedHash(const QString &sha256);

} // namespace Extera
