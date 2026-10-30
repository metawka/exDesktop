#pragma once

#include "base/timer.h"

#include <QtCore/QJsonObject>

namespace Extera {

class Config final {
public:
	[[nodiscard]] static Config &Instance();

	void setWorkingDir(const QString &path);

	[[nodiscard]] bool boolValue(const QString &key) const;
	[[nodiscard]] int intValue(const QString &key) const;
	[[nodiscard]] QString stringValue(const QString &key) const;

	void setBool(const QString &key, bool value);
	void setInt(const QString &key, int value);
	void setString(const QString &key, const QString &value);

	[[nodiscard]] rpl::producer<bool> boolChanges(const QString &key) const;
	[[nodiscard]] rpl::producer<int> intChanges(const QString &key) const;
	[[nodiscard]] rpl::producer<QString> stringChanges(
		const QString &key) const;
	[[nodiscard]] rpl::producer<QString> changes() const;

	[[nodiscard]] QByteArray exportJson() const;
	bool importJson(const QByteArray &data);
	void reset();
	void saveNow();

private:
	Config();

	[[nodiscard]] QJsonValue value(const QString &key) const;
	void set(const QString &key, QJsonValue value);
	void load();
	[[nodiscard]] QString path() const;

	QString _path;
	QJsonObject _values;
	rpl::event_stream<QString> _changes;
	base::Timer _saveTimer;
	bool _loaded = false;

};

[[nodiscard]] bool Bool(const QString &key);
[[nodiscard]] bool IsBoolKey(const QString &key);
[[nodiscard]] int Int(const QString &key);
[[nodiscard]] QString String(const QString &key);
[[nodiscard]] int RecentStickersLimit(int serverLimit);

} // namespace Extera
