#include "extera/extera_utils.h"

#include "extera/extera_config.h"

#include <QtCore/QLocale>
#include <QtCore/QTime>

#include <cmath>
#include <QtCore/QRegularExpression>

namespace Extera {
namespace {


[[nodiscard]] QString FormatWithSeconds(QString format) {
	if (format.contains(u"ss"_q)) {
		return format;
	}
	const auto minutes = format.indexOf(u"mm"_q);
	if (minutes < 0) {
		return format;
	}
	format.insert(minutes + 2, u":ss"_q);
	return format;
}

} // namespace

QString FormatTime(const QTime &time) {
	const auto locale = QLocale();
	if (!Bool(u"time_with_seconds"_q)) {
		return locale.toString(time, QLocale::ShortFormat);
	}
	return locale.toString(
		time,
		FormatWithSeconds(locale.timeFormat(QLocale::ShortFormat)));
}

const QRegularExpression &ZalgoPattern() {
	static const auto result = QRegularExpression(
		u"\\p{Mn}{3,}|[\\x{202A}-\\x{202E}\\x{2066}-\\x{2069}"
		"\\x{200E}\\x{200F}\\x{061C}]"_q,
		QRegularExpression::UseUnicodePropertiesOption);
	return result;
}

bool HasZalgo(const QString &text) {
	return Bool(u"filter_zalgo"_q) && ZalgoPattern().match(text).hasMatch();
}

QString FilterZalgo(QString text) {
	if (HasZalgo(text)) {
		text.remove(ZalgoPattern());
	}
	return text;
}

class MathParser final {
public:
	explicit MathParser(const QString &text) : _text(text) {
	}

	[[nodiscard]] std::optional<double> parse() {
		const auto result = expression();
		skipSpaces();
		if (!result || _position != _text.size()) {
			return std::nullopt;
		}
		return result;
	}

private:
	void skipSpaces() {
		while (_position < _text.size() && _text[_position].isSpace()) {
			++_position;
		}
	}

	[[nodiscard]] bool take(QChar ch) {
		skipSpaces();
		if (_position < _text.size() && _text[_position] == ch) {
			++_position;
			return true;
		}
		return false;
	}

	[[nodiscard]] std::optional<double> number() {
		skipSpaces();
		const auto start = _position;
		while (_position < _text.size()
			&& (_text[_position].isDigit()
				|| _text[_position] == '.'
				|| _text[_position] == ',')) {
			++_position;
		}
		if (start == _position) {
			return std::nullopt;
		}
		auto ok = false;
		const auto value = _text.mid(start, _position - start).replace(
			',',
			'.').toDouble(&ok);
		return ok ? std::make_optional(value) : std::nullopt;
	}

	[[nodiscard]] std::optional<double> factor() {
		if (take('-')) {
			const auto value = factor();
			return value ? std::make_optional(-*value) : std::nullopt;
		} else if (take('(')) {
			const auto value = expression();
			return (value && take(')')) ? value : std::nullopt;
		}
		return number();
	}

	[[nodiscard]] std::optional<double> power() {
		auto left = factor();
		if (left && take('^')) {
			const auto right = power();
			return right
				? std::make_optional(std::pow(*left, *right))
				: std::nullopt;
		}
		return left;
	}

	[[nodiscard]] std::optional<double> term() {
		auto left = power();
		while (left) {
			if (take('*') || take(QChar(0x00D7))) {
				const auto right = power();
				left = right ? std::make_optional(*left * *right) : std::nullopt;
			} else if (take('/') || take(QChar(0x00F7))) {
				const auto right = power();
				left = (right && *right != 0.)
					? std::make_optional(*left / *right)
					: std::nullopt;
			} else if (take('%')) {
				const auto right = power();
				left = (right && *right != 0.)
					? std::make_optional(std::fmod(*left, *right))
					: std::nullopt;
			} else {
				break;
			}
		}
		return left;
	}

	[[nodiscard]] std::optional<double> expression() {
		auto left = term();
		while (left) {
			if (take('+')) {
				const auto right = term();
				left = right ? std::make_optional(*left + *right) : std::nullopt;
			} else if (take('-')) {
				const auto right = term();
				left = right ? std::make_optional(*left - *right) : std::nullopt;
			} else {
				break;
			}
		}
		return left;
	}

	const QString _text;
	int _position = 0;

};

std::optional<QString> InlineMathValue(const QString &text) {
	if (!Bool(u"inline_math"_q) || !text.endsWith('=')) {
		return std::nullopt;
	}
	const auto body = text.left(text.size() - 1);
	auto start = body.size();
	while (start > 0) {
		const auto ch = body[start - 1];
		if (ch.isDigit() || ch.isSpace() || QString(u"+-*/^%().,"_q).contains(ch)
			|| ch == QChar(0x00D7) || ch == QChar(0x00F7)) {
			--start;
		} else {
			break;
		}
	}
	const auto expression = body.mid(start).trimmed();
	if (expression.isEmpty()
		|| !std::any_of(expression.begin(), expression.end(), [](QChar ch) {
			return QString(u"+-*/^%"_q).contains(ch)
				|| ch == QChar(0x00D7)
				|| ch == QChar(0x00F7);
		})) {
		return std::nullopt;
	}
	const auto value = MathParser(expression).parse();
	if (!value || !std::isfinite(*value)) {
		return std::nullopt;
	}
	const auto rounded = (std::abs(*value) < 1e9)
		? (std::round(*value * 1e9) / 1e9)
		: *value;
	return QString::number(rounded, 'g', 15);
}

std::optional<QString> InlineMathResult(const QString &text) {
	if (!text.endsWith(u"= "_q)) {
		return std::nullopt;
	}
	const auto withEquals = text.left(text.size() - 1);
	if (const auto value = InlineMathValue(withEquals)) {
		return withEquals + *value + ' ';
	}
	return std::nullopt;
}

} // namespace Extera
