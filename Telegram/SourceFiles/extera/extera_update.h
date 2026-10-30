#pragma once

namespace Extera::Update {

enum class CheckResult {
	Available,
	Latest,
	Failed,
};

void Start();
[[nodiscard]] int Build();
[[nodiscard]] QString VersionText();
void CheckNow(Fn<void(CheckResult)> done);
[[nodiscard]] rpl::producer<bool> AvailableValue();
[[nodiscard]] QString AvailableVersion();
void OpenRelease();
void Install();

} // namespace Extera::Update
