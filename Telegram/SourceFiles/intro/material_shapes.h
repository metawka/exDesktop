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

// A rounded rectangle whose edge waves along its normal by `amplitude`; `phase`
// moves the wave around. Walked analytically (edges and corner arcs), so it is
// cheap to rebuild every frame; the wave count is whole, so it closes cleanly.
// A radius of half the height gives a wavy pill.
[[nodiscard]] QPainterPath WavyRoundedRect(
	const QRectF &rect,
	double radius,
	double amplitude,
	double phase,
	double wavelength = 22.);

} // namespace Ui::MaterialShapes
