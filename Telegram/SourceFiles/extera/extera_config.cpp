#include "extera/extera_config.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QFile>
#include <QtCore/QSaveFile>

namespace Extera {
namespace {

constexpr auto kSaveDelay = crl::time(1000);

[[nodiscard]] const QJsonObject &Defaults() {
	static const auto result = QJsonObject{
		{ u"translate_button"_q, true },
		{ u"translate_chats"_q, false },
		{ u"translate_provider"_q, 0 },
		{ u"translate_target"_q, QString() },
		{ u"translate_skip"_q, QString() },
		{ u"disable_number_rounding"_q, false },
		{ u"time_with_seconds"_q, true },
		{ u"filter_zalgo"_q, true },
		{ u"download_boost"_q, 0 },
		{ u"upload_boost"_q, false },
		{ u"relative_last_seen"_q, false },
		{ u"hide_phone"_q, true },
		{ u"show_id_dc"_q, 1 },
		{ u"hide_archive"_q, false },

		{ u"avatar_corners"_q, 28 },
		{ u"single_corner_radius"_q, true },
		{ u"hide_status"_q, false },
		{ u"hide_stories"_q, false },
		{ u"sender_mini_avatars"_q, false },
		{ u"folder_title_style"_q, 0 },
		{ u"folder_counter"_q, 0 },
		{ u"hide_all_chats"_q, false },
		{ u"streamer_mode"_q, false },
		{ u"streamer_full"_q, false },
		{ u"streamer_capture"_q, 0 },
		{ u"menu_order"_q, QString() },
		{ u"menu_hidden"_q, QString() },
		{ u"streamer_name"_q, true },
		{ u"streamer_avatar"_q, true },
		{ u"streamer_profile"_q, true },
		{ u"streamer_private"_q, true },
		{ u"streamer_channels"_q, true },
		{ u"streamer_phone"_q, 0 },
		{ u"streamer_username"_q, 0 },
		{ u"streamer_id"_q, 0 },
		{ u"streamer_private_avatar"_q, true },
		{ u"streamer_private_name"_q, true },
		{ u"streamer_private_text"_q, true },
		{ u"streamer_bots_avatar"_q, true },
		{ u"streamer_bots_name"_q, true },
		{ u"streamer_bots_text"_q, true },
		{ u"streamer_groups_avatar"_q, true },
		{ u"streamer_groups_name"_q, true },
		{ u"streamer_groups_text"_q, true },
		{ u"streamer_channels_avatar"_q, false },
		{ u"streamer_channels_name"_q, false },
		{ u"streamer_channels_text"_q, true },
		{ u"streamer_shown"_q, QString() },
		{ u"streamer_hidden"_q, QString() },
		{ u"feed_in_menu"_q, true },
		{ u"feed_wide_posts"_q, true },
		{ u"wide_posts_group"_q, true },
		{ u"feed_unread_counter"_q, true },
		{ u"feed_include_archived"_q, true },
		{ u"feed_hidden"_q, QString() },
		{ u"system_emoji"_q, false },

		{ u"sticker_size"_q, 14 },
		{ u"sticker_time"_q, 0 },
		{ u"unlimited_recent_stickers"_q, true },
		{ u"hide_reactions_private"_q, false },
		{ u"hide_reactions_groups"_q, false },
		{ u"hide_reactions_channels"_q, false },
		{ u"double_click_incoming"_q, 0 },
		{ u"double_click_outgoing"_q, 0 },
		{ u"disable_notification_delay"_q, false },
		{ u"disable_telegram_ai"_q, false },
		{ u"disable_greeting_sticker"_q, false },
		{ u"comma_after_mention"_q, false },
		{ u"hide_copy_toast"_q, false },
		{ u"chars_counter"_q, true },
		{ u"select_service"_q, true },
		{ u"gif_bot"_q, QString() },
		{ u"favorite_emoji"_q, QString() },
		{ u"auto_update"_q, 1 },
		{ u"plugins_no_auto_safe"_q, false },
		{ u"quote_reply"_q, false },
		{ u"name_accent"_q, false },
		{ u"inline_math"_q, true },
		{ u"hide_send_as"_q, false },
		{ u"remove_message_tail"_q, false },
		{ u"edited_icon"_q, true },
		{ u"online_indicator"_q, true },
		{ u"forward_count"_q, true },
		{ u"hide_share_button"_q, false },
		{ u"menu_copy_photo"_q, true },
		{ u"menu_save"_q, true },
		{ u"menu_repeat"_q, false },
		{ u"menu_quick_row"_q, true },
		{ u"menu_quick_items"_q, u"reply,delete,copy,forward,pin"_q },
		{ u"menu_history"_q, true },
		{ u"menu_details"_q, true },
		{ u"always_send_hd"_q, true },
		{ u"video_seek_step"_q, 5 },
		{ u"prefer_original_quality"_q, true },
		{ u"pause_on_minimize_video"_q, false },
		{ u"pause_on_minimize_voice"_q, false },
		{ u"pause_on_minimize_round"_q, false },

		{ u"md3_switches"_q, true },
		{ u"md3_sliders"_q, true },
		{ u"material_design"_q, true },
		{ u"custom_chat_themes"_q, true },
		{ u"app_icon"_q, QString() },
		{ u"app_icon_square"_q, false },
		{ u"full_members"_q, true },
		{ u"ws_proxy"_q, true },
		{ u"ws_proxy_secret"_q, QString() },
		{ u"ws_proxy_port"_q, 0 },
		{ u"welcome_shown"_q, false },
		{ u"wizard_shown"_q, false },
		{ u"notify_sound"_q, QString() },
		{ u"local_premium_emoji"_q, true },
		{ u"poll_results_before_voting"_q, true },
		{ u"bottom_button"_q, 0 },
		{ u"wide_posts"_q, false },
		{ u"ai_chat_key"_q, QString() },
		{ u"ai_chat_model"_q, 0 },
		{ u"ai_chat_role"_q, 0 },
		{ u"ai_chat_history"_q, 3 },
		{ u"ai_chat_streaming"_q, true },
		{ u"ai_chat_temperature"_q, 10 },
		{ u"plugins_enabled"_q, true },
		{ u"plugins_pinned"_q, QString() },
		{ u"plugins_developer_mode"_q, false },
		{ u"plugins_compact_view"_q, false },
		{ u"plugins_unknown_sources"_q, false },
		{ u"plugins_safe_mode"_q, false },
	};
	return result;
}

} // namespace

Config &Config::Instance() {
	static const auto result = new Config();
	return *result;
}

Config::Config()
: _saveTimer([=] { saveNow(); }) {
}

void Config::setWorkingDir(const QString &path) {
	_path = path + u"tdata/extera.json"_q;
	_loaded = false;
	_values = QJsonObject();
	load();
}

QString Config::path() const {
	return _path;
}

void Config::load() {
	if (_loaded) {
		return;
	}
	if (_path.isEmpty()) {
		return;
	}
	_loaded = true;
	auto file = QFile(path());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	if (document.isObject()) {
		_values = document.object();
	}
}

QJsonValue Config::value(const QString &key) const {
	const_cast<Config*>(this)->load();
	const auto i = _values.constFind(key);
	if (i != _values.constEnd()) {
		return *i;
	}
	return Defaults().value(key);
}

void Config::set(const QString &key, QJsonValue value) {
	load();
	if (value == Defaults().value(key)) {
		if (!_values.contains(key)) {
			return;
		}
		_values.remove(key);
	} else if (_values.value(key) == value) {
		return;
	} else {
		_values.insert(key, value);
	}
	_saveTimer.callOnce(kSaveDelay);
	_changes.fire_copy(key);
}

bool Config::boolValue(const QString &key) const {
	return value(key).toBool();
}

int Config::intValue(const QString &key) const {
	return value(key).toInt();
}

QString Config::stringValue(const QString &key) const {
	return value(key).toString();
}

void Config::setBool(const QString &key, bool value) {
	set(key, value);
}

void Config::setInt(const QString &key, int value) {
	set(key, value);
}

void Config::setString(const QString &key, const QString &value) {
	set(key, value);
}

rpl::producer<bool> Config::boolChanges(const QString &key) const {
	return rpl::single(
		boolValue(key)
	) | rpl::then(_changes.events(
	) | rpl::filter(
		rpl::mappers::_1 == key
	) | rpl::map([=] {
		return boolValue(key);
	})) | rpl::distinct_until_changed();
}

rpl::producer<QString> Config::stringChanges(const QString &key) const {
	return rpl::single(
		stringValue(key)
	) | rpl::then(_changes.events(
	) | rpl::filter(
		rpl::mappers::_1 == key
	) | rpl::map([=] {
		return stringValue(key);
	})) | rpl::distinct_until_changed();
}

rpl::producer<int> Config::intChanges(const QString &key) const {
	return rpl::single(
		intValue(key)
	) | rpl::then(_changes.events(
	) | rpl::filter(
		rpl::mappers::_1 == key
	) | rpl::map([=] {
		return intValue(key);
	})) | rpl::distinct_until_changed();
}

rpl::producer<QString> Config::changes() const {
	return _changes.events();
}

QByteArray Config::exportJson() const {
	const_cast<Config*>(this)->load();
	return QJsonDocument(_values).toJson(QJsonDocument::Indented);
}

bool Config::importJson(const QByteArray &data) {
	const auto document = QJsonDocument::fromJson(data);
	if (!document.isObject()) {
		return false;
	}
	load();
	const auto imported = document.object();
	for (auto i = imported.begin(); i != imported.end(); ++i) {
		if (Defaults().contains(i.key())) {
			set(i.key(), i.value());
		}
	}
	return true;
}

void Config::reset() {
	load();
	const auto keys = _values.keys();
	for (const auto &key : keys) {
		set(key, Defaults().value(key));
	}
}

void Config::saveNow() {
	_saveTimer.cancel();
	if (_path.isEmpty()) {
		return;
	}
	auto file = QSaveFile(path());
	if (!file.open(QIODevice::WriteOnly)) {
		return;
	}
	file.write(QJsonDocument(_values).toJson(QJsonDocument::Indented));
	file.commit();
}

bool IsBoolKey(const QString &key) {
	return Defaults().value(key).isBool();
}

bool Bool(const QString &key) {
	return Config::Instance().boolValue(key);
}

int Int(const QString &key) {
	return Config::Instance().intValue(key);
}

QString String(const QString &key) {
	return Config::Instance().stringValue(key);
}

int RecentStickersLimit(int serverLimit) {
	return Bool(u"unlimited_recent_stickers"_q)
		? std::max(serverLimit, 200)
		: serverLimit;
}

} // namespace Extera
