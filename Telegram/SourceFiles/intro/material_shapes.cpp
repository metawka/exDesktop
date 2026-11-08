/*
exDesktop: the 35 Material 3 Expressive shapes (a port of androidx MaterialShapes).
Licensed under GPLv3, like the rest of the project.
*/
#include "intro/material_shapes.h"

#include <QtCore/QRandomGenerator>
#include <QtCore/QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Ui::MaterialShapes {
namespace {

struct Vertex {
	double x = 0.;
	double y = 0.;
	double radius = 0.;
	double smoothing = 0.;
};

struct Round {
	double radius = 0.;
	double smoothing = 0.;
};

constexpr auto R15 = Round{ 0.15 };
constexpr auto R20 = Round{ 0.20 };
constexpr auto R30 = Round{ 0.30 };
constexpr auto R50 = Round{ 0.50 };
constexpr auto R100 = Round{ 1.00 };

struct PNR {
	double x = 0.;
	double y = 0.;
	Round r;
};

using Vertices = std::vector<Vertex>;

Vertices Regular(int n, std::vector<Round> rounds) {
	auto result = Vertices();
	for (auto i = 0; i != n; ++i) {
		const auto a = 2. * M_PI * i / n;
		const auto &r = rounds[i % rounds.size()];
		result.push_back({ std::cos(a), std::sin(a), r.radius, r.smoothing });
	}
	return result;
}

Vertices Star(int n, double inner, Round r) {
	auto result = Vertices();
	for (auto i = 0; i != 2 * n; ++i) {
		const auto a = M_PI * i / n;
		const auto rr = (i % 2 == 0) ? 1. : inner;
		result.push_back({ rr * std::cos(a), rr * std::sin(a), r.radius, r.smoothing });
	}
	return result;
}

Vertices Rect(double w, double h, std::vector<Round> rounds) {
	const auto hw = w / 2., hh = h / 2.;
	const auto corners = std::array<QPointF, 4>{
		QPointF(hw, hh), QPointF(-hw, hh), QPointF(-hw, -hh), QPointF(hw, -hh) };
	auto result = Vertices();
	for (auto i = 0; i != 4; ++i) {
		const auto &r = rounds[i % rounds.size()];
		result.push_back({ corners[i].x(), corners[i].y(), r.radius, r.smoothing });
	}
	return result;
}

Vertices Transform(Vertices v, double degrees, double sx = 1., double sy = 1.) {
	const auto a = qDegreesToRadians(degrees);
	const auto c = std::cos(a), s = std::sin(a);
	for (auto &p : v) {
		const auto x = p.x * sx, y = p.y * sy;
		p.x = x * c - y * s;
		p.y = x * s + y * c;
	}
	return v;
}

// androidx doRepeat(): rotate (or rotate + mirror) a list of points around the centre
Vertices Custom(const std::vector<PNR> &pnr, int reps, bool mirroring = false) {
	const auto cx = 0.5, cy = 0.5;
	auto result = Vertices();
	if (mirroring) {
		auto angles = std::vector<double>();
		auto dists = std::vector<double>();
		for (const auto &p : pnr) {
			angles.push_back(qRadiansToDegrees(std::atan2(p.y - cy, p.x - cx)));
			dists.push_back(std::hypot(p.x - cx, p.y - cy));
		}
		const auto actual = reps * 2;
		const auto section = 360. / actual;
		const auto last = int(pnr.size()) - 1;
		for (auto it = 0; it != actual; ++it) {
			for (auto index = 0; index != int(pnr.size()); ++index) {
				const auto i = (it % 2 == 0) ? index : (last - index);
				if (i > 0 || it % 2 == 0) {
					const auto a = qDegreesToRadians(section * it + ((it % 2 == 0)
						? angles[i]
						: (section - angles[i] + 2 * angles[0])));
					result.push_back({
						std::cos(a) * dists[i] + cx,
						std::sin(a) * dists[i] + cy,
						pnr[i].r.radius,
						pnr[i].r.smoothing });
				}
			}
		}
	} else {
		const auto np = int(pnr.size());
		for (auto it = 0; it != np * reps; ++it) {
			const auto &p = pnr[it % np];
			const auto a = qDegreesToRadians((it / np) * 360. / reps);
			const auto ox = p.x - cx, oy = p.y - cy;
			result.push_back({
				ox * std::cos(a) - oy * std::sin(a) + cx,
				ox * std::sin(a) + oy * std::cos(a) + cy,
				p.r.radius,
				p.r.smoothing });
		}
	}
	return result;
}

Vertices Make(Shape shape) {
	switch (shape) {
	case Shape::Circle: return Regular(10, { R100 });
	case Shape::Square: return Rect(1, 1, { R30 });
	case Shape::Slanted: return Custom({ { 0.926, 0.970, { 0.189, 0.811 } }, { -0.021, 0.967, { 0.187, 0.057 } } }, 2);
	case Shape::Arch: return Transform(Regular(4, { R100, R100, R20, R20 }), -135);
	case Shape::Fan: return Custom({ { 1.004, 1.000, { 0.148, 0.417 } }, { 0., 1., { 0.151 } }, { 0., -0.003, { 0.148 } }, { 0.978, 0.020, { 0.803 } } }, 1);
	case Shape::Arrow: return Custom({ { 0.5, 0.892, { 0.313 } }, { -0.216, 1.050, { 0.207 } }, { 0.499, -0.160, { 0.215, 1. } }, { 1.225, 1.060, { 0.211 } } }, 1);
	case Shape::SemiCircle: return Rect(1.6, 1, { R20, R20, R100, R100 });
	case Shape::Oval: return Transform(Regular(10, { R100 }), -45, 1., 0.64);
	case Shape::Pill: return Custom({ { 0.961, 0.039, { 0.426 } }, { 1.001, 0.428 }, { 1.000, 0.609, { 1. } } }, 2, true);
	case Shape::Triangle: return Transform(Regular(3, { R20 }), -90);
	case Shape::Diamond: return Custom({ { 0.5, 1.096, { 0.151, 0.524 } }, { 0.040, 0.5, { 0.159 } } }, 2);
	case Shape::ClamShell: return Custom({ { 0.171, 0.841, { 0.159 } }, { -0.020, 0.5, { 0.140 } }, { 0.170, 0.159, { 0.159 } } }, 2);
	case Shape::Pentagon: return Custom({ { 0.5, -0.009, { 0.172 } }, { 1.030, 0.365, { 0.164 } }, { 0.828, 0.970, { 0.169 } } }, 1, true);
	case Shape::Gem: return Custom({ { 0.499, 1.023, { 0.241, 0.778 } }, { -0.005, 0.792, { 0.208 } }, { 0.073, 0.258, { 0.228 } }, { 0.433, -0.0, { 0.491 } } }, 1, true);
	case Shape::Sunny: return Star(8, 0.8, R15);
	case Shape::VerySunny: return Custom({ { 0.5, 1.080, { 0.085 } }, { 0.358, 0.843, { 0.085 } } }, 8);
	case Shape::Cookie4: return Custom({ { 1.237, 1.236, { 0.258 } }, { 0.5, 0.918, { 0.233 } } }, 4);
	case Shape::Cookie6: return Custom({ { 0.723, 0.884, { 0.394 } }, { 0.5, 1.099, { 0.398 } } }, 6);
	case Shape::Cookie7: return Transform(Star(7, 0.75, R50), -90);
	case Shape::Cookie9: return Transform(Star(9, 0.8, R50), -90);
	case Shape::Cookie12: return Transform(Star(12, 0.8, R50), -90);
	case Shape::Ghostish: return Custom({ { 0.5, 0., { 1. } }, { 1., 0., { 1. } }, { 1., 1.140, { 0.254, 0.106 } }, { 0.575, 0.906, { 0.253 } } }, 1, true);
	case Shape::Clover4: return Custom({ { 0.5, 0.074 }, { 0.725, -0.099, { 0.476 } } }, 4, true);
	case Shape::Clover8: return Custom({ { 0.5, 0.036 }, { 0.758, -0.101, { 0.209 } } }, 8);
	case Shape::Burst: return Custom({ { 0.5, -0.006, { 0.006 } }, { 0.592, 0.158, { 0.006 } } }, 12);
	case Shape::SoftBurst: return Custom({ { 0.193, 0.277, { 0.053 } }, { 0.176, 0.055, { 0.053 } } }, 10);
	case Shape::Boom: return Custom({ { 0.457, 0.296, { 0.007 } }, { 0.5, -0.051, { 0.007 } } }, 15);
	case Shape::SoftBoom: return Custom({ { 0.733, 0.454 }, { 0.839, 0.437, { 0.532 } }, { 0.949, 0.449, { 0.439, 1. } }, { 0.998, 0.478, { 0.174 } } }, 16, true);
	case Shape::Flower: return Custom({ { 0.370, 0.187 }, { 0.416, 0.049, { 0.381 } }, { 0.479, 0.001, { 0.095 } } }, 8, true);
	case Shape::Puffy: return Transform(Custom({ { 0.5, 0.053 }, { 0.545, -0.040, { 0.405 } }, { 0.670, -0.035, { 0.426 } }, { 0.717, 0.066, { 0.574 } }, { 0.722, 0.128 }, { 0.777, 0.002, { 0.360 } }, { 0.914, 0.149, { 0.660 } }, { 0.926, 0.289, { 0.660 } }, { 0.881, 0.346 }, { 0.940, 0.344, { 0.126 } }, { 1.003, 0.437, { 0.255 } } }, 2, true), 0, 1., 0.742);
	case Shape::PuffyDiamond: return Custom({ { 0.870, 0.130, { 0.146 } }, { 0.818, 0.357 }, { 1.000, 0.332, { 0.853 } } }, 4, true);
	case Shape::PixelCircle: return Custom({ { 0.5, 0. }, { 0.704, 0. }, { 0.704, 0.065 }, { 0.843, 0.065 }, { 0.843, 0.148 }, { 0.926, 0.148 }, { 0.926, 0.296 }, { 1., 0.296 } }, 2, true);
	case Shape::PixelTriangle: return Custom({ { 0.110, 0.5 }, { 0.113, 0. }, { 0.287, 0. }, { 0.287, 0.087 }, { 0.421, 0.087 }, { 0.421, 0.170 }, { 0.560, 0.170 }, { 0.560, 0.265 }, { 0.674, 0.265 }, { 0.675, 0.344 }, { 0.789, 0.344 }, { 0.789, 0.439 }, { 0.888, 0.439 } }, 1, true);
	case Shape::Bun: return Custom({ { 0.796, 0.5 }, { 0.853, 0.518, { 1. } }, { 0.992, 0.631, { 1. } }, { 0.968, 1., { 1. } } }, 2, true);
	case Shape::Heart: return Custom({ { 0.5, 0.268, { 0.016 } }, { 0.792, -0.066, { 0.958 } }, { 1.064, 0.276, { 1. } }, { 0.501, 0.946, { 0.129 } } }, 1, true);
	case Shape::Count: break;
	}
	return Regular(10, { R100 });
}

// Rounded polygon: every corner is cut by its own radius (and smoothing), the cut is limited by
// the shared edges, and the corner is drawn as a cubic through the original vertex.
QPainterPath Outline(const Vertices &v) {
	const auto n = int(v.size());
	auto want = std::vector<double>(n);
	for (auto i = 0; i != n; ++i) {
		const auto &p = v[(i + n - 1) % n];
		const auto &c = v[i];
		const auto &q = v[(i + 1) % n];
		const auto a1 = std::atan2(p.y - c.y, p.x - c.x);
		const auto a2 = std::atan2(q.y - c.y, q.x - c.x);
		auto theta = std::fmod(a1 - a2 + M_PI, 2. * M_PI);
		if (theta < 0) {
			theta += 2. * M_PI;
		}
		theta = std::abs(theta - M_PI);
		const auto d = (theta > 1e-3 && c.radius > 0.) ? c.radius / std::tan(theta / 2.) : 0.;
		want[i] = d * (1. + c.smoothing);
	}
	auto cut = want;
	for (auto i = 0; i != n; ++i) {
		const auto j = (i + 1) % n;
		const auto length = std::hypot(v[j].x - v[i].x, v[j].y - v[i].y);
		if (length > 0. && want[i] + want[j] > length) {
			const auto k = length / (want[i] + want[j]);
			cut[i] = std::min(cut[i], want[i] * k);
			cut[j] = std::min(cut[j], want[j] * k);
		}
	}
	const auto toward = [](const Vertex &a, const Vertex &b, double d) {
		const auto length = std::hypot(b.x - a.x, b.y - a.y);
		const auto k = (length > 0.) ? (d / length) : 0.;
		return QPointF(a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k);
	};
	auto path = QPainterPath();
	for (auto i = 0; i != n; ++i) {
		const auto &p = v[(i + n - 1) % n];
		const auto &c = v[i];
		const auto &q = v[(i + 1) % n];
		const auto a = toward(c, p, cut[i]);
		const auto b = toward(c, q, cut[i]);
		const auto vertex = QPointF(c.x, c.y);
		const auto k = 0.55 + 0.25 * c.smoothing;
		(i == 0) ? path.moveTo(a) : path.lineTo(a);
		path.cubicTo(a + (vertex - a) * k, b + (vertex - b) * k, b);
	}
	path.closeSubpath();
	return path;
}

} // namespace

QPainterPath Path(Shape shape, const QRectF &rect) {
	auto path = Outline(Make(shape));
	const auto box = path.boundingRect();
	const auto scale = std::min(
		rect.width() / std::max(box.width(), 1e-6),
		rect.height() / std::max(box.height(), 1e-6));
	auto transform = QTransform();
	transform.translate(rect.center().x(), rect.center().y());
	transform.scale(scale, scale);
	transform.translate(-box.center().x(), -box.center().y());
	return transform.map(path);
}

Shape RandomSoft(Shape avoid) {
	static constexpr auto kSoft = std::array{
		Shape::Circle, Shape::Square, Shape::Slanted, Shape::Arch, Shape::Fan,
		Shape::Arrow, Shape::SemiCircle, Shape::Oval, Shape::Pill, Shape::Triangle,
		Shape::Diamond, Shape::ClamShell, Shape::Pentagon, Shape::Gem, Shape::Sunny,
		Shape::VerySunny, Shape::Cookie4, Shape::Cookie6, Shape::Cookie7,
		Shape::Cookie9, Shape::Cookie12, Shape::Ghostish, Shape::Clover4,
		Shape::Clover8, Shape::SoftBurst, Shape::Flower, Shape::Puffy,
		Shape::PuffyDiamond, Shape::PixelCircle, Shape::Bun, Shape::Heart,
	};
	auto result = avoid;
	while (result == avoid) {
		result = kSoft[QRandomGenerator::global()->bounded(int(kSoft.size()))];
	}
	return result;
}

QPainterPath WavyRoundedRect(
		const QRectF &rect,
		double radius,
		double amplitude,
		double phase,
		double wavelength) {
	const auto r = std::clamp(
		radius,
		0.,
		std::min(rect.width(), rect.height()) / 2.);
	const auto horizontal = rect.width() - 2. * r;
	const auto vertical = rect.height() - 2. * r;
	const auto arc = M_PI * r / 2.;
	// clockwise from the top-left end of the top edge
	struct Segment {
		double length = 0.;
		bool straight = true;
		QPointF from; // straight: start; arc: centre
		QPointF direction; // straight: unit direction; arc: (start angle, 0)
	};
	const auto l = rect.left(), t = rect.top();
	const auto rt = rect.right(), b = rect.bottom();
	const auto segments = std::array<Segment, 8>{ {
		{ horizontal, true, { l + r, t }, { 1., 0. } },
		{ arc, false, { rt - r, t + r }, { -M_PI / 2., 0. } },
		{ vertical, true, { rt, t + r }, { 0., 1. } },
		{ arc, false, { rt - r, b - r }, { 0., 0. } },
		{ horizontal, true, { rt - r, b }, { -1., 0. } },
		{ arc, false, { l + r, b - r }, { M_PI / 2., 0. } },
		{ vertical, true, { l, b - r }, { 0., -1. } },
		{ arc, false, { l + r, t + r }, { M_PI, 0. } },
	} };
	auto perimeter = 0.;
	for (const auto &segment : segments) {
		perimeter += segment.length;
	}
	auto result = QPainterPath();
	if (perimeter <= 0.) {
		return result;
	}
	const auto waves = std::max(6., std::round(perimeter / wavelength));
	const auto steps = std::max(64, int(perimeter / 2.5));
	auto segment = 0;
	auto passed = 0.;
	for (auto i = 0; i <= steps; ++i) {
		const auto s = std::min(perimeter * i / steps, perimeter);
		while (segment < 7 && s > passed + segments[segment].length) {
			passed += segments[segment].length;
			++segment;
		}
		const auto &current = segments[segment];
		const auto local = s - passed;
		auto point = QPointF();
		auto normal = QPointF();
		if (current.straight) {
			point = current.from + current.direction * local;
			// outward normal of a clockwise walk (y down): (dy, -dx)
			normal = QPointF(current.direction.y(), -current.direction.x());
		} else {
			const auto angle = current.direction.x() + (r > 0. ? local / r : 0.);
			normal = QPointF(std::cos(angle), std::sin(angle));
			point = current.from + normal * r;
		}
		const auto push = amplitude
			* std::sin(2. * M_PI * waves * s / perimeter - phase);
		const auto at = point + normal * push;
		(i == 0) ? result.moveTo(at) : result.lineTo(at);
	}
	result.closeSubpath();
	return result;
}

} // namespace Ui::MaterialShapes
