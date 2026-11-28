#pragma once

namespace Data {
class ForumTopic;
class Forum;
} // namespace Data

namespace Exd::HiddenTopics {

[[nodiscard]] bool Contains(PeerId peer, MsgId rootId);
[[nodiscard]] std::vector<MsgId> List(PeerId peer);
void Set(not_null<Data::ForumTopic*> topic, bool hidden);
void Show(not_null<Data::Forum*> forum, MsgId rootId);

} // namespace Exd::HiddenTopics
