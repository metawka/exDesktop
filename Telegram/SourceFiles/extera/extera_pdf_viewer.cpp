#include "extera/extera_pdf_viewer.h"

#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/abstract_button.h"
#include "ui/ui_utility.h"
#include "ui/style/style_core.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"

#include <QtGui/QPainterPath>

#ifdef Q_OS_WIN
#include "base/platform/win/base_windows_winrt.h"

#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#endif

namespace Extera::PdfViewer {
namespace {

constexpr auto kMaxPages = 200;

struct Page {
	int index = 0;
	QImage image;
};

#ifdef Q_OS_WIN
void Render(
		const QString &path,
		int width,
		Fn<void(int count)> counted,
		Fn<void(Page page)> rendered) {
	crl::async([=] {
		base::WinRT::Try([] { winrt::init_apartment(); });
		using namespace winrt::Windows::Data::Pdf;
		using namespace winrt::Windows::Storage;
		using namespace winrt::Windows::Storage::Streams;
		const auto native = QDir::toNativeSeparators(path).toStdWString();
		auto document = std::optional<PdfDocument>();
		base::WinRT::Try([&] {
			const auto file = StorageFile::GetFileFromPathAsync(
				winrt::hstring(native)).get();
			document = PdfDocument::LoadFromFileAsync(file).get();
		});
		const auto count = document
			? std::min(int(document->PageCount()), kMaxPages)
			: 0;
		crl::on_main([=] { counted(count); });
		for (auto i = 0; i != count; ++i) {
			auto image = QImage();
			base::WinRT::Try([&] {
				const auto page = document->GetPage(uint32_t(i));
				auto stream = InMemoryRandomAccessStream();
				auto options = PdfPageRenderOptions();
				options.DestinationWidth(uint32_t(width));
				page.RenderToStreamAsync(stream, options).get();
				const auto size = uint32_t(stream.Size());
				auto reader = DataReader(stream.GetInputStreamAt(0));
				reader.LoadAsync(size).get();
				auto bytes = std::vector<uint8_t>(size);
				reader.ReadBytes(bytes);
				image = QImage::fromData(
					reinterpret_cast<const uchar*>(bytes.data()),
					int(bytes.size()));
			});
			crl::on_main([=] { rendered({ i, image }); });
		}
	});
}
#endif

[[nodiscard]] QColor Mix(QColor a, QColor b, float64 k) {
	return QColor(
		anim::interpolate(a.red(), b.red(), k),
		anim::interpolate(a.green(), b.green(), k),
		anim::interpolate(a.blue(), b.blue(), k));
}

[[nodiscard]] QColor CardColor() {
	return Mix(st::windowBg->c, st::windowFg->c, 0.055);
}

[[nodiscard]] QColor TonalColor(bool over) {
	return Mix(st::windowBg->c, st::windowBgActive->c, over ? 0.3 : 0.22);
}

void SetupHoverRepaint(not_null<Ui::AbstractButton*> button) {
	button->setCursor(style::cur_pointer);
	button->events() | rpl::filter([](not_null<QEvent*> e) {
		return (e->type() == QEvent::Enter) || (e->type() == QEvent::Leave);
	}) | rpl::on_next([=] {
		crl::on_main(button, [=] { button->update(); });
	}, button->lifetime());
}

not_null<Ui::RpWidget*> AddMd3Header(
		not_null<Ui::GenericBox*> box,
		const QString &title,
		rpl::producer<int> pages,
		Fn<void()> openExternal) {
	const auto margin = st::exdPdfHeaderMargin;
	const auto height = st::exdPdfHeaderHeight;
	const auto header = box->setPinnedToTopContent(
		object_ptr<Ui::RpWidget>(box));
	header->resize(st::boxWideWidth * 3 / 2, height + margin * 2);
	struct State {
		int pages = 0;
	};
	const auto state = header->lifetime().make_state<State>();
	std::move(pages) | rpl::on_next([=](int count) {
		state->pages = count;
		header->update();
	}, header->lifetime());

	const auto close = Ui::CreateChild<Ui::AbstractButton>(header);
	close->resize(st::exdPdfCloseSize, st::exdPdfCloseSize);
	close->setAccessibleName(tr::lng_close(tr::now));
	close->setClickedCallback([=] { box->closeBox(); });
	SetupHoverRepaint(close);
	close->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(close);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(Mix(
			st::windowBg->c,
			st::windowFg->c,
			close->isOver() ? 0.14 : 0.08));
		p.drawEllipse(close->rect());
		auto pen = QPen(st::windowFg->c);
		pen.setWidthF(style::ConvertScaleExact(2.));
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);
		const auto c = QRectF(close->rect()).center();
		const auto d = style::ConvertScaleExact(6.);
		p.drawLine(c + QPointF(-d, -d), c + QPointF(d, d));
		p.drawLine(c + QPointF(d, -d), c + QPointF(-d, d));
	}, close->lifetime());

	const auto open = Ui::CreateChild<Ui::AbstractButton>(header);
	const auto openText = tr::lng_exd_pdf_open_external(tr::now);
	const auto &font = st::semiboldFont;
	const auto &openIcon = st::menuIconNewWindow;
	const auto openPadding = st::exdPdfButtonPadding;
	open->resize(
		openPadding + openIcon.width() + st::exdPdfButtonIconSkip
			+ font->width(openText) + openPadding,
		st::exdPdfButtonHeight);
	open->setAccessibleName(openText);
	open->setClickedCallback(openExternal);
	SetupHoverRepaint(open);
	open->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(open);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(TonalColor(open->isOver()));
		const auto radius = open->height() / 2.;
		p.drawRoundedRect(open->rect(), radius, radius);
		openIcon.paint(
			p,
			openPadding,
			(open->height() - openIcon.height()) / 2,
			open->width(),
			st::windowBgActive->c);
		p.setPen(st::windowBgActive);
		p.setFont(font);
		p.drawText(
			openPadding + openIcon.width() + st::exdPdfButtonIconSkip,
			(open->height() - font->height) / 2 + font->ascent,
			openText);
	}, open->lifetime());

	header->widthValue() | rpl::on_next([=](int width) {
		const auto plaque = QRect(margin, margin, width - margin * 2, height);
		close->moveToLeft(
			plaque.x() + plaque.width() - st::exdPdfInnerSkip - close->width(),
			plaque.y() + (plaque.height() - close->height()) / 2);
		open->moveToLeft(
			close->x() - st::exdPdfInnerSkip - open->width(),
			plaque.y() + (plaque.height() - open->height()) / 2);
	}, header->lifetime());

	header->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(header);
		auto hq = PainterHighQualityEnabler(p);
		const auto plaque = QRect(
			margin,
			margin,
			header->width() - margin * 2,
			height);
		p.setPen(Qt::NoPen);
		p.setBrush(CardColor());
		p.drawRoundedRect(
			plaque,
			st::exdPdfHeaderRadius,
			st::exdPdfHeaderRadius);
		const auto tile = st::exdPdfTileSize;
		const auto tileRect = QRect(
			plaque.x() + st::exdPdfInnerSkip + st::exdPdfInnerSkip / 2,
			plaque.y() + (plaque.height() - tile) / 2,
			tile,
			tile);
		p.setBrush(TonalColor(false));
		p.drawRoundedRect(tileRect, st::exdPdfTileRadius, st::exdPdfTileRadius);
		st::menuIconFile.paintInCenter(p, tileRect, st::windowBgActive->c);
		const auto textLeft = tileRect.x() + tile + st::exdPdfInnerSkip;
		const auto textWidth = std::max(open->x() - st::exdPdfInnerSkip - textLeft, 0);
		const auto &sub = st::normalFont;
		const auto textTop = plaque.y()
			+ (plaque.height() - font->height - sub->height) / 2;
		p.setPen(st::windowBoldFg);
		p.setFont(font);
		p.drawText(
			textLeft,
			textTop + font->ascent,
			font->elided(title, textWidth));
		if (state->pages > 0) {
			p.setPen(st::windowSubTextFg);
			p.setFont(sub);
			p.drawText(
				textLeft,
				textTop + font->height + sub->ascent,
				sub->elided(
					tr::lng_exd_pdf_pages(
						tr::now,
						lt_amount,
						QString::number(state->pages)),
					textWidth));
		}
	}, header->lifetime());
	return header;
}

} // namespace

bool Supported() {
#ifdef Q_OS_WIN
	return true;
#else
	return false;
#endif
}

void Show(
		not_null<Window::SessionController*> controller,
		const QString &path,
		const QString &title) {
#ifdef Q_OS_WIN
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto md3 = style::Md3Enabled();
		const auto pagesCount = box->lifetime().make_state<
			rpl::variable<int>>(0);
		box->setWidth(st::boxWideWidth * 3 / 2);
		box->setMaxHeight(st::boxWideWidth * 2);
		if (md3) {
			box->setStyle(st::exdPdfBox);
			box->setNoContentMargin(true);
			AddMd3Header(box, title, pagesCount->value(), [=] {
				File::Launch(path);
				box->closeBox();
			});
		} else {
			box->setTitle(rpl::single(title));
			box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });
		}
		const auto content = box->verticalLayout();
		const auto status = content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_exd_pdf_loading(),
				st::boxLabel),
			st::boxRowPadding);
		struct State {
			std::vector<Ui::RpWidget*> pages;
			std::vector<QImage> images;
		};
		const auto state = box->lifetime().make_state<State>();
		const auto boxWidth = st::boxWideWidth * 3 / 2;
		const auto pageWidth = md3
			? (boxWidth * 62 / 100)
			: (boxWidth
				- st::boxRowPadding.left()
				- st::boxRowPadding.right());
		const auto pageMargins = md3
			? QMargins(
				(boxWidth - pageWidth) / 2,
				0,
				boxWidth - pageWidth - (boxWidth - pageWidth) / 2,
				st::exdPdfPageSkip)
			: (st::boxRowPadding
				+ QMargins(0, 0, 0, st::boxRowPadding.left() / 2));
		const auto ratio = style::DevicePixelRatio();
		const auto guard = base::make_weak(box.get());
		Render(path, pageWidth * ratio, [=](int count) {
			if (!guard) {
				return;
			}
			if (!count) {
				status->setText(tr::lng_exd_pdf_failed(tr::now));
				return;
			}
			status->hide();
			*pagesCount = count;
			state->images.resize(count);
			for (auto i = 0; i != count; ++i) {
				const auto page = content->add(
					object_ptr<Ui::RpWidget>(content),
					pageMargins);
				page->resize(pageWidth, int(pageWidth * 1.414));
				page->paintRequest() | rpl::on_next([=] {
					auto p = QPainter(page);
					auto hq = PainterHighQualityEnabler(p);
					if (md3) {
						auto clip = QPainterPath();
						const auto radius = st::exdPdfPageRadius;
						clip.addRoundedRect(QRectF(page->rect()), radius, radius);
						p.setClipPath(clip);
					}
					const auto &image = state->images[i];
					if (image.isNull()) {
						p.fillRect(
							page->rect(),
							md3 ? QBrush(CardColor()) : st::boxDividerBg->b);
						return;
					}
					p.drawImage(page->rect(), image);
				}, page->lifetime());
				state->pages.push_back(page);
			}
		}, [=](Page page) {
			if (!guard || page.index >= int(state->pages.size())) {
				return;
			}
			const auto widget = state->pages[page.index];
			if (!page.image.isNull()) {
				widget->resize(
					pageWidth,
					pageWidth * page.image.height()
						/ std::max(page.image.width(), 1));
			}
			state->images[page.index] = std::move(page.image);
			widget->update();
		});
		if (md3) {
			const auto counter = Ui::CreateChild<Ui::RpWidget>(box.get());
			counter->setAttribute(Qt::WA_TransparentForMouseEvents);
			const auto current = counter->lifetime().make_state<int>(0);
			const auto updateCounter = [=] {
				const auto count = int(state->pages.size());
				if (!count) {
					counter->hide();
					return;
				}
				const auto middle = box->scrollTop() + box->scrollHeight() / 2;
				auto index = 0;
				for (auto i = 0; i != count; ++i) {
					if (state->pages[i]->y() <= middle) {
						index = i;
					}
				}
				*current = index + 1;
				const auto text = QString::number(*current)
					+ u" / "_q
					+ QString::number(count);
				const auto &font = st::semiboldFont;
				counter->resize(
					font->width(text) + st::exdPdfCounterPadding * 2,
					st::exdPdfCounterHeight);
				counter->moveToLeft(
					(box->width() - counter->width()) / 2,
					box->height() - st::exdPdfCounterSkip - counter->height());
				counter->show();
				counter->raise();
				counter->update();
			};
			counter->paintRequest() | rpl::on_next([=] {
				auto p = QPainter(counter);
				auto hq = PainterHighQualityEnabler(p);
				p.setPen(Qt::NoPen);
				p.setBrush(Mix(st::windowBg->c, st::windowFg->c, 0.08));
				const auto radius = counter->height() / 2.;
				p.drawRoundedRect(counter->rect(), radius, radius);
				p.setPen(st::windowFg);
				p.setFont(st::semiboldFont);
				p.drawText(
					counter->rect(),
					Qt::AlignCenter,
					QString::number(*current)
						+ u" / "_q
						+ QString::number(int(state->pages.size())));
			}, counter->lifetime());
			counter->hide();
			box->scrolls() | rpl::on_next(updateCounter, counter->lifetime());
			box->sizeValue() | rpl::on_next([=] {
				updateCounter();
			}, counter->lifetime());
			pagesCount->value() | rpl::on_next([=] {
				crl::on_main(counter, updateCounter);
			}, counter->lifetime());
		} else {
			box->addButton(tr::lng_exd_pdf_open_external(), [=] {
				File::Launch(path);
				box->closeBox();
			});
		}
	}));
#else
	File::Launch(path);
#endif
}

} // namespace Extera::PdfViewer
