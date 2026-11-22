#pragma once

namespace Exd::CrashLog {

void Install();
[[nodiscard]] QString Folder();
[[nodiscard]] QString TakeNewReport();

} // namespace Exd::CrashLog
