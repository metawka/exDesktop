/*
exDesktop: Material 3 Expressive elements for the intro screen.
Licensed under GPLv3, like the rest of the project.
*/
#pragma once

#include <QtGui/QPainter>
#include <QtCore/QObject>
#include <QtCore/QRect>

class QWidget;

namespace Intro::details::Expressive {

// Banner: rows of Unbounded words drifting slowly, with the exDesktop logo in the middle.
// `logoLimit` is the lowest y the logo may reach (the badge sits below it).
// `fullHeight` is the banner's height when fully shown: rows are laid out for it
// (whole rows only, centred) and slide with the banner's bottom edge.
// `wavy` (0..1, may overshoot) grows wavy outlines around the "exDesktop" pills.
// The logo's rectangle is written to `logoRect` for hit testing.
void PaintBanner(
	QPainter &p,
	QRect rect,
	const QColor &base,
	double seconds,
	int logoLimit,
	int fullHeight,
	double wavy,
	QRectF *logoRect);

// A "sunny" badge (round shape with soft lobes) with the Telegram plane inside.
// `angle` is how far it has turned (kept by the caller, so speed changes never jump).
void PaintSunBadge(
	QPainter &p,
	QPointF center,
	double diameter,
	const QColor &fill,
	const QColor &plane,
	const QColor &fold,
	double angle,
	double seconds);

// Shows a wavy outline around `target` while the mouse is over it. `pill` makes the
// outline fully rounded; the outline grows with a spring-like overshoot.
void AddWavyHover(
	QWidget *target,
	QWidget *host,
	const QColor &color,
	bool pill = true);

// Lifts `target` by a few pixels with a spring while the mouse is over it.
void AddLiftHover(QWidget *target);

} // namespace Intro::details::Expressive
