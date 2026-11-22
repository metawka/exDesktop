#include "extera/exd_message_shot.h"

#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "dialogs/ui/dialogs_video_userpic.h"
#include "extera/extera_config.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "settings/settings_common.h"
#include "ui/boxes/single_choice_box.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_themes_embedded.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtWidgets/QFileDialog>

namespace Exd::MessageShot {
namespace {

class Delegate final : public HistoryView::DefaultElementDelegate {
public:
	Delegate(
		not_null<QWidget*> parent,
		not_null<Ui::ChatStyle*> st,
		Fn<void()> update)
	: _parent(parent)
	, _pathGradient(HistoryView::MakePathShiftGradient(st, update)) {
	}

	bool elementAnimationsPaused() override {
		return true;
	}
	not_null<Ui::PathShiftGradient*> elementPathShiftGradient() override {
		return _pathGradient.get();
	}
	HistoryView::Context elementContext() override {
		return HistoryView::Context::AdminLog;
	}
	HistoryView::ElementChatMode elementChatMode() override {
		return HistoryView::ElementChatMode::Wide;
	}

private:
	const not_null<QWidget*> _parent;
	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;

};

[[nodiscard]] QImage Trim(const QImage &original) {
	if (original.isNull()) {
		return {};
	}
	auto minX = original.width();
	auto minY = original.height();
	auto maxX = -1;
	auto maxY = -1;
	for (auto y = 0; y != original.height(); ++y) {
		const auto line = reinterpret_cast<const QRgb*>(
			original.constScanLine(y));
		for (auto x = 0; x != original.width(); ++x) {
			if (qAlpha(line[x]) != 0) {
				minX = std::min(minX, x);
				maxX = std::max(maxX, x);
				minY = std::min(minY, y);
				maxY = std::max(maxY, y);
			}
		}
	}
	if (maxX < 0) {
		return {};
	}
	auto result = original.copy(minX, minY, maxX - minX + 1, maxY - minY + 1);
	result.setDevicePixelRatio(original.devicePixelRatio());
	return result;
}

struct ActiveShot {
	const void *delegate = nullptr;
	bool date = true;
	bool reactions = true;
	bool badges = true;
	bool spoilers = false;
};

ActiveShot Active;

struct ThemeOption {
	QString name;
	std::shared_ptr<style::palette> palette;
	QColor background;
};

struct ShotState {
	std::vector<not_null<HistoryItem*>> items;
	std::vector<ThemeOption> themes;
	int theme = 0;
	std::shared_ptr<Ui::ChatStyle> st;
	std::unique_ptr<Delegate> delegate;
	std::vector<std::unique_ptr<HistoryView::Element>> views;
	std::vector<std::shared_ptr<Data::PhotoMedia>> photos;
	std::vector<std::shared_ptr<Data::DocumentMedia>> documents;
	base::flat_map<not_null<PeerData*>, Ui::PeerUserpicView> userpics;
	rpl::variable<QString> themeName;
	QImage image;
	bool background = true;
	bool date = true;
	bool reactions = true;
	bool badges = true;
	bool spoilers = false;
};

[[nodiscard]] std::vector<ThemeOption> CollectThemes() {
	using namespace Window::Theme;
	auto result = std::vector<ThemeOption>();
	result.push_back({
		.name = tr::lng_exd_shot_theme_current(tr::now),
		.background = anim::color(st::windowBg->c, st::windowFg->c, 0.07),
	});
	for (const auto &scheme : EmbeddedThemes()) {
		auto palette = std::shared_ptr<style::palette>(
			PrepareEmbeddedPalette(scheme.type, std::nullopt));
		if (!palette) {
			continue;
		}
		const auto name = [&] {
			switch (scheme.type) {
			case EmbeddedType::DayBlue: return tr::lng_settings_theme_day(tr::now);
			case EmbeddedType::Default: return tr::lng_settings_theme_classic(tr::now);
			case EmbeddedType::Night: return tr::lng_settings_theme_tinted(tr::now);
			case EmbeddedType::NightGreen: return tr::lng_settings_theme_night(tr::now);
			}
			return QString();
		}();
		result.push_back({
			.name = name,
			.palette = std::move(palette),
			.background = scheme.background,
		});
	}
	return result;
}

void Rebuild(
		not_null<QWidget*> preview,
		not_null<Window::SessionController*> controller,
		not_null<ShotState*> state) {
	const auto &theme = state->themes[state->theme];
	state->views.clear();
	state->delegate = nullptr;
	state->st = theme.palette
		? std::make_shared<Ui::ChatStyle>(theme.palette.get())
		: std::make_shared<Ui::ChatStyle>(controller->chatStyle());
	state->delegate = std::make_unique<Delegate>(
		preview,
		state->st.get(),
		[=] { preview->update(); });
	Active = ActiveShot{
		.delegate = state->delegate.get(),
		.date = state->date,
		.reactions = state->reactions,
		.badges = state->badges,
		.spoilers = state->spoilers,
	};
	for (const auto item : state->items) {
		state->views.push_back(item->createView(state->delegate.get()));
	}
	for (auto i = 0; i != int(state->views.size()); ++i) {
		const auto view = state->views[i].get();
		const auto previous = (i > 0) ? state->views[i - 1].get() : nullptr;
		if (previous) {
			view->setDisplayDate(state->date
				&& (view->dateTime().date() != previous->dateTime().date()));
			const auto attached = view->computeIsAttachToPrevious(previous);
			view->setAttachToPrevious(attached, previous);
			previous->setAttachToNext(attached, view);
		} else {
			view->setDisplayDate(false);
			view->setAttachToPrevious(false);
		}
	}
	state->views.back()->setAttachToNext(false);
}

[[nodiscard]] QImage Render(
		not_null<Window::SessionController*> controller,
		not_null<ShotState*> state) {
	const auto ratio = style::DevicePixelRatio();
	const auto width = st::msgMaxWidth
		+ st::msgPhotoSkip
		+ st::boxPadding.left()
		+ st::boxPadding.right();
	auto height = 0;
	for (const auto &view : state->views) {
		view->itemDataChanged();
		height += view->resizeGetHeight(width);
	}
	if (height <= 0) {
		return {};
	}
	auto image = QImage(
		QSize(width, height) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	{
		auto p = Painter(&image);
		const auto viewport = QRect(0, 0, width, height);
		auto y = 0;
		for (const auto &view : state->views) {
			const auto item = view->data();
			const auto rect = QRect(0, 0, width, view->height());
			auto context = controller->defaultChatTheme()->preparePaintContext(
				state->st.get(),
				viewport,
				rect,
				rect,
				true);
			p.translate(0, y);
			view->draw(p, context);
			p.translate(0, -y);
			if (view->displayFromPhoto() || item->isPost()) {
				const auto x = st::msgMargin.left();
				const auto top = y + view->height() - st::msgPhotoSize;
				if (const auto from = item->displayFrom()) {
					Dialogs::Ui::PaintUserpic(
						p,
						from,
						nullptr,
						state->userpics[from],
						x,
						top,
						width,
						st::msgPhotoSize,
						true);
				} else if (const auto info = item->displayHiddenSenderInfo()) {
					if (info->customUserpic.empty()) {
						info->emptyUserpic.paintCircle(
							p,
							x,
							top,
							width,
							st::msgPhotoSize);
					}
				}
			}
			y += view->height();
		}
	}
	auto trimmed = Trim(image);
	if (trimmed.isNull()) {
		return {};
	}
	const auto padding = st::boxPadding.left();
	auto result = QImage(
		trimmed.size() + QSize(padding * 2, padding * 2) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	if (state->background) {
		result.fill(state->themes[state->theme].background);
	} else {
		result.fill(Qt::transparent);
	}
	{
		auto p = QPainter(&result);
		p.drawImage(padding, padding, trimmed);
	}
	return result;
}

} // namespace

bool Hidden(not_null<const HistoryView::Element*> view, Part part) {
	if (!Active.delegate || view->delegate().get() != Active.delegate) {
		return false;
	}
	switch (part) {
	case Part::Date: return !Active.date;
	case Part::Reactions: return !Active.reactions;
	case Part::Badges: return !Active.badges;
	case Part::Spoilers: return Active.spoilers;
	}
	return false;
}

void Show(
		not_null<Window::SessionController*> controller,
		MessageIdsList ids) {
	auto items = std::vector<not_null<HistoryItem*>>();
	const auto owner = &controller->session().data();
	for (const auto &id : ids) {
		if (const auto item = owner->message(id)) {
			items.push_back(item);
		}
	}
	if (items.empty()) {
		return;
	}
	ranges::sort(items, [](not_null<HistoryItem*> a, not_null<HistoryItem*> b) {
		return a->id < b->id;
	});
	auto hasReactions = false;
	auto hasBadges = false;
	auto hasSpoilers = false;
	for (const auto item : items) {
		if (!item->reactions().empty()) {
			hasReactions = true;
		}
		if (!item->fromRank().isEmpty()
			|| item->boostsApplied() > 0
			|| item->Get<HistoryMessageSigned>()
			|| item->isDiscussionPost()) {
			hasBadges = true;
		}
		for (const auto &entity : item->originalText().entities) {
			if (entity.type() == EntityType::Spoiler) {
				hasSpoilers = true;
				break;
			}
		}
	}
	if (!hasBadges) {
		for (const auto item : items) {
			if (item->history()->peer->isMegagroup()
				|| item->history()->peer->isChat()) {
				hasBadges = true;
				break;
			}
		}
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_exd_shot_title());
		box->setWidth(st::boxWideWidth);
		const auto state = box->lifetime().make_state<ShotState>();
		box->lifetime().add([] { Active = ActiveShot(); });
		state->items = items;
		state->themes = CollectThemes();
		state->theme = std::clamp(
			Extera::Int(u"exd_shot_theme"_q),
			0,
			int(state->themes.size()) - 1);
		state->themeName = state->themes[state->theme].name;
		state->background = Extera::Bool(u"exd_shot_background"_q);
		state->date = !Extera::Bool(u"exd_shot_hide_date"_q);
		state->reactions = !Extera::Bool(u"exd_shot_hide_reactions"_q);
		state->badges = !Extera::Bool(u"exd_shot_hide_badges"_q);
		state->spoilers = Extera::Bool(u"exd_shot_spoilers"_q);

		const auto content = box->verticalLayout();
		Ui::AddSubsectionTitle(content, tr::lng_exd_shot_preview());
		const auto preview = box->addRow(
			object_ptr<Ui::RpWidget>(box),
			st::boxRowPadding);
		for (const auto item : items) {
			if (const auto media = item->media()) {
				const auto origin = Data::FileOrigin(item->fullId());
				if (const auto photo = media->photo()) {
					auto view = photo->createMediaView();
					photo->load(origin, LoadFromCloudOrLocal, false);
					state->photos.push_back(std::move(view));
				} else if (const auto document = media->document()) {
					auto view = document->createMediaView();
					document->loadThumbnail(origin);
					state->documents.push_back(std::move(view));
				}
			}
		}

		const auto rerender = [=] {
			state->image = Render(controller, state);
			const auto available = st::boxWideWidth
				- st::boxRowPadding.left()
				- st::boxRowPadding.right();
			const auto size = state->image.size()
				/ std::max(state->image.devicePixelRatio(), 1.);
			const auto scaled = (size.width() > available)
				? QSize(available, size.height() * available / std::max(size.width(), 1))
				: size;
			preview->resize(available, std::min(scaled.height(), st::boxMaxListHeight));
			preview->update();
		};
		const auto rebuild = [=] {
			Rebuild(preview, controller, state);
			rerender();
		};
		preview->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(preview);
			auto hq = PainterHighQualityEnabler(p);
			if (state->image.isNull()) {
				return;
			}
			const auto size = state->image.size()
				/ state->image.devicePixelRatio();
			auto target = QRect(
				QPoint(),
				size.scaled(preview->size(), Qt::KeepAspectRatio));
			if (target.width() > size.width()) {
				target.setSize(size);
			}
			target.moveCenter(preview->rect().center());
			p.drawImage(target, state->image);
		}, preview->lifetime());

		Ui::AddSkip(content);
		Ui::AddDivider(content);
		Ui::AddSkip(content);
		Ui::AddSubsectionTitle(content, tr::lng_exd_shot_options());

		Settings::AddButtonWithLabel(
			content,
			tr::lng_exd_shot_theme(),
			state->themeName.value(),
			st::settingsButtonNoIcon
		)->addClickHandler([=] {
			auto options = std::vector<QString>();
			for (const auto &theme : state->themes) {
				options.push_back(theme.name);
			}
			box->uiShow()->showBox(Box([=](not_null<Ui::GenericBox*> choice) {
				SingleChoiceBox(choice, {
					.title = tr::lng_exd_shot_theme(),
					.options = options,
					.initialSelection = state->theme,
					.callback = [=](int value) {
						state->theme = value;
						state->themeName = state->themes[value].name;
						Extera::Config::Instance().setInt(
							u"exd_shot_theme"_q,
							value);
						rebuild();
					},
				});
			}), Ui::LayerOption::KeepOther);
		});

		const auto addToggle = [&](
				rpl::producer<QString> text,
				bool initial,
				Fn<void(bool)> changed) {
			const auto button = content->add(
				object_ptr<Ui::SettingsButton>(
					content,
					std::move(text),
					st::settingsButtonNoIcon));
			button->toggleOn(rpl::single(initial));
			button->toggledChanges(
			) | rpl::on_next(changed, button->lifetime());
		};
		const auto setFlag = [](const QString &key, bool value) {
			Extera::Config::Instance().setBool(key, value);
		};
		addToggle(tr::lng_exd_shot_background(), state->background, [=](bool v) {
			state->background = v;
			setFlag(u"exd_shot_background"_q, v);
			rerender();
		});
		addToggle(tr::lng_exd_shot_date(), state->date, [=](bool v) {
			state->date = v;
			setFlag(u"exd_shot_hide_date"_q, !v);
			rebuild();
		});
		if (hasReactions) {
			addToggle(tr::lng_exd_shot_reactions(), state->reactions, [=](bool v) {
				state->reactions = v;
				setFlag(u"exd_shot_hide_reactions"_q, !v);
				rebuild();
			});
		}
		if (hasBadges) {
			addToggle(tr::lng_exd_shot_badges(), state->badges, [=](bool v) {
				state->badges = v;
				setFlag(u"exd_shot_hide_badges"_q, !v);
				rebuild();
			});
		}
		if (hasSpoilers) {
			addToggle(tr::lng_exd_shot_spoilers(), state->spoilers, [=](bool v) {
				state->spoilers = v;
				setFlag(u"exd_shot_spoilers"_q, v);
				rebuild();
			});
		}
		Ui::AddSkip(content);

		controller->session().downloaderTaskFinished(
		) | rpl::on_next(rerender, box->lifetime());

		box->addButton(tr::lng_exd_shot_copy(), [=] {
			if (!state->image.isNull()) {
				QGuiApplication::clipboard()->setImage(state->image);
				controller->showToast(tr::lng_exd_shot_copied(tr::now));
			}
			box->closeBox();
		});
		box->addButton(tr::lng_exd_shot_save(), [=] {
			if (state->image.isNull()) {
				return;
			}
			const auto path = QFileDialog::getSaveFileName(
				box,
				tr::lng_save_file(tr::now),
				u"exDesktop_shot.png"_q,
				u"PNG (*.png)"_q);
			if (!path.isEmpty()) {
				state->image.save(path, "PNG");
				box->closeBox();
			}
		});
		box->addLeftButton(tr::lng_cancel(), [=] { box->closeBox(); });
		rebuild();
	}));
}

} // namespace Exd::MessageShot
