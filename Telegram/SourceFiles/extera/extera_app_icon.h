#pragma once

namespace Extera::AppIcon {

struct Variant {
	QString id;
	QColor from;
	QColor to;
	QColor fg;
};

[[nodiscard]] std::vector<Variant> Variants();
[[nodiscard]] QImage Render(int size, const QString &id, bool square);
[[nodiscard]] QImage Current(int size);
void ApplyToShortcuts();

} // namespace Extera::AppIcon
