#include "extera/extera_ws_proxy.h"

#include "extera/extera_config.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "mtproto/mtproto_proxy_data.h"
#include "base/timer.h"

#include <QtCore/QProcess>
#include <QtCore/QRandomGenerator>
#include <QtNetwork/QTcpServer>

namespace Extera::WsProxy {
namespace {

constexpr auto kDefaultPort = 1443;
constexpr auto kPortAttempts = 20;
constexpr auto kRestartDelay = crl::time(3000);
constexpr auto kMaxRestarts = 5;

struct State {
	QProcess *process = nullptr;
	MTP::ProxyData proxy;
	std::unique_ptr<base::Timer> restartTimer;
	int restarts = 0;
	bool stopping = false;
};

State &Instance() {
	static auto result = State();
	return result;
}

[[nodiscard]] QString PythonDir() {
	return cExeDir() + u"python/"_q;
}

[[nodiscard]] QString Interpreter() {
#ifdef Q_OS_WIN
	return PythonDir() + u"pythonw.exe"_q;
#else
	return PythonDir() + u"bin/python3"_q;
#endif
}

[[nodiscard]] QString Script() {
	return PythonDir() + u"tgws/exd_tgws.py"_q;
}

[[nodiscard]] QString Secret() {
	auto secret = Extera::String(u"ws_proxy_secret"_q);
	if (secret.size() != 32) {
		auto bytes = QByteArray(16, Qt::Uninitialized);
		QRandomGenerator::system()->fillRange(
			reinterpret_cast<quint32*>(bytes.data()),
			bytes.size() / int(sizeof(quint32)));
		secret = QString::fromLatin1(bytes.toHex());
		Extera::Config::Instance().setString(u"ws_proxy_secret"_q, secret);
	}
	return secret;
}

[[nodiscard]] bool PortFree(int port) {
	auto server = QTcpServer();
	return server.listen(QHostAddress::LocalHost, port);
}

[[nodiscard]] int ChoosePort() {
	const auto saved = Extera::Int(u"ws_proxy_port"_q);
	const auto start = saved ? saved : kDefaultPort;
	for (auto i = 0; i != kPortAttempts; ++i) {
		if (PortFree(start + i)) {
			Extera::Config::Instance().setInt(u"ws_proxy_port"_q, start + i);
			return start + i;
		}
	}
	return start;
}

[[nodiscard]] bool IsOurs(const MTP::ProxyData &proxy) {
	return proxy.type == MTP::ProxyData::Type::Mtproto
		&& proxy.host == u"127.0.0.1"_q
		&& proxy.password.endsWith(Secret());
}

void Launch();

void ScheduleRestart() {
	auto &state = Instance();
	if (state.stopping || !Enabled()) {
		return;
	}
	if (state.restarts >= kMaxRestarts) {
		const auto &settings = Core::App().settings().proxy();
		if (settings.isEnabled() && IsOurs(settings.selected())) {
			Core::App().setCurrentProxy(
				settings.selected(),
				MTP::ProxyData::Settings::Disabled);
		}
		return;
	}
	++state.restarts;
	if (!state.restartTimer) {
		state.restartTimer = std::make_unique<base::Timer>([] { Launch(); });
	}
	state.restartTimer->callOnce(kRestartDelay);
}

void Launch() {
	auto &state = Instance();
	if (state.process || !Available()) {
		return;
	}
	state.stopping = false;
	const auto port = ChoosePort();
	const auto secret = Secret();
	state.proxy = MTP::ProxyData();
	state.proxy.type = MTP::ProxyData::Type::Mtproto;
	state.proxy.host = u"127.0.0.1"_q;
	state.proxy.port = uint32(port);
	state.proxy.password = u"dd"_q + secret;

	const auto process = new QProcess(qApp);
	state.process = process;
	process->setWorkingDirectory(PythonDir());
	process->setProgram(Interpreter());
	process->setArguments({
		u"-I"_q,
		Script(),
		QString::number(QCoreApplication::applicationPid()),
		u"--port"_q,
		QString::number(port),
		u"--secret"_q,
		secret,
	});
	process->setStandardOutputFile(QProcess::nullDevice());
	process->setStandardErrorFile(QProcess::nullDevice());
	QObject::connect(process, &QProcess::started, [=] {
		Core::App().setCurrentProxy(
			Instance().proxy,
			MTP::ProxyData::Settings::Enabled);
	});
	QObject::connect(
		process,
		&QProcess::finished,
		[=](int, QProcess::ExitStatus) {
			auto &state = Instance();
			if (state.process == process) {
				state.process = nullptr;
				process->deleteLater();
				ScheduleRestart();
			}
		});
	QObject::connect(
		process,
		&QProcess::errorOccurred,
		[=](QProcess::ProcessError error) {
			if (error != QProcess::FailedToStart) {
				return;
			}
			auto &state = Instance();
			if (state.process == process) {
				state.process = nullptr;
				process->deleteLater();
			}
		});
	process->start();
}

void Stop() {
	auto &state = Instance();
	state.stopping = true;
	if (state.restartTimer) {
		state.restartTimer->cancel();
	}
	if (const auto process = base::take(state.process)) {
		process->kill();
		process->waitForFinished(1000);
		process->deleteLater();
	}
	const auto &settings = Core::App().settings().proxy();
	if (settings.isEnabled() && IsOurs(settings.selected())) {
		Core::App().setCurrentProxy(
			settings.selected(),
			MTP::ProxyData::Settings::Disabled);
	}
}

} // namespace

bool Available() {
	return QFile::exists(Interpreter()) && QFile::exists(Script());
}

bool Enabled() {
	return Available() && Extera::Bool(u"ws_proxy"_q);
}

void Start() {
	if (Enabled()) {
		Instance().restarts = 0;
		Launch();
	}
}

void SetEnabled(bool enabled) {
	Extera::Config::Instance().setBool(u"ws_proxy"_q, enabled);
	if (enabled) {
		Start();
	} else {
		Stop();
	}
}

} // namespace Extera::WsProxy
