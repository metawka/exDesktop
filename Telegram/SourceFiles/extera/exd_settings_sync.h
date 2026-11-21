#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Exd::SettingsSync {

[[nodiscard]] TimeId LocalDate();
void FetchCloudDate(
	not_null<Main::Session*> session,
	Fn<void(TimeId date, QString error)> done);
void Upload(not_null<Main::Session*> session, Fn<void(QString error)> done);
void Restore(not_null<Main::Session*> session, Fn<void(QString error)> done);
void Clear(not_null<Main::Session*> session, Fn<void(QString error)> done);
void Start(not_null<Main::Session*> session);

[[nodiscard]] QStringList Presets();
void SavePreset(const QString &name);
bool ApplyPreset(const QString &name);
void DeletePreset(const QString &name);

} // namespace Exd::SettingsSync
