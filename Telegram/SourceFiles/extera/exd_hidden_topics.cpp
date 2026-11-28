#include "extera/exd_hidden_topics.h"

#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "extera/extera_config.h"
#include "history/history.h"

namespace Exd::HiddenTopics {
namespace {

const auto kKey = u"exd_hidden_topics"_q;

[[nodiscard]] base::flat_set<std::pair<uint64, int64>> &Cache() {
	static auto result = base::flat_set<std::pair<uint64, int64>>();
	static auto loaded = false;
	if (!loaded) {
		loaded = true;
		const auto value = Extera::String(kKey);
		for (const auto &entry : value.split(',', Qt::SkipEmptyParts)) {
			const auto parts = entry.split(':');
			if (parts.size() == 2) {
				result.emplace(
					parts[0].toULongLong(),
					parts[1].toLongLong());
			}
		}
	}
	return result;
}

void Save() {
	auto list = QStringList();
	for (const auto &[peer, root] : Cache()) {
		list.push_back(QString::number(peer) + ':' + QString::number(root));
	}
	Extera::Config::Instance().setString(kKey, list.join(','));
}

} // namespace

bool Contains(PeerId peer, MsgId rootId) {
	return Cache().contains(std::make_pair(peer.value, rootId.bare));
}

std::vector<MsgId> List(PeerId peer) {
	auto result = std::vector<MsgId>();
	for (const auto &[id, root] : Cache()) {
		if (id == peer.value) {
			result.push_back(MsgId(root));
		}
	}
	return result;
}

void Set(not_null<Data::ForumTopic*> topic, bool hidden) {
	const auto key = std::make_pair(
		topic->history()->peer->id.value,
		topic->rootId().bare);
	if (hidden) {
		Cache().emplace(key);
	} else {
		Cache().remove(key);
	}
	Save();
	topic->exdSetHidden(hidden);
}

void Show(not_null<Data::Forum*> forum, MsgId rootId) {
	if (const auto topic = forum->topicFor(rootId)) {
		Set(topic, false);
		return;
	}
	Cache().remove(std::make_pair(
		forum->history()->peer->id.value,
		rootId.bare));
	Save();
}

} // namespace Exd::HiddenTopics
