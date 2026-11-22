#include "extera/exd_radar.h"

#include "data/data_user.h"
#include "extera/extera_config.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

#include <QtCore/QRegularExpression>

namespace Exd::Radar {
namespace {

[[nodiscard]] QChar Skeleton(QChar ch) {
	switch (ch.unicode()) {
	case 0x0430: return QChar('a');
	case 0x0435: case 0x0451: return QChar('e');
	case 0x043E: return QChar('o');
	case 0x0440: return QChar('p');
	case 0x0441: return QChar('c');
	case 0x0443: return QChar('y');
	case 0x0445: return QChar('x');
	case 0x043A: return QChar('k');
	case 0x043C: return QChar('m');
	case 0x0442: return QChar('t');
	case 0x043D: return QChar('h');
	case 0x0432: return QChar('b');
	case 0x0456: return QChar('i');
	case 0x0458: return QChar('j');
	case 0x0455: return QChar('s');
	case '0': return QChar('o');
	case '1': return QChar('l');
	case '3': return QChar('e');
	case '@': return QChar('a');
	}
	return ch;
}

[[nodiscard]] QString Normalize(const QString &text) {
	const auto decomposed = text.normalized(QString::NormalizationForm_KD);
	auto result = QString();
	result.reserve(decomposed.size());
	for (const auto ch : decomposed) {
		const auto category = ch.category();
		if (category == QChar::Mark_NonSpacing
			|| category == QChar::Mark_SpacingCombining
			|| category == QChar::Mark_Enclosing) {
			continue;
		}
		if (!ch.isLetterOrNumber() && ch != '@') {
			continue;
		}
		result.append(Skeleton(ch.toLower()));
	}
	return result;
}

[[nodiscard]] QStringList Words() {
	auto result = QStringList();
	const auto words = Extera::String(u"exd_radar_words"_q);
	for (auto word : words.split(QRegularExpression(u"[,;\\n]"_q))) {
		word = Normalize(word);
		if (word.size() >= 3) {
			result.push_back(word);
		}
	}
	return result;
}

[[nodiscard]] QStringList Names(not_null<Main::Session*> session) {
	auto result = QStringList();
	if (!Extera::Bool(u"exd_radar_name"_q)) {
		return result;
	}
	const auto self = session->user();
	auto parts = QStringList{
		self->firstName,
		self->lastName,
		self->username(),
		self->name(),
	};
	for (const auto &username : self->usernames()) {
		parts.push_back(username);
	}
	for (const auto &name : parts) {
		const auto normalized = Normalize(name);
		if (normalized.size() >= 3) {
			result.push_back(normalized);
		}
	}
	result.removeDuplicates();
	return result;
}

[[nodiscard]] QStringList Tokens(const QString &text) {
	auto result = QStringList();
	for (const auto &part : text.split(
			QRegularExpression(u"[\\s,.!?;:()\\[\\]{}\"'«»]+"_q),
			Qt::SkipEmptyParts)) {
		const auto normalized = Normalize(part);
		if (!normalized.isEmpty()) {
			result.push_back(normalized);
		}
	}
	return result;
}

} // namespace

bool Matches(not_null<HistoryItem*> item) {
	if (!Extera::Bool(u"exd_keyword_radar"_q)
		|| item->out()
		|| item->history()->peer->isSelf()) {
		return false;
	}
	const auto text = item->originalText().text;
	if (text.isEmpty()) {
		return false;
	}
	const auto normalized = Normalize(text);
	for (const auto &word : Words()) {
		if (normalized.contains(word)) {
			return true;
		}
	}
	const auto names = Names(&item->history()->session());
	if (names.isEmpty()) {
		return false;
	}
	for (const auto &name : names) {
		if (normalized.contains(name)) {
			return true;
		}
	}
	for (const auto &token : Tokens(text)) {
		if (token.size() < 4) {
			continue;
		}
		for (const auto &name : names) {
			if (name.size() >= 4
				&& (name.startsWith(token) || token.startsWith(name))
				&& token.size() * 5 >= name.size() * 3) {
				return true;
			}
		}
	}
	return false;
}

} // namespace Exd::Radar
