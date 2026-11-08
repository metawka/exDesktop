/*
exDesktop: fonts bundled for the Material 3 Expressive parts of the interface.
Licensed under GPLv3, like the rest of the project.
*/
#include "ui/expressive_fonts.h"

#include <QtGui/QFontDatabase>

#include <algorithm>
#include <cmath>

namespace Ui::ExpressiveFonts {
namespace {

// Registers the bundled files once; returns the family Qt gave each of them.
struct Families {
	QString unbounded;
	QString symbols;
};

const Families &Loaded() {
	static const auto result = [] {
		const auto load = [](const QString &path, const QString &fallback) {
			const auto id = QFontDatabase::addApplicationFont(path);
			const auto families = (id >= 0)
				? QFontDatabase::applicationFontFamilies(id)
				: QStringList();
			return families.isEmpty() ? fallback : families.front();
		};
		return Families{
			.unbounded = load(
				u":/exd/fonts/Unbounded.ttf"_q,
				u"Unbounded"_q),
			.symbols = load(
				u":/exd/fonts/ExdSymbols.ttf"_q,
				u"exDesktop Symbols"_q),
		};
	}();
	return result;
}

} // namespace

QFont Unbounded(int pixelSize, QFont::Weight weight) {
	auto result = QFont(Loaded().unbounded);
	result.setPixelSize(std::max(pixelSize, 1));
	result.setWeight(weight);
	return result;
}

QFont Symbols(double pixelSize, bool filled) {
	auto result = QFont(Loaded().symbols);
	result.setPixelSize(std::max(1, int(std::round(pixelSize))));
	result.setVariableAxis(QFont::Tag("FILL"), filled ? 1.f : 0.f);
	result.setVariableAxis(QFont::Tag("wght"), 500.f);
	result.setVariableAxis(
		QFont::Tag("opsz"),
		float(std::clamp(pixelSize, 20., 48.)));
	return result;
}

} // namespace Ui::ExpressiveFonts
