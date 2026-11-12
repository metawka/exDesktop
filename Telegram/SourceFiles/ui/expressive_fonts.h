/*
exDesktop: fonts bundled for the Material 3 Expressive parts of the interface.
Licensed under GPLv3, like the rest of the project.
*/
#pragma once

#include <QtGui/QFont>

namespace Ui::ExpressiveFonts {

// "Unbounded" (variable weight), bundled, so it looks the same on every system.
[[nodiscard]] QFont Unbounded(int pixelSize, QFont::Weight weight);

// Material Symbols glyphs (a bundled subset, family "exDesktop Symbols").
inline constexpr auto kPasskey = char16_t(0xF87F);
inline constexpr auto kBluetooth = char16_t(0xE1A7);
inline constexpr auto kBluetoothOff = char16_t(0xE1A9);
inline constexpr auto kError = char16_t(0xF8B6);
inline constexpr auto kKey = char16_t(0xE73C);
inline constexpr auto kClose = char16_t(0xE5CD);
inline constexpr auto kCheck = char16_t(0xE5CA);

[[nodiscard]] QFont Symbols(double pixelSize, bool filled);

} // namespace Ui::ExpressiveFonts
