// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/utils/telegram_helpers.h"

#include "data/data_media_types.h"
#include "data/data_poll.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "ui/text/text_entity.h"
#include "chat_helpers/message_field.h"

TextWithTags extractText(not_null<HistoryItem*> item) {
	TextWithTags result;

	QString text;
	if (const auto media = item->media()) {
		if (const auto poll = media->poll()) {
			text.append("\xF0\x9F\x93\x8A ")
				.append(poll->question.text).append("\n");
			for (const auto &answer : poll->answers) {
				text.append("\u2022 ").append(answer.text.text).append("\n");
			}
		}
	}

	result.tags = TextUtilities::ConvertEntitiesToTextTags(item->originalText().entities);
	result.text = text.isEmpty() ? item->originalText().text : text;
	return result;
}

bool mediaDownloadable(const Data::Media *media) {
	if (!media
		|| media->webpage() || media->poll() || media->game()
		|| media->invoice() || media->location() || media->paper()
		|| media->giveawayStart() || media->giveawayResults()
		|| media->sharedContact() || media->call()
	) {
		return false;
	}
	return true;
}

bool exdIsDeleted(not_null<const HistoryItem*> item) {
	const auto history = item->history();
	const auto &deleted = history->owner().exteraDeletedFor(history->peer->id);
	return deleted.find(item->id) != deleted.end();
}
