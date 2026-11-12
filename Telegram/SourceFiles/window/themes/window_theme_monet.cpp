/*
exDesktop: Monet colours. Licensed under GPLv3, like the rest of the project.

The whole palette is rebuilt from one seed colour, the way Material You does it:
  1. the seed is the system accent (Windows 10/11, macOS and Linux desktops report
     it through Qt; on Linux a matugen palette can be handed over instead);
  2. Material 3 "tonal spot" palettes are made from it (tone = CIELAB L*, so the
     tones match Material's; hue and chroma are kept per palette);
  3. every colour of the current theme is recoloured into those palettes, keeping
     its lightness, and the main surfaces get their exact Material 3 roles.
The theme the user picked stays the base, so Monet works with day, night and
custom themes alike, and turning it off brings the theme back as it was.
*/
#include "window/themes/window_theme_monet.h"

#include "window/themes/window_theme.h"
#include "extera/extera_config.h"
#include "base/event_filter.h"
#include "base/timer.h"
#include "styles/palette.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QFileSystemWatcher>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtGui/QGuiApplication>
#include <QtGui/QPalette>
#include <QtWidgets/QApplication>

#include <array>
#include <cmath>
#include <map>

namespace Window::Theme {
namespace {

constexpr auto kMaxJsonSize = 1024 * 1024;
constexpr auto kRefreshDelay = crl::time(300);

[[nodiscard]] QString EnabledKey() {
	return u"monet_colors"_q;
}

using Roles = std::map<QString, QColor>;

// ---------------------------------------------------------------------------
// Colour spaces.

[[nodiscard]] double ToLinear(double c) {
	return (c <= 0.04045) ? (c / 12.92) : std::pow((c + 0.055) / 1.055, 2.4);
}

[[nodiscard]] double ToGamma(double c) {
	c = std::clamp(c, 0., 1.);
	return (c <= 0.0031308) ? (12.92 * c) : (1.055 * std::pow(c, 1. / 2.4) - 0.055);
}

struct Lch {
	double l = 0.;
	double c = 0.;
	double h = 0.; // degrees
};

// CIELAB, D65.
constexpr auto kWhiteX = 0.95047;
constexpr auto kWhiteY = 1.0;
constexpr auto kWhiteZ = 1.08883;

[[nodiscard]] double LabF(double t) {
	constexpr auto e = 216. / 24389.;
	constexpr auto k = 24389. / 27.;
	return (t > e) ? std::cbrt(t) : ((k * t + 16.) / 116.);
}

[[nodiscard]] double LabFInverse(double t) {
	constexpr auto e = 216. / 24389.;
	constexpr auto k = 24389. / 27.;
	const auto cube = t * t * t;
	return (cube > e) ? cube : ((116. * t - 16.) / k);
}

[[nodiscard]] Lch ToLabLch(const QColor &color) {
	const auto r = ToLinear(color.redF());
	const auto g = ToLinear(color.greenF());
	const auto b = ToLinear(color.blueF());
	const auto x = 0.4124 * r + 0.3576 * g + 0.1805 * b;
	const auto y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
	const auto z = 0.0193 * r + 0.1192 * g + 0.9505 * b;
	const auto fx = LabF(x / kWhiteX);
	const auto fy = LabF(y / kWhiteY);
	const auto fz = LabF(z / kWhiteZ);
	const auto l = 116. * fy - 16.;
	const auto a = 500. * (fx - fy);
	const auto bb = 200. * (fy - fz);
	const auto h = std::fmod(std::atan2(bb, a) * 180. / M_PI + 360., 360.);
	return { l, std::hypot(a, bb), h };
}

[[nodiscard]] std::array<double, 3> LabLchToLinearRgb(double l, double c, double h) {
	const auto a = c * std::cos(h * M_PI / 180.);
	const auto b = c * std::sin(h * M_PI / 180.);
	const auto fy = (l + 16.) / 116.;
	const auto x = LabFInverse(fy + a / 500.) * kWhiteX;
	const auto y = LabFInverse(fy) * kWhiteY;
	const auto z = LabFInverse(fy - b / 200.) * kWhiteZ;
	return {
		3.2406 * x - 1.5372 * y - 0.4986 * z,
		-0.9689 * x + 1.8758 * y + 0.0415 * z,
		0.0557 * x - 0.2040 * y + 1.0570 * z,
	};
}

[[nodiscard]] QColor FromLinear(const std::array<double, 3> &rgb) {
	const auto channel = [](double v) {
		return std::clamp(int(std::round(ToGamma(v) * 255.)), 0, 255);
	};
	return QColor(channel(rgb[0]), channel(rgb[1]), channel(rgb[2]));
}

// A Material tone: lightness `tone` (CIELAB L*), the hue kept, the chroma
// lowered just enough to stay inside sRGB.
[[nodiscard]] QColor Tone(double hue, double chroma, double tone) {
	const auto inside = [](const std::array<double, 3> &rgb) {
		constexpr auto eps = 1e-4;
		return ranges::all_of(rgb, [](double v) {
			return (v >= -eps) && (v <= 1. + eps);
		});
	};
	auto rgb = LabLchToLinearRgb(tone, chroma, hue);
	if (!inside(rgb)) {
		auto low = 0., high = chroma;
		for (auto i = 0; i != 24; ++i) {
			const auto middle = (low + high) / 2.;
			if (inside(LabLchToLinearRgb(tone, middle, hue))) {
				low = middle;
			} else {
				high = middle;
			}
		}
		rgb = LabLchToLinearRgb(tone, low, hue);
	}
	return FromLinear(rgb);
}

// OKLCH, used to recolour theme colours (keeps perceived lightness well).
[[nodiscard]] Lch ToOklch(const QColor &color) {
	const auto r = ToLinear(color.redF());
	const auto g = ToLinear(color.greenF());
	const auto b = ToLinear(color.blueF());
	const auto l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
	const auto m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
	const auto s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
	const auto L = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
	const auto a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
	const auto bb = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
	const auto h = std::fmod(std::atan2(bb, a) * 180. / M_PI + 360., 360.);
	return { L, std::hypot(a, bb), h };
}

[[nodiscard]] QColor FromOklch(double L, double C, double H) {
	const auto a = C * std::cos(H * M_PI / 180.);
	const auto b = C * std::sin(H * M_PI / 180.);
	const auto l = std::pow(L + 0.3963377774 * a + 0.2158037573 * b, 3);
	const auto m = std::pow(L - 0.1055613458 * a - 0.0638541728 * b, 3);
	const auto s = std::pow(L - 0.0894841775 * a - 1.2914855480 * b, 3);
	return FromLinear({
		4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
		-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
		-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s,
	});
}

[[nodiscard]] QColor Mix(const QColor &a, const QColor &b, double share) {
	return QColor::fromRgbF(
		a.redF() + (b.redF() - a.redF()) * share,
		a.greenF() + (b.greenF() - a.greenF()) * share,
		a.blueF() + (b.blueF() - a.blueF()) * share,
		a.alphaF() + (b.alphaF() - a.alphaF()) * share);
}

[[nodiscard]] QColor WithAlpha(QColor color, double alpha) {
	color.setAlphaF(alpha);
	return color;
}

// ---------------------------------------------------------------------------
// Material 3 roles.

// Tonal spot, as Material You does by default. Chroma values are in CIELAB units
// (checked against matugen: every role lands within a few units of it).
[[nodiscard]] Roles RolesFromSeed(const QColor &seed, bool dark) {
	struct Palette {
		double hue = 0.;
		double chroma = 0.;
	};
	const auto hue = ToLabLch(seed).h;
	const auto primary = Palette{ hue, 32. };
	const auto secondary = Palette{ hue, 12. };
	const auto tertiary = Palette{ std::fmod(hue + 40., 360.), 22. };
	const auto neutral = Palette{ hue, 3.6 };
	const auto variant = Palette{ hue, 5.5 };
	const auto error = Palette{ 33., 72. };

	auto result = Roles();
	const auto set = [&](
			const QString &name,
			Palette palette,
			double darkTone,
			double lightTone) {
		result[name] = Tone(
			palette.hue,
			palette.chroma,
			dark ? darkTone : lightTone);
	};
	const auto accent = [&](const QString &name, Palette palette) {
		set(name, palette, 80., 40.);
		set(u"on_"_q + name, palette, 20., 100.);
		set(name + u"_container"_q, palette, 30., 90.);
		set(u"on_"_q + name + u"_container"_q, palette, 90., 10.);
	};
	accent(u"primary"_q, primary);
	accent(u"secondary"_q, secondary);
	accent(u"tertiary"_q, tertiary);
	accent(u"error"_q, error);
	set(u"surface"_q, neutral, 6., 98.);
	set(u"surface_container_lowest"_q, neutral, 4., 100.);
	set(u"surface_container_low"_q, neutral, 10., 96.);
	set(u"surface_container"_q, neutral, 12., 94.);
	set(u"surface_container_high"_q, neutral, 17., 92.);
	set(u"surface_container_highest"_q, neutral, 22., 90.);
	set(u"on_surface"_q, neutral, 90., 10.);
	set(u"on_surface_variant"_q, variant, 80., 30.);
	set(u"outline"_q, variant, 60., 50.);
	set(u"outline_variant"_q, variant, 30., 80.);
	set(u"shadow"_q, neutral, 0., 0.);
	return result;
}

#if !defined Q_OS_WIN && !defined Q_OS_MAC
// Linux desktops themed with matugen can hand its full output (matugen's
// "--json hex" format) to the client; its roles are then used as they are, so
// the colours come from the wallpaper exactly like on Android. The file is
// $EXDESKTOP_MATUGEN_JSON or ~/.config/exdesktop/matugen.json (a symlink to
// wherever the desktop keeps it works too).
[[nodiscard]] QString MatugenPalettePath() {
	const auto custom = qEnvironmentVariable("EXDESKTOP_MATUGEN_JSON");
	if (!custom.isEmpty()) {
		return custom;
	}
	auto config = qEnvironmentVariable("XDG_CONFIG_HOME");
	if (config.isEmpty()) {
		config = QDir::homePath() + u"/.config"_q;
	}
	return config + u"/exdesktop/matugen.json"_q;
}
#endif // !Q_OS_WIN && !Q_OS_MAC

[[nodiscard]] std::optional<Roles> MatugenRoles(bool dark) {
#if !defined Q_OS_WIN && !defined Q_OS_MAC
	auto file = QFile(MatugenPalettePath());
	if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxJsonSize) {
		return std::nullopt;
	}
	const auto colors = QJsonDocument::fromJson(
		file.readAll()
	).object().value(u"colors"_q).toObject();
	const auto mode = dark ? u"dark"_q : u"light"_q;
	auto result = Roles();
	for (auto i = colors.begin(); i != colors.end(); ++i) {
		const auto color = QColor(i.value().toObject()
			.value(mode).toObject()
			.value(u"color"_q).toString());
		if (color.isValid()) {
			result[i.key()] = color;
		}
	}
	const auto required = {
		u"primary"_q,
		u"surface"_q,
		u"on_surface"_q,
		u"primary_container"_q,
	};
	for (const auto &key : required) {
		if (!result.contains(key)) {
			return std::nullopt;
		}
	}
	return result;
#else // !Q_OS_WIN && !Q_OS_MAC
	return std::nullopt;
#endif // !Q_OS_WIN && !Q_OS_MAC
}

// The accent the system reports (Windows 10/11, macOS, KDE, GNOME via portal).
[[nodiscard]] std::optional<QColor> SystemSeed() {
	const auto palette = QGuiApplication::palette();
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
	const auto color = palette.color(QPalette::Accent);
#else // Qt >= 6.6.0
	const auto color = palette.color(QPalette::Highlight);
#endif // Qt < 6.6.0
	// a grey accent has no hue to build palettes from
	if (!color.isValid() || ToLabLch(color).c < 8.) {
		return std::nullopt;
	}
	return color;
}

[[nodiscard]] std::optional<Roles> CurrentRoles(bool dark) {
	if (auto roles = MatugenRoles(dark)) {
		return roles;
	} else if (const auto seed = SystemSeed()) {
		return RolesFromSeed(*seed, dark);
	}
	return std::nullopt;
}

[[nodiscard]] bool HasRoles(const Roles &roles, std::initializer_list<QString> keys) {
	return ranges::all_of(keys, [&](const QString &key) {
		return roles.contains(key);
	});
}

// ---------------------------------------------------------------------------
// The palette.

// One theme colour moved into the Monet palettes, its lightness kept.
[[nodiscard]] QColor Recolor(
		const QColor &color,
		const Lch &neutral,
		const Lch &primary) {
	static const auto telegramBlue = ToOklch(QColor(0x40, 0xa7, 0xe3)).c;
	const auto v = ToOklch(color);
	auto result = QColor();
	if (v.c < 0.035) {
		// greys take the system's neutral tint
		result = FromOklch(v.l, neutral.c * 1.2, neutral.h);
	} else if (v.h >= 205. && v.h <= 290.) {
		// Telegram's blues become the primary
		result = FromOklch(
			v.l,
			std::min(v.c, v.c * primary.c / telegramBlue),
			primary.h);
	} else if (v.h < 40. || v.h > 335.) {
		// reds stay red: errors, delete, mute
		result = color;
	} else {
		// other accents lean a little towards the primary
		const auto diff = std::fmod(primary.h - v.h + 540., 360.) - 180.;
		result = FromOklch(
			v.l,
			v.c * 0.9,
			std::fmod(v.h + std::clamp(diff * 0.12, -15., 15.) + 360., 360.));
	}
	result.setAlpha(color.alpha());
	return result;
}

// Exact Material 3 roles for the main surfaces, text and controls.
void ApplyRoles(std::map<QString, QColor> &out, const Roles &r, bool dark) {
	const auto white = QColor(255, 255, 255);
	const auto black = QColor(0, 0, 0);
	const auto role = [&](const QString &name) {
		const auto i = r.find(name);
		return (i != end(r)) ? i->second : QColor();
	};
	const auto over = [&](const QString &name) {
		return Mix(role(name), dark ? white : black, 0.08);
	};
	const auto ripple = [&](const QString &name) {
		return Mix(role(name), dark ? white : black, 0.14);
	};
	const auto alpha = [&](const QString &name, double value) {
		return WithAlpha(role(name), value);
	};
	const auto set = [&](const QString &key, const QColor &color) {
		if (color.isValid() && out.contains(key)) {
			out[key] = color;
		}
	};
	const auto same = [&](const QString &key, const QString &name) {
		set(key, role(name));
	};

	// base surfaces and text (most of the palette inherits from these)
	same(u"windowBg"_q, u"surface"_q);
	same(u"windowFg"_q, u"on_surface"_q);
	same(u"windowBgOver"_q, u"surface_container_high"_q);
	same(u"windowBgRipple"_q, u"surface_container_highest"_q);
	same(u"windowFgOver"_q, u"on_surface"_q);
	same(u"windowSubTextFg"_q, u"on_surface_variant"_q);
	same(u"windowSubTextFgOver"_q, u"on_surface_variant"_q);
	same(u"windowBoldFg"_q, u"on_surface"_q);
	same(u"windowBoldFgOver"_q, u"on_surface"_q);
	same(u"windowBgActive"_q, u"primary"_q);
	same(u"windowFgActive"_q, u"on_primary"_q);
	same(u"windowActiveTextFg"_q, u"primary"_q);
	same(u"windowShadowFg"_q, u"shadow"_q);

	// dialogs: rows share the surface, sections are soft tonal cards
	same(u"boxBg"_q, u"surface"_q);
	same(u"boxDividerBg"_q, u"surface_container_low"_q);
	set(u"boxDividerFg"_q, alpha(u"outline_variant"_q, 0.5));
	same(u"boxSearchBg"_q, u"surface_container_high"_q);
	same(u"menuBg"_q, u"surface_container"_q);
	same(u"menuBgOver"_q, u"surface_container_highest"_q);
	set(u"menuBgRipple"_q, ripple(u"surface_container_highest"_q));
	same(u"menuIconFg"_q, u"on_surface_variant"_q);
	same(u"menuIconFgOver"_q, u"on_surface"_q);
	same(u"menuFgDisabled"_q, u"outline"_q);
	same(u"menuSeparatorFg"_q, u"outline_variant"_q);

	// filled and tonal buttons
	same(u"activeButtonBg"_q, u"primary"_q);
	set(u"activeButtonBgOver"_q, over(u"primary"_q));
	set(u"activeButtonBgRipple"_q, ripple(u"primary"_q));
	same(u"activeButtonFg"_q, u"on_primary"_q);
	same(u"activeButtonFgOver"_q, u"on_primary"_q);
	set(u"activeButtonSecondaryFg"_q, alpha(u"on_primary"_q, 0.7));
	set(u"activeButtonSecondaryFgOver"_q, alpha(u"on_primary"_q, 0.7));
	same(u"lightButtonBg"_q, u"secondary_container"_q);
	set(u"lightButtonBgOver"_q, over(u"secondary_container"_q));
	set(u"lightButtonBgRipple"_q, ripple(u"secondary_container"_q));
	same(u"lightButtonFg"_q, u"on_secondary_container"_q);
	same(u"lightButtonFgOver"_q, u"on_secondary_container"_q);

	// inputs, lines, sliders, scroll bars
	same(u"inputBorderFg"_q, u"outline_variant"_q);
	same(u"inputBorderFgOver"_q, u"outline"_q);
	same(u"inputBorderFgActive"_q, u"primary"_q);
	same(u"sliderBgInactive"_q, u"secondary_container"_q);
	same(u"sliderBgActive"_q, u"primary"_q);
	same(u"checkboxFg"_q, u"outline"_q);
	set(u"scrollBarBg"_q, alpha(u"on_surface_variant"_q, 0.35));
	set(u"scrollBarBgOver"_q, alpha(u"on_surface_variant"_q, 0.5));
	set(u"scrollBg"_q, alpha(u"on_surface_variant"_q, 0.12));
	set(u"scrollBgOver"_q, alpha(u"on_surface_variant"_q, 0.2));

	// title bar
	same(u"titleBg"_q, u"surface_container_low"_q);
	same(u"titleBgActive"_q, u"surface_container_low"_q);
	same(u"titleFg"_q, u"on_surface_variant"_q);
	same(u"titleFgActive"_q, u"on_surface"_q);

	// chat list
	same(u"dialogsBg"_q, u"surface"_q);
	same(u"dialogsBgOver"_q, u"surface_container"_q);
	same(u"dialogsBgActive"_q, u"primary_container"_q);
	same(u"dialogsNameFg"_q, u"on_surface"_q);
	same(u"dialogsNameFgOver"_q, u"on_surface"_q);
	same(u"dialogsNameFgActive"_q, u"on_primary_container"_q);
	same(u"dialogsTextFg"_q, u"on_surface_variant"_q);
	same(u"dialogsTextFgOver"_q, u"on_surface_variant"_q);
	same(u"dialogsTextFgActive"_q, u"on_primary_container"_q);
	same(u"dialogsDateFg"_q, u"outline"_q);
	same(u"dialogsDateFgOver"_q, u"outline"_q);
	same(u"dialogsDateFgActive"_q, u"on_primary_container"_q);
	same(u"dialogsUnreadBg"_q, u"primary"_q);
	same(u"dialogsUnreadBgOver"_q, u"primary"_q);
	same(u"dialogsUnreadBgActive"_q, u"on_primary_container"_q);
	same(u"dialogsUnreadFg"_q, u"on_primary"_q);
	same(u"dialogsUnreadFgOver"_q, u"on_primary"_q);
	same(u"dialogsUnreadFgActive"_q, u"primary_container"_q);
	same(u"dialogsUnreadBgMuted"_q, u"outline"_q);
	same(u"dialogsUnreadBgMutedOver"_q, u"outline"_q);
	same(u"dialogsUnreadBgMutedActive"_q, u"on_primary_container"_q);

	// folders side bar
	same(u"sideBarBg"_q, u"surface_container_low"_q);
	same(u"sideBarBgActive"_q, u"secondary_container"_q);
	set(u"sideBarBgRipple"_q, ripple(u"surface_container_low"_q));
	same(u"sideBarTextFg"_q, u"on_surface_variant"_q);
	same(u"sideBarTextFgActive"_q, u"on_secondary_container"_q);
	same(u"sideBarIconFg"_q, u"on_surface_variant"_q);
	same(u"sideBarIconFgActive"_q, u"on_secondary_container"_q);
	same(u"sideBarBadgeBg"_q, u"primary"_q);
	same(u"sideBarBadgeBgMuted"_q, u"outline"_q);
	same(u"sideBarBadgeFg"_q, u"on_primary"_q);

	// messages
	same(u"msgInBg"_q, u"surface_container_high"_q);
	same(u"msgInBgSelected"_q, u"surface_container_highest"_q);
	same(u"msgOutBg"_q, u"primary_container"_q);
	set(u"msgOutBgSelected"_q, over(u"primary_container"_q));
	same(u"historyTextInFg"_q, u"on_surface"_q);
	same(u"historyTextInFgSelected"_q, u"on_surface"_q);
	same(u"historyTextOutFg"_q, u"on_primary_container"_q);
	same(u"historyTextOutFgSelected"_q, u"on_primary_container"_q);
	same(u"historyLinkInFg"_q, u"primary"_q);
	same(u"historyLinkInFgSelected"_q, u"primary"_q);
	same(u"historyLinkOutFg"_q, u"on_primary_container"_q);
	same(u"historyLinkOutFgSelected"_q, u"on_primary_container"_q);
	same(u"msgInDateFg"_q, u"on_surface_variant"_q);
	same(u"msgInDateFgSelected"_q, u"on_surface_variant"_q);
	set(u"msgOutDateFg"_q, alpha(u"on_primary_container"_q, 0.7));
	set(u"msgOutDateFgSelected"_q, alpha(u"on_primary_container"_q, 0.7));
	same(u"msgInServiceFg"_q, u"primary"_q);
	same(u"msgInServiceFgSelected"_q, u"primary"_q);
	same(u"msgOutServiceFg"_q, u"on_primary_container"_q);
	same(u"msgOutServiceFgSelected"_q, u"on_primary_container"_q);
	same(u"msgWaveformInActive"_q, u"primary"_q);
	set(u"msgWaveformInInactive"_q, alpha(u"on_surface_variant"_q, 0.5));
	same(u"msgWaveformOutActive"_q, u"on_primary_container"_q);
	set(u"msgWaveformOutInactive"_q, alpha(u"on_primary_container"_q, 0.5));
	same(u"historyComposeAreaBg"_q, u"surface_container"_q);
	same(u"historyComposeAreaFg"_q, u"on_surface"_q);
	same(u"historyComposeAreaFgService"_q, u"on_surface_variant"_q);
	same(u"historyComposeIconFg"_q, u"on_surface_variant"_q);
	same(u"historyComposeIconFgOver"_q, u"primary"_q);
	same(u"historySendIconFg"_q, u"primary"_q);
	set(u"historySendIconFgOver"_q, over(u"primary"_q));
	set(u"historyScrollBarBg"_q, alpha(u"on_surface_variant"_q, 0.35));
	set(u"historyScrollBarBgOver"_q, alpha(u"on_surface_variant"_q, 0.5));
	same(u"historyToDownBg"_q, u"surface_container_high"_q);
	same(u"historyToDownBgOver"_q, u"surface_container_highest"_q);
	same(u"historyToDownFg"_q, u"on_surface_variant"_q);
	same(u"historyToDownFgOver"_q, u"primary"_q);
	same(u"historyToDownShadow"_q, u"shadow"_q);

	// login / start screen
	same(u"introTitleFg"_q, u"on_surface"_q);
	same(u"introDescriptionFg"_q, u"on_surface_variant"_q);
	same(u"introCoverTopBg"_q, u"primary_container"_q);
	if (HasRoles(r, { u"primary_container"_q, u"tertiary_container"_q })) {
		set(u"introCoverBottomBg"_q, Mix(
			role(u"primary_container"_q),
			role(u"tertiary_container"_q),
			0.7));
	}
	set(u"introCoverIconsFg"_q, alpha(u"on_primary_container"_q, 0.25));
}

// A palette saved by style::main_palette::save(): four bytes (RGBA) per colour,
// in palette index order.
[[nodiscard]] QColor ColorOf(const QByteArray &palette, QLatin1String name) {
	const auto index = style::internal::GetPaletteIndex(name);
	if (index < 0 || (index + 1) * 4 > palette.size()) {
		return QColor();
	}
	const auto p = reinterpret_cast<const uchar*>(palette.constData()) + index * 4;
	return QColor(p[0], p[1], p[2], p[3]);
}

[[nodiscard]] bool IsDark(const QByteArray &palette) {
	const auto background = ColorOf(palette, QLatin1String("windowBg"));
	return background.isValid() && (ToLabLch(background).l < 50.);
}

// The full palette in the .tdesktop-palette format, every key listed.
[[nodiscard]] QByteArray BuildPalette(
		const QByteArray &base,
		const Roles &roles,
		bool dark) {
	if (!HasRoles(roles, { u"surface"_q, u"primary"_q })) {
		return {};
	}
	const auto neutral = ToOklch(roles.at(u"surface"_q));
	const auto primary = ToOklch(roles.at(u"primary"_q));
	auto colors = std::map<QString, QColor>();
	for (const auto &row : style::main_palette::data()) {
		const auto color = ColorOf(base, row.name);
		if (color.isValid()) {
			colors[QString(row.name)] = Recolor(color, neutral, primary);
		}
	}
	ApplyRoles(colors, roles, dark);

	auto result = QByteArray();
	result.reserve(int(colors.size()) * 32);
	for (const auto &[key, color] : colors) {
		result += key.toLatin1();
		result += ": #";
		result += QByteArray::number(color.rgb() & 0xFFFFFF, 16)
			.rightJustified(6, '0');
		result += QByteArray::number(color.alpha(), 16).rightJustified(2, '0');
		result += ";\n";
	}
	return result;
}

// ---------------------------------------------------------------------------

class Monet final {
public:
	void start();

private:
	void capture();
	void refresh();
	void restore();
	void watchMatugen();

	QByteArray _base; // the theme's own palette, before Monet
	QByteArray _content; // what Monet applied last
	bool _applied = false;
	bool _working = false;
	base::Timer _timer;
	rpl::lifetime _lifetime;

};

void Monet::start() {
	_timer.setCallback([=] { refresh(); });
	capture();

	// the user picked another theme (or night mode switched it): that theme is
	// the new base, Monet goes on top of it again
	Background()->updates(
	) | rpl::filter([=](const BackgroundUpdate &update) {
		return !_working
			&& update.paletteChanged()
			&& (update.type != BackgroundUpdate::Type::ApplyingEdit);
	}) | rpl::on_next([=] {
		_applied = false;
		_content.clear();
		crl::on_main([=] {
			capture();
			refresh();
		});
	}, _lifetime);

	// the system accent changed
	base::install_event_filter(qApp, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::ApplicationPaletteChange) {
			_timer.callOnce(kRefreshDelay);
		}
		return base::EventFilterResult::Continue;
	});

	watchMatugen();

	// turning Monet on and off (fires right away with the current value)
	Extera::Config::Instance().boolChanges(
		EnabledKey()
	) | rpl::on_next([=] {
		refresh();
	}, _lifetime);
}

void Monet::capture() {
	_base = style::main_palette::save();
}

void Monet::refresh() {
	if (!Extera::Bool(EnabledKey())) {
		restore();
		return;
	}
	if (_base.isEmpty()) {
		return;
	}
	const auto dark = IsDark(_base);
	const auto roles = CurrentRoles(dark);
	if (!roles) {
		restore();
		return;
	}
	const auto content = BuildPalette(_base, *roles, dark);
	if (content.isEmpty() || (_applied && content == _content)) {
		return;
	}
	_working = true;
	if (ApplyEditedPalette(content)) {
		_applied = true;
		_content = content;
	}
	_working = false;
}

void Monet::restore() {
	if (!_applied) {
		return;
	}
	_applied = false;
	_content.clear();
	_working = true;
	if (style::main_palette::load(_base)) {
		Background()->appliedEditedPalette();
	}
	_working = false;
}

void Monet::watchMatugen() {
#if !defined Q_OS_WIN && !defined Q_OS_MAC
	const auto path = MatugenPalettePath();
	if (!QFileInfo::exists(path)) {
		return;
	}
	// a symlink is followed: the real file is what changes
	const auto watcher = new QFileSystemWatcher(qApp);
	const auto rewatch = [=] {
		const auto real = QFileInfo(path).canonicalFilePath();
		if (real.isEmpty()) {
			return;
		}
		const auto directory = QFileInfo(real).absolutePath();
		// matugen replaces the file, so the watch is put back after every change
		if (!watcher->files().contains(real)) {
			watcher->addPath(real);
		}
		if (!watcher->directories().contains(directory)) {
			watcher->addPath(directory);
		}
	};
	const auto changed = [=] {
		rewatch();
		_timer.callOnce(kRefreshDelay);
	};
	QObject::connect(watcher, &QFileSystemWatcher::fileChanged, changed);
	QObject::connect(watcher, &QFileSystemWatcher::directoryChanged, changed);
	rewatch();
#endif // !Q_OS_WIN && !Q_OS_MAC
}

} // namespace

void StartMonet() {
	// lives as long as the process; the theme machinery it listens to does too
	static const auto instance = [] {
		const auto result = new Monet();
		result->start();
		return result;
	}();
	(void)instance;
}

} // namespace Window::Theme
