#pragma once

class HistoryItem;

namespace Exd::Radar {

[[nodiscard]] bool Matches(not_null<HistoryItem*> item);

} // namespace Exd::Radar
