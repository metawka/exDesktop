#include "extera/extera_update.h"

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

constexpr auto kExteraBuild = 4;
constexpr auto kCheckInterval = 6 * 60 * 60 * crl::time(1000);
constexpr auto kFirstCheckDelay = 10 * crl::time(1000);

struct State {
	QNetworkAccessManager *manager = nullptr;
	rpl::variable<bool> available = false;
	QString version;
	QString url;
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
		state.available = (Parse(tag) > Current());
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

rpl::producer<bool> AvailableValue() {
	return Instance().available.value();
}

QString AvailableVersion() {
	return Instance().version;
}

void OpenRelease() {
	const auto &url = Instance().url;
	UrlClickHandler::Open(url.isEmpty()
		? u"https://github.com/metawka/exDesktop/releases/latest"_q
		: url);
}

} // namespace Extera::Update
