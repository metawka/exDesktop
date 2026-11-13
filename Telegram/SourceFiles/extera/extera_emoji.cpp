#include "extera/extera_emoji.h"

#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "extera/extera_config.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "ui/emoji_config.h"

#include <QtCore/QRegularExpression>

namespace Extera {
namespace {

constexpr auto kPrefix = "tg://emoji?id=";

[[nodiscard]] auto &Originals() {
	static auto result = base::flat_map<
		not_null<const HistoryItem*>,
		TextWithEntities>();
	return result;
}

} // namespace

void ConvertPremiumEmojiForSending(
		TextWithEntities &text,
		not_null<History*> history) {
	const auto peer = history->peer;
	if (!Bool(u"local_premium_emoji"_q)
		|| peer->isSelf()
		|| history->session().user()->isPremiumReal()) {
		return;
	}
	const auto owner = &history->owner();
	for (auto &entity : text.entities) {
		if (entity.type() != EntityType::CustomEmoji) {
			continue;
		}
		const auto id = Data::ParseCustomEmojiData(entity.data());
		if (!id) {
			continue;
		}
		const auto document = owner->document(id);
		if (Data::AllowEmojiWithoutPremium(peer, document)
			&& !document->isPremiumEmoji()) {
			continue;
		}
		if (const auto channel = peer->asMegagroup()) {
			const auto sticker = document->sticker();
			if (sticker
				&& sticker->set.id
				&& channel->mgInfo->emojiSet.id == sticker->set.id) {
				continue;
			}
		}
		entity = EntityInText(
			EntityType::CustomUrl,
			entity.offset(),
			entity.length(),
			QString::fromLatin1(kPrefix) + QString::number(id));
	}
}

void ApplyLocalPremiumEmoji(
		not_null<const HistoryItem*> item,
		TextWithEntities &text) {
	Originals().remove(item);
	if (!Bool(u"local_premium_emoji"_q)) {
		return;
	}
	const auto original = text;
	auto changed = false;
	static const auto pattern = QRegularExpression(
		u"^tg://emoji\\?id=(\\d+)$"_q);
	for (auto &entity : text.entities) {
		if (entity.type() != EntityType::CustomUrl) {
			continue;
		}
		const auto match = pattern.match(entity.data());
		if (!match.hasMatch()) {
			continue;
		}
		const auto part = text.text.mid(entity.offset(), entity.length());
		auto length = 0;
		const auto emoji = Ui::Emoji::Find(part, &length);
		if (!emoji || length != part.size()) {
			continue;
		}
		const auto id = match.captured(1).toULongLong();
		if (!id) {
			continue;
		}
		entity = EntityInText(
			EntityType::CustomEmoji,
			entity.offset(),
			entity.length(),
			Data::SerializeCustomEmojiId(DocumentId(id)));
		changed = true;
	}
	if (changed) {
		Originals().emplace(item, original);
	}
}

void ForgetLocalPremiumEmoji(not_null<const HistoryItem*> item) {
	Originals().remove(item);
}

void ReapplyLocalPremiumEmoji(not_null<HistoryItem*> item) {
	const auto i = Originals().find(item);
	if (i != end(Originals())) {
		auto original = i->second;
		item->setText(std::move(original));
		return;
	}
	const auto &text = item->originalText();
	for (const auto &entity : text.entities) {
		if (entity.type() == EntityType::CustomUrl
			&& entity.data().startsWith(QString::fromLatin1(kPrefix))) {
			item->setText(text);
			return;
		}
	}
}

} // namespace Extera
