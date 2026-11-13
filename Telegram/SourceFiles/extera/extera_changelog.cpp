#include "extera/extera_changelog.h"

#include "base/call_delayed.h"
#include "boxes/translate_box.h"
#include "core/application.h"
#include "core/version.h"
#include "data/data_user.h"
#include "extera/extera_config.h"
#include "extera/extera_update.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/main_window.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Extera::Changelog {
namespace {

constexpr auto kMaxReleases = 20;
constexpr auto kShowDelay = 2500;

struct Release {
	QString version;
	QDate date;
	QString body;
};

struct State {
	QNetworkAccessManager *manager = nullptr;
	std::vector<Release> releases;
	std::vector<Fn<void()>> waiting;
	bool loading = false;
	bool failed = false;
	rpl::lifetime lifetime;
};

State &Instance() {
	static auto result = State();
	return result;
}

[[nodiscard]] QString CachePath() {
	return cWorkingDir() + u"tdata/exd_releases.json"_q;
}

[[nodiscard]] int CurrentKey() {
	return Extera::Update::BaseVersion() * 100 + Extera::Update::Build();
}

bool Apply(const QByteArray &bytes) {
	const auto document = QJsonDocument::fromJson(bytes);
	if (!document.isArray()) {
		return false;
	}
	auto releases = std::vector<Release>();
	for (const auto &entry : document.array()) {
		const auto object = entry.toObject();
		if (object.value(u"draft"_q).toBool()) {
			continue;
		}
		auto version = object.value(u"tag_name"_q).toString();
		if (version.startsWith('v')) {
			version = version.mid(1);
		}
		releases.push_back({
			.version = version,
			.date = QDateTime::fromString(
				object.value(u"published_at"_q).toString(),
				Qt::ISODate).toLocalTime().date(),
			.body = object.value(u"body"_q).toString(),
		});
		if (int(releases.size()) >= kMaxReleases) {
			break;
		}
	}
	if (releases.empty()) {
		return false;
	}
	Instance().releases = std::move(releases);
	return true;
}

void Finish(bool ok) {
	auto &state = Instance();
	state.loading = false;
	state.failed = !ok && state.releases.empty();
	for (const auto &callback : base::take(state.waiting)) {
		callback();
	}
}

void Load(Fn<void()> done) {
	auto &state = Instance();
	if (done) {
		state.waiting.push_back(std::move(done));
	}
	if (state.loading) {
		return;
	}
	state.loading = true;
	if (!state.manager) {
		state.manager = new QNetworkAccessManager(qApp);
	}
	auto request = QNetworkRequest(QUrl(
		u"https://api.github.com/repos/metawka/exDesktop/releases?per_page=%1"_q
			.arg(kMaxReleases)));
	request.setRawHeader("Accept", "application/vnd.github+json");
	request.setRawHeader("User-Agent", "exDesktop");
	const auto reply = state.manager->get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			Finish(false);
			return;
		}
		const auto bytes = reply->readAll();
		const auto ok = Apply(bytes);
		if (ok) {
			auto file = QFile(CachePath());
			if (file.open(QIODevice::WriteOnly)) {
				file.write(bytes);
			}
		}
		Finish(ok);
	});
}

[[nodiscard]] QString LocalizedBody(const QString &body) {
	const auto marker = u"<!-- ru -->"_q;
	const auto index = body.indexOf(marker);
	if (index < 0) {
		return body;
	}
	return Lang::Id().startsWith(u"ru"_q)
		? body.mid(index + marker.size())
		: body.mid(0, index);
}

[[nodiscard]] bool SkippedSection(const QString &title) {
	return title.startsWith(u"Installation"_q, Qt::CaseInsensitive)
		|| title.startsWith(
			QString::fromUtf8("\xD0\xA3\xD1\x81\xD1\x82\xD0\xB0\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xBA\xD0\xB0"),
			Qt::CaseInsensitive);
}

[[nodiscard]] TextWithEntities Format(const QString &body) {
	auto result = TextWithEntities();
	auto skip = false;
	auto gap = false;
	auto normalized = body;
	normalized.remove('\r');
	for (auto line : normalized.split('\n')) {
		line = line.trimmed();
		if (line.startsWith(u"<!--"_q)) {
			continue;
		}
		if (line.startsWith('#')) {
			const auto title = line.mid(line.indexOf(' ') + 1).trimmed();
			skip = SkippedSection(title);
			if (skip) {
				continue;
			}
			if (!result.empty()) {
				result.append(u"\n\n"_q);
			}
			result.append(Ui::Text::Bold(title));
			gap = false;
			continue;
		}
		if (skip) {
			continue;
		}
		if (line.isEmpty()) {
			gap = !result.empty();
			continue;
		}
		line.remove('`');
		line.remove(u"**"_q);
		if (line.startsWith(u"- "_q) || line.startsWith(u"* "_q)) {
			line = QString::fromUtf8("\xE2\x80\xA2 ") + line.mid(2);
		}
		if (!result.empty()) {
			result.append(gap ? u"\n\n"_q : u"\n"_q);
		}
		result.append(line);
		gap = false;
	}
	return result;
}

void Fill(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto &state = Instance();
	const auto content = box->verticalLayout();
	if (state.releases.empty()) {
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				(state.failed
					? tr::lng_exd_changelog_failed()
					: tr::lng_exd_changelog_loading()),
				st::boxLabel),
			st::boxRowPadding);
		return;
	}
	auto first = true;
	for (const auto &release : state.releases) {
		const auto text = Format(LocalizedBody(release.body));
		if (text.empty()) {
			continue;
		}
		if (!first) {
			Ui::AddSkip(content);
			Ui::AddDivider(content);
			Ui::AddSkip(content);
		}
		first = false;
		auto title = Ui::Text::Bold(u"exDesktop "_q + release.version);
		if (release.date.isValid()) {
			title.append(QString::fromUtf8("  \xC2\xB7  ")).append(
				QLocale().toString(release.date, QLocale::LongFormat));
		}
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				rpl::single(title),
				st::boxLabel),
			st::boxRowPadding);
		Ui::AddSkip(content, st::settingsCheckboxesSkip);
		const auto label = content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				rpl::single(text),
				st::boxLabel),
			st::boxRowPadding);
		label->setSelectable(true);
	}
}

[[nodiscard]] TextWithEntities TranslationText() {
	constexpr auto kLimit = 3500;
	auto result = TextWithEntities();
	for (const auto &release : Instance().releases) {
		const auto text = Format(LocalizedBody(release.body));
		if (text.empty()) {
			continue;
		}
		auto part = Ui::Text::Bold(u"exDesktop "_q + release.version);
		part.append(QChar('\n')).append(text);
		if (!result.empty()
			&& result.text.size() + part.text.size() + 2 > kLimit) {
			break;
		}
		if (!result.empty()) {
			result.append(u"\n\n"_q);
		}
		result.append(part);
	}
	return result;
}

void ShowLoaded(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_exd_changelog_title());
		box->setWidth(st::boxWideWidth * 3 / 2);
		box->setMaxHeight(st::boxMaxListHeight);
		Fill(box, controller);
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		if (!Instance().releases.empty()) {
			box->addLeftButton(tr::lng_exd_changelog_translate(), [=] {
				controller->show(
					Box(
						Ui::TranslateBox,
						controller->session().user(),
						MsgId(),
						TranslationText(),
						false),
					Ui::LayerOption::KeepOther);
			});
		}
	}));
}

void ShowWhenReady(not_null<Window::SessionController*> controller) {
	const auto weak = base::make_weak(controller);
	Load([=] {
		if (const auto strong = weak.get()) {
			ShowLoaded(strong);
		}
	});
}

} // namespace

void Start() {
	static auto started = false;
	if (started) {
		return;
	}
	started = true;
	auto file = QFile(CachePath());
	if (file.open(QIODevice::ReadOnly)) {
		Apply(file.readAll());
	}
	Core::App().domain().activeSessionValue(
	) | rpl::filter([](Main::Session *session) {
		return session != nullptr;
	}) | rpl::take(1) | rpl::on_next([](Main::Session *session) {
		const auto seen = Int(u"changelog_seen"_q);
		const auto current = CurrentKey();
		if (seen >= current || (!seen && !Bool(u"wizard_shown"_q))) {
			Config::Instance().setInt(u"changelog_seen"_q, current);
			return;
		}
		base::call_delayed(kShowDelay, [=] {
			const auto window = Core::App().activePrimaryWindow();
			if (const auto controller = window
					? window->sessionController()
					: nullptr) {
				Config::Instance().setInt(u"changelog_seen"_q, current);
				ShowWhenReady(controller);
			}
		});
	}, Instance().lifetime);
}

void Show(not_null<Window::SessionController*> controller) {
	if (Instance().releases.empty()) {
		ShowWhenReady(controller);
	} else {
		ShowLoaded(controller);
		Load(nullptr);
	}
}

} // namespace Extera::Changelog
