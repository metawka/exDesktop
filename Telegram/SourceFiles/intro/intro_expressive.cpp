/*
exDesktop: Material 3 Expressive elements for the intro screen.
Licensed under GPLv3, like the rest of the project.
*/
#include "intro/intro_expressive.h"

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "base/event_filter.h"
#include "styles/style_widgets.h"
#include "ui/expressive_fonts.h"

#include <QtGui/QFont>
#include <QtGui/QFontMetrics>
#include <QtGui/QPainterPath>
#include <QtGui/QTransform>
#include <QtGui/QMouseEvent>
#include <QtCore/QtMath>
#include <QtCore/QPointer>
#include <QtCore/QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace Intro::details::Expressive {
namespace {

constexpr auto kWords = std::array{
	"exDesktop", "Unofficial Client", "Telegram", "Material 3 Expressive",
	"Python Plugins", "Open Source", "Made for Desktop", "exDesktop",
	"Custom Themes", "Monet Colors", "Unofficial Client", "Telegram",
};

QColor Mixed(const QColor &a, const QColor &b, double share) {
	return QColor::fromRgbF(
		a.redF() + (b.redF() - a.redF()) * share,
		a.greenF() + (b.greenF() - a.greenF()) * share,
		a.blueF() + (b.blueF() - a.blueF()) * share,
		a.alphaF() + (b.alphaF() - a.alphaF()) * share);
}

// A stadium (fully rounded rect) whose edge waves along the normal. Walked
// analytically (two straight edges and two half circles), so it is cheap to
// rebuild every frame; the wave count is whole, so the outline closes cleanly.
QPainterPath WavyPill(const QRectF &rect, double amplitude, double phase) {
	const auto r = rect.height() / 2.;
	const auto straight = std::max(0., rect.width() - rect.height());
	const auto arc = M_PI * r;
	const auto perimeter = 2. * straight + 2. * arc;
	const auto waves = std::max(6., std::round(perimeter / 22.));
	const auto steps = std::max(48, int(perimeter / 2.5));
	const auto cy = rect.center().y();
	const auto lx = rect.left() + r;
	const auto rx = rect.right() - r;
	auto result = QPainterPath();
	for (auto i = 0; i <= steps; ++i) {
		const auto s = perimeter * i / steps;
		auto x = 0., y = 0., nx = 0., ny = 0.;
		if (s < straight) { // top, to the right
			x = lx + s; y = rect.top(); ny = -1.;
		} else if (s < straight + arc) { // right half circle, top to bottom
			const auto a = -M_PI / 2. + (s - straight) / r;
			nx = std::cos(a); ny = std::sin(a);
			x = rx + r * nx; y = cy + r * ny;
		} else if (s < 2. * straight + arc) { // bottom, to the left
			x = rx - (s - straight - arc); y = rect.bottom(); ny = 1.;
		} else { // left half circle, bottom to top
			const auto a = M_PI / 2. + (s - 2. * straight - arc) / r;
			nx = std::cos(a); ny = std::sin(a);
			x = lx + r * nx; y = cy + r * ny;
		}
		const auto push = amplitude
			* std::sin(2. * M_PI * waves * s / perimeter - phase);
		const auto point = QPointF(x + nx * push, y + ny * push);
		(i == 0) ? result.moveTo(point) : result.lineTo(point);
	}
	result.closeSubpath();
	return result;
}

// A rectangle with its own radius at every corner.
QPainterPath Panel(QRectF r, double tl, double tr, double br, double bl) {
	auto path = QPainterPath();
	path.moveTo(r.left() + tl, r.top());
	path.lineTo(r.right() - tr, r.top());
	if (tr > 0) path.arcTo(QRectF(r.right() - 2 * tr, r.top(), 2 * tr, 2 * tr), 90, -90);
	path.lineTo(r.right(), r.bottom() - br);
	if (br > 0) path.arcTo(QRectF(r.right() - 2 * br, r.bottom() - 2 * br, 2 * br, 2 * br), 0, -90);
	path.lineTo(r.left() + bl, r.bottom());
	if (bl > 0) path.arcTo(QRectF(r.left(), r.bottom() - 2 * bl, 2 * bl, 2 * bl), -90, -90);
	path.lineTo(r.left(), r.top() + tl);
	if (tl > 0) path.arcTo(QRectF(r.left(), r.top(), 2 * tl, 2 * tl), 180, -90);
	path.closeSubpath();
	return path;
}

// exDesktop logo, measured from the original artwork (368 x 294 units): three panels,
// each with one rounded outer corner, separated by 32-unit gaps.
QPainterPath LogoPath(double width) {
	const auto k = width / 368.;
	auto path = QPainterPath();
	path.addPath(Panel(QRectF(0, 0, 102, 117) , 62, 0, 0, 0));
	path.addPath(Panel(QRectF(134, 0, 234, 117), 0, 62, 0, 0));
	path.addPath(Panel(QRectF(0, 150, 368, 144), 0, 0, 62, 62));
	return QTransform::fromScale(k, k).map(path);
}

// A paper plane in a unit box.
void PlanePaths(QPainterPath &body, QPainterPath &fold) {
	body.moveTo(0.06, 0.47);
	body.lineTo(0.94, 0.09);
	body.lineTo(0.77, 0.91);
	body.lineTo(0.50, 0.70);
	body.lineTo(0.36, 0.86);
	body.lineTo(0.34, 0.63);
	body.closeSubpath();
	fold.moveTo(0.34, 0.63);
	fold.lineTo(0.94, 0.09);
	fold.lineTo(0.50, 0.70);
	fold.closeSubpath();
}

class WavyOutline final : public Ui::RpWidget {
public:
	WavyOutline(QWidget *host, QWidget *target, QColor color, bool pill)
	: RpWidget(host)
	, _target(target)
	, _color(color)
	, _pill(pill) {
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setAttribute(Qt::WA_NoSystemBackground);
		_wave.init([=](crl::time now) {
			_phase = now / 1000. * 5.2;
			reposition();
			update();
			return true;
		});
		hide();
		base::install_event_filter(this, target, [=](not_null<QEvent*> e) {
			switch (e->type()) {
			case QEvent::Enter: setHover(true); break;
			case QEvent::Leave: setHover(false); break;
			case QEvent::Move:
			case QEvent::Resize: reposition(); break;
			case QEvent::Hide: _grow.stop(); _wave.stop(); hide(); break;
			default: break;
			}
			return base::EventFilterResult::Continue;
		});
		QObject::connect(target, &QObject::destroyed, this, [=] {
			delete this;
		});
		reposition();
	}

private:
	static constexpr int kMargin = 10;

	void reposition() {
		if (!_target || !parentWidget()) {
			return;
		}
		const auto topLeft = _target->mapTo(parentWidget(), QPoint());
		const auto geometry = QRect(topLeft, _target->size()).marginsAdded(
			QMargins(kMargin, kMargin, kMargin, kMargin));
		if (geometry != this->geometry()) {
			setGeometry(geometry);
		}
	}

	void setHover(bool on) {
		if (on == _hover) {
			return;
		}
		_hover = on;
		reposition();
		show();
		raise();
		_grow.start([=] {
			if (!_hover && _grow.value(0.) <= 0.001) {
				_wave.stop();
				hide();
			}
			update();
		}, _grow.value(on ? 0. : 1.), on ? 1. : 0., on ? 480 : 420, on
			? anim::easeOutBack
			: anim::easeOutCubic);
		if (on) {
			_wave.start();
		}
	}

	void paintEvent(QPaintEvent *) override {
		const auto k = std::max(0., _grow.value(_hover ? 1. : 0.));
		if (k <= 0.001) {
			return;
		}
		auto p = QPainter(this);
		p.setRenderHint(QPainter::Antialiasing);
		const auto inner = QRectF(rect()).marginsRemoved(
			QMarginsF(kMargin, kMargin, kMargin, kMargin));
		if (!_pill) {
			// a wavy underline that grows from the left under the link's text
			const auto width = inner.width() * std::min(1., k);
			const auto y = inner.bottom() + 1.5;
			auto line = QPainterPath();
			const auto count = std::max(2, int(width / 1.5));
			for (auto i = 0; i <= count; ++i) {
				const auto x = inner.left() + width * i / count;
				const auto wy = y + 1.6 * std::min(1., k) * std::sin((x - inner.left()) / 5.5 - _phase);
				(i == 0) ? line.moveTo(x, wy) : line.lineTo(x, wy);
			}
			auto c = _color;
			c.setAlphaF(std::min(1., k * 1.5) * _color.alphaF());
			p.setPen(QPen(c, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			p.setBrush(Qt::NoBrush);
			p.drawPath(line);
			return;
		}
		const auto grow = 4.5 * k;
		const auto box = inner.marginsAdded(QMarginsF(grow, grow, grow, grow));
		const auto radius = _pill ? box.height() / 2. : std::min(14., box.height() / 2.);
		const auto amplitude = 2.0 * std::min(1., k);

		// walk the rounded rectangle's outline and push each point along the normal
		auto base = QPainterPath();
		base.addRoundedRect(box, radius, radius);
		const auto length = base.length();
		const auto steps = std::max(40, int(length / 2.));
		const auto waves = std::max(8., std::round(length / 26.));
		auto wavy = QPainterPath();
		for (auto i = 0; i <= steps; ++i) {
			const auto t = double(i) / steps;
			const auto at = base.pointAtPercent(t);
			const auto ahead = base.pointAtPercent(std::fmod(t + 0.001, 1.));
			auto nx = ahead.y() - at.y(), ny = -(ahead.x() - at.x());
			const auto n = std::hypot(nx, ny);
			if (n > 0.) {
				nx /= n;
				ny /= n;
			}
			const auto push = amplitude * std::sin(t * waves * 2. * M_PI - _phase);
			const auto point = QPointF(at.x() + nx * push, at.y() + ny * push);
			(i == 0) ? wavy.moveTo(point) : wavy.lineTo(point);
		}
		wavy.closeSubpath();
		auto pen = QPen(_color, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
		auto c = _color;
		c.setAlphaF(std::min(1., k) * _color.alphaF());
		pen.setColor(c);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawPath(wavy);
	}

	QPointer<QWidget> _target;
	QColor _color;
	bool _pill = true;
	bool _hover = false;
	double _phase = 0.;
	Ui::Animations::Simple _grow;
	Ui::Animations::Basic _wave;

};

} // namespace

void PaintBanner(
		QPainter &p,
		QRect rect,
		const QColor &base,
		double seconds,
		int logoLimit,
		int fullHeight,
		double wavy,
		QRectF *logoRect) {
	p.save();
	p.setClipRect(rect);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::TextAntialiasing);

	const auto text = Mixed(base, QColor(255, 255, 255), 0.20);
	const auto accent = st::windowActiveTextFg->c;
	// the logo carries the same accent as the highlighted "exDesktop" words,
	// so it never looks dimmer than they do
	const auto gray = Mixed(base, accent, 0.92);
	const auto full = std::max(fullHeight, 1);
	auto font = Ui::ExpressiveFonts::Unbounded(
		std::clamp(full / 8, 14, 26),
		QFont::DemiBold);
	// no hinting: glyphs may sit between pixels, so the drift stays smooth
	font.setHintingPreference(QFont::PreferNoHinting);
	p.setFont(font);
	const auto metrics = QFontMetricsF(font);
	const auto rowHeight = std::round(metrics.height() * 1.18);
	// only whole rows, centred in the full banner; they move with its bottom edge
	const auto rows = std::max(1, int(full / rowHeight));
	const auto rowsTop = rect.top()
		+ (rect.height() - full)
		+ std::floor((full - rows * rowHeight) / 2.);
	// every row starts somewhere else, runs left or right, and gets cut by the edges
	constexpr auto kOffsets = std::array{ -60, -230, -20, -150, -95, -200, -40 };
	constexpr auto kSpeeds = std::array{ 9., -12., 7., -10., 11., -8., 6. };
	const auto count = int(kWords.size());

	for (auto row = 0; row != rows; ++row) {
		// one period = every word exactly once, so copies join without a seam
		auto period = QString();
		for (auto i = 0; i != count; ++i) {
			period += QString::fromUtf8(kWords[(row * 3 + i) % count]);
			period += QStringLiteral("     "); // just space between the words
		}
		const auto periodWidth = metrics.horizontalAdvance(period + period)
			- metrics.horizontalAdvance(period);
		if (periodWidth <= 0.) {
			continue;
		}
		auto x = std::fmod(
			kOffsets[row % kOffsets.size()]
				+ seconds * kSpeeds[row % kSpeeds.size()],
			periodWidth);
		if (x > 0.) {
			x -= periodWidth;
		}
		x += rect.left();
		const auto copies = int(std::ceil((rect.right() - x) / periodWidth)) + 1;
		auto c = text;
		c.setAlphaF(row % 2 ? 0.55 : 0.85);
		const auto baseline = rowsTop + (row + 1) * rowHeight - metrics.descent();
		// every word is placed by the advance of everything before it, so the
		// pieces line up exactly as one long string would
		auto pieceStart = 0;
		const auto drawPlain = [&](double from, int till) {
			if (till > pieceStart) {
				p.setPen(c);
				p.drawText(
					QPointF(from, baseline),
					period.mid(pieceStart, till - pieceStart));
			}
		};
		for (auto copy = 0; copy != copies; ++copy) {
			const auto left = x + copy * periodWidth;
			pieceStart = 0;
			auto at = 0;
			for (auto i = 0; i != count; ++i) {
				const auto word = QString::fromUtf8(kWords[(row * 3 + i) % count]);
				if (word == u"exDesktop"_q) {
					drawPlain(left + metrics.horizontalAdvance(period.left(pieceStart)), at);
					const auto wordLeft = left + metrics.horizontalAdvance(period.left(at));
					const auto wordWidth = metrics.horizontalAdvance(word);
					// the project's own name: a tonal pill in the accent colour with the
					// word in full strength on top. The pill never reaches the next row,
					// and its own edge turns wavy while the logo is hovered.
					const auto amplitude = 1.6 * std::clamp(wavy, 0., 1.25);
					const auto padX = metrics.height() * 0.45;
					const auto height = std::min(
						metrics.ascent() + metrics.descent(),
						rowHeight - 6. - 2 * 1.6 * 1.25);
					const auto middle = baseline - metrics.capHeight() / 2.;
					// the bottom edge sits a bit lower: the letters look tight there otherwise
					const auto pill = QRectF(
						wordLeft - padX,
						middle - height / 2.,
						wordWidth + 2 * padX,
						height + 1.5);
					auto fill = accent;
					fill.setAlphaF(row % 2 ? 0.16 : 0.24);
					p.setPen(Qt::NoPen);
					p.setBrush(fill);
					if (amplitude < 0.05) {
						p.drawRoundedRect(pill, pill.height() / 2., pill.height() / 2.);
					} else {
						p.drawPath(WavyPill(pill, amplitude, seconds * 4.4 + row));
					}
					auto strong = accent;
					strong.setAlphaF(row % 2 ? 0.8 : 1.);
					p.setPen(strong);
					p.drawText(QPointF(wordLeft, baseline), word);
					pieceStart = at + word.size();
				}
				at += word.size() + 5; // the five spaces between words
			}
			drawPlain(left + metrics.horizontalAdvance(period.left(pieceStart)), period.size());
		}
	}

	// the logo, in grey, on a soft halo so the words do not run over it; it shrinks to
	// stay above the badge when the window is low
	const auto top = rect.top() + 12;
	const auto room = std::max(40, std::min(rect.bottom(), logoLimit) - top - 8);
	const auto height = std::min(room - 32., std::min(rect.height() * 0.62, 150.) * (294. / 368.));
	const auto size = std::max(24., height * (368. / 294.));
	const auto logo = LogoPath(size);
	const auto box = logo.boundingRect();
	const auto at = QPointF(
		rect.center().x() - box.width() / 2.,
		top + (room - box.height()) / 2.);
	// the halo follows the logo's outline, and the two gaps between the panels are cut out of
	// it completely, so the words show through them
	const auto k = size / 368.;
	const auto margin = std::max(6., size * 0.07);
	auto strips = QPainterPath();
	strips.addRect(QRectF(102 * k, -margin * 4, 32 * k, 117 * k + margin * 4));
	// the horizontal gap runs out through the halo on both sides, like the vertical one
	strips.addRect(QRectF(-margin * 4, 117 * k, 368 * k + margin * 8, 33 * k));
	auto stroker = QPainterPathStroker();
	stroker.setWidth(margin * 2.);
	stroker.setJoinStyle(Qt::RoundJoin);
	stroker.setCapStyle(Qt::RoundCap);
	const auto halo = stroker.createStroke(logo).united(logo).subtracted(strips);
	if (logoRect) {
		*logoRect = box.translated(at);
	}
	p.translate(at);
	p.setPen(Qt::NoPen);
	p.setBrush(base);
	p.drawPath(halo);
	p.setBrush(gray);
	p.drawPath(logo);
	p.restore();
}

void PaintSunBadge(
		QPainter &p,
		QPointF center,
		double diameter,
		const QColor &fill,
		const QColor &plane,
		const QColor &fold,
		double angle,
		double seconds) {
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.translate(center);

	const auto radius = diameter / 2.;

	// two soft rings around the shape, breathing very gently
	for (auto ring = 2; ring >= 1; --ring) {
		const auto breathe = 0.5 + 0.5 * std::sin(seconds * 0.9 - ring * 0.8);
		const auto rr = radius * (1.20 + 0.24 * ring + 0.02 * breathe);
		auto c = fill;
		c.setAlphaF((0.13 - 0.04 * ring) * (0.85 + 0.15 * breathe));
		p.setPen(Qt::NoPen);
		p.setBrush(c);
		p.drawEllipse(QPointF(0, 0), rr, rr);
	}

	// eight wide, soft lobes: r(a) = R * (1 - d + d * cos(n a)), turning slowly
	constexpr auto kLobes = 8;
	const auto depth = 0.07;
	const auto turn = angle;
	auto sun = QPainterPath();
	constexpr auto kSteps = 360;
	for (auto i = 0; i <= kSteps; ++i) {
		const auto a = 2. * M_PI * i / kSteps;
		const auto r = radius * (1. - depth + depth * std::cos(kLobes * a + turn));
		const auto point = QPointF(std::cos(a) * r, std::sin(a) * r);
		(i == 0) ? sun.moveTo(point) : sun.lineTo(point);
	}
	sun.closeSubpath();
	p.setPen(Qt::NoPen);
	p.setBrush(fill);
	p.drawPath(sun);

	// the Telegram plane stays upright
	auto body = QPainterPath();
	auto foldPath = QPainterPath();
	PlanePaths(body, foldPath);
	const auto side = diameter * 0.52;
	p.translate(-side / 2. - side * 0.03, -side / 2.);
	p.scale(side, side);
	p.setBrush(plane);
	p.drawPath(body);
	p.setBrush(fold);
	p.drawPath(foldPath);
	p.restore();
}

void AddWavyHover(
		QWidget *target,
		QWidget *host,
		const QColor &color,
		bool pill) {
	if (!target || !host) {
		return;
	}
	new WavyOutline(host, target, color, pill);
}

void AddLiftHover(QWidget *target) {
	if (!target) {
		return;
	}
	struct State {
		Ui::Animations::Simple lift;
		QPoint base;
		bool over = false;
		bool moving = false;
	};
	const auto state = std::make_shared<State>();
	const auto apply = [=] {
		const auto shift = int(std::round(style::ConvertScaleExact(4.) * state->lift.value(state->over ? 1. : 0.)));
		state->moving = true;
		target->move(state->base.x(), state->base.y() - shift);
		state->moving = false;
	};
	base::install_event_filter(target, target, [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::Enter || type == QEvent::Leave) {
			const auto over = (type == QEvent::Enter);
			if (over == state->over) {
				return base::EventFilterResult::Continue;
			}
			if (!state->lift.animating() && !state->over) {
				state->base = target->pos();
			}
			state->over = over;
			state->lift.start(
				apply,
				state->lift.value(over ? 0. : 1.),
				over ? 1. : 0.,
				over ? 420 : 320,
				over ? anim::easeOutBack : anim::easeOutCubic);
		} else if (type == QEvent::Move && !state->moving && !state->lift.animating() && !state->over) {
			state->base = target->pos();
		}
		return base::EventFilterResult::Continue;
	});
}

} // namespace Intro::details::Expressive
