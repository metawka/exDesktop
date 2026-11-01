#include "extera/extera_plugin_trust.h"

#include "apiwrap.h"
#include "extera/extera_badges.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "mtproto/facade.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace Extera::PluginTrust {
namespace {

constexpr auto kChunk = 512 * 1024;
constexpr auto kMaxSize = int64(8 * 1024 * 1024);

struct Verification {
	QString hash;
	QString name;
	int64 size = 0;
	int pending = 0;
	bool finished = false;
	Fn<void(bool)> done;
	base::flat_set<DocumentId> checked;
};

void Finish(const std::shared_ptr<Verification> &state, bool result) {
	if (state->finished) {
		return;
	}
	if (result || state->pending <= 0) {
		state->finished = true;
		if (const auto done = base::take(state->done)) {
			done(result);
		}
	}
}

void Release(const std::shared_ptr<Verification> &state) {
	--state->pending;
	Finish(state, false);
}

void Download(
		not_null<Main::Session*> session,
		not_null<DocumentData*> document,
		int64 offset,
		QByteArray collected,
		Fn<void(QByteArray)> done) {
	auto location = MTPInputFileLocation();
	document->mtpInput().match([&](const MTPDinputDocument &data) {
		location = MTP_inputDocumentFileLocation(
			data.vid(),
			data.vaccess_hash(),
			data.vfile_reference(),
			MTP_string());
	}, [&](const MTPDinputDocumentEmpty &) {
	});
	if (location.type() != mtpc_inputDocumentFileLocation) {
		done(QByteArray());
		return;
	}
	session->api().request(MTPupload_GetFile(
		MTP_flags(0),
		location,
		MTP_long(offset),
		MTP_int(kChunk)
	)).done([=](const MTPupload_File &result) mutable {
		auto part = QByteArray();
		result.match([&](const MTPDupload_file &data) {
			part = qba(data.vbytes());
		}, [](const MTPDupload_fileCdnRedirect &) {
		});
		collected.append(part);
		if (part.size() == kChunk && collected.size() < kMaxSize) {
			Download(session, document, offset + kChunk, collected, done);
		} else {
			done(collected);
		}
	}).fail([=] {
		done(QByteArray());
	}).toDC(MTP::downloadDcId(document->exdDcId(), 0)).send();
}

void CheckCandidate(
		not_null<Main::Session*> session,
		const std::shared_ptr<Verification> &state,
		not_null<DocumentData*> document) {
	if (state->finished
		|| state->checked.contains(document->id)
		|| document->size != state->size) {
		return;
	}
	state->checked.emplace(document->id);
	++state->pending;
	Download(session, document, 0, QByteArray(), [=](QByteArray bytes) {
		const auto hash = QString::fromLatin1(QCryptographicHash::hash(
			bytes,
			QCryptographicHash::Sha256).toHex());
		if (!bytes.isEmpty() && hash == state->hash) {
			Finish(state, true);
		} else {
			Release(state);
		}
	});
}

void Search(
		not_null<Main::Session*> session,
		const std::shared_ptr<Verification> &state,
		not_null<ChannelData*> channel,
		const QString &query) {
	using Flag = MTPmessages_Search::Flag;
	++state->pending;
	session->api().request(MTPmessages_Search(
		MTP_flags(Flag(0)),
		channel->input(),
		MTP_string(query),
		MTP_inputPeerEmpty(),
		MTPInputPeer(),
		MTPVector<MTPReaction>(),
		MTPint(),
		MTP_inputMessagesFilterDocument(),
		MTP_int(0),
		MTP_int(0),
		MTP_int(0),
		MTP_int(0),
		MTP_int(100),
		MTP_int(0),
		MTP_int(0),
		MTP_long(0)
	)).done([=](const MTPmessages_Messages &result) {
		const auto owner = &session->data();
		result.match([](const MTPDmessages_messagesNotModified &) {
		}, [&](const auto &data) {
			owner->processUsers(data.vusers());
			owner->processChats(data.vchats());
			for (const auto &message : data.vmessages().v) {
				message.match([&](const MTPDmessage &data) {
					const auto media = data.vmedia();
					if (!media) {
						return;
					}
					media->match([&](const MTPDmessageMediaDocument &data) {
						if (const auto document = data.vdocument()) {
							CheckCandidate(
								session,
								state,
								owner->processDocument(*document));
						}
					}, [](const auto &) {
					});
				}, [](const auto &) {
				});
			}
		});
		Release(state);
	}).fail([=] {
		Release(state);
	}).send();
}

} // namespace

std::vector<Source> Sources() {
	if (const auto remote = Extera::TrustedSources()) {
		auto result = std::vector<Source>();
		result.reserve(remote->size());
		for (const auto &source : *remote) {
			result.push_back({ .id = source.id, .username = source.username });
		}
		return result;
	}
	return {
		{ .id = 2189715950ULL, .username = u"exteraPlugins"_q },
		{ .id = 3914431400ULL, .username = u"exDesktopPlugins"_q },
	};
}

std::vector<uint64> TrustedChannels() {
	auto result = std::vector<uint64>();
	for (const auto &source : Sources()) {
		result.push_back(source.id);
	}
	return result;
}

DocumentId TrustedEmoji() {
	return Extera::BadgeTypeEmoji(u"trusted"_q);
}

bool IsTrustedChannel(not_null<PeerData*> peer) {
	if (!peer->isChannel()) {
		return false;
	}
	const auto bare = uint64(peerToChannel(peer->id).bare);
	return ranges::contains(TrustedChannels(), bare);
}

QString Sha256(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return QString();
	}
	auto hash = QCryptographicHash(QCryptographicHash::Sha256);
	hash.addData(&file);
	return QString::fromLatin1(hash.result().toHex());
}

void Verify(
		not_null<Main::Session*> session,
		const QString &path,
		Fn<void(bool)> done) {
	const auto info = QFileInfo(path);
	const auto state = std::make_shared<Verification>();
	state->hash = Sha256(path);
	state->name = info.fileName();
	state->size = info.size();
	state->done = std::move(done);
	if (state->hash.isEmpty() || state->size <= 0 || state->size > kMaxSize) {
		Finish(state, false);
		return;
	}
	if (Extera::IsTrustedHash(state->hash)) {
		Finish(state, true);
		return;
	}
	++state->pending;
	const auto query = info.completeBaseName();
	for (const auto &source : Sources()) {
		const auto id = ChannelId(source.id);
		if (const auto channel = session->data().channelLoaded(id)) {
			Search(session, state, channel, query);
			Search(session, state, channel, QString());
		} else if (!source.username.isEmpty()) {
			++state->pending;
			session->api().request(MTPcontacts_ResolveUsername(
				MTP_flags(0),
				MTP_string(source.username),
				MTP_string()
			)).done([=](const MTPcontacts_ResolvedPeer &result) {
				session->data().processUsers(result.data().vusers());
				session->data().processChats(result.data().vchats());
				if (const auto channel = session->data().channelLoaded(id)) {
					Search(session, state, channel, query);
					Search(session, state, channel, QString());
				}
				Release(state);
			}).fail([=] {
				Release(state);
			}).send();
		}
	}
	Release(state);
}

} // namespace Extera::PluginTrust
