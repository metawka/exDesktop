#pragma once

#include <QtCore/QJsonObject>

namespace Window {
class SessionController;
} // namespace Window

namespace Exd::PluginPorter {

struct Progress {
	enum class Stage {
		Requesting,
		Checking,
		Fixing,
		Done,
		Nothing,
		Failed,
	};
	Stage stage = Stage::Requesting;
	int attempt = 0;
	int attempts = 1;
	int elapsed = 0;
	int total = 0;
	int fixed = 0;
	bool limit = false;
	bool library = false;
	QStringList errors;
	QStringList sections;
	QStringList requested;
	QStringList hints;
	QString error;
	QString path;
	QJsonObject report;
};

struct Info {
	int provider = 0;
	QString model;
	QString endpoint;
	bool local = false;
	bool agent = true;
};

struct Job;

[[nodiscard]] bool Configured();
[[nodiscard]] bool Suggested(const QJsonObject &info);
[[nodiscard]] Info CurrentInfo();

[[nodiscard]] std::shared_ptr<Job> Run(
	const QString &path,
	Fn<void(const Progress&)> update,
	QString *error);
void Cancel(const std::shared_ptr<Job> &job);

[[nodiscard]] int ProvidersCount();
[[nodiscard]] QString DefaultModel(int provider);
[[nodiscard]] bool ProviderLocal(int provider);

void Start(
	not_null<Window::SessionController*> controller,
	const QString &path);

} // namespace Exd::PluginPorter
