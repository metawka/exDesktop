#include "extera/extera_pdf_viewer.h"

#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

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
		std::shared_ptr<std::atomic<bool>> cancelled,
		Fn<void(int count)> counted,
		Fn<void(Page page)> rendered) {
	crl::async([=] {
		auto uninit = false;
		base::WinRT::Try([&] {
			winrt::init_apartment();
			uninit = true;
		});
		const auto uninitGuard = gsl::finally([&] {
			if (uninit) {
				base::WinRT::Try([] { winrt::uninit_apartment(); });
			}
		});
		if (cancelled && cancelled->load(std::memory_order_relaxed)) {
			return;
		}
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
		if (cancelled && cancelled->load(std::memory_order_relaxed)) {
			return;
		}
		const auto count = document
			? std::min(int(document->PageCount()), kMaxPages)
			: 0;
		crl::on_main([=] { counted(count); });
		if (!document || count <= 0) {
			return;
		}
		for (auto i = 0; i != count; ++i) {
			if (cancelled && cancelled->load(std::memory_order_relaxed)) {
				break;
			}
			auto image = QImage();
			base::WinRT::Try([&] {
				const auto page = document->GetPage(uint32_t(i));
				auto stream = InMemoryRandomAccessStream();
				auto options = PdfPageRenderOptions();
				options.DestinationWidth(uint32_t(width));
				page.RenderToStreamAsync(stream, options).get();
				const auto size = uint32_t(stream.Size());
				if (!size) {
					return;
				}
				auto reader = DataReader(stream.GetInputStreamAt(0));
				reader.LoadAsync(size).get();
				auto bytes = std::vector<uint8_t>(size);
				reader.ReadBytes(bytes);
				image = QImage::fromData(
					reinterpret_cast<const uchar*>(bytes.data()),
					int(bytes.size()));
			});
			if (cancelled && cancelled->load(std::memory_order_relaxed)) {
				break;
			}
			crl::on_main([=] { rendered({ i, image }); });
		}
	});
}
#endif

} // namespace

bool Supported() {
#ifdef Q_OS_WIN
	return base::WinRT::Supported();
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
		box->setTitle(rpl::single(title));
		box->setWidth(st::boxWideWidth * 3 / 2);
		box->setMaxHeight(st::boxWideWidth * 2);
		box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });
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
		const auto pageWidth = st::boxWideWidth * 3 / 2
			- st::boxRowPadding.left()
			- st::boxRowPadding.right();
		const auto ratio = style::DevicePixelRatio();
		const auto guard = base::make_weak(box.get());
		const auto cancelled = std::make_shared<std::atomic<bool>>(false);
		box->lifetime().add([=] {
			cancelled->store(true, std::memory_order_relaxed);
		});
		Render(path, pageWidth * ratio, cancelled, [=](int count) {
			if (!guard) {
				return;
			}
			if (!count) {
				status->setText(tr::lng_exd_pdf_failed(tr::now));
				return;
			}
			status->hide();
			state->images.resize(count);
			for (auto i = 0; i != count; ++i) {
				const auto page = content->add(
					object_ptr<Ui::RpWidget>(content),
					st::boxRowPadding + QMargins(0, 0, 0, st::boxRowPadding.left() / 2));
				page->resize(pageWidth, int(pageWidth * 1.414));
				page->paintRequest() | rpl::on_next([=] {
					auto p = QPainter(page);
					const auto &image = state->images[i];
					if (image.isNull()) {
						p.fillRect(page->rect(), st::boxDividerBg);
						return;
					}
					auto hq = PainterHighQualityEnabler(p);
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
		box->addButton(tr::lng_exd_pdf_open_external(), [=] {
			File::Launch(path);
			box->closeBox();
		});
	}));
#else
	File::Launch(path);
#endif
}

} // namespace Extera::PdfViewer
