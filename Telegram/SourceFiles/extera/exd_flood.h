#pragma once

class History;
class HistoryItem;

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Exd::Flood {

void Refresh(not_null<History*> history);
void Toggle(not_null<HistoryItem*> item);
void RefreshViews(
	not_null<History*> history,
	const std::vector<not_null<HistoryView::Element*>> &views);

} // namespace Exd::Flood
