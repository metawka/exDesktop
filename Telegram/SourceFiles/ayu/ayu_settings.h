// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "extera/extera_config.h"

class MessageShotSettings final {
public:
	[[nodiscard]] bool showBackground() const { return Extera::Bool(u"exd_shot_background"_q); }
	void setShowBackground(bool v) { Set(u"exd_shot_background"_q, v); }
	[[nodiscard]] bool showDate() const { return Extera::Bool(u"exd_shot_date"_q); }
	void setShowDate(bool v) { Set(u"exd_shot_date"_q, v); }
	[[nodiscard]] bool showReactions() const { return Extera::Bool(u"exd_shot_reactions"_q); }
	void setShowReactions(bool v) { Set(u"exd_shot_reactions"_q, v); }
	[[nodiscard]] bool showHeaderDecorations() const { return Extera::Bool(u"exd_shot_header"_q); }
	void setShowHeaderDecorations(bool v) { Set(u"exd_shot_header"_q, v); }
	[[nodiscard]] bool showColorfulReplies() const { return Extera::Bool(u"exd_shot_colorful_replies"_q); }
	void setShowColorfulReplies(bool v) { Set(u"exd_shot_colorful_replies"_q, v); }
	[[nodiscard]] bool revealSpoilers() const { return Extera::Bool(u"exd_shot_spoilers"_q); }
	void setRevealSpoilers(bool v) { Set(u"exd_shot_spoilers"_q, v); }

	[[nodiscard]] int embeddedThemeType() const { return Extera::Int(u"exd_shot_embedded_type"_q); }
	[[nodiscard]] uint32 embeddedThemeAccentColor() const { return uint32(Extera::String(u"exd_shot_embedded_accent"_q).toULongLong()); }
	[[nodiscard]] uint64 cloudThemeId() const { return Extera::String(u"exd_shot_cloud_id"_q).toULongLong(); }
	[[nodiscard]] uint64 cloudThemeAccessHash() const { return Extera::String(u"exd_shot_cloud_hash"_q).toULongLong(); }
	[[nodiscard]] uint64 cloudThemeDocumentId() const { return Extera::String(u"exd_shot_cloud_document"_q).toULongLong(); }
	[[nodiscard]] QString cloudThemeTitle() const { return Extera::String(u"exd_shot_cloud_title"_q); }
	[[nodiscard]] uint64 cloudThemeAccountId() const { return Extera::String(u"exd_shot_cloud_account"_q).toULongLong(); }

	void setEmbeddedTheme(int type, uint32 accent) {
		clearTheme();
		Extera::Config::Instance().setInt(u"exd_shot_embedded_type"_q, type);
		Extera::Config::Instance().setString(u"exd_shot_embedded_accent"_q, QString::number(accent));
	}
	void setCloudTheme(uint64 accountId, uint64 id, uint64 accessHash, uint64 documentId, const QString &title) {
		clearTheme();
		auto &config = Extera::Config::Instance();
		config.setString(u"exd_shot_cloud_account"_q, QString::number(accountId));
		config.setString(u"exd_shot_cloud_id"_q, QString::number(id));
		config.setString(u"exd_shot_cloud_hash"_q, QString::number(accessHash));
		config.setString(u"exd_shot_cloud_document"_q, QString::number(documentId));
		config.setString(u"exd_shot_cloud_title"_q, title);
	}
	void clearTheme() {
		auto &config = Extera::Config::Instance();
		config.setInt(u"exd_shot_embedded_type"_q, -1);
		config.setString(u"exd_shot_embedded_accent"_q, QString());
		for (const auto &key : { u"exd_shot_cloud_account"_q, u"exd_shot_cloud_id"_q, u"exd_shot_cloud_hash"_q, u"exd_shot_cloud_document"_q, u"exd_shot_cloud_title"_q }) {
			config.setString(key, QString());
		}
	}

private:
	static void Set(const QString &key, bool value) {
		Extera::Config::Instance().setBool(key, value);
	}

};

class AyuSettings final {
public:
	[[nodiscard]] static AyuSettings &getInstance() {
		static auto result = AyuSettings();
		return result;
	}
	[[nodiscard]] MessageShotSettings &messageShotSettings() {
		return _shot;
	}
	[[nodiscard]] bool simpleQuotesAndReplies() const {
		return Extera::Bool(u"disable_colorful_replies"_q);
	}
	void setSimpleQuotesAndReplies(bool value) {
		Extera::Config::Instance().setBool(u"disable_colorful_replies"_q, value);
	}

private:
	MessageShotSettings _shot;

};
