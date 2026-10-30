#include "extera/extera_app_icon.h"

#include "extera/extera_config.h"
#include "ui/painter.h"

#include <QtCore/QBuffer>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>

#ifdef Q_OS_WIN
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace Extera::AppIcon {
namespace {

[[nodiscard]] QColor Lighter(QColor color, float64 amount) {
	return QColor(
		color.red() + int((255 - color.red()) * amount),
		color.green() + int((255 - color.green()) * amount),
		color.blue() + int((255 - color.blue()) * amount));
}

[[nodiscard]] QColor AccentColor() {
#ifdef Q_OS_WIN
	const auto settings = QSettings(
		u"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\DWM"_q,
		QSettings::NativeFormat);
	const auto value = settings.value(u"AccentColor"_q);
	if (value.isValid()) {
		const auto abgr = value.toUInt();
		return QColor(abgr & 0xFF, (abgr >> 8) & 0xFF, (abgr >> 16) & 0xFF);
	}
#endif
	return QColor(34, 135, 203);
}

[[nodiscard]] const QImage &Glyph() {
	static const auto result = QImage(u":/gui/art/app_icon_glyph.png"_q)
		.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	return result;
}

[[nodiscard]] QString IconPath() {
	return QStandardPaths::writableLocation(
		QStandardPaths::AppDataLocation) + u"/exDesktop.ico"_q;
}

[[nodiscard]] QByteArray IconFile(const QString &id, bool square) {
	const auto sizes = std::vector<int>{ 16, 24, 32, 48, 64, 128, 256 };
	auto images = std::vector<QByteArray>();
	for (const auto size : sizes) {
		auto bytes = QByteArray();
		auto buffer = QBuffer(&bytes);
		buffer.open(QIODevice::WriteOnly);
		Render(size, id, square).save(&buffer, "PNG");
		images.push_back(bytes);
	}
	auto result = QByteArray();
	const auto push16 = [&](int value) {
		result.append(char(value & 0xFF));
		result.append(char((value >> 8) & 0xFF));
	};
	const auto push32 = [&](int value) {
		push16(value & 0xFFFF);
		push16((value >> 16) & 0xFFFF);
	};
	push16(0);
	push16(1);
	push16(int(sizes.size()));
	auto offset = 6 + 16 * int(sizes.size());
	for (auto i = 0; i != int(sizes.size()); ++i) {
		const auto size = sizes[i];
		result.append(char(size >= 256 ? 0 : size));
		result.append(char(size >= 256 ? 0 : size));
		result.append(char(0));
		result.append(char(0));
		push16(1);
		push16(32);
		push32(int(images[i].size()));
		push32(offset);
		offset += int(images[i].size());
	}
	for (const auto &image : images) {
		result.append(image);
	}
	return result;
}

#ifdef Q_OS_WIN
void SetShortcutIcon(const QString &link, const QString &icon) {
	if (!QFile::exists(link)) {
		return;
	}
	const auto hr = CoInitialize(nullptr);
	auto shellLink = (IShellLinkW*)nullptr;
	if (SUCCEEDED(CoCreateInstance(
			CLSID_ShellLink,
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&shellLink)))) {
		auto file = (IPersistFile*)nullptr;
		if (SUCCEEDED(shellLink->QueryInterface(IID_PPV_ARGS(&file)))) {
			const auto path = QDir::toNativeSeparators(link).toStdWString();
			if (SUCCEEDED(file->Load(path.c_str(), STGM_READWRITE))) {
				const auto location = QDir::toNativeSeparators(icon)
					.toStdWString();
				shellLink->SetIconLocation(location.c_str(), 0);
				file->Save(nullptr, TRUE);
			}
			file->Release();
		}
		shellLink->Release();
	}
	if (SUCCEEDED(hr)) {
		CoUninitialize();
	}
}
#endif

} // namespace

std::vector<Variant> Variants() {
	const auto accent = AccentColor();
	return {
		{ QString(), QColor(233, 89, 51), QColor(251, 123, 119), Qt::white },
		{
			u"telegram"_q,
			QColor(34, 135, 203),
			QColor(85, 190, 240),
			Qt::white,
		},
		{ u"accent"_q, accent, Lighter(accent, 0.35), Qt::white },
		{
			u"dark"_q,
			QColor(28, 28, 32),
			QColor(58, 58, 66),
			QColor(251, 124, 116),
		},
		{
			u"light"_q,
			QColor(240, 240, 244),
			QColor(255, 255, 255),
			QColor(233, 89, 51),
		},
	};
}

QImage Render(int size, const QString &id, bool square) {
	const auto variants = Variants();
	const auto i = ranges::find(variants, id, &Variant::id);
	const auto &variant = (i != end(variants)) ? *i : variants.front();

	auto result = QImage(
		QSize(size, size),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);

	auto glyph = Glyph().scaled(
		size,
		size,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	{
		auto p = QPainter(&glyph);
		p.setCompositionMode(QPainter::CompositionMode_SourceIn);
		p.fillRect(glyph.rect(), variant.fg);
	}

	auto p = QPainter(&result);
	auto hq = PainterHighQualityEnabler(p);
	auto gradient = QLinearGradient(0, size, size, 0);
	gradient.setColorAt(0., variant.from);
	gradient.setColorAt(1., variant.to);
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	const auto rect = QRectF(0, 0, size, size);
	if (square) {
		const auto radius = size * 0.23;
		p.drawRoundedRect(rect, radius, radius);
	} else {
		p.drawEllipse(rect);
	}
	p.drawImage(0, 0, glyph);
	return result;
}

QImage Current(int size) {
	return Render(
		size,
		Extera::String(u"app_icon"_q),
		Extera::Bool(u"app_icon_square"_q));
}

void ApplyToShortcuts() {
#ifdef Q_OS_WIN
	const auto id = Extera::String(u"app_icon"_q);
	const auto square = Extera::Bool(u"app_icon_square"_q);
	auto icon = QCoreApplication::applicationFilePath();
	if (!id.isEmpty() || square) {
		const auto path = IconPath();
		QDir().mkpath(QFileInfo(path).absolutePath());
		auto file = QFile(path);
		if (file.open(QIODevice::WriteOnly)) {
			file.write(IconFile(id, square));
			file.close();
			icon = path;
		}
	}
	for (const auto location : {
			QStandardPaths::ApplicationsLocation,
			QStandardPaths::DesktopLocation }) {
		for (const auto &folder : QStandardPaths::standardLocations(
				location)) {
			SetShortcutIcon(folder + u"/exDesktop.lnk"_q, icon);
			SetShortcutIcon(folder + u"/exDesktop/exDesktop.lnk"_q, icon);
		}
	}
	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
#endif
}

} // namespace Extera::AppIcon
