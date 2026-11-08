#pragma once

class History;
class HistoryItem;
struct TextWithEntities;

namespace Extera {

void ConvertPremiumEmojiForSending(
	TextWithEntities &text,
	not_null<History*> history);
void ApplyLocalPremiumEmoji(
	not_null<const HistoryItem*> item,
	TextWithEntities &text);
void ForgetLocalPremiumEmoji(not_null<const HistoryItem*> item);
void ReapplyLocalPremiumEmoji(not_null<HistoryItem*> item);

} // namespace Extera
