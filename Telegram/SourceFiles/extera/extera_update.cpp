#include "extera/extera_update.h"
#include "core/application.h"
#include "extera/extera_config.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QProcess>

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

constexpr auto kExteraBuild = 6;
constexpr auto kCheckInterval = 6 * 60 * 60 * crl::time(1000);
constexpr auto kFirstCheckDelay = 10 * crl::time(1000);

struct State {
	QNetworkAccessManager *manager = nullptr;
	rpl::variable<bool> available = false;
	QString version;
	QString url;
	QString assetUrl;
	QString assetName;
	QString downloaded;
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
	auto result = Parse(QString::fromLatin1(AppVersionStr));
	result.resize(3);
	result.push_back(kExteraBuild);
	return result;
}

[[nodiscard]] bool Installed() {
	return QFile::exists(
		QCoreApplication::applicationDirPath() + u"/unins000.exe"_q);
}

void RunInstaller(const QString &path) {
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
	if (!state.manager) {
		state.manager = new QNetworkAccessManager(qApp);
	}
	auto request = QNetworkRequest(QUrl(state.assetUrl));
	request.setRawHeader("User-Agent", "exDesktop");
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	const auto reply = state.manager->get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		auto &state = Instance();
		state.downloading = false;
		if (reply->error() != QNetworkReply::NoError) {
			return;
		}
		const auto folder = QDir::tempPath() + u"/exDesktop/update"_q;
		QDir().mkpath(folder);
		const auto path = folder + '/' + state.assetName;
		auto file = QFile(path);
		if (!file.open(QIODevice::WriteOnly)) {
			return;
		}
		file.write(reply->readAll());
		file.close();
		state.downloaded = path;
		if (state.installAfterDownload) {
			RunInstaller(path);
		}
	});
}

void Check(Fn<void(CheckResult)> done = nullptr) {
	auto &state = Instance();
	if (!state.manager) {
		state.manager = new QNetworkAccessManager(qApp);
	}
	auto request = QNetworkRequest(QUrl(
		u"https://api.github.com/repos/metawka/exDesktop/releases/latest"_q));
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
		const auto object = QJsonDocument::fromJson(
			reply->readAll()).object();
		const auto tag = object.value(u"tag_name"_q).toString();
		if (tag.isEmpty() || object.value(u"draft"_q).toBool()) {
			if (done) {
				done(CheckResult::Failed);
			}
			return;
		}
		auto &state = Instance();
		state.version = tag;
		state.url = object.value(u"html_url"_q).toString();
		state.assetUrl = QString();
		state.assetName = QString();
		for (const auto &asset : object.value(u"assets"_q).toArray()) {
			const auto info = asset.toObject();
			const auto name = info.value(u"name"_q).toString();
			if (name.startsWith(u"exDesktop-setup-x64"_q)
				&& name.endsWith(u".exe"_q)) {
				state.assetName = name;
				state.assetUrl = info.value(
					u"browser_download_url"_q).toString();
				break;
			}
		}
		state.available = (Parse(tag) > Current());
		if (state.available.current()) {
			const auto mode = Extera::Int(u"auto_update"_q);
			if (mode == 0 || mode == 1) {
				Download(mode == 0);
			}
		}
		if (done) {
			done(state.available.current()
				? CheckResult::Available
				: CheckResult::Latest);
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

QString VersionText() {
	return QString::fromLatin1(AppVersionStr)
		+ u" ("_q
		+ QString::number(kExteraBuild)
		+ u")"_q;
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

void OpenRelease() {
	const auto &url = Instance().url;
	UrlClickHandler::Open(url.isEmpty()
		? u"https://github.com/metawka/exDesktop/releases/latest"_q
		: url);
}

} // namespace Extera::Update
