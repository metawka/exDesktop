#include "extera/extera_update.h"
#include "core/application.h"
#include "extera/extera_config.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QProcess>
#include <QtCore/QCryptographicHash>
#include <QtCore/QUuid>
#include <QtCore/QDateTime>

#include "base/timer.h"
#include "core/version.h"
#include "ui/basic_click_handlers.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Extera::Update {
namespace {

constexpr auto kExteraBase = AppVersionStr;
constexpr auto kExteraBaseNumber = AppVersion;
constexpr auto kExteraBuild = 2;
constexpr auto kExteraSuffix = "b";
constexpr auto kCheckInterval = 6 * 60 * 60 * crl::time(1000);
constexpr auto kFirstCheckDelay = 10 * crl::time(1000);

struct State {
	QNetworkAccessManager *manager = nullptr;
	rpl::variable<bool> available = false;
	QString version;
	QString url;
	QString assetUrl;
	QString assetName;
	QString assetDigest;
	QString downloaded;
	QString notes;
	QDateTime published;
	bool prerelease = false;
	bool newer = false;
	rpl::variable<float64> progress = -1.;
	bool downloading = false;
	bool installAfterDownload = false;
	std::unique_ptr<base::Timer> timer;
};

State &Instance() {
	static auto result = State();
	return result;
}

[[nodiscard]] std::vector<int> Parse(QString version) {
	if (version.startsWith('v') || version.startsWith('V')) {
		version = version.mid(1);
	}
	auto result = std::vector<int>();
	for (const auto &part : version.split('.')) {
		auto digits = QString();
		for (const auto ch : part) {
			if (!ch.isDigit()) {
				break;
			}
			digits += ch;
		}
		result.push_back(digits.toInt());
	}
	while (result.size() < 4) {
		result.push_back(0);
	}
	return result;
}

[[nodiscard]] std::vector<int> Current() {
	auto result = Parse(QString::fromLatin1(kExteraBase));
	result.resize(3);
	result.push_back(kExteraBuild);
	return result;
}

[[nodiscard]] bool Installed() {
	return QFile::exists(
		QCoreApplication::applicationDirPath() + u"/unins000.exe"_q);
}

[[nodiscard]] QString FileDigest(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return QString();
	}
	auto hash = QCryptographicHash(QCryptographicHash::Sha256);
	if (!hash.addData(&file)) {
		return QString();
	}
	return QString::fromLatin1(hash.result().toHex());
}

void RunInstaller(const QString &path) {
	const auto &state = Instance();
	if (state.assetDigest.isEmpty() || FileDigest(path) != state.assetDigest) {
		QFile::remove(path);
		Instance().downloaded = QString();
		return;
	}
	const auto started = QProcess::startDetached(path, {
		u"/SILENT"_q,
		u"/SUPPRESSMSGBOXES"_q,
		u"/NORESTART"_q,
		u"/CLOSEAPPLICATIONS"_q,
		u"/RESTARTAPPLICATIONS"_q,
	});
	if (started) {
		Core::Quit();
	}
}

void Download(bool install) {
	auto &state = Instance();
	if (!Installed() || state.assetUrl.isEmpty()) {
		return;
	}
	state.installAfterDownload = state.installAfterDownload || install;
	if (!state.downloaded.isEmpty() && QFile::exists(state.downloaded)) {
		if (state.installAfterDownload) {
			RunInstaller(state.downloaded);
		}
		return;
	} else if (state.downloading) {
		return;
	}
	state.downloading = true;
	state.progress = 0.;
	if (!state.manager) {
		state.manager = new QNetworkAccessManager(qApp);
	}
	auto request = QNetworkRequest(QUrl(state.assetUrl));
	request.setRawHeader("User-Agent", "exDesktop");
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	const auto reply = state.manager->get(request);
	QObject::connect(reply, &QNetworkReply::downloadProgress, [=](
			qint64 received,
			qint64 total) {
		if (total > 0) {
			Instance().progress = std::clamp(
				float64(received) / float64(total),
				0.,
				0.99);
		}
	});
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		auto &state = Instance();
		state.downloading = false;
		if (reply->error() != QNetworkReply::NoError) {
			state.progress = -1.;
			return;
		}
		const auto data = reply->readAll();
		const auto digest = QString::fromLatin1(QCryptographicHash::hash(
			data,
			QCryptographicHash::Sha256).toHex());
		if (state.assetDigest.isEmpty() || digest != state.assetDigest) {
			state.progress = -1.;
			return;
		}
		const auto folder = QDir::tempPath()
			+ u"/exDesktop/update/"_q
			+ QUuid::createUuid().toString(QUuid::WithoutBraces);
		QDir().mkpath(folder);
		const auto path = folder + '/' + state.assetName;
		auto file = QFile(path);
		if (!file.open(QIODevice::WriteOnly)) {
			return;
		}
		file.write(data);
		file.close();
		state.downloaded = path;
		state.progress = 1.;
		if (state.installAfterDownload) {
			RunInstaller(path);
		}
	});
}

void Apply(const QJsonObject &object, Fn<void(CheckResult)> done) {
	const auto tag = object.value(u"tag_name"_q).toString();
	if (tag.isEmpty() || object.value(u"draft"_q).toBool()) {
		if (done) {
			done(CheckResult::Failed);
		}
		return;
	}
	auto &state = Instance();
	if (state.version != tag) {
		state.downloaded = QString();
		state.progress = -1.;
	}
	state.version = tag;
	state.notes = object.value(u"body"_q).toString();
	state.published = QDateTime::fromString(
		object.value(u"published_at"_q).toString(),
		Qt::ISODate).toLocalTime();
	state.prerelease = object.value(u"prerelease"_q).toBool();
	const auto page = object.value(u"html_url"_q).toString();
	state.url = page.startsWith(
		u"https://github.com/metawka/exDesktop/"_q)
		? page
		: QString();
	state.assetUrl = QString();
	state.assetName = QString();
	state.assetDigest = QString();
	for (const auto &asset : object.value(u"assets"_q).toArray()) {
		const auto info = asset.toObject();
		const auto name = info.value(u"name"_q).toString();
		const auto url = info.value(u"browser_download_url"_q).toString();
		const auto digest = info.value(u"digest"_q).toString();
		if (name.startsWith(u"exDesktop-setup-x64"_q)
			&& name.endsWith(u".exe"_q)
			&& !name.contains('/')
			&& !name.contains('\\')
			&& url.startsWith(
				u"https://github.com/metawka/exDesktop/releases/download/"_q)
			&& digest.startsWith(u"sha256:"_q)) {
			state.assetName = name;
			state.assetUrl = url;
			state.assetDigest = digest.mid(7).toLower();
			break;
		}
	}
	state.newer = (Parse(tag) > Current());
	state.available = state.newer
		&& (Extera::String(u"exd_update_skip"_q) != tag);
	if (state.available.current()) {
		const auto mode = Extera::Int(u"auto_update"_q);
		if (mode == 0 || mode == 1) {
			Download(mode == 0);
		}
	}
	if (done) {
		done(state.newer ? CheckResult::Available : CheckResult::Latest);
	}
}

void Check(Fn<void(CheckResult)> done = nullptr) {
	auto &state = Instance();
	if (!state.manager) {
		state.manager = new QNetworkAccessManager(qApp);
	}
	const auto beta = Extera::Bool(u"exd_update_beta"_q);
	auto request = QNetworkRequest(QUrl(beta
		? u"https://api.github.com/repos/metawka/exDesktop/releases?per_page=10"_q
		: u"https://api.github.com/repos/metawka/exDesktop/releases/latest"_q));
	request.setRawHeader("Accept", "application/vnd.github+json");
	request.setRawHeader("User-Agent", "exDesktop");
	const auto reply = state.manager->get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			if (done) {
				done(CheckResult::Failed);
			}
			return;
		}
		const auto document = QJsonDocument::fromJson(reply->readAll());
		if (!beta) {
			Apply(document.object(), done);
			return;
		}
		for (const auto &item : document.array()) {
			const auto object = item.toObject();
			if (!object.value(u"draft"_q).toBool()) {
				Apply(object, done);
				return;
			}
		}
		if (done) {
			done(CheckResult::Failed);
		}
	});
}

} // namespace

void Start() {
	auto &state = Instance();
	if (state.timer) {
		return;
	}
	state.timer = std::make_unique<base::Timer>([] {
		Check();
		Instance().timer->callOnce(kCheckInterval);
	});
	state.timer->callOnce(kFirstCheckDelay);
}

void CheckNow(Fn<void(CheckResult)> done) {
	Check(std::move(done));
}

int Build() {
	return kExteraBuild;
}

int BaseVersion() {
	return kExteraBaseNumber;
}

QString VersionText() {
	return QString::fromLatin1(kExteraBase)
		+ '.'
		+ QString::number(kExteraBuild)
		+ QString::fromLatin1(kExteraSuffix);
}

rpl::producer<bool> AvailableValue() {
	return Instance().available.value();
}

QString AvailableVersion() {
	return Instance().version;
}

void Install() {
	auto &state = Instance();
	if (Installed() && !state.assetUrl.isEmpty()) {
		Download(true);
	} else {
		OpenRelease();
	}
}

Release LatestRelease() {
	const auto &state = Instance();
	return {
		.version = state.version,
		.notes = state.notes,
		.published = state.published,
		.prerelease = state.prerelease,
		.newer = state.newer,
		.installable = Installed() && !state.assetUrl.isEmpty(),
	};
}

rpl::producer<float64> ProgressValue() {
	return Instance().progress.value();
}

void Skip() {
	auto &state = Instance();
	Extera::Config::Instance().setString(u"exd_update_skip"_q, state.version);
	state.available = false;
}

void OpenAllReleases() {
	UrlClickHandler::Open(u"https://github.com/metawka/exDesktop/releases"_q);
}

void OpenRelease() {
	const auto &url = Instance().url;
	UrlClickHandler::Open(url.isEmpty()
		? u"https://github.com/metawka/exDesktop/releases/latest"_q
		: url);
}

} // namespace Extera::Update
