#include "extera/exd_ttl_saver.h"

#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "extera/extera_config.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtCore/QStandardPaths>

namespace Exd::TtlSaver {
namespace {

struct PendingPhoto {
	std::shared_ptr<Data::PhotoMedia> media;
	QString path;
};

[[nodiscard]] QString Folder() {
	const auto base = QStandardPaths::writableLocation(
		QStandardPaths::DownloadLocation);
	const auto result = base + u"/exDesktop/View Once"_q;
	QDir().mkpath(result);
	return result;
}

[[nodiscard]] QString BaseName(not_null<HistoryItem*> item) {
	const auto peer = item->history()->peer;
	auto name = peer->name();
	name.replace(QRegularExpression(u"[\\\\/:*?\"<>|]"_q), u"_"_q);
	return name.left(40)
		+ u"_"_q
		+ QString::number(item->id.bare)
		+ u"_"_q
		+ QString::number(item->date());
}

} // namespace

void Start(not_null<Main::Session*> session) {
	static auto started = base::flat_set<not_null<Main::Session*>>();
	if (!started.emplace(session).second) {
		return;
	}
	session->lifetime().add([=] { started.remove(session); });
	const auto pending = std::make_shared<std::vector<PendingPhoto>>();
	session->data().newItemAdded(
	) | rpl::on_next([=](not_null<HistoryItem*> item) {
		if (!Extera::Bool(u"exd_save_ttl"_q) || item->out()) {
			return;
		}
		const auto media = item->media();
		if (!media || media->ttlSeconds() <= 0) {
			return;
		}
		const auto origin = Data::FileOrigin(item->fullId());
		const auto base = Folder() + '/' + BaseName(item);
		if (const auto photo = media->photo()) {
			auto view = photo->createMediaView();
			photo->load(origin, LoadFromCloudOrLocal, false);
			if (view->loaded()) {
				view->saveToFile(base + u".jpg"_q);
			} else {
				pending->push_back({ std::move(view), base + u".jpg"_q });
			}
		} else if (const auto document = media->document()) {
			auto extension = QFileInfo(document->filename()).suffix();
			if (extension.isEmpty()) {
				extension = document->isVideoMessage()
					? u"mp4"_q
					: document->isVoiceMessage()
					? u"ogg"_q
					: u"mp4"_q;
			}
			document->save(origin, base + '.' + extension);
		}
	}, session->lifetime());
	session->downloaderTaskFinished(
	) | rpl::on_next([=] {
		for (auto i = pending->begin(); i != pending->end();) {
			if (i->media->loaded()) {
				i->media->saveToFile(i->path);
				i = pending->erase(i);
			} else {
				++i;
			}
		}
	}, session->lifetime());
}

} // namespace Exd::TtlSaver
