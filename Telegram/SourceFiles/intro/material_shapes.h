/*
exDesktop: the 35 Material 3 Expressive shapes (a port of androidx MaterialShapes).
Licensed under GPLv3, like the rest of the project.
*/
#pragma once

#include <QtGui/QPainterPath>
#include <QtCore/QRectF>

namespace Ui::MaterialShapes {

enum class Shape : int {
	Circle, Square, Slanted, Arch, Fan, Arrow, SemiCircle, Oval, Pill, Triangle,
	Diamond, ClamShell, Pentagon, Gem, Sunny, VerySunny, Cookie4, Cookie6, Cookie7,
	Cookie9, Cookie12, Ghostish, Clover4, Clover8, Burst, SoftBurst, Boom, SoftBoom,
	Flower, Puffy, PuffyDiamond, PixelCircle, PixelTriangle, Bun, Heart,
	Count,
};

// The shape fitted into `rect` (aspect kept, centred).
[[nodiscard]] QPainterPath Path(Shape shape, const QRectF &rect);

// A random shape that reads well when small (no thin spikes), never equal to `avoid`.
[[nodiscard]] Shape RandomSoft(Shape avoid = Shape::Count);

} // namespace Ui::MaterialShapes
