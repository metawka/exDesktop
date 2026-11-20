#pragma once

#include "ui/text/text_entity.h"

#include <QtCore/QJsonValue>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>

class History;
class HistoryItem;
class PeerData;

namespace Extera::Plugins {

inline constexpr auto kSdkVersion = "1.4.6.2";

enum class SendStrategy {
	Default,
	Cancel,
	Modify,
};

struct SendResult {
	SendStrategy strategy = SendStrategy::Default;
	QString text;
	EntitiesInText entities;
	bool quote = false;
};

struct MenuItem {
	QString id;
	QString text;
};

void Start();
void Shutdown();
int ExportAll(const QString &folder);

[[nodiscard]] bool Available();
[[nodiscard]] bool IsPluginFile(const QString &path);
[[nodiscard]] bool Started();
[[nodiscard]] QJsonArray ProfileRows(
	int64 dialogId,
	const QString &phone,
	const QString &username,
	const QString &bio);
[[nodiscard]] QString RuntimeError();

[[nodiscard]] QJsonValue Call(
	const QString &method,
	const QJsonObject &args = QJsonObject());

QJsonValue SandboxQuery(
	const QString &method,
	const QString &first,
	const QString &second = QString(),
	const QString &third = QString(),
	const QStringList &paths = QStringList());

[[nodiscard]] bool Bypassing();
[[nodiscard]] SendResult OnSendMessage(
	not_null<History*> history,
	const QString &text,
	MsgId replyTo);

[[nodiscard]] std::vector<MenuItem> MenuItems(const QString &type);
void ClickMenuItem(const QString &id, QJsonObject context);

void NotifyNewMessage(not_null<HistoryItem*> item);
void NotifyAppEvent(const QString &name);
void NotifyScreen(const QString &screen, int64 dialogId);
[[nodiscard]] bool InterceptLocalUrl(const QString &url);
[[nodiscard]] bool InterceptUrl(const QString &url);
[[nodiscard]] bool InterceptFileOpen(const QString &path, HistoryItem *item);
[[nodiscard]] bool HasUiHook(const QString &kind);
[[nodiscard]] int UiHookGeneration();
void NotifyPlayerShown(bool shown);
void RegisterComposeField(
	QObject *guard,
	Fn<QString()> get,
	Fn<void(const QString&, bool)> set);
[[nodiscard]] std::vector<std::pair<int, QString>> PlayerMenuItems();
void PlayerMenuClick(int id);
[[nodiscard]] std::optional<QString> UiHookText(
	const QString &kind,
	HistoryItem *item,
	PeerData *peer,
	const QString &text);
[[nodiscard]] bool InterceptClientEvent(
	const QString &kind,
	HistoryItem *item,
	QJsonObject args = QJsonObject());

[[nodiscard]] int64 DialogId(not_null<PeerData*> peer);
[[nodiscard]] bool IsTrustedSource(not_null<PeerData*> peer);

[[nodiscard]] rpl::producer<> listChanges();
[[nodiscard]] rpl::producer<QString> settingsChanges();
void notifyListChanged();

} // namespace Extera::Plugins
