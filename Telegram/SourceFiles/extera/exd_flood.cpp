#include "extera/exd_flood.h"

#include "core/click_handler_types.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "extera/extera_config.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/history_item_components.h"
#include "data/data_session.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_service_message.h"
#include "lang/lang_keys.h"

namespace Exd::Flood {
namespace {

constexpr auto kMinRun = 4;
constexpr auto kMaxTextLength = 20;
constexpr auto kMaxGap = TimeId(300);

[[nodiscard]] base::flat_set<FullMsgId> &Expanded() {
	static auto result = base::flat_set<FullMsgId>();
	return result;
}

[[nodiscard]] bool IsFlood(not_null<HistoryItem*> item) {
	if (item->isService()
		|| item->Get<HistoryMessageForwarded>()
		|| item->replyToId()
		|| item->inlineReplyMarkup()) {
		return false;
	}
	if (const auto media = item->media()) {
		if (const auto document = media->document()) {
			return document->sticker()
				|| document->isAnimation()
				|| document->isVideoMessage();
		}
		return (dynamic_cast<Data::MediaDice*>(media) != nullptr);
	}
	const auto text = item->originalText().text.trimmed();
	if (text.isEmpty() || text.size() > kMaxTextLength) {
		return false;
	}
	return (text.split(' ', Qt::SkipEmptyParts).size() <= 2);
}

[[nodiscard]] bool Enabled(not_null<History*> history) {
	const auto peer = history->peer;
	return Extera::Bool(u"exd_noise_collapse"_q)
		&& (peer->isChat() || peer->isMegagroup());
}

void Apply(
		const std::vector<not_null<HistoryView::Element*>> &run,
		bool collapse) {
	const auto first = run.front();
	for (auto i = 1; i < int(run.size()); ++i) {
		run[i]->setExdCollapsed(collapse);
		run[i]->setExdFloodHead(0);
	}
	const auto count = int(run.size()) - 1;
	first->setExdCollapsed(false);
	first->setExdFloodBar({}, nullptr);
	first->setExdFloodHead(collapse ? count : -count);
}

} // namespace

void Refresh(not_null<History*> history) {
	auto views = std::vector<not_null<HistoryView::Element*>>();
	for (const auto &block : history->blocks) {
		for (const auto &view : block->messages) {
			views.push_back(view.get());
		}
	}
	RefreshViews(history, views);
}

void RefreshViews(
		not_null<History*> history,
		const std::vector<not_null<HistoryView::Element*>> &views) {
	const auto enabled = Enabled(history);
	auto run = std::vector<not_null<HistoryView::Element*>>();
	const auto flush = [&] {
		if (run.empty()) {
			return;
		}
		const auto id = run.front()->data()->fullId();
		if (!enabled || int(run.size()) < kMinRun) {
			for (const auto element : run) {
				element->setExdCollapsed(false);
				element->setExdFloodBar({}, nullptr);
				element->setExdFloodHead(0);
			}
		} else {
			Apply(run, !Expanded().contains(id));
		}
		run.clear();
	};
	for (const auto element : views) {
		const auto item = element->data();
		if (!enabled || !IsFlood(item)) {
			flush();
			if (element->exdCollapsed()
				|| element->hasExdFloodBar()
				|| element->exdFloodHead()) {
				element->setExdCollapsed(false);
				element->setExdFloodBar({}, nullptr);
				element->setExdFloodHead(0);
			}
			continue;
		}
		if (!run.empty()) {
			const auto last = run.back()->data();
			if (last->from() != item->from()
				|| (item->date() - last->date()) > kMaxGap) {
				flush();
			}
		}
		run.push_back(element);
	}
	flush();
}

void Toggle(not_null<HistoryItem*> item) {
	const auto id = item->fullId();
	if (Expanded().contains(id)) {
		Expanded().remove(id);
	} else {
		Expanded().emplace(id);
	}
	Refresh(item->history());
	item->history()->owner().requestItemViewRefresh(item);
	item->history()->owner().requestItemResize(item);
}

} // namespace Exd::Flood
