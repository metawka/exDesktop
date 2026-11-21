#include "extera/exd_settings_sync.h"

#include "apiwrap.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "extera/extera_config.h"
#include "main/main_session.h"
#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Exd::SettingsSync {
namespace {

const auto kBot = u"exDesktop_bot"_q;
const auto kCountKey = u"exd_settings"_q;
const auto kChunkPrefix = u"exd_settings_"_q;
const auto kUpdatedKey = u"exd_settings_updated"_q;
const auto kEncodingKey = u"exd_settings_encoding"_q;
const auto kEncoding = u"qz_base64_v1"_q;
constexpr auto kChunkSize = 3000;
constexpr auto kAutoSyncDelay = 15 * crl::time(1000);

using Done = Fn<void(QString error)>;
using Values = Fn<void(QJsonObject values, QString error)>;

[[nodiscard]] bool IsLocalKey(const QString &key) {
	return key.startsWith(u"exd_sync_"_q)
		|| (key == u"install_conf"_q)
		|| (key == u"md3_intro_shown"_q)
		|| key.startsWith(u"migrated_"_q);
}

[[nodiscard]] bool IsSecretKey(const QString &key) {
	const auto lower = key.toLower();
	for (const auto &part : {
			u"token"_q,
			u"secret"_q,
			u"password"_q,
			u"api_key"_q,
			u"apikey"_q,
			u"_key"_q }) {
		if (lower.contains(part)) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] QString PresetsPath() {
	return cWorkingDir() + u"tdata/exd_presets.json"_q;
}

[[nodiscard]] QJsonObject ReadPresets() {
	auto file = QFile(PresetsPath());
	if (!file.open(QIODevice::ReadOnly)) {
		return QJsonObject();
	}
	return QJsonDocument::fromJson(file.readAll()).object();
}

void WritePresets(const QJsonObject &presets) {
	QDir().mkpath(QFileInfo(PresetsPath()).absolutePath());
	auto file = QFile(PresetsPath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(presets).toJson(QJsonDocument::Compact));
	}
}

[[nodiscard]] QJsonObject CurrentSettings(bool includeSecrets) {
	auto result = QJsonDocument::fromJson(
		Extera::Config::Instance().exportJson()).object();
	for (const auto &key : result.keys()) {
		if (IsLocalKey(key) || (!includeSecrets && IsSecretKey(key))) {
			result.remove(key);
		}
	}
	return result;
}

void ApplySettings(const QJsonObject &settings) {
	auto filtered = QJsonObject();
	for (auto i = settings.begin(); i != settings.end(); ++i) {
		if (!IsLocalKey(i.key())) {
			filtered.insert(i.key(), i.value());
		}
	}
	Extera::Config::Instance().importJson(
		QJsonDocument(filtered).toJson(QJsonDocument::Compact));
}

void WithBot(
		not_null<Main::Session*> session,
		Fn<void(UserData*)> done) {
	if (const auto peer = session->data().peerByUsername(kBot)) {
		done(peer->asUser());
		return;
	}
	session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(kBot),
		MTP_string()
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		session->data().processUsers(result.data().vusers());
		session->data().processChats(result.data().vchats());
		const auto peer = session->data().peerLoaded(
			peerFromMTP(result.data().vpeer()));
		done(peer ? peer->asUser() : nullptr);
	}).fail([=] {
		done(nullptr);
	}).send();
}

void Invoke(
		not_null<Main::Session*> session,
		const QString &method,
		const QJsonDocument &params,
		Fn<void(QByteArray data, QString error)> done) {
	WithBot(session, [=](UserData *bot) {
		if (!bot) {
			done(QByteArray(), u"BOT_NOT_FOUND"_q);
			return;
		}
		session->api().request(MTPbots_InvokeWebViewCustomMethod(
			bot->inputUser(),
			MTP_string(method),
			MTP_dataJSON(MTP_bytes(params.toJson(QJsonDocument::Compact)))
		)).done([=](const MTPDataJSON &result) {
			done(result.data().vdata().v, QString());
		}).fail([=](const MTP::Error &error) {
			done(QByteArray(), error.type());
		}).send();
	});
}

void SetValue(
		not_null<Main::Session*> session,
		const QString &key,
		const QString &value,
		Done done) {
	Invoke(session, u"saveStorageValue"_q, QJsonDocument(QJsonObject{
		{ u"key"_q, key },
		{ u"value"_q, value },
	}), [=](QByteArray, QString error) {
		done(error);
	});
}

void GetValues(
		not_null<Main::Session*> session,
		const QStringList &keys,
		Values done) {
	auto list = QJsonArray();
	for (const auto &key : keys) {
		list.push_back(key);
	}
	Invoke(session, u"getStorageValues"_q, QJsonDocument(QJsonObject{
		{ u"keys"_q, list },
	}), [=](QByteArray data, QString error) {
		if (!error.isEmpty()) {
			done(QJsonObject(), error);
			return;
		}
		done(QJsonDocument::fromJson(data).object(), QString());
	});
}

void UploadChunks(
		not_null<Main::Session*> session,
		QString payload,
		int index,
		int count,
		TimeId date,
		Done done) {
	if (index == count) {
		SetValue(session, kEncodingKey, kEncoding, [=](QString error) {
			if (!error.isEmpty()) {
				done(error);
				return;
			}
			SetValue(session, kCountKey, QString::number(count), [=](
					QString error) {
				if (!error.isEmpty()) {
					done(error);
					return;
				}
				SetValue(
					session,
					kUpdatedKey,
					QString::number(date),
					[=](QString error) {
						if (error.isEmpty()) {
							Extera::Config::Instance().setInt(
								u"exd_sync_date"_q,
								int(date));
						}
						done(error);
					});
			});
		});
		return;
	}
	const auto chunk = payload.mid(index * kChunkSize, kChunkSize);
	SetValue(
		session,
		kChunkPrefix + QString::number(index),
		chunk,
		[=](QString error) {
			if (!error.isEmpty()) {
				done(error);
				return;
			}
			UploadChunks(session, payload, index + 1, count, date, done);
		});
}

void FetchChunks(
		not_null<Main::Session*> session,
		int count,
		Fn<void(QString payload, QString error)> done) {
	auto keys = QStringList();
	for (auto i = 0; i != count; ++i) {
		keys.push_back(kChunkPrefix + QString::number(i));
	}
	GetValues(session, keys, [=](QJsonObject values, QString error) {
		if (!error.isEmpty()) {
			done(QString(), error);
			return;
		}
		auto result = QString();
		for (const auto &key : keys) {
			if (!values.contains(key)) {
				done(QString(), u"CHUNK_MISSING"_q);
				return;
			}
			result += values.value(key).toString();
		}
		done(result, QString());
	});
}

[[nodiscard]] base::flat_set<not_null<Main::Session*>> &Started() {
	static auto result = base::flat_set<not_null<Main::Session*>>();
	return result;
}

} // namespace

TimeId LocalDate() {
	return TimeId(Extera::Int(u"exd_sync_date"_q));
}

void FetchCloudDate(
		not_null<Main::Session*> session,
		Fn<void(TimeId date, QString error)> done) {
	GetValues(session, { kUpdatedKey }, [=](
			QJsonObject values,
			QString error) {
		done(
			error.isEmpty()
				? TimeId(values.value(kUpdatedKey).toString().toLongLong())
				: TimeId(),
			error);
	});
}

void Upload(not_null<Main::Session*> session, Done done) {
	const auto payload = QJsonObject{
		{ u"v"_q, 1 },
		{ u"settings"_q, CurrentSettings(Extera::Bool(u"exd_sync_keys"_q)) },
		{ u"presets"_q, ReadPresets() },
	};
	const auto encoded = QString::fromLatin1(qCompress(
		QJsonDocument(payload).toJson(QJsonDocument::Compact),
		9).toBase64());
	const auto count = (encoded.size() + kChunkSize - 1) / kChunkSize;
	UploadChunks(session, encoded, 0, count, base::unixtime::now(), done);
}

void Restore(not_null<Main::Session*> session, Done done) {
	GetValues(session, { kCountKey, kEncodingKey }, [=](
			QJsonObject values,
			QString error) {
		if (!error.isEmpty()) {
			done(error);
			return;
		}
		const auto count = values.value(kCountKey).toString().toInt();
		if (count <= 0) {
			done(u"NO_BACKUP"_q);
			return;
		} else if (values.value(kEncodingKey).toString() != kEncoding) {
			done(u"BAD_ENCODING"_q);
			return;
		}
		FetchChunks(session, count, [=](QString payload, QString error) {
			if (!error.isEmpty()) {
				done(error);
				return;
			}
			const auto json = qUncompress(
				QByteArray::fromBase64(payload.toLatin1()));
			const auto object = QJsonDocument::fromJson(json).object();
			if (object.isEmpty()) {
				done(u"BAD_PAYLOAD"_q);
				return;
			}
			ApplySettings(object.value(u"settings"_q).toObject());
			auto presets = ReadPresets();
			const auto remote = object.value(u"presets"_q).toObject();
			for (auto i = remote.begin(); i != remote.end(); ++i) {
				presets.insert(i.key(), i.value());
			}
			WritePresets(presets);
			done(QString());
		});
	});
}

void Clear(not_null<Main::Session*> session, Done done) {
	Invoke(session, u"getStorageKeys"_q, QJsonDocument(QJsonObject()), [=](
			QByteArray data,
			QString error) {
		if (!error.isEmpty()) {
			done(error);
			return;
		}
		auto keys = QJsonArray();
		for (const auto &value : QJsonDocument::fromJson(data).array()) {
			const auto key = value.toString();
			if (key.startsWith(kCountKey)) {
				keys.push_back(key);
			}
		}
		if (keys.isEmpty()) {
			done(u"NO_BACKUP"_q);
			return;
		}
		Invoke(session, u"deleteStorageValues"_q, QJsonDocument(QJsonObject{
			{ u"keys"_q, keys },
		}), [=](QByteArray, QString error) {
			done(error);
		});
	});
}

void Start(not_null<Main::Session*> session) {
	if (Started().contains(session)) {
		return;
	}
	Started().emplace(session);
	const auto timer = session->lifetime().make_state<base::Timer>([=] {
		if (Extera::Bool(u"exd_sync_auto"_q)) {
			Upload(session, [](QString) {});
		}
	});
	Extera::Config::Instance().changes(
	) | rpl::filter([](const QString &key) {
		return !IsLocalKey(key);
	}) | rpl::on_next([=](const QString &) {
		if (Extera::Bool(u"exd_sync_auto"_q)) {
			timer->callOnce(kAutoSyncDelay);
		}
	}, session->lifetime());
	session->lifetime().add([=] {
		Started().remove(session);
	});
}

QStringList Presets() {
	auto result = ReadPresets().keys();
	result.sort(Qt::CaseInsensitive);
	return result;
}

void SavePreset(const QString &name) {
	auto presets = ReadPresets();
	presets.insert(name, CurrentSettings(false));
	WritePresets(presets);
	Extera::Config::Instance().setInt(
		u"exd_presets_changed"_q,
		int(base::unixtime::now()));
}

bool ApplyPreset(const QString &name) {
	const auto presets = ReadPresets();
	if (!presets.contains(name)) {
		return false;
	}
	ApplySettings(presets.value(name).toObject());
	return true;
}

void DeletePreset(const QString &name) {
	auto presets = ReadPresets();
	presets.remove(name);
	WritePresets(presets);
	Extera::Config::Instance().setInt(
		u"exd_presets_changed"_q,
		int(base::unixtime::now()));
}

} // namespace Exd::SettingsSync
