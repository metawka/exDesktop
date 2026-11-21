#pragma once

#include <QtCore/QDateTime>

namespace Extera::Update {

enum class CheckResult {
	Available,
	Latest,
	Failed,
};

void Start();
[[nodiscard]] int Build();
[[nodiscard]] int BaseVersion();
[[nodiscard]] QString VersionText();
void CheckNow(Fn<void(CheckResult)> done);
[[nodiscard]] rpl::producer<bool> AvailableValue();
[[nodiscard]] QString AvailableVersion();
void OpenRelease();
void OpenAllReleases();
void Install();

struct Release {
	QString version;
	QString notes;
	QDateTime published;
	bool prerelease = false;
	bool newer = false;
	bool installable = false;
};
[[nodiscard]] Release LatestRelease();
[[nodiscard]] rpl::producer<float64> ProgressValue();
void Skip();

} // namespace Extera::Update
