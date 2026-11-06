#include "extera/extera_badges.h"
#include "ui/text/text_custom_emoji.h"
#include "window/themes/window_theme.h"
#include "ui/painter.h"
#include "styles/style_basic.h"

#include "data/data_peer.h"
#include "settings.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Extera {
namespace {

constexpr auto kPorterEmoji = DocumentId(5395339798430919126ULL);
constexpr auto kDeveloperEmoji = DocumentId(5359407509327085568ULL);
constexpr auto kSupporterEmoji = DocumentId(5391059537102927631ULL);
constexpr auto kRefreshInterval = 60 * 60 * 1000;
constexpr auto kPorterUsers = std::array<uint64, 2>{
	1388181090ULL,
	5288193145ULL,
};
constexpr auto kPorterChannels = std::array<uint64, 3>{
	2661845134ULL,
	4433892778ULL,
	4478798161ULL,
};

struct CustomBadge {
	DocumentId documentId = 0;
	QString text;
};

struct OwnBadge {
	QString type;
	DocumentId documentId = 0;
	QString text;
};

struct OwnLists {
	base::flat_map<QString, DocumentId> typeEmoji;
	base::flat_map<QString, QString> typeText;
	base::flat_map<uint64, OwnBadge> users;
	base::flat_map<uint64, OwnBadge> channels;
	base::flat_map<uint64, OwnBadge> bots;
	std::optional<std::vector<TrustedSource>> trusted;
};

struct Lists {
	base::flat_set<uint64> developers;
	base::flat_set<uint64> officialChannels;
	base::flat_set<uint64> supporters;
	base::flat_set<uint64> supporterChannels;
	base::flat_map<uint64, CustomBadge> custom;
};

Lists &Data() {
	static auto result = Lists();
	return result;
}

OwnLists &Own() {
	static auto result = OwnLists();
	return result;
}

base::flat_set<QString> &Hashes() {
	static auto result = base::flat_set<QString>();
	return result;
}

[[nodiscard]] QString HashesCachePath() {
	return cWorkingDir() + u"tdata/exd_trusted_hashes.json"_q;
}

[[nodiscard]] QString OwnCachePath() {
	return cWorkingDir() + u"tdata/exd_badges.json"_q;
}

QByteArray &ETag() {
	static auto result = QByteArray();
	return result;
}

[[nodiscard]] QString CachePath() {
	return cWorkingDir() + u"tdata/extera_badges.json"_q;
}

[[nodiscard]] base::flat_set<uint64> ReadIds(const QJsonValue &value) {
	auto result = base::flat_set<uint64>();
	for (const auto &id : value.toArray()) {
		result.emplace(uint64(id.toDouble()));
	}
	return result;
}

[[nodiscard]] uint64 ReadId(const QJsonValue &value) {
	return value.isString()
		? value.toString().toULongLong()
		: uint64(value.toDouble());
}

bool Apply(const QByteArray &bytes) {
	const auto document = QJsonDocument::fromJson(bytes);
	if (!document.isObject()) {
		return false;
	}
	const auto object = document.object();
	auto lists = Lists();
	lists.developers = ReadIds(object.value(u"developers"_q));
	lists.officialChannels = ReadIds(object.value(u"officialChannels"_q));
	lists.supporters = ReadIds(object.value(u"supporters"_q));
	lists.supporterChannels = ReadIds(object.value(u"supporterChannels"_q));
	for (const auto &entry : object.value(u"customBadges"_q).toArray()) {
		const auto badge = entry.toObject();
		const auto info = badge.value(u"badge"_q).toObject();
		lists.custom.emplace(ReadId(badge.value(u"id"_q)), CustomBadge{
			.documentId = DocumentId(ReadId(info.value(u"documentId"_q))),
			.text = info.value(u"text"_q).toString(),
		});
	}
	Data() = std::move(lists);
	return true;
}

[[nodiscard]] base::flat_map<uint64, OwnBadge> ReadOwn(
		const QJsonValue &value) {
	auto result = base::flat_map<uint64, OwnBadge>();
	for (const auto &entry : value.toArray()) {
		const auto object = entry.toObject();
		const auto id = ReadId(object.value(u"id"_q));
		if (!id) {
			continue;
		}
		result.emplace(id, OwnBadge{
			.type = object.value(u"type"_q).toString(),
			.documentId = DocumentId(ReadId(object.value(u"emoji"_q))),
			.text = object.value(u"text"_q).toString(),
		});
	}
	return result;
}

bool ApplyOwn(const QByteArray &bytes) {
	const auto document = QJsonDocument::fromJson(bytes);
	if (!document.isObject()) {
		return false;
	}
	const auto object = document.object();
	auto lists = OwnLists();
	const auto types = object.value(u"types"_q).toObject();
	for (auto i = types.begin(); i != types.end(); ++i) {
		const auto type = i.value().toObject();
		lists.typeEmoji.emplace(
			i.key(),
			DocumentId(ReadId(type.value(u"emoji"_q))));
		lists.typeText.emplace(i.key(), type.value(u"text"_q).toString());
	}
	lists.users = ReadOwn(object.value(u"users"_q));
	lists.channels = ReadOwn(object.value(u"channels"_q));
	lists.bots = ReadOwn(object.value(u"bots"_q));
	if (object.contains(u"trusted"_q)) {
		auto trusted = std::vector<TrustedSource>();
		for (const auto &entry : object.value(u"trusted"_q).toArray()) {
			const auto source = entry.toObject();
			if (const auto id = ReadId(source.value(u"id"_q))) {
				trusted.push_back({
					.id = id,
					.username = source.value(u"username"_q).toString(),
				});
			}
		}
		lists.trusted = std::move(trusted);
	}
	Own() = std::move(lists);
	return true;
}

bool ApplyHashes(const QByteArray &bytes) {
	const auto document = QJsonDocument::fromJson(bytes);
	if (!document.isObject()) {
		return false;
	}
	auto hashes = base::flat_set<QString>();
	const auto list = document.object().value(u"hashes"_q).toArray();
	for (const auto &entry : list) {
		const auto hash = entry.toObject().value(u"sha256"_q).toString();
		if (hash.size() == 64) {
			hashes.emplace(hash.toLower());
		}
	}
	Hashes() = std::move(hashes);
	return true;
}

void RefreshHashes(QNetworkAccessManager *manager) {
	auto request = QNetworkRequest(QUrl(
		u"https://raw.githubusercontent.com/metawka/exDesktop/main/trusted_hashes.json"_q));
	request.setRawHeader("User-Agent", "exDesktop");
	const auto reply = manager->get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			return;
		}
		const auto bytes = reply->readAll();
		if (ApplyHashes(bytes)) {
			auto file = QFile(HashesCachePath());
			if (file.open(QIODevice::WriteOnly)) {
				file.write(bytes);
			}
		}
	});
}

void RefreshOwn(QNetworkAccessManager *manager) {
	auto request = QNetworkRequest(QUrl(
		u"https://raw.githubusercontent.com/metawka/exDesktop/main/badges.json"_q));
	request.setRawHeader("User-Agent", "exDesktop");
	const auto reply = manager->get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			return;
		}
		const auto bytes = reply->readAll();
		if (ApplyOwn(bytes)) {
			auto file = QFile(OwnCachePath());
			if (file.open(QIODevice::WriteOnly)) {
				file.write(bytes);
			}
		}
	});
}

[[nodiscard]] uint64 BareId(not_null<PeerData*> peer) {
	if (peer->isUser()) {
		return uint64(peerToUser(peer->id).bare);
	} else if (peer->isChannel()) {
		return uint64(peerToChannel(peer->id).bare);
	} else if (peer->isChat()) {
		return uint64(peerToChat(peer->id).bare);
	}
	return 0;
}

[[nodiscard]] const OwnBadge *FindOwn(not_null<PeerData*> peer) {
	const auto bare = BareId(peer);
	const auto find = [&](const base::flat_map<uint64, OwnBadge> &map) {
		const auto i = map.find(bare);
		return (i != end(map)) ? &i->second : nullptr;
	};
	if (!peer->isUser()) {
		return find(Own().channels);
	} else if (const auto result = find(Own().users)) {
		return result;
	} else if (const auto result = find(Own().bots)) {
		return result;
	}
	return find(Own().channels);
}

[[nodiscard]] BadgeKind OwnKind(
		not_null<PeerData*> peer,
		const OwnBadge &badge) {
	if (badge.type == u"developer"_q) {
		return peer->isUser() ? BadgeKind::Porter : BadgeKind::PorterChannel;
	} else if (badge.type == u"bughunter"_q) {
		return BadgeKind::Bughunter;
	} else if (badge.type == u"helper"_q) {
		return BadgeKind::Helper;
	} else if (badge.type == u"donor"_q) {
		return BadgeKind::Donor;
	}
	return BadgeKind::Custom;
}

} // namespace

void PaintBadgeEmoji(
		QPainter &p,
		not_null<Ui::Text::CustomEmoji*> emoji,
		const Ui::Text::CustomEmojiPaintContext &context) {
	if (Window::Theme::IsNightMode()) {
		emoji->paint(p, context);
		return;
	}
	const auto side = std::max(emoji->width(), st::emojiSize)
		+ style::ConvertScale(4);
	const auto ratio = style::DevicePixelRatio();
	auto image = QImage(
		QSize(side, side) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	{
		auto q = QPainter(&image);
		auto copy = context;
		copy.position = QPoint();
		emoji->paint(q, copy);
	}
	auto weight = 0.;
	auto light = 0.;
	for (auto y = 0; y < image.height(); y += 2) {
		const auto line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
		for (auto x = 0; x < image.width(); x += 2) {
			const auto pixel = line[x];
			const auto alpha = qAlpha(pixel);
			if (alpha < 32) {
				continue;
			}
			const auto factor = 255. / alpha;
			const auto luma = (0.299 * qRed(pixel)
				+ 0.587 * qGreen(pixel)
				+ 0.114 * qBlue(pixel)) * factor / 255.;
			weight += alpha;
			light += luma * alpha;
		}
	}
	if (weight > 0. && (light / weight) > 0.8) {
		auto q = QPainter(&image);
		q.setCompositionMode(QPainter::CompositionMode_SourceIn);
		const auto color = QColor(context.textColor);
		q.fillRect(
			QRect(0, 0, side, side),
			color.alpha() ? color : st::windowFg->c);
	}
	p.drawImage(context.position, image);
}

void StartBadges() {
	static auto started = false;
	if (started) {
		return;
	}
	started = true;
	QFile::remove(CachePath());
	auto own = QFile(OwnCachePath());
	if (own.open(QIODevice::ReadOnly)) {
		ApplyOwn(own.readAll());
	}
	auto hashes = QFile(HashesCachePath());
	if (hashes.open(QIODevice::ReadOnly)) {
		ApplyHashes(hashes.readAll());
	}
	const auto manager = new QNetworkAccessManager(qApp);
	RefreshOwn(manager);
	RefreshHashes(manager);
	const auto timer = new QTimer(manager);
	QObject::connect(timer, &QTimer::timeout, [=] {
		RefreshOwn(manager);
		RefreshHashes(manager);
	});
	timer->start(kRefreshInterval);
}

BadgeKind BadgeKindFor(not_null<PeerData*> peer) {
	const auto bare = BareId(peer);
	if (!bare) {
		return BadgeKind::None;
	}
	if (const auto own = FindOwn(peer)) {
		return OwnKind(peer, *own);
	}
	const auto &data = Data();
	if (peer->isUser()) {
		if (ranges::contains(kPorterUsers, bare)) {
			return BadgeKind::Porter;
		} else if (data.custom.contains(bare)) {
			return BadgeKind::Custom;
		} else if (data.developers.contains(bare)) {
			return BadgeKind::Developer;
		} else if (data.supporters.contains(bare)) {
			return BadgeKind::Supporter;
		}
	} else {
		if (ranges::contains(kPorterChannels, bare)) {
			return BadgeKind::PorterChannel;
		} else if (data.custom.contains(bare)) {
			return BadgeKind::Custom;
		} else if (data.officialChannels.contains(bare)) {
			return BadgeKind::OfficialChannel;
		} else if (data.supporterChannels.contains(bare)) {
			return BadgeKind::Supporter;
		}
	}
	return BadgeKind::None;
}

DocumentId BadgeEmoji(not_null<PeerData*> peer) {
	if (const auto own = FindOwn(peer)) {
		if (own->documentId) {
			return own->documentId;
		}
		const auto i = Own().typeEmoji.find(own->type);
		if (i != end(Own().typeEmoji) && i->second) {
			return i->second;
		}
		const auto fallback = Own().typeEmoji.find(u"default"_q);
		if (fallback != end(Own().typeEmoji) && fallback->second) {
			return fallback->second;
		}
	}
	switch (BadgeKindFor(peer)) {
	case BadgeKind::Porter:
	case BadgeKind::PorterChannel:
		return kPorterEmoji;
	case BadgeKind::Developer:
	case BadgeKind::OfficialChannel:
		return kDeveloperEmoji;
	case BadgeKind::Supporter:
	case BadgeKind::Bughunter:
	case BadgeKind::Helper:
	case BadgeKind::Donor:
		return kSupporterEmoji;
	case BadgeKind::Custom: {
		const auto i = Data().custom.find(BareId(peer));
		return (i != end(Data().custom) && i->second.documentId)
			? i->second.documentId
			: kSupporterEmoji;
	}
	case BadgeKind::None:
		break;
	}
	return DocumentId();
}

QString BadgeCustomText(not_null<PeerData*> peer) {
	if (const auto own = FindOwn(peer)) {
		if (!own->text.isEmpty()) {
			return own->text;
		}
		const auto i = Own().typeText.find(own->type);
		return (i != end(Own().typeText)) ? i->second : QString();
	}
	const auto i = Data().custom.find(BareId(peer));
	if (i == end(Data().custom)) {
		return QString();
	}
	auto text = i->second.text;
	return text.replace(u"**"_q, QString());
}

std::optional<std::vector<TrustedSource>> TrustedSources() {
	return Own().trusted;
}

bool IsTrustedHash(const QString &sha256) {
	return Hashes().contains(sha256.toLower());
}

bool IsBadgeEmoji(DocumentId id) {
	if (!id) {
		return false;
	} else if (id == kPorterEmoji
		|| id == kDeveloperEmoji
		|| id == kSupporterEmoji) {
		return true;
	}
	for (const auto &[type, emoji] : Own().typeEmoji) {
		if (emoji == id) {
			return true;
		}
	}
	for (const auto map : { &Own().users, &Own().channels, &Own().bots }) {
		for (const auto &[bare, badge] : *map) {
			if (badge.documentId == id) {
				return true;
			}
		}
	}
	for (const auto &[bare, custom] : Data().custom) {
		if (custom.documentId == id) {
			return true;
		}
	}
	return false;
}

DocumentId BadgeTypeEmoji(const QString &type) {
	const auto i = Own().typeEmoji.find(type);
	return (i != end(Own().typeEmoji)) ? i->second : DocumentId();
}

} // namespace Extera
