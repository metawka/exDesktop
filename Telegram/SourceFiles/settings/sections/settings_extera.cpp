#include "settings/sections/settings_extera.h"

#include <QtCore/QRegularExpression>

#include "ui/search_field_controller.h"

#include "core/application.h"
#include "core/file_utilities.h"
#include "core/version.h"
#include "window/notifications_manager.h"
#include "extera/extera_app_icon.h"
#include "ui/new_badges.h"
#include "ui/widgets/popup_menu.h"
#include "base/event_filter.h"
#include "extera/extera_ws_proxy.h"
#include "ui/style/style_core_font.h"
#include "core/core_settings.h"
#include "ui/boxes/choose_language_box.h"
#include "boxes/translate_box.h"
#include "boxes/download_path_box.h"
#include "extera/extera_config.h"
#include "extera/extera_plugin_trust.h"
#include "extera/extera_badges.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "extera/extera_menu.h"
#include "history/view/media/history_view_sticker.h"
#include "ui/filter_icons.h"
#include "extera/extera_navigation.h"
#include "extera/extera_streamer.h"
#include "extera/extera_ai_chat.h"
#include "extera/extera_welcome.h"
#include "extera/extera_changelog.h"
#include "extera/extera_plugins_ui.h"
#include "extera/extera_plugins.h"
#include "extera/extera_update.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "spellcheck/spellcheck_types.h"
#include "settings/settings_common.h"
#include "settings/settings_common_session.h"
#include "ui/basic_click_handlers.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "core/ui_integration.h"
#include "data/stickers/data_custom_emoji.h"
#include "ui/userpic_view.h"
#include "ui/painter.h"
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtSvg/QSvgRenderer>
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/vertical_layout_reorder.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "window/main_window.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtGui/QMouseEvent>
#include "styles/style_edit_peer_members.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"

not_null<Ui::RpWidget*> AddInnerToggle(
	not_null<Ui::VerticalLayout*> container,
	const style::SettingsButton &st,
	std::vector<not_null<Ui::AbstractCheckView*>> innerCheckViews,
	not_null<Ui::SlideWrap<>*> wrap,
	rpl::producer<QString> buttonLabel,
	std::optional<QString> locked,
	Settings::IconDescriptor &&icon);

namespace Settings {
namespace {

using namespace Builder;

using Phrase = tr::phrase<>;

[[nodiscard]] Extera::Config &Config() {
	return Extera::Config::Instance();
}

void AddToggle(
		SectionBuilder &builder,
		const QString &key,
		rpl::producer<QString> title,
		QStringList keywords = {},
		rpl::producer<QString> subtitle = nullptr,
		bool longSubtitle = false) {
	const auto withSubtitle = bool(subtitle);
	const auto &subtitleSt = st::exteraButtonWithSubtitle;
	const auto button = builder.addButton({
		.id = u"extera/"_q + key,
		.title = std::move(title),
		.st = (withSubtitle
			? &subtitleSt
			: &st::settingsButtonNoIcon),
		.toggled = rpl::single(Extera::Bool(key)),
		.keywords = std::move(keywords),
	});
	if (button && withSubtitle) {
		const auto label = Ui::CreateChild<Ui::FlatLabel>(
			button,
			std::move(subtitle),
			st::exteraSubtitle);
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
		const auto &st = subtitleSt;
		button->widthValue(
		) | rpl::on_next([=, &st](int width) {
			label->resizeToWidth(width
				- st.padding.left()
				- st.padding.right()
				- st.toggleSkip
				- st.toggle.diameter * 2);
			label->moveToLeft(
				st.padding.left(),
				st.padding.top() + st.height + st::exteraSubtitleSkip);
			const auto extra = std::max(
				label->height() - st::exteraSubtitle.style.font->height,
				0);
			auto padding = st.padding;
			padding.setBottom(padding.bottom() + extra);
			button->setPaddingOverride(padding);
		}, label->lifetime());
	}
	if (button) {
		const auto controller = builder.controller();
		button->toggledValue(
		) | rpl::on_next([=](bool value) {
			const auto changed = (Extera::Bool(key) != value);
			Config().setBool(key, value);
			static const auto restart = QStringList{
				u"hide_stories"_q,
				u"sender_mini_avatars"_q,
				u"local_premium"_q,
			};
			if (changed && controller && restart.contains(key)) {
				controller->show(Ui::MakeConfirmBox({
					.text = tr::lng_extera_restart_required(),
					.confirmed = [] { Core::Restart(); },
					.confirmText = tr::lng_extera_restart(),
				}));
			}
		}, button->lifetime());
	}
}

void AddChoice(
		SectionBuilder &builder,
		const QString &key,
		const Phrase &title,
		std::vector<const Phrase*> options,
		QStringList keywords = {}) {
	const auto controller = builder.controller();
	const auto optionText = [=](int index) {
		return (index >= 0 && index < int(options.size()))
			? (*options[index])(tr::now)
			: QString();
	};
	builder.addButton({
		.id = u"extera/"_q + key,
		.title = title(),
		.st = &st::settingsButtonNoIcon,
		.label = Config().intChanges(key) | rpl::map(optionText),
		.onClick = [=] {
			auto texts = std::vector<QString>();
			for (auto i = 0; i != int(options.size()); ++i) {
				texts.push_back(optionText(i));
			}
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				SingleChoiceBox(box, {
					.title = title(),
					.options = texts,
					.initialSelection = Extera::Int(key),
					.callback = [=](int index) {
						Config().setInt(key, index);
					},
				});
			}));
		},
		.keywords = std::move(keywords),
	});
}

void AddValueChoice(
		SectionBuilder &builder,
		const QString &key,
		const Phrase &title,
		std::vector<int> values,
		Fn<QString(int)> text,
		QStringList keywords = {}) {
	const auto controller = builder.controller();
	builder.addButton({
		.id = u"extera/"_q + key,
		.title = title(),
		.st = &st::settingsButtonNoIcon,
		.label = Config().intChanges(key) | rpl::map(text),
		.onClick = [=] {
			auto texts = std::vector<QString>();
			auto selected = 0;
			const auto current = Extera::Int(key);
			for (auto i = 0; i != int(values.size()); ++i) {
				texts.push_back(text(values[i]));
				if (values[i] == current) {
					selected = i;
				}
			}
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				SingleChoiceBox(box, {
					.title = title(),
					.options = texts,
					.initialSelection = selected,
					.callback = [=](int index) {
						Config().setInt(key, values[index]);
					},
				});
			}));
		},
		.keywords = std::move(keywords),
	});
}

void AddLink(
		SectionBuilder &builder,
		const QString &id,
		rpl::producer<QString> title,
		const QString &label,
		const QString &url,
		IconDescriptor icon) {
	builder.addButton({
		.id = u"extera/"_q + id,
		.title = std::move(title),
		.icon = std::move(icon),
		.label = rpl::single(label),
		.onClick = [=] { UrlClickHandler::Open(url); },
	});
}

void AddSlider(
		SectionBuilder &builder,
		const QString &key,
		const Phrase &title,
		std::vector<int> values,
		std::vector<const Phrase*> captions,
		Fn<QString(int)> text,
		bool withTitle = true) {
	if (withTitle) {
		builder.add([=](const WidgetContext &ctx) {
			return SectionBuilder::WidgetToAdd{
				.widget = object_ptr<Ui::FlatLabel>(
					ctx.container,
					title(),
					st::defaultSubsectionTitle),
				.margin = st::defaultSubsectionTitlePadding,
			};
		});
	}
	builder.addControl({
		.factory = [=](not_null<Ui::VerticalLayout*> container) {
			auto result = object_ptr<Ui::VerticalLayout>(container);
			const auto raw = result.data();
			auto labelWidth = 0;
			for (const auto value : values) {
				labelWidth = std::max(
					labelWidth,
					st::settingsScaleLabel.style.font->width(text(value)));
			}
			const auto skip = st::normalFont->spacew * 2;
			auto sliderWithLabel = MakeSliderWithLabel(
				raw,
				st::settingsScale,
				st::settingsScaleLabel,
				skip,
				labelWidth,
				true);
			raw->add(
				std::move(sliderWithLabel.widget),
				st::settingsBigScalePadding);
			const auto slider = sliderWithLabel.slider;
			const auto label = sliderWithLabel.label;
			slider->setMaterialStyle(Extera::Bool(u"md3_sliders"_q));

			const auto current = Extera::Int(key);
			auto index = 0;
			for (auto i = 0; i != int(values.size()); ++i) {
				if (std::abs(values[i] - current)
					< std::abs(values[index] - current)) {
					index = i;
				}
			}
			label->setText(text(values[index]));
			slider->setPseudoDiscrete(
				int(values.size()),
				[=](int i) { return values[i]; },
				values[index],
				[=](int value) {
					label->setText(text(value));
					Config().setInt(key, value);
				},
				[=](int value) {
					label->setText(text(value));
					Config().setInt(key, value);
				});

			const auto padding = st::settingsBigScalePadding;
			const auto row = raw->add(
				object_ptr<Ui::FixedHeightWidget>(
					raw,
					st::slowmodeLabel.font->height),
				QMargins(
					padding.left(),
					0,
					padding.right() + labelWidth + skip,
					padding.bottom()));
			const auto count = int(captions.size());
			for (auto i = 0; i != count; ++i) {
				const auto caption = Ui::CreateChild<Ui::LabelSimple>(
					row,
					st::slowmodeLabel,
					(*captions[i])(tr::now));
				rpl::combine(
					row->widthValue(),
					caption->widthValue()
				) | rpl::on_next([=](int outer, int inner) {
					const auto seek = st::settingsScale.seekSize.width();
					const auto available = outer - seek;
					const auto center = (count > 1)
						? (seek / 2 + available * i / (count - 1))
						: (outer / 2);
					const auto left = (i == 0)
						? 0
						: (i == count - 1)
						? (outer - inner)
						: (center - inner / 2);
					caption->moveToLeft(left, 0, outer);
				}, caption->lifetime());
			}
			return result;
		},
		.id = u"extera/"_q + key,
		.title = title(),
	});
}

struct GroupEntry {
	QString key;
	const Phrase *title = nullptr;
};

void AddSwitchGroup(
		SectionBuilder &builder,
		const QString &id,
		const Phrase &title,
		std::vector<GroupEntry> entries) {
	builder.addControl({
		.factory = [=](not_null<Ui::VerticalLayout*> container) {
			auto result = object_ptr<Ui::VerticalLayout>(container);
			const auto raw = result.data();
			auto wrap = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				raw,
				object_ptr<Ui::VerticalLayout>(raw));
			const auto inner = wrap->entity();
			auto checks = std::vector<not_null<Ui::AbstractCheckView*>>();
			for (const auto &entry : entries) {
				const auto checkbox = inner->add(
					object_ptr<Ui::Checkbox>(
						inner,
						(*entry.title)(tr::now),
						Extera::Bool(entry.key),
						st::settingsCheckbox),
					st::settingsButtonNoIcon.padding);
				const auto button = Ui::CreateChild<Ui::RippleButton>(
					inner,
					st::defaultRippleAnimation);
				button->stackUnder(checkbox);
				rpl::combine(
					inner->widthValue(),
					checkbox->geometryValue()
				) | rpl::on_next([=](int w, const QRect &r) {
					button->setGeometry(0, r.y(), w, r.height());
				}, button->lifetime());
				checkbox->setAttribute(Qt::WA_TransparentForMouseEvents);
				const auto view = checkbox->checkView();
				button->setClickedCallback([=] {
					view->setChecked(!view->checked(), anim::type::normal);
				});
				const auto key = entry.key;
				view->checkedChanges(
				) | rpl::on_next([=](bool checked) {
					Config().setBool(key, checked);
				}, checkbox->lifetime());
				checks.push_back(view);
			}
			const auto wrapRaw = wrap.data();
			wrapRaw->hide(anim::type::instant);
			AddInnerToggle(
				raw,
				st::settingsButtonNoIcon,
				std::move(checks),
				wrapRaw,
				title(),
				std::nullopt,
				{});
			raw->add(std::move(wrap));
			return result;
		},
		.id = u"extera/"_q + id,
		.title = title(),
	});
}

void AddPreview(
		SectionBuilder &builder,
		const QString &key,
		int height,
		Fn<void(QPainter&, QRect, int)> paint,
		Fn<int()> heightValue = nullptr) {
	builder.add([=](const WidgetContext &ctx) {
		auto result = object_ptr<Ui::FixedHeightWidget>(
			ctx.container,
			heightValue ? heightValue() : height);
		const auto raw = result.data();
		raw->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(raw);
			auto hq = PainterHighQualityEnabler(p);
			const auto frame = raw->rect();
			p.setPen(Qt::NoPen);
			p.setBrush(st::boxDividerBg);
			const auto radius = st::exteraCardRadius;
			p.drawRoundedRect(frame, radius, radius);
			paint(p, frame, Extera::Int(key));
		}, raw->lifetime());
		Config().intChanges(
			key
		) | rpl::on_next([=] {
			if (heightValue) {
				raw->resize(raw->width(), heightValue());
			}
			raw->update();
		}, raw->lifetime());
		return SectionBuilder::WidgetToAdd{
			.widget = std::move(result),
			.margin = st::exteraPreviewPadding,
		};
	});
}

void PaintAvatarPreview(QPainter &p, QRect frame, int corners) {
	const auto size = st::exteraPreviewAvatar;
	const auto left = frame.x() + st::exteraPreviewSkip;
	const auto top = frame.y() + (frame.height() - size) / 2;
	const auto radius = size * std::clamp(corners, 0, 28) / 56.;
	p.setBrush(st::windowSubTextFg);
	p.setOpacity(0.5);
	p.drawRoundedRect(QRectF(left, top, size, size), radius, radius);
	const auto line = st::exteraPreviewLine;
	const auto textLeft = left + size + st::exteraPreviewSkip;
	const auto available = frame.right() - textLeft - st::exteraPreviewSkip;
	p.setOpacity(0.3);
	p.drawRoundedRect(
		QRectF(textLeft, top + size / 4. - line / 2., available * 0.45, line),
		line / 2.,
		line / 2.);
	p.drawRoundedRect(
		QRectF(textLeft, top + size * 3 / 4. - line / 2., available * 0.8, line),
		line / 2.,
		line / 2.);
	p.setOpacity(1.);
}

void AddLivePreview(
		SectionBuilder &builder,
		Fn<int()> height,
		Fn<void(QPainter&, QRect)> paint,
		std::vector<QString> keys) {
	builder.add([=](const WidgetContext &ctx) {
		auto result = object_ptr<Ui::FixedHeightWidget>(
			ctx.container,
			height());
		const auto raw = result.data();
		raw->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(raw);
			auto hq = PainterHighQualityEnabler(p);
			const auto frame = raw->rect();
			p.setPen(Qt::NoPen);
			p.setBrush(st::boxDividerBg);
			const auto radius = st::exteraCardRadius;
			p.drawRoundedRect(frame, radius, radius);
			paint(p, frame);
		}, raw->lifetime());
		Config().changes(
		) | rpl::filter([=](const QString &key) {
			return ranges::contains(keys, key);
		}) | rpl::on_next([=] {
			raw->resize(raw->width(), height());
			raw->update();
		}, raw->lifetime());
		return SectionBuilder::WidgetToAdd{
			.widget = std::move(result),
			.margin = st::exteraPreviewPadding,
		};
	});
}

void PaintTextBars(
		QPainter &p,
		QRectF rect,
		const style::color &color,
		int lines) {
	const auto line = st::exteraPreviewLine / 2.;
	const auto gap = (rect.height() - lines * line) / (lines + 1);
	p.setBrush(color);
	for (auto i = 0; i != lines; ++i) {
		const auto width = rect.width() * ((i + 1 == lines) ? 0.55 : 0.9);
		p.drawRoundedRect(
			QRectF(rect.x(), rect.y() + gap + i * (line + gap), width, line),
			line / 2.,
			line / 2.);
	}
}

void PaintLastSeenPreview(QPainter &p, QRect frame) {
	const auto skip = st::exteraPreviewSkip;
	const auto size = st::exteraPreviewAvatar;
	const auto top = frame.y() + (frame.height() - size) / 2;
	p.setPen(Qt::NoPen);
	p.setBrush(st::activeButtonBg);
	p.drawEllipse(QRect(frame.x() + skip, top, size, size));
	const auto left = frame.x() + skip * 2 + size;
	PaintTextBars(
		p,
		QRectF(left, top, frame.width() * 0.3, size / 2.),
		st::windowFg,
		1);
	const auto text = Extera::Bool(u"relative_last_seen"_q)
		? tr::lng_status_lastseen_minutes(tr::now, lt_count, 5)
		: tr::lng_status_lastseen_today(
			tr::now,
			lt_time,
			QLocale().toString(
				QTime::currentTime().addSecs(-300),
				QLocale::ShortFormat));
	p.setPen(st::windowSubTextFg);
	p.setFont(st::normalFont);
	p.drawText(
		QRect(left, top + size / 2, frame.width() - left, size / 2),
		Qt::AlignLeft | Qt::AlignVCenter,
		text);
}

void PaintFolderTabsPreview(QPainter &p, QRect frame) {
	const auto style = Extera::Int(u"folder_title_style"_q);
	struct Tab {
		Ui::FilterIcon icon;
		QString title;
	};
	const auto tabs = std::vector<Tab>{
		{ Ui::FilterIcon::All, tr::lng_filters_all(tr::now) },
		{ Ui::FilterIcon::Groups, tr::lng_filters_type_groups(tr::now) },
		{ Ui::FilterIcon::Channels, tr::lng_filters_type_channels(tr::now) },
	};
	const auto skip = st::exteraPreviewSkip;
	const auto &font = st::semiboldFont;
	auto left = frame.x() + skip;
	const auto center = frame.y() + frame.height() / 2;
	for (auto i = 0; i != int(tabs.size()); ++i) {
		const auto &tab = tabs[i];
		const auto active = (i == 0);
		const auto &icons = Ui::LookupFilterIcon(tab.icon);
		const auto icon = active ? icons.active : icons.normal;
		const auto showIcon = (style != 1);
		const auto showText = (style != 2);
		auto width = 0;
		if (showIcon) {
			width += icon->width();
		}
		if (showText) {
			width += (showIcon ? skip / 2 : 0) + font->width(tab.title);
		}
		auto x = left;
		if (showIcon) {
			icon->paint(
				p,
				x,
				center - icon->height() / 2,
				frame.width(),
				active ? st::activeButtonBg->c : st::windowSubTextFg->c);
			x += icon->width() + skip / 2;
		}
		if (showText) {
			p.setFont(font);
			p.setPen(active ? st::activeButtonBg : st::windowSubTextFg);
			p.drawText(
				QRect(x, frame.y(), font->width(tab.title), frame.height()),
				Qt::AlignVCenter,
				tab.title);
		}
		if (active) {
			p.setPen(Qt::NoPen);
			p.setBrush(st::activeButtonBg);
			const auto line = st::lineWidth * 3;
			p.drawRoundedRect(
				QRect(left, frame.bottom() - line - skip / 2, width, line),
				line / 2.,
				line / 2.);
		}
		left += width + skip * 2;
	}
}

[[nodiscard]] int StickerPreviewHeight() {
	return HistoryView::Sticker::Size().height()
		+ st::exteraPreviewBubble
		+ st::exteraPreviewSkip * 3;
}

void PaintStickerPreview(QPainter &p, QRect frame, int value) {
	static const auto image = QImage(u":/gui/art/exteradesktop.png"_q);
	const auto skip = st::exteraPreviewSkip;
	const auto side = HistoryView::Sticker::Size().width();
	const auto bubble = QRectF(
		frame.x() + skip,
		frame.y() + skip,
		frame.width() * 0.5,
		st::exteraPreviewBubble);
	p.setPen(Qt::NoPen);
	p.setBrush(st::msgInBg);
	p.drawRoundedRect(bubble, st::bubbleRadiusLarge, st::bubbleRadiusLarge);
	PaintTextBars(
		p,
		bubble.marginsRemoved({ 12., 6., 12., 6. }),
		st::msgInDateFg,
		2);
	const auto sticker = QRect(
		frame.x() + frame.width() - skip - side,
		frame.y() + frame.height() - skip - side,
		side,
		side);
	p.drawImage(sticker, image);
	const auto timeMode = Extera::Int(u"sticker_time"_q);
	if (timeMode != 2) {
		const auto time = QLocale().toString(
			QTime::currentTime(),
			QLocale::ShortFormat);
		const auto &font = st::msgDateFont;
		const auto width = font->width(time) + skip;
		const auto height = font->height + 4;
		const auto rect = (timeMode == 1)
			? QRect(
				sticker.x() - width - skip / 2,
				sticker.bottom() - height,
				width,
				height)
			: QRect(
				sticker.right() - width - 4,
				sticker.bottom() - height - 4,
				width,
				height);
		p.setBrush(st::msgServiceBg);
		p.drawRoundedRect(rect, height / 2., height / 2.);
		p.setPen(st::msgServiceFg);
		p.setFont(font);
		p.drawText(rect, Qt::AlignCenter, time);
		p.setPen(Qt::NoPen);
	}
}

void PaintGesturesPreview(
		QPainter &p,
		QRect frame,
		const std::vector<const Phrase*> &actions) {
	const auto skip = st::exteraPreviewSkip;
	const auto height = st::exteraPreviewBubble;
	const auto width = frame.width() * 0.5;
	const auto &font = st::normalFont;
	const auto paintBubble = [&](bool out, int top, int action) {
		const auto x = out
			? (frame.x() + frame.width() - skip - width)
			: (frame.x() + skip);
		const auto bubble = QRectF(x, top, width, height);
		p.setPen(Qt::NoPen);
		p.setBrush(out ? st::msgOutBg : st::msgInBg);
		p.drawRoundedRect(
			bubble,
			st::bubbleRadiusLarge,
			st::bubbleRadiusLarge);
		PaintTextBars(
			p,
			bubble.marginsRemoved({ 12., 6., 12., 6. }),
			out ? st::msgOutDateFg : st::msgInDateFg,
			2);
		const auto index = std::clamp(action, 0, int(actions.size()) - 1);
		const auto label = (*actions[index])(tr::now);
		const auto labelWidth = font->width(label) + skip;
		const auto labelHeight = font->height + 6;
		const auto labelRect = QRect(
			out
				? int(bubble.x() - labelWidth - skip / 2)
				: int(bubble.right() + skip / 2),
			int(bubble.y() + (height - labelHeight) / 2),
			labelWidth,
			labelHeight);
		p.setBrush(st::activeButtonBg);
		p.drawRoundedRect(labelRect, labelHeight / 2., labelHeight / 2.);
		p.setPen(st::activeButtonFg);
		p.setFont(font);
		p.drawText(labelRect, Qt::AlignCenter, label);
	};
	paintBubble(
		false,
		frame.y() + skip,
		Extera::Int(u"double_click_incoming"_q));
	paintBubble(
		true,
		frame.y() + skip * 2 + height,
		Extera::Int(u"double_click_outgoing"_q));
}

void AddBetaBadge(
		not_null<Ui::RpWidget*> button,
		rpl::producer<QString> text,
		const style::SettingsButton &st) {
	const auto badge = Ui::NewBadge::CreateNewBadge(
		button,
		tr::lng_exd_beta());
	rpl::combine(
		std::move(text),
		button->widthValue()
	) | rpl::on_next([=, &st](const QString &text, int width) {
		const auto left = st.padding.left()
			+ st.style.font->width(text)
			+ st.style.font->spacew;
		badge->setVisible(width - left - st.padding.right() >= badge->width());
		badge->moveToLeft(
			left,
			st.padding.top()
				+ st.style.font->ascent
				- st::settingsPremiumNewBadge.style.font->ascent
				- st::settingsPremiumNewBadgePadding.top(),
			width);
	}, badge->lifetime());
}

void ShowBadgeInfoBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::boxWideWidth);
		box->setNoContentMargin(true);
		box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });
		const auto session = &controller->session();

		const auto logoSize = st::exteraBadgeLogoSize;
		const auto logo = box->addRow(
			object_ptr<Ui::FixedHeightWidget>(box, logoSize),
			st::exteraBadgeLogoPadding);
		logo->paintRequest() | rpl::on_next([=] {
			static const auto image = QImage(u":/gui/art/logo_256.png"_q);
			auto p = QPainter(logo);
			auto hq = PainterHighQualityEnabler(p);
			p.drawImage(
				QRect((logo->width() - logoSize) / 2, 0, logoSize, logoSize),
				image);
		}, logo->lifetime());

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_exd_support_title(),
				st::exteraBadgeTitle),
			st::exteraBadgeTitlePadding,
			style::al_top);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_exd_support_text(),
				st::exteraBadgeText),
			st::exteraBadgeTextPadding,
			style::al_top);

		const auto step = [&](
				const style::icon *icon,
				rpl::producer<QString> title,
				rpl::producer<TextWithEntities> text) {
			const auto row = box->addRow(
				object_ptr<Ui::VerticalLayout>(box),
				st::exteraBadgeStepPadding);
			const auto titleLabel = row->add(
				object_ptr<Ui::FlatLabel>(
					row,
					std::move(title),
					st::exteraBadgeStepTitle),
				st::exteraBadgeStepLabelPadding);
			const auto textLabel = row->add(
				object_ptr<Ui::FlatLabel>(
					row,
					std::move(text),
					st::exteraBadgeStepText),
				st::exteraBadgeStepLabelPadding);
			textLabel->setClickHandlerFilter([=](const auto &...) {
				crl::on_main(box, [=] { box->closeBox(); });
				return true;
			});
			const auto iconWidget = Ui::CreateChild<Ui::RpWidget>(row);
			iconWidget->resize(icon->width(), icon->height());
			iconWidget->paintRequest() | rpl::on_next([=] {
				auto p = QPainter(iconWidget);
				icon->paint(p, 0, 0, iconWidget->width(), st::windowFg->c);
			}, iconWidget->lifetime());
			titleLabel->topValue() | rpl::on_next([=](int top) {
				iconWidget->moveToLeft(0, top);
			}, iconWidget->lifetime());
		};
		step(
			&st::menuIconEarn,
			tr::lng_exd_support_step1_title(),
			tr::lng_exd_support_step1_text(
				lt_link,
				rpl::single(Ui::Text::Link(
					tr::lng_exd_support_other(tr::now),
					u"tg://settings/extera/other?highlight=extera/donate_card"_q)),
				tr::marked));
		step(
			&st::menuIconPhoto,
			tr::lng_exd_support_step2_title(),
			tr::lng_exd_support_step2_text(
				lt_user,
				rpl::single(Ui::Text::Link(
					u"@metawka"_q,
					u"https://t.me/metawka"_q)),
				tr::marked));
		step(
			&st::menuIconPremium,
			tr::lng_exd_support_step3_title(),
			tr::lng_exd_support_step3_text(tr::rich));

		const auto preview = box->addRow(
			object_ptr<Ui::FixedHeightWidget>(box, st::exteraBadgePreviewHeight),
			st::exteraBadgePreviewPadding);
		const auto user = session->user();
		const auto view = preview->lifetime().make_state<
			Ui::PeerUserpicView>();
		const auto badgeId = Extera::BadgeTypeEmoji(u"donor"_q);
		auto nameText = TextWithEntities{ user->name() };
		if (badgeId) {
			nameText.append(' ').append(Data::SingleCustomEmoji(badgeId));
		}
		const auto name = Ui::CreateChild<Ui::FlatLabel>(
			preview,
			st::exteraBadgePreviewName);
		name->setMarkedText(nameText, Core::TextContext({
			.session = session,
			.repaint = [=] { name->update(); },
		}));
		const auto message = Ui::CreateChild<Ui::FlatLabel>(
			preview,
			tr::lng_exd_support_preview(),
			st::exteraBadgePreviewText);
		preview->widthValue() | rpl::on_next([=](int width) {
			const auto left = st::exteraBadgePreviewUserpic
				+ st::exteraBadgePreviewSkip * 3;
			name->resizeToWidth(width - left - st::exteraBadgePreviewSkip);
			message->resizeToWidth(width - left - st::exteraBadgePreviewSkip);
			name->moveToLeft(left, st::exteraBadgePreviewSkip);
			message->moveToLeft(
				left,
				st::exteraBadgePreviewSkip + name->height());
		}, preview->lifetime());
		preview->paintRequest() | rpl::on_next([=] {
			auto p = Painter(preview);
			auto hq = PainterHighQualityEnabler(p);
			const auto size = st::exteraBadgePreviewUserpic;
			const auto skip = st::exteraBadgePreviewSkip;
			user->paintUserpic(
				p,
				*view,
				skip,
				(preview->height() - size) / 2,
				size);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			const auto left = size + skip * 2;
			p.drawRoundedRect(
				QRect(left, 0, preview->width() - left, preview->height()),
				st::bubbleRadiusLarge,
				st::bubbleRadiusLarge);
		}, preview->lifetime());

		box->addButton(tr::lng_exd_badge_contact(), [=] {
			UrlClickHandler::Open(u"https://t.me/metawka"_q);
			box->closeBox();
		});
	}));
}

void ShowAppIconBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_extera_app_icon());
		box->setWidth(st::exteraAppIconBoxWidth);
		box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });
		const auto variants = Extera::AppIcon::Variants();
		const auto count = int(variants.size());
		const auto titles = std::vector<QString>{
			tr::lng_extera_app_icon_default(tr::now),
			tr::lng_exd_app_icon_telegram(tr::now),
			tr::lng_exd_app_icon_accent(tr::now),
			tr::lng_exd_app_icon_dark(tr::now),
			tr::lng_exd_app_icon_light(tr::now),
		};
		const auto currentId = Extera::String(u"app_icon"_q);
		const auto currentSquare = Extera::Bool(u"app_icon_square"_q);
		const auto current = [&] {
			for (auto i = 0; i != count; ++i) {
				if (variants[i].id == currentId) {
					return i;
				}
			}
			return 0;
		}();
		struct State {
			int selected = 0;
			bool square = false;
			std::vector<QImage> images;
		};
		const auto state = box->lifetime().make_state<State>();
		state->selected = current;
		state->square = currentSquare;
		const auto ratio = style::DevicePixelRatio();
		const auto size = st::exteraAppIconSize;
		const auto render = [=] {
			state->images.clear();
			for (const auto &variant : variants) {
				auto image = Extera::AppIcon::Render(
					size * ratio,
					variant.id,
					state->square);
				image.setDevicePixelRatio(ratio);
				state->images.push_back(std::move(image));
			}
		};
		render();

		const auto columns = 3;
		const auto cell = st::exteraAppIconCellWidth;
		const auto cellHeight = st::exteraAppIconCellHeight;
		const auto rows = (count + columns - 1) / columns;
		const auto grid = box->addRow(
			object_ptr<Ui::FixedHeightWidget>(box, rows * cellHeight),
			st::exteraAppIconGridPadding);
		const auto skip = [=] {
			return (grid->width() - columns * cell) / 2;
		};
		grid->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(grid);
			auto hq = PainterHighQualityEnabler(p);
			const auto left = skip();
			for (auto i = 0; i != count; ++i) {
				const auto x = left + (i % columns) * cell;
				const auto y = (i / columns) * cellHeight + 4;
				const auto rect = QRect(x + (cell - size) / 2, y, size, size);
				p.drawImage(rect, state->images[i]);
				if (i == state->selected) {
					p.setBrush(Qt::NoBrush);
					p.setPen(QPen(st::windowActiveTextFg, st::lineWidth * 2));
					const auto outer = rect.marginsAdded({ 4, 4, 4, 4 });
					if (state->square) {
						const auto radius = outer.width() * 0.25;
						p.drawRoundedRect(outer, radius, radius);
					} else {
						p.drawEllipse(outer);
					}
				}
				p.setPen(st::windowFg);
				p.setFont(st::exteraAppIconCaptionFont);
				p.drawText(
					QRect(
						x,
						y + size + st::exteraAppIconCaptionSkip,
						cell,
						st::exteraAppIconCaptionFont->height),
					Qt::AlignHCenter | Qt::AlignTop,
					st::exteraAppIconCaptionFont->elided(
						titles[i],
						cell - 4));
			}
		}, grid->lifetime());
		grid->setCursor(style::cur_pointer);
		grid->events(
		) | rpl::filter([](not_null<QEvent*> e) {
			return e->type() == QEvent::MouseButtonRelease;
		}) | rpl::on_next([=](not_null<QEvent*> e) {
			const auto point = static_cast<QMouseEvent*>(e.get())->pos();
			const auto column = (point.x() - skip()) / cell;
			const auto row = point.y() / cellHeight;
			const auto index = row * columns + column;
			if (point.x() < skip()
				|| column >= columns
				|| index < 0
				|| index >= count) {
				return;
			}
			state->selected = index;
			grid->update();
		}, grid->lifetime());

		const auto square = box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				tr::lng_exd_app_icon_square(tr::now),
				currentSquare,
				st::defaultBoxCheckbox),
			st::exteraAppIconGridPadding);
		square->checkedChanges() | rpl::on_next([=](bool checked) {
			state->square = checked;
			render();
			grid->update();
		}, square->lifetime());

		box->addButton(tr::lng_extera_app_icon_apply(), [=] {
			const auto index = state->selected;
			const auto changed = (index != current)
				|| (state->square != currentSquare);
			box->closeBox();
			if (!changed) {
				return;
			}
			Config().setString(u"app_icon"_q, variants[index].id);
			Config().setBool(u"app_icon_square"_q, state->square);
			Extera::AppIcon::Apply();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void ShowGifBotBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_exd_gif_bot());
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(u"@gif"_q),
			Extera::String(u"gif_bot"_q)));
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_exd_gif_bot_about(),
				st::boxDividerLabel),
			st::exteraQuickAboutPadding);
		const auto save = [=] {
			Config().setString(
				u"gif_bot"_q,
				field->getLastText().trimmed().remove('@'));
			box->closeBox();
		};
		field->submits() | rpl::on_next(save, field->lifetime());
		box->setFocusCallback([=] { field->setFocusFast(); });
		box->addButton(tr::lng_settings_save(), save);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void ShowQuickActionsBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		constexpr auto kLimit = 5;
		box->setTitle(tr::lng_extera_quick_actions_buttons());
		box->addButton(tr::lng_box_done(), [=] { box->closeBox(); });

		auto chosen = Extera::String(u"menu_quick_items"_q).split(
			',',
			Qt::SkipEmptyParts);
		auto order = chosen;
		for (const auto &id : Extera::QuickActionIds()) {
			if (!order.contains(id)) {
				order.push_back(id);
			}
		}
		const auto state = box->lifetime().make_state<QStringList>(chosen);
		const auto list = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			style::margins());
		const auto dragging = list->lifetime().make_state<bool>(false);
		const auto save = [=] {
			auto result = QStringList();
			for (auto i = 0; i != list->count(); ++i) {
				const auto id = list->widgetAt(i)->property(
					"extera_quick_id").toString();
				if (state->contains(id)) {
					result.push_back(id);
				}
			}
			*state = result;
			Config().setString(u"menu_quick_items"_q, result.join(','));
		};
		for (const auto &id : order) {
			const auto button = list->add(Settings::CreateButtonWithIcon(
				list,
				rpl::single(Extera::QuickActionTitle(id)),
				st::settingsButton,
				{ Extera::QuickActionIcon(id) }));
			button->setProperty("extera_quick_id", id);
			const auto forced = button->lifetime().make_state<
				rpl::event_stream<bool>>();
			button->toggleOn(rpl::single(
				chosen.contains(id)
			) | rpl::then(forced->events()), true);
			button->setClickedCallback([=] {
				if (!*dragging) {
					forced->fire(!button->toggled());
				}
			});
			button->toggledValue(
			) | rpl::skip(1) | rpl::on_next([=](bool on) {
				if (on && !state->contains(id)) {
					if (state->size() >= kLimit) {
						controller->showToast(
							tr::lng_extera_quick_actions_limit(tr::now));
						crl::on_main(button, [=] {
							forced->fire(false);
						});
						return;
					}
					state->push_back(id);
				} else if (!on) {
					state->removeAll(id);
				}
				save();
			}, button->lifetime());
		}
		const auto reorder = list->lifetime().make_state<
			Ui::VerticalLayoutReorder>(list);
		reorder->updates(
		) | rpl::on_next([=](Ui::VerticalLayoutReorder::Single data) {
			using State = Ui::VerticalLayoutReorder::State;
			if (data.state == State::Started) {
				*dragging = true;
				return;
			}
			crl::on_main(list, [=] {
				*dragging = false;
			});
			if (data.state == State::Applied) {
				save();
			}
		}, list->lifetime());
		reorder->start();
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_extera_quick_actions_about(),
				st::boxDividerLabel),
			st::exteraQuickAboutPadding);
	}));
}

void AddAbout(SectionBuilder &builder, rpl::producer<QString> text) {
	builder.addAbout(std::move(text));
}

void AddHeader(SectionBuilder &builder) {
	builder.addControl({
		.factory = [=](not_null<Ui::VerticalLayout*> container) {
			auto result = object_ptr<Ui::VerticalLayout>(container);
			const auto raw = result.data();
			const auto size = st::exteraHeaderLogoSize;
			const auto logo = raw->add(
				object_ptr<Ui::FixedHeightWidget>(raw, size),
				st::exteraHeaderLogoPadding);
			logo->paintRequest(
			) | rpl::on_next([=] {
				auto p = QPainter(logo);
				auto hq = PainterHighQualityEnabler(p);
				const auto left = (logo->width() - size) / 2;
				p.drawImage(
					QRect(left, 0, size, size),
					Window::Logo());
			}, logo->lifetime());
			raw->add(
				object_ptr<Ui::FlatLabel>(
					raw,
					rpl::single(AppName.utf16()),
					st::exteraHeaderTitle),
				st::exteraHeaderTitlePadding,
				style::al_top);
			raw->add(
				object_ptr<Ui::FlatLabel>(
					raw,
					rpl::single(Extera::Update::VersionText()),
					st::exteraHeaderVersion),
				st::exteraHeaderVersionPadding,
				style::al_top);
			return result;
		},
	});
}

// General

void AddCoreToggle(
		SectionBuilder &builder,
		const QString &id,
		rpl::producer<QString> title,
		Fn<bool()> get,
		Fn<void(bool)> set) {
	const auto button = builder.addButton({
		.id = u"extera/"_q + id,
		.title = std::move(title),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(get()),
	});
	if (button) {
		button->toggledValue(
		) | rpl::filter([=](bool value) {
			return value != get();
		}) | rpl::on_next([=](bool value) {
			set(value);
			Core::App().saveSettingsDelayed();
		}, button->lifetime());
	}
}

void AddStreamerRow(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto &st = st::exteraButtonWithSubtitle;
	const auto button = builder.addButton({
		.id = u"extera/streamer_mode"_q,
		.title = tr::lng_extera_streamer_mode(),
		.st = &st,
		.onClick = [=] {
			if (controller) {
				Extera::Streamer::ShowSettings(controller);
			}
		},
		.keywords = { u"streamer"_q, u"privacy"_q },
	});
	if (!button) {
		return;
	}
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		button,
		tr::lng_extera_streamer_mode_about(),
		st::exteraSubtitle);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto toggle = Ui::CreateChild<Ui::AbstractButton>(button);
	const auto view = toggle->lifetime().make_state<Ui::ToggleView>(
		st.toggle,
		Extera::Streamer::Active(),
		[=] { toggle->update(); });
	const auto size = view->getSize();
	const auto skip = st::exteraStreamerToggleSkip;
	toggle->resize(size.width() + 2 * skip, button->height());
	toggle->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(toggle);
		p.fillRect(
			QRect(0, (toggle->height() - st::exteraStreamerSplit) / 2, st::lineWidth, st::exteraStreamerSplit),
			st::windowBgRipple);
		view->paint(
			p,
			skip,
			(toggle->height() - size.height()) / 2,
			toggle->width());
	}, toggle->lifetime());
	toggle->setClickedCallback([=] {
		Config().setBool(
			u"streamer_mode"_q,
			!Extera::Streamer::Active());
	});
	Config().boolChanges(
		u"streamer_mode"_q
	) | rpl::on_next([=](bool value) {
		view->setChecked(value, anim::type::normal);
	}, toggle->lifetime());
	button->sizeValue() | rpl::on_next([=](QSize outer) {
		toggle->resize(toggle->width(), outer.height());
		toggle->moveToRight(st.padding.right() - skip, 0, outer.width());
		label->resizeToWidth(outer.width()
			- st.padding.left()
			- st.padding.right()
			- toggle->width());
		label->moveToLeft(
			st.padding.left(),
			st.padding.top() + st.height + st::exteraSubtitleSkip);
	}, toggle->lifetime());
}

void PartGeneral0(SectionBuilder &builder) {
	[[maybe_unused]] const auto controller = builder.controller();
	[[maybe_unused]] const auto settings = &Core::App().settings();

	builder.addSkip();
	builder.card([&] {
		AddStreamerRow(builder);
	});
	builder.addDivider();
}

void ShowWsProxySettings(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_exd_ws_settings());
		box->setWidth(st::boxWideWidth);
		const auto field = [&](
				const QString &key,
				rpl::producer<QString> placeholder,
				bool number) {
			const auto value = number
				? QString::number(Extera::Int(key))
				: Extera::String(key);
			const auto result = box->addRow(object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				std::move(placeholder),
				value));
			return std::make_pair(key, result);
		};
		const auto check = [&](const QString &key, rpl::producer<QString> text) {
			const auto result = box->addRow(
				object_ptr<Ui::Checkbox>(
					box,
					std::move(text),
					Extera::Bool(key),
					st::defaultBoxCheckbox),
				st::boxRowPadding + QMargins(0, st::boxLittleSkip, 0, 0));
			return std::make_pair(key, result);
		};
		const auto numbers = std::vector{
			field(u"ws_proxy_port"_q, tr::lng_exd_ws_port(), true),
			field(u"ws_proxy_pool"_q, tr::lng_exd_ws_pool(), true),
			field(u"ws_proxy_buf_kb"_q, tr::lng_exd_ws_buffer(), true),
		};
		const auto strings = std::vector{
			field(u"ws_proxy_dc_ip"_q, tr::lng_exd_ws_dc_ip(), false),
			field(u"ws_proxy_cf_domain"_q, tr::lng_exd_ws_cf_domain(), false),
			field(u"ws_proxy_cf_worker"_q, tr::lng_exd_ws_cf_worker(), false),
			field(u"ws_proxy_fake_tls"_q, tr::lng_exd_ws_fake_tls(), false),
		};
		const auto checks = std::vector{
			check(u"ws_proxy_cf"_q, tr::lng_exd_ws_cf()),
			check(u"ws_proxy_h2"_q, tr::lng_exd_ws_h2()),
			check(u"ws_proxy_secure"_q, tr::lng_exd_ws_secure()),
			check(u"ws_proxy_log"_q, tr::lng_exd_ws_log()),
		};
		Ui::AddSkip(box->verticalLayout());
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_exd_ws_settings_about(),
			st::boxDividerLabel));
		box->addButton(tr::lng_settings_save(), [=] {
			for (const auto &[key, input] : numbers) {
				auto ok = false;
				const auto value = input->getLastText().trimmed().toInt(&ok);
				if (ok && value >= 0) {
					Config().setInt(key, value);
				}
			}
			for (const auto &[key, input] : strings) {
				Config().setString(key, input->getLastText().trimmed());
			}
			for (const auto &[key, checkbox] : checks) {
				Config().setBool(key, checkbox->checked());
			}
			Extera::WsProxy::Restart();
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void PartGeneral1(SectionBuilder &builder) {
	[[maybe_unused]] const auto controller = builder.controller();
	[[maybe_unused]] const auto settings = &Core::App().settings();

	if (Extera::WsProxy::Available()) {
		builder.addSkip();
		builder.card([&] {
			AddCoreToggle(
				builder,
				u"ws_proxy"_q,
				tr::lng_exd_ws_proxy(),
				[] { return Extera::WsProxy::Enabled(); },
				[](bool v) { Extera::WsProxy::SetEnabled(v); });
			builder.addButton({
				.id = u"extera/ws_proxy_settings"_q,
				.title = tr::lng_exd_ws_settings(),
				.st = &st::settingsButtonNoIcon,
				.onClick = [=] {
					if (controller) {
						ShowWsProxySettings(controller);
					}
				},
				.keywords = { u"proxy"_q, u"websocket"_q },
			});
		});
		builder.addAbout(tr::lng_exd_ws_proxy_about());
	}
}

void PartGeneral2(SectionBuilder &builder) {
	[[maybe_unused]] const auto controller = builder.controller();
	[[maybe_unused]] const auto settings = &Core::App().settings();

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_translate_messages());
	builder.card([&] {
		AddCoreToggle(
			builder,
			u"translate_button"_q,
			tr::lng_extera_translate_button(),
			[=] { return settings->translateButtonEnabled(); },
			[=](bool v) { settings->setTranslateButtonEnabled(v); });
		AddCoreToggle(
			builder,
			u"translate_chats"_q,
			tr::lng_extera_translate_chats(),
			[=] { return settings->translateChatEnabled(); },
			[=](bool v) { settings->setTranslateChatEnabled(v); });
		AddChoice(
			builder,
			u"translate_provider"_q,
			tr::lng_extera_translate_provider,
			{
				&tr::lng_extera_provider_telegram,
				&tr::lng_extera_provider_google,
			});
		builder.addButton({
			.id = u"extera/translate_target"_q,
			.title = tr::lng_extera_translate_target(),
			.st = &st::settingsButtonNoIcon,
			.label = settings->translateToValue(
			) | rpl::map([](LanguageId id) {
				return Extera::Bool(u"translate_follow_app"_q)
					? tr::lng_exd_translate_follow_app(tr::now)
					: Ui::LanguageName(id);
			}),
			.onClick = [=] {
				controller->show(Box([=](not_null<Ui::GenericBox*> box) {
					const auto &core = Core::App().settings();
					auto selected = std::vector<LanguageId>{
						core.translateTo(),
					};
					for (const auto &id : core.skipTranslationLanguages()) {
						if (id != selected.front()) {
							selected.push_back(id);
						}
					}
					Ui::ChooseLanguageBox(
						box,
						tr::lng_languages(),
						[=](const std::vector<LanguageId> &ids) {
							Config().setBool(
								u"translate_follow_app"_q,
								false);
							settings->setTranslateTo(ids.front());
							Core::App().saveSettingsDelayed();
						},
						selected,
						false,
						nullptr);
					const auto follow = Extera::Bool(u"translate_follow_app"_q);
					auto text = tr::lng_exd_translate_follow_app(
						tr::now);
					const auto button = box->verticalLayout()->insert(
						0,
						object_ptr<Ui::SettingsButton>(
							box->verticalLayout(),
							rpl::single(follow
								? Ui::Text::Bold(text)
								: TextWithEntities{ text }),
							st::settingsButtonNoIcon));
					button->setClickedCallback([=] {
						Config().setBool(u"translate_follow_app"_q, true);
						settings->setTranslateTo(LanguageId());
						Core::App().saveSettingsDelayed();
						box->closeBox();
					});
				}));
			},
		});
		builder.addButton({
			.id = u"extera/translate_skip"_q,
			.title = tr::lng_extera_translate_skip(),
			.st = &st::settingsButtonNoIcon,
			.label = settings->skipTranslationLanguagesValue(
			) | rpl::map([](const std::vector<LanguageId> &list) {
				auto names = QStringList();
				for (const auto &id : list) {
					names.push_back(Ui::LanguageName(id));
				}
				return names.join(u", "_q);
			}),
			.onClick = [=] {
				controller->show(Ui::EditSkipTranslationLanguages());
			},
		});
	});
	AddAbout(builder, tr::lng_extera_translate_about());
}

void PartGeneral3(SectionBuilder &builder) {
	[[maybe_unused]] const auto controller = builder.controller();
	[[maybe_unused]] const auto settings = &Core::App().settings();

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_general());
	builder.card([&] {
		AddToggle(
			builder,
			u"disable_number_rounding"_q,
			tr::lng_extera_number_rounding(),
			{},
			rpl::single(QString::fromUtf8("1.23K \xE2\x86\x92 1,234")));
		AddToggle(
			builder,
			u"time_with_seconds"_q,
			tr::lng_extera_time_seconds(),
			{},
			rpl::single(QString::fromUtf8("12:34 \xE2\x86\x92 12:34:56")));
		AddToggle(
			builder,
			u"filter_zalgo"_q,
			tr::lng_extera_filter_zalgo());
	});
	AddAbout(builder, tr::lng_extera_filter_zalgo_about());
}

void PartGeneral4(SectionBuilder &builder) {
	[[maybe_unused]] const auto controller = builder.controller();
	[[maybe_unused]] const auto settings = &Core::App().settings();

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_speed_boost());
	builder.card([&] {
		const auto speeds = std::vector<const Phrase*>{
			&tr::lng_extera_speed_off,
			&tr::lng_extera_speed_fast,
			&tr::lng_extera_speed_ultra,
		};
		AddSlider(
			builder,
			u"download_boost"_q,
			tr::lng_extera_speed_boost,
			{ 0, 1, 2 },
			speeds,
			[=](int value) { return (*speeds[value])(tr::now); },
			false);
		AddToggle(
			builder,
			u"upload_boost"_q,
			tr::lng_extera_upload_boost());
	});
	AddAbout(builder, tr::lng_extera_speed_about());
}

void PartGeneral5(SectionBuilder &builder) {
	[[maybe_unused]] const auto controller = builder.controller();
	[[maybe_unused]] const auto settings = &Core::App().settings();

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_storage());
	builder.card([&] {
		builder.addButton({
			.id = u"extera/save_folder"_q,
			.title = tr::lng_extera_save_folder(),
			.st = &st::settingsButtonNoIcon,
			.onClick = [=] {
				controller->show(Box<DownloadPathBox>(controller));
			},
		});
	});
	AddAbout(builder, tr::lng_extera_save_folder_about());
}

void PartGeneral6(SectionBuilder &builder) {
	[[maybe_unused]] const auto controller = builder.controller();
	[[maybe_unused]] const auto settings = &Core::App().settings();

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_profile());
	builder.card([&] {
		AddToggle(
			builder,
			u"relative_last_seen"_q,
			tr::lng_extera_relative_last_seen(),
			{},
			tr::lng_extera_lastseen_days(lt_count, rpl::single(2.)));
		AddLivePreview(
			builder,
			[] { return st::exteraPreviewAvatarHeight; },
			PaintLastSeenPreview,
			{ u"relative_last_seen"_q });
		AddToggle(
			builder,
			u"hide_phone"_q,
			tr::lng_extera_hide_phone());
		AddChoice(
			builder,
			u"hide_phone_mode"_q,
			tr::lng_exd_hide_phone_mode,
			{
				&tr::lng_exd_hide_phone_blur,
				&tr::lng_exd_hide_phone_unknown,
			});
		AddChoice(
			builder,
			u"show_id_dc"_q,
			tr::lng_extera_show_id_dc,
			{
				&tr::lng_extera_id_hidden,
				&tr::lng_extera_id_telegram,
				&tr::lng_extera_id_bot,
			});
	});
	AddAbout(builder, tr::lng_extera_show_id_about());
}

void PartGeneral7(SectionBuilder &builder) {
	[[maybe_unused]] const auto controller = builder.controller();
	[[maybe_unused]] const auto settings = &Core::App().settings();

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_archive());
	builder.card([&] {
		const auto session = builder.session();
		const auto hideArchive = builder.addButton({
			.id = u"extera/hide_archive"_q,
			.title = tr::lng_extera_hide_archive(),
			.st = &st::settingsButtonNoIcon,
			.toggled = rpl::single(session->settings().archiveInMainMenu()),
			.keywords = { u"archive"_q },
		});
		if (hideArchive) {
			hideArchive->toggledValue(
			) | rpl::filter([=](bool value) {
				return (value != session->settings().archiveInMainMenu());
			}) | rpl::on_next([=](bool value) {
				session->settings().setArchiveInMainMenu(value);
				session->saveSettingsDelayed();
			}, hideArchive->lifetime());
		}
	});
	AddAbout(builder, tr::lng_extera_hide_archive_about());
}

void PartAppearance0(SectionBuilder &builder) {
	[[maybe_unused]] const auto controller = builder.controller();

	builder.addSkip();
	builder.card([&] {
		auto corners = std::vector<int>();
		for (auto i = 0; i <= 28; ++i) {
			corners.push_back(i);
		}
		AddSlider(
			builder,
			u"avatar_corners"_q,
			tr::lng_extera_avatar_corners,
			std::move(corners),
			{ &tr::lng_extera_corners_square, &tr::lng_extera_corners_circle },
			[](int value) { return QString::number(value); });
		AddPreview(
			builder,
			u"avatar_corners"_q,
			st::exteraPreviewAvatarHeight,
			PaintAvatarPreview);
		AddToggle(
			builder,
			u"single_corner_radius"_q,
			tr::lng_extera_single_corner());
	});
	AddAbout(builder, tr::lng_extera_single_corner_about());
}

void PartAppearance1(SectionBuilder &builder) {
	[[maybe_unused]] const auto controller = builder.controller();

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_chat_list());
	builder.card([&] {
		AddToggle(
			builder,
			u"hide_status"_q,
			tr::lng_extera_hide_status());
		AddToggle(
			builder,
			u"hide_stories"_q,
			tr::lng_extera_hide_stories());
		AddToggle(
			builder,
			u"sender_mini_avatars"_q,
			tr::lng_extera_mini_avatars());
	});
	AddAbout(builder, tr::lng_extera_chat_list_about());
}

void PartAppearance2(SectionBuilder &builder) {
	[[maybe_unused]] const auto controller = builder.controller();

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_folders());
	builder.card([&] {
		AddChoice(
			builder,
			u"folder_title_style"_q,
			tr::lng_extera_folder_title,
			{
				&tr::lng_extera_folder_title_both,
				&tr::lng_extera_folder_title_text,
				&tr::lng_extera_folder_title_icons,
			});
		AddLivePreview(
			builder,
			[] { return st::exteraPreviewAvatarHeight; },
			PaintFolderTabsPreview,
			{ u"folder_title_style"_q });
		AddChoice(
			builder,
			u"folder_counter"_q,
			tr::lng_extera_folder_counter,
			{
				&tr::lng_extera_counter_all,
				&tr::lng_extera_counter_unmuted,
			});
		AddToggle(
			builder,
			u"hide_all_chats"_q,
			tr::lng_extera_hide_all_chats());
	});
}

void PartAppearance3(SectionBuilder &builder) {
	[[maybe_unused]] const auto controller = builder.controller();

	builder.addSkip();
	builder.card([&] {
		builder.addButton({
			.id = u"extera/app_icon"_q,
			.title = tr::lng_extera_app_icon(),
			.icon = { &st::exteraIconAppearance },
			.onClick = [=] { ShowAppIconBox(controller); },
		});
	});

	builder.addDivider();
}

void PartAppearance4(SectionBuilder &builder) {
	[[maybe_unused]] const auto controller = builder.controller();

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_appearance());
	builder.card([&] {
		const auto settings = &Core::App().settings();
		AddCoreToggle(
			builder,
			u"system_fonts"_q,
			tr::lng_extera_system_fonts(),
			[=] {
				return settings->customFontFamily() == style::SystemFontTag();
			},
			[=](bool v) {
				settings->setCustomFontFamily(v
					? style::SystemFontTag()
					: QString());
				Core::App().saveSettings();
				controller->show(Ui::MakeConfirmBox({
					.text = tr::lng_extera_restart_required(),
					.confirmed = [] { Core::Restart(); },
					.confirmText = tr::lng_extera_restart(),
				}));
			});
		AddToggle(
			builder,
			u"system_emoji"_q,
			tr::lng_extera_system_emoji());
		AddSwitchGroup(
			builder,
			u"material_design"_q,
			tr::lng_extera_material,
			{
				{ u"md3_sliders"_q, &tr::lng_extera_md3_sliders },
				{ u"md3_switches"_q, &tr::lng_extera_md3_switches },
				{ u"md3_switches_muted"_q, &tr::lng_exd_md3_switches_muted },
			});
		AddToggle(
			builder,
			u"custom_chat_themes"_q,
			tr::lng_extera_custom_themes());
	});
	AddAbout(builder, tr::lng_extera_custom_themes_about());
}

void PartChats0(SectionBuilder &builder) {
	[[maybe_unused]] const auto actions = std::vector<const Phrase*>{
		&tr::lng_extera_action_reply,
		&tr::lng_extera_action_reaction,
		&tr::lng_extera_action_edit,
		&tr::lng_extera_action_copy,
		&tr::lng_extera_action_forward,
		&tr::lng_extera_action_none,
	};

	builder.addSkip();
	builder.card([&] {
		auto sizes = std::vector<int>();
		for (auto i = 4; i <= 20; ++i) {
			sizes.push_back(i);
		}
		AddSlider(
			builder,
			u"sticker_size"_q,
			tr::lng_extera_sticker_size,
			std::move(sizes),
			{ &tr::lng_extera_sticker_small, &tr::lng_extera_sticker_large },
			[](int value) { return QString::number(value); });
		AddLivePreview(
			builder,
			StickerPreviewHeight,
			[](QPainter &p, QRect frame) {
				PaintStickerPreview(p, frame, 0);
			},
			{ u"sticker_size"_q, u"sticker_time"_q });
		AddChoice(
			builder,
			u"sticker_time"_q,
			tr::lng_extera_sticker_time,
			{
				&tr::lng_extera_sticker_time_default,
				&tr::lng_extera_sticker_time_side,
				&tr::lng_extera_sticker_time_hidden,
			});
	});
	builder.addDivider();
}

void PartChats1(SectionBuilder &builder) {
	[[maybe_unused]] const auto actions = std::vector<const Phrase*>{
		&tr::lng_extera_action_reply,
		&tr::lng_extera_action_reaction,
		&tr::lng_extera_action_edit,
		&tr::lng_extera_action_copy,
		&tr::lng_extera_action_forward,
		&tr::lng_extera_action_none,
	};

	builder.addSkip();
	builder.card([&] {
		const auto aiController = builder.controller();
		const auto aiShowOther = builder.showOther();
		const auto aiButton = builder.addButton({
			.id = u"extera/ai_chat_open"_q,
			.title = tr::lng_extera_ai_chat(),
			.icon = { &st::menuIconBotCommands },
			.onClick = [=] {
				if (aiController) {
					Extera::ShowAiChat(aiController);
				}
			},
			.keywords = { u"ai"_q, u"chat"_q },
		});
		if (aiButton) {
			AddBetaBadge(aiButton, tr::lng_extera_ai_chat(), st::settingsButton);
			const auto controller = builder.controller();
			const auto menu = aiButton->lifetime().make_state<
				base::unique_qptr<Ui::PopupMenu>>();
			base::install_event_filter(aiButton, [=](not_null<QEvent*> e) {
				if (e->type() != QEvent::ContextMenu) {
					return base::EventFilterResult::Continue;
				}
				*menu = base::make_unique_q<Ui::PopupMenu>(
					aiButton,
					st::popupMenuWithIcons);
				(*menu)->addAction(tr::lng_exd_ai_settings(tr::now), [=] {
					if (aiShowOther) {
						aiShowOther(ExteraAiChatId());
					}
				}, &st::menuIconSettings);
				(*menu)->addAction(tr::lng_exd_ai_show_main(tr::now), [=] {
					Config().setBool(u"ai_chat_main"_q, true);
					aiButton->hide();
					if (controller) {
						controller->showToast(
							tr::lng_exd_ai_returned(tr::now));
					}
				}, &st::menuIconUnarchive);
				(*menu)->popup(QCursor::pos());
				return base::EventFilterResult::Cancel;
			});
		}
	});
	AddAbout(builder, tr::lng_extera_ai_settings_about());
}

void PartChats2(SectionBuilder &builder) {
	[[maybe_unused]] const auto actions = std::vector<const Phrase*>{
		&tr::lng_extera_action_reply,
		&tr::lng_extera_action_reaction,
		&tr::lng_extera_action_edit,
		&tr::lng_extera_action_copy,
		&tr::lng_extera_action_forward,
		&tr::lng_extera_action_none,
	};

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_stickers());
	builder.card([&] {
		const auto gifController = builder.controller();
		builder.addButton({
			.id = u"extera/gif_bot"_q,
			.title = tr::lng_exd_gif_bot(),
			.st = &st::settingsButtonNoIcon,
			.label = Config().stringChanges(
				u"gif_bot"_q
			) | rpl::map([](const QString &bot) {
				return bot.trimmed().isEmpty()
					? u"@gif"_q
					: (u"@"_q + bot.trimmed());
			}),
			.onClick = [=] { ShowGifBotBox(gifController); },
			.keywords = { u"gif"_q },
		});
		AddToggle(
			builder,
			u"unlimited_recent_stickers"_q,
			tr::lng_extera_unlimited_stickers());
		AddSwitchGroup(
			builder,
			u"hide_reactions"_q,
			tr::lng_extera_hide_reactions,
			{
				{ u"hide_reactions_channels"_q, &tr::lng_extera_hide_reactions_channels },
				{ u"hide_reactions_groups"_q, &tr::lng_extera_hide_reactions_groups },
				{ u"hide_reactions_private"_q, &tr::lng_extera_hide_reactions_private },
			});
	});
	AddAbout(builder, tr::lng_extera_hide_reactions_about());
}

void PartChats3(SectionBuilder &builder) {
	[[maybe_unused]] const auto actions = std::vector<const Phrase*>{
		&tr::lng_extera_action_reply,
		&tr::lng_extera_action_reaction,
		&tr::lng_extera_action_edit,
		&tr::lng_extera_action_copy,
		&tr::lng_extera_action_forward,
		&tr::lng_extera_action_none,
	};

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_gestures());
	builder.card([&] {
		AddChoice(
			builder,
			u"double_click_incoming"_q,
			tr::lng_extera_double_click_incoming,
			actions);
		AddChoice(
			builder,
			u"double_click_outgoing"_q,
			tr::lng_extera_double_click_outgoing,
			actions);
		AddChoice(
			builder,
			u"right_click_incoming"_q,
			tr::lng_exd_right_click_incoming,
			actions);
		AddChoice(
			builder,
			u"right_click_outgoing"_q,
			tr::lng_exd_right_click_outgoing,
			actions);
		AddLivePreview(
			builder,
			[] {
				return st::exteraPreviewBubble * 2
					+ st::exteraPreviewSkip * 3;
			},
			[=](QPainter &p, QRect frame) {
				PaintGesturesPreview(p, frame, actions);
			},
			{ u"double_click_incoming"_q, u"double_click_outgoing"_q });
	});
	AddAbout(builder, tr::lng_extera_double_tap_about());
}

void PartChats4(SectionBuilder &builder) {
	[[maybe_unused]] const auto actions = std::vector<const Phrase*>{
		&tr::lng_extera_action_reply,
		&tr::lng_extera_action_reaction,
		&tr::lng_extera_action_edit,
		&tr::lng_extera_action_copy,
		&tr::lng_extera_action_forward,
		&tr::lng_extera_action_none,
	};

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_chats_section());
	builder.card([&] {
		AddChoice(
			builder,
			u"bottom_button"_q,
			tr::lng_extera_bottom_button,
			{
				&tr::lng_extera_bottom_mute,
				&tr::lng_extera_bottom_discuss,
				&tr::lng_extera_bottom_hidden,
			});
		AddSwitchGroup(
			builder,
			u"wide_posts_group"_q,
			tr::lng_extera_wide_posts,
			{
				{ u"feed_wide_posts"_q, &tr::lng_extera_wide_feed },
				{ u"wide_posts"_q, &tr::lng_extera_wide_channels },
			});
		AddToggle(
			builder,
			u"disable_telegram_ai"_q,
			tr::lng_extera_disable_ai());
		AddToggle(
			builder,
			u"disable_greeting_sticker"_q,
			tr::lng_extera_disable_greeting());
		AddToggle(
			builder,
			u"comma_after_mention"_q,
			tr::lng_extera_comma_mention());
		AddToggle(
			builder,
			u"quote_reply"_q,
			tr::lng_exd_quote_reply(),
			{ u"quote"_q, u"reply"_q },
			tr::lng_exd_quote_reply_about(),
			true);
		AddToggle(
			builder,
			u"hide_copy_toast"_q,
			tr::lng_exd_hide_copy_toast());
		AddToggle(
			builder,
			u"chars_counter"_q,
			tr::lng_exd_chars_counter());
		AddToggle(
			builder,
			u"select_service"_q,
			tr::lng_exd_select_service(),
			{ u"select"_q, u"service"_q },
			tr::lng_exd_select_service_about(),
			true);
		AddToggle(
			builder,
			u"full_members"_q,
			tr::lng_exd_full_members(),
			{ u"members"_q, u"participants"_q },
			tr::lng_exd_full_members_about(),
			true);
		AddToggle(
			builder,
			u"name_accent"_q,
			tr::lng_exd_name_accent(),
			{ u"color"_q, u"name"_q },
			tr::lng_exd_name_accent_about(),
			true);
		AddToggle(
			builder,
			u"inline_math"_q,
			tr::lng_extera_inline_math(),
			{},
			tr::lng_extera_inline_math_about(),
			true);
		AddToggle(
			builder,
			u"hide_send_as"_q,
			tr::lng_extera_hide_send_as());
	});
	AddAbout(builder, tr::lng_extera_hide_send_as_about());
}

void PartChats5(SectionBuilder &builder) {
	[[maybe_unused]] const auto actions = std::vector<const Phrase*>{
		&tr::lng_extera_action_reply,
		&tr::lng_extera_action_reaction,
		&tr::lng_extera_action_edit,
		&tr::lng_extera_action_copy,
		&tr::lng_extera_action_forward,
		&tr::lng_extera_action_none,
	};

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_messages());
	builder.card([&] {
		AddToggle(
			builder,
			u"remove_message_tail"_q,
			tr::lng_extera_remove_tail());
		AddToggle(
			builder,
			u"edited_icon"_q,
			tr::lng_extera_edited_icon());
		AddToggle(
			builder,
			u"disable_colorful_replies"_q,
			tr::lng_exd_disable_colorful_replies());
		AddToggle(
			builder,
			u"translucent_deleted"_q,
			tr::lng_exd_translucent_deleted());
		AddToggle(
			builder,
			u"online_indicator"_q,
			tr::lng_extera_online_indicator());
		AddToggle(
			builder,
			u"forward_count"_q,
			tr::lng_extera_forward_count());
		AddToggle(
			builder,
			u"hide_share_button"_q,
			tr::lng_extera_hide_share());
		AddToggle(
			builder,
			u"poll_results_before_voting"_q,
			tr::lng_extera_poll_results(),
			{ u"poll"_q },
			tr::lng_extera_poll_results_about(),
			true);
		AddSwitchGroup(
			builder,
			u"message_menu"_q,
			tr::lng_extera_message_menu,
			{
				{ u"menu_copy_photo"_q, &tr::lng_extera_menu_copy_photo },
				{ u"menu_save"_q, &tr::lng_extera_menu_save },
				{ u"menu_repeat"_q, &tr::lng_extera_menu_repeat },
				{ u"menu_history"_q, &tr::lng_extera_menu_history },
				{ u"menu_details"_q, &tr::lng_extera_menu_details },
			});
		AddToggle(
			builder,
			u"menu_quick_row"_q,
			tr::lng_extera_quick_actions(),
			{ u"menu"_q },
			tr::lng_extera_quick_actions_about_toggle(),
			true);
		const auto controller = builder.controller();
		builder.addButton({
			.id = u"extera/menu_quick_items"_q,
			.title = tr::lng_extera_quick_actions_buttons(),
			.st = &st::settingsButtonNoIcon,
			.onClick = [=] { ShowQuickActionsBox(controller); },
			.keywords = { u"menu"_q },
		});
	});
	builder.addDivider();
}

void PartChats6(SectionBuilder &builder) {
	[[maybe_unused]] const auto actions = std::vector<const Phrase*>{
		&tr::lng_extera_action_reply,
		&tr::lng_extera_action_reaction,
		&tr::lng_extera_action_edit,
		&tr::lng_extera_action_copy,
		&tr::lng_extera_action_forward,
		&tr::lng_extera_action_none,
	};

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_photos());
	builder.card([&] {
		AddToggle(builder, u"always_send_hd"_q, tr::lng_extera_send_hd());
	});
	builder.addDivider();
}

void PartChats7(SectionBuilder &builder) {
	[[maybe_unused]] const auto actions = std::vector<const Phrase*>{
		&tr::lng_extera_action_reply,
		&tr::lng_extera_action_reaction,
		&tr::lng_extera_action_edit,
		&tr::lng_extera_action_copy,
		&tr::lng_extera_action_forward,
		&tr::lng_extera_action_none,
	};

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_videos());
	builder.card([&] {
		AddValueChoice(
			builder,
			u"video_seek_step"_q,
			tr::lng_extera_seek_step,
			{ 5, 10, 15, 30 },
			[](int value) {
				return tr::lng_extera_seconds(tr::now, lt_count, value);
			});
		AddToggle(
			builder,
			u"prefer_original_quality"_q,
			tr::lng_extera_original_quality());
		AddSwitchGroup(
			builder,
			u"auto_pause"_q,
			tr::lng_extera_auto_pause,
			{
				{ u"pause_on_minimize_video"_q, &tr::lng_extera_pause_video },
				{ u"pause_on_minimize_voice"_q, &tr::lng_extera_pause_voice },
				{ u"pause_on_minimize_round"_q, &tr::lng_extera_pause_round },
			});
	});
	AddAbout(builder, tr::lng_extera_auto_pause_about());
}

void ShowStringBox(
		not_null<Window::SessionController*> controller,
		const QString &key,
		rpl::producer<QString> title,
		rpl::producer<QString> placeholder) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(std::move(title));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			std::move(placeholder),
			Extera::String(key)));
		box->setFocusCallback([=] { field->setFocusFast(); });
		const auto save = [=] {
			Config().setString(key, field->getLastText().trimmed());
			box->closeBox();
		};
		field->submits() | rpl::on_next(save, field->lifetime());
		box->addButton(tr::lng_settings_save(), save);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void PartPluginsGuard(SectionBuilder &builder) {
	const auto controller = builder.controller();
	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_exd_guard_title());
	builder.card([&] {
		AddChoice(
			builder,
			u"plugins_guard"_q,
			tr::lng_exd_guard_mode,
			{
				&tr::lng_exd_guard_mode_auto,
				&tr::lng_exd_guard_mode_manual,
				&tr::lng_exd_guard_mode_off,
			},
			{ u"security"_q, u"guard"_q, u"plugins"_q });
		AddChoice(
			builder,
			u"guard_llm_provider"_q,
			tr::lng_exd_guard_llm,
			{
				&tr::lng_exd_guard_llm_none,
				&tr::lng_exd_guard_llm_groq,
				&tr::lng_exd_guard_llm_openrouter,
				&tr::lng_exd_guard_llm_deepseek,
				&tr::lng_exd_guard_llm_openai,
				&tr::lng_exd_guard_llm_mistral,
			},
			{ u"ai"_q, u"llm"_q });
		builder.addButton({
			.id = u"extera/guard_llm_key"_q,
			.title = tr::lng_exd_guard_llm_key(),
			.st = &st::settingsButtonNoIcon,
			.label = Config().stringChanges(
				u"guard_llm_key"_q
			) | rpl::map([](const QString &key) {
				return key.trimmed().isEmpty()
					? tr::lng_exd_streamer_hotkey_none(tr::now)
					: tr::lng_exd_premium_gemini_set(tr::now);
			}),
			.onClick = [=] {
				if (controller) {
					ShowStringBox(
						controller,
						u"guard_llm_key"_q,
						tr::lng_exd_guard_llm_key(),
						tr::lng_exd_guard_llm_key_placeholder());
				}
			},
		});
		builder.addButton({
			.id = u"extera/guard_llm_model"_q,
			.title = tr::lng_exd_guard_llm_model(),
			.st = &st::settingsButtonNoIcon,
			.label = Config().stringChanges(
				u"guard_llm_model"_q
			) | rpl::map([](const QString &model) {
				return model.trimmed().isEmpty()
					? tr::lng_exd_guard_llm_model_default(tr::now)
					: model.trimmed();
			}),
			.onClick = [=] {
				if (controller) {
					ShowStringBox(
						controller,
						u"guard_llm_model"_q,
						tr::lng_exd_guard_llm_model(),
						tr::lng_exd_guard_llm_model_placeholder());
				}
			},
		});
	});
	AddAbout(builder, tr::lng_exd_guard_about());
}

void PartPlugins0(SectionBuilder &builder) {
	[[maybe_unused]] const auto controller = builder.controller();

	builder.addSkip();
	builder.card([&] {
		const auto engine = builder.addButton({
			.id = u"extera/plugins_enabled"_q,
			.title = tr::lng_extera_plugins_engine(),
			.st = &st::settingsButtonNoIcon,
			.toggled = rpl::single(Extera::Bool(u"plugins_enabled"_q)),
			.keywords = { u"plugins"_q },
		});
		if (engine) {
			engine->toggledValue(
			) | rpl::filter([](bool value) {
				return value != Extera::Bool(u"plugins_enabled"_q);
			}) | rpl::on_next([=](bool value) {
				Config().setBool(u"plugins_enabled"_q, value);
				controller->show(Ui::MakeConfirmBox({
					.text = tr::lng_extera_restart_required(),
					.confirmed = [] { Core::Restart(); },
					.confirmText = tr::lng_extera_restart(),
				}));
			}, engine->lifetime());
		}
		builder.addSectionButton({
			.title = tr::lng_extera_plugins_engine_title(),
			.targetSection = ExteraPluginsEngineId(),
			.icon = { &st::menuIconSettings },
		});
		builder.addButton({
			.id = u"extera/plugins_install"_q,
			.title = tr::lng_extera_plugins_install_file(),
			.icon = { &st::menuIconDownload },
			.onClick = [=] {
				if (Extera::Plugins::Started()) {
					Extera::Plugins::ChooseAndInstall(controller);
				} else {
					controller->showToast(
						tr::lng_extera_plugins_not_running(tr::now));
				}
			},
		});
	});
}

void PartPlugins1(SectionBuilder &builder) {
	[[maybe_unused]] const auto controller = builder.controller();

	if (!Extera::Plugins::Available()) {
		AddAbout(builder, tr::lng_extera_plugins_runtime_missing());
	} else if (!Extera::Plugins::RuntimeError().isEmpty()) {
		AddAbout(builder, rpl::single(Extera::Plugins::RuntimeError()));
	} else {
		builder.addDivider();
	}
}

void PartPlugins2(SectionBuilder &builder) {
	[[maybe_unused]] const auto controller = builder.controller();

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_plugins());
	builder.add([=](const WidgetContext &ctx) {
		auto list = object_ptr<Ui::VerticalLayout>(ctx.container);
		Extera::Plugins::FillPluginsList(list.data(), controller);
		return SectionBuilder::WidgetToAdd{ .widget = std::move(list) };
	});
}

void PartOther0(SectionBuilder &builder) {
	[[maybe_unused]] const auto controller = builder.controller();

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_exd_badge_title());
	builder.card([&] {
		builder.addButton({
			.id = u"extera/badge_buy"_q,
			.title = tr::lng_exd_badge_get(),
			.icon = { &st::menuIconPremium },
			.onClick = [=] { ShowBadgeInfoBox(controller); },
			.keywords = { u"badge"_q, u"verified"_q },
		});
	});
	AddAbout(builder, tr::lng_exd_badge_about());

	builder.addSkip();
	builder.card([&] {
		builder.addButton({
			.id = u"extera/default_client"_q,
			.title = tr::lng_exd_default_client(),
			.icon = { &st::menuIconLink },
			.onClick = [=] {
				Extera::AppIcon::MakeDefaultClient();
				if (controller) {
					controller->showToast(tr::lng_exd_default_client_done(
						tr::now));
				}
			},
			.keywords = { u"default"_q, u"tg"_q, u"links"_q },
		});
	});
	AddAbout(builder, tr::lng_exd_default_client_about());
}

void PartOther1(SectionBuilder &builder) {
	[[maybe_unused]] const auto controller = builder.controller();

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_exd_donate());
	builder.card([&] {
		const auto wallet = [&](
				const QString &id,
				rpl::producer<QString> title,
				const QString &address) {
			builder.addButton({
				.id = u"extera/donate_"_q + id,
				.title = std::move(title),
				.icon = { &st::menuIconCopy },
				.label = rpl::single((id == u"card"_q)
					? address
					: (address.left(6)
						+ QString::fromUtf8("\xE2\x80\xA6")
						+ address.right(4))),
				.onClick = [=] {
					QGuiApplication::clipboard()->setText(
						QString(address).remove(' '));
					controller->showToast(
						tr::lng_exd_donate_copied(tr::now));
				},
				.keywords = { u"donate"_q, u"crypto"_q },
			});
		};
		wallet(
			u"card"_q,
			tr::lng_exd_donate_card(),
			u"2200 7021 9261 6667"_q);
		wallet(
			u"ton"_q,
			tr::lng_exd_donate_ton(),
			u"UQBBklguWPdtllk0jUozVZm6DLqbEzqsXUZ0lK6tiKGxAS0T"_q);
		wallet(
			u"usdt"_q,
			tr::lng_exd_donate_usdt(),
			u"TPbiCARyfZpUgFtQsuHsKVikphB6viD2jk"_q);
	});
	AddAbout(builder, tr::lng_exd_donate_about());
}

void PartOther2(SectionBuilder &builder) {
	[[maybe_unused]] const auto controller = builder.controller();

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_updates());
	builder.card([&] {
		builder.addButton({
			.id = u"extera/check_updates"_q,
			.title = tr::lng_extera_check_updates(),
			.icon = { &st::menuIconRestore },
			.onClick = [=] {
				using Result = Extera::Update::CheckResult;
				const auto weak = base::make_weak(controller);
				Extera::Update::CheckNow([=](Result result) {
					const auto strong = weak.get();
					if (!strong) {
						return;
					}
					switch (result) {
					case Result::Available:
						Extera::Update::Install();
						break;
					case Result::Latest:
						strong->showToast(
							tr::lng_extera_updates_latest(tr::now));
						break;
					case Result::Failed:
						strong->showToast(
							tr::lng_extera_updates_failed(tr::now));
						break;
					}
				});
			},
			.keywords = { u"update"_q, u"version"_q },
		});
		builder.addButton({
			.id = u"extera/changelog"_q,
			.title = tr::lng_exd_changelog_title(),
			.icon = { &st::menuIconInfo },
			.onClick = [=] {
				if (controller) {
					Extera::Changelog::Show(controller);
				}
			},
			.keywords = { u"changelog"_q, u"release"_q, u"what's new"_q },
		});
		AddChoice(
			builder,
			u"auto_update"_q,
			tr::lng_exd_auto_update,
			{
				&tr::lng_exd_auto_update_install,
				&tr::lng_exd_auto_update_download,
				&tr::lng_exd_auto_update_off,
			});
	});
	builder.addDivider();
}

void PartOther3(SectionBuilder &builder) {
	[[maybe_unused]] const auto controller = builder.controller();

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_backup());
	builder.card([&] {
		builder.addButton({
			.id = u"extera/export"_q,
			.title = tr::lng_extera_export(),
			.icon = { &st::exteraIconExport },
			.onClick = [=] {
				FileDialog::GetWritePath(
					Core::App().getFileDialogParent(),
					tr::lng_extera_export(tr::now),
					u"JSON (*.json)"_q,
					u"extera-settings.json"_q,
					[=](QString &&path) {
						auto file = QFile(path);
						if (file.open(QIODevice::WriteOnly)) {
							file.write(Config().exportJson());
							controller->showToast(
								tr::lng_extera_exported(tr::now));
						}
					});
			},
		});
		builder.addButton({
			.id = u"extera/import"_q,
			.title = tr::lng_extera_import(),
			.icon = { &st::exteraIconImport },
			.onClick = [=] {
				FileDialog::GetOpenPath(
					Core::App().getFileDialogParent(),
					tr::lng_extera_import(tr::now),
					u"JSON (*.json)"_q,
					[=](FileDialog::OpenResult &&result) {
						auto data = result.remoteContent;
						if (data.isEmpty() && !result.paths.isEmpty()) {
							auto file = QFile(result.paths.front());
							if (file.open(QIODevice::ReadOnly)) {
								data = file.readAll();
							}
						}
						controller->showToast(Config().importJson(data)
							? tr::lng_extera_imported(tr::now)
							: tr::lng_extera_import_failed(tr::now));
					});
			},
		});
		builder.addButton({
			.id = u"extera/reset"_q,
			.title = tr::lng_extera_reset(),
			.icon = { &st::exteraIconReset },
			.onClick = [=] {
				controller->show(Ui::MakeConfirmBox({
					.text = tr::lng_extera_reset_sure(),
					.confirmed = [=](Fn<void()> close) {
						Config().reset();
						close();
					},
					.confirmText = tr::lng_extera_reset(),
					.confirmStyle = &st::attentionBoxButton,
				}));
			},
		});
	});
}

void PartMain2(SectionBuilder &builder) {
	[[maybe_unused]] const auto showOther = builder.showOther();

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_port());
	builder.card([&] {
		AddLink(
			builder,
			u"developer"_q,
			tr::lng_extera_port_developer(),
			u"@metawka"_q,
			u"https://t.me/metawka"_q,
			{ &st::exteraIconDeveloper });
		AddLink(
			builder,
			u"channel"_q,
			tr::lng_extera_channel(),
			u"@exDesktop"_q,
			u"https://t.me/exDesktop"_q,
			{ &st::exteraIconChannel });
		AddLink(
			builder,
			u"forum"_q,
			tr::lng_extera_forum(),
			u"@exDesktopChat"_q,
			u"https://t.me/exDesktopChat"_q,
			{ &st::exteraIconForum });
		AddLink(
			builder,
			u"website"_q,
			tr::lng_extera_website(),
			u"exteradesktop.xyz"_q,
			u"https://exteradesktop.xyz"_q,
			{ &st::exteraIconWebsite });
	});
}

// Search

[[nodiscard]] QString NormalizeQuery(QString text) {
	text = text.toLower();
	text.replace(QChar(0x0451), QChar(0x0435));
	return text;
}

[[nodiscard]] QStringList QueryWords(const QString &text) {
	static const auto split = QRegularExpression(u"[^\\p{L}\\p{N}]+"_q);
	return NormalizeQuery(text).split(split, Qt::SkipEmptyParts);
}

[[nodiscard]] const std::vector<QStringList> &SynonymGroups() {
	static const auto result = std::vector<QStringList>{
		{ u"скры"_q, u"спрят"_q, u"убра"_q, u"hide"_q, u"hidden"_q },
		{ u"телефон"_q, u"номер"_q, u"phone"_q, u"number"_q },
		{ u"секунд"_q, u"second"_q, u"время"_q, u"time"_q },
		{ u"перев"_q, u"translat"_q, u"язык"_q, u"language"_q },
		{ u"эмодзи"_q, u"emoji"_q, u"смайл"_q },
		{ u"стикер"_q, u"sticker"_q },
		{ u"прокси"_q, u"proxy"_q, u"сеть"_q, u"network"_q, u"соедин"_q },
		{ u"загруз"_q, u"скач"_q, u"выгруз"_q, u"download"_q, u"upload"_q },
		{ u"уведом"_q, u"notif"_q, u"звук"_q, u"sound"_q },
		{ u"тем"_q, u"theme"_q, u"цвет"_q, u"color"_q, u"оформл"_q },
		{ u"иконк"_q, u"значок"_q, u"icon"_q, u"logo"_q },
		{ u"аватар"_q, u"avatar"_q, u"скругл"_q, u"corner"_q, u"round"_q },
		{ u"папк"_q, u"вкладк"_q, u"folder"_q, u"tab"_q },
		{ u"архив"_q, u"archive"_q },
		{ u"стример"_q, u"streamer"_q, u"stream"_q },
		{ u"плагин"_q, u"plugin"_q },
		{ u"нейросет"_q, u"gpt"_q, u"модел"_q, u"model"_q, u"ai"_q, u"ии"_q },
		{ u"обнов"_q, u"update"_q },
		{ u"бейдж"_q, u"галочк"_q, u"badge"_q, u"verif"_q },
		{ u"донат"_q, u"поддерж"_q, u"donat"_q, u"support"_q },
		{ u"сообщен"_q, u"message"_q },
		{ u"участник"_q, u"member"_q, u"participant"_q },
		{ u"ответ"_q, u"цитат"_q, u"reply"_q, u"quote"_q },
		{ u"вычисл"_q, u"калькул"_q, u"формул"_q, u"math"_q, u"calc"_q },
		{ u"счетчик"_q, u"символ"_q, u"counter"_q, u"char"_q },
		{ u"фото"_q, u"изображ"_q, u"photo"_q, u"image"_q },
		{ u"видео"_q, u"video"_q, u"кружок"_q, u"round"_q },
		{ u"качеств"_q, u"quality"_q, u"hd"_q },
		{ u"резерв"_q, u"экспорт"_q, u"импорт"_q, u"сброс"_q,
			u"backup"_q, u"export"_q, u"import"_q, u"reset"_q },
		{ u"жест"_q, u"двойн"_q, u"gesture"_q, u"double"_q },
		{ u"реакц"_q, u"reaction"_q },
		{ u"шрифт"_q, u"font"_q },
		{ u"гиф"_q, u"gif"_q },
	};
	return result;
}

[[nodiscard]] QStringList ExpandWord(const QString &word) {
	auto result = QStringList{ word };
	for (const auto &group : SynonymGroups()) {
		const auto hit = ranges::any_of(group, [&](const QString &stem) {
			return word.startsWith(stem)
				|| (word.size() >= 3 && stem.startsWith(word));
		});
		if (hit) {
			for (const auto &stem : group) {
				if (!result.contains(stem)) {
					result.push_back(stem);
				}
			}
		}
	}
	return result;
}

[[nodiscard]] int EditDistance(const QString &a, const QString &b) {
	const auto n = int(a.size());
	const auto m = int(b.size());
	auto prev2 = std::vector<int>(m + 1);
	auto prev = std::vector<int>(m + 1);
	auto cur = std::vector<int>(m + 1);
	for (auto j = 0; j <= m; ++j) {
		prev[j] = j;
	}
	for (auto i = 1; i <= n; ++i) {
		cur[0] = i;
		for (auto j = 1; j <= m; ++j) {
			const auto cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
			cur[j] = std::min({
				prev[j] + 1,
				cur[j - 1] + 1,
				prev[j - 1] + cost,
			});
			if (i > 1
				&& j > 1
				&& a[i - 1] == b[j - 2]
				&& a[i - 2] == b[j - 1]) {
				cur[j] = std::min(cur[j], prev2[j - 2] + 1);
			}
		}
		prev2 = prev;
		prev = cur;
	}
	return prev[m];
}

[[nodiscard]] bool FuzzyPrefix(const QString &word, const QString &term) {
	if (word.size() < 4) {
		return false;
	}
	const auto allowed = (word.size() >= 7) ? 2 : 1;
	for (auto delta = -1; delta <= 1; ++delta) {
		const auto length = int(word.size()) + delta;
		if (length <= 0 || length > term.size()) {
			continue;
		}
		if (EditDistance(word, term.left(length)) <= allowed) {
			return true;
		}
	}
	return false;
}

struct SearchMatch {
	Builder::SearchEntry entry;
	int score = 0;
	QStringList matched;
};

[[nodiscard]] std::optional<SearchMatch> MatchEntry(
		const Builder::SearchEntry &entry,
		const QStringList &words) {
	auto terms = QueryWords(entry.title);
	for (const auto &keyword : entry.keywords) {
		terms += QueryWords(keyword);
	}
	terms += QueryWords(QString(entry.id).replace('_', ' ').replace('/', ' '));
	auto result = SearchMatch{ .entry = entry };
	for (const auto &word : words) {
		auto best = 0;
		auto stem = QString();
		const auto expanded = ExpandWord(word);
		for (const auto &term : terms) {
			if (term.startsWith(word)) {
				best = 3;
				stem = word;
				break;
			}
			for (const auto &synonym : expanded) {
				if (synonym != word && term.startsWith(synonym)) {
					if (best < 2) {
						best = 2;
						stem = synonym;
					}
				}
			}
			if (best < 1 && FuzzyPrefix(word, term)) {
				best = 1;
				stem = term.left(word.size());
			}
		}
		if (!best) {
			return std::nullopt;
		}
		result.score += best;
		result.matched.push_back(stem);
	}
	return result;
}

[[nodiscard]] TextWithEntities HighlightTitle(
		const QString &title,
		const QStringList &stems) {
	auto result = TextWithEntities{ title };
	const auto normalized = NormalizeQuery(title);
	for (const auto &stem : stems) {
		if (stem.isEmpty()) {
			continue;
		}
		auto from = 0;
		while (true) {
			const auto index = normalized.indexOf(stem, from);
			if (index < 0) {
				break;
			}
			const auto boundary = !index
				|| !normalized[index - 1].isLetterOrNumber();
			if (boundary) {
				result.entities.push_back(EntityInText(
					EntityType::Bold,
					index,
					int(stem.size())));
				break;
			}
			from = index + 1;
		}
	}
	return result;
}

[[nodiscard]] QString EntryPath(Type section) {
	const auto &registry = Builder::SearchRegistry::Instance();
	const auto root = tr::lng_extera_title(tr::now);
	auto parts = registry.sectionPath(section).split(u" > "_q);
	const auto index = parts.indexOf(root);
	if (index >= 0) {
		parts = parts.mid(index + 1);
	}
	return parts.join(u" › "_q);
}

void FillSearchResults(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		Fn<void(Type)> showOther,
		const std::vector<Type> &sections,
		const QString &query) {
	container->clear();
	const auto words = QueryWords(query);
	if (words.isEmpty()) {
		container->resizeToWidth(container->width());
		return;
	}
	auto matches = std::vector<SearchMatch>();
	const auto &registry = Builder::SearchRegistry::Instance();
	for (const auto &entry : registry.collectAll(&controller->session())) {
		if (entry.id.isEmpty() || !ranges::contains(sections, entry.section)) {
			continue;
		}
		if (auto match = MatchEntry(entry, words)) {
			matches.push_back(std::move(*match));
		}
	}
	ranges::stable_sort(matches, ranges::greater(), &SearchMatch::score);
	if (matches.empty()) {
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				tr::lng_search_tab_no_results(),
				st::settingsSearchNoResults),
			st::settingsSearchNoResultsPadding);
	}
	const auto &st = st::exteraButtonWithSubtitle;
	for (const auto &match : matches | ranges::views::take(40)) {
		const auto entry = match.entry;
		const auto key = entry.id.startsWith(u"extera/"_q)
			? entry.id.mid(7)
			: QString();
		const auto toggle = !key.isEmpty() && Extera::IsBoolKey(key);
		const auto button = container->add(object_ptr<Ui::SettingsButton>(
			container,
			rpl::single(QString()),
			st));
		const auto title = Ui::CreateChild<Ui::FlatLabel>(
			button,
			st::exteraSearchTitle);
		title->setMarkedText(HighlightTitle(entry.title, match.matched));
		title->setAttribute(Qt::WA_TransparentForMouseEvents);
		title->show();
		const auto path = Ui::CreateChild<Ui::FlatLabel>(
			button,
			EntryPath(entry.section),
			st::exteraSubtitle);
		path->setAttribute(Qt::WA_TransparentForMouseEvents);
		path->show();
		button->widthValue(
		) | rpl::on_next([=, &st](int width) {
			const auto available = width
				- st.padding.left()
				- st.padding.right()
				- (toggle ? (st.toggleSkip + st.toggle.diameter * 2) : 0);
			title->resizeToWidth(available);
			path->resizeToWidth(available);
			title->moveToLeft(st.padding.left(), st.padding.top());
			path->moveToLeft(
				st.padding.left(),
				st.padding.top() + st.height + st::exteraSubtitleSkip);
		}, button->lifetime());
		if (toggle) {
			button->toggleOn(rpl::single(Extera::Bool(key)));
			button->toggledChanges(
			) | rpl::on_next([=](bool value) {
				Extera::Config::Instance().setBool(key, value);
			}, button->lifetime());
		} else {
			button->setClickedCallback([=] {
				crl::on_main(controller, [=] {
					controller->setHighlightControlId(entry.id);
					showOther(entry.section);
				});
			});
		}
	}
	container->resizeToWidth(container->width());
}

[[nodiscard]] std::vector<Type> AllCategorySections() {
	return {
		ExteraId(),
		ExteraGeneralId(),
		ExteraAppearanceId(),
		ExteraChatsId(),
		ExteraPluginsId(),
		ExteraPluginsEngineId(),
		ExteraNavigationId(),
		ExteraAiChatId(),
		ExteraPrivacyId(),
		ExteraNetworkId(),
		ExteraUpdatesId(),
		ExteraSupportId(),
	};
}

void AddCategorySearch(
		SectionBuilder &builder,
		std::vector<Type> sections,
		FnMut<void()> content) {
	const auto query = std::make_shared<rpl::variable<QString>>();
	if (const auto controller = builder.controller()) {
		const auto showOther = builder.showOther();
		builder.add([=](const WidgetContext &ctx) {
			auto result = object_ptr<Ui::VerticalLayout>(ctx.container);
			const auto container = result.data();
			auto search = CreateSectionSearchRow(container);
			const auto row = search.row;
			container->add(
				object_ptr<Ui::RpWidget>::fromRaw(row.get()),
				st::exteraSearchFieldPadding);
			const auto results = container->add(
				object_ptr<Ui::VerticalLayout>(container));
			const auto searchController = container->lifetime().make_state<
				std::unique_ptr<Ui::SearchFieldController>>(
					std::move(search.controller));
			(*searchController)->queryChanges(
			) | rpl::on_next([=](const QString &value) {
				*query = value;
				FillSearchResults(
					results,
					controller,
					showOther,
					sections,
					value);
			}, row->lifetime());
			return SectionBuilder::WidgetToAdd{ .widget = std::move(result) };
		});
	}
	builder.scope(std::move(content), query->value(
	) | rpl::map([](const QString &value) {
		return value.trimmed().isEmpty();
	}));
}

// Grid

const char *kCardSvgs[] = {
	R"exd(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24"><path fill="#ffffff" d="M480-60.78q-85.86 0-162.3-33.2-76.44-33.19-133.48-90.24-57.05-57.04-90.24-133.49-33.2-76.45-33.2-162.32 0-86.93 33.63-163.32 33.63-76.39 91.68-133.22 58.04-56.82 135.39-89.74 77.35-32.91 165.16-32.91 83.62 0 158.3 28.91 74.67 28.92 131.28 79.96 56.61 51.05 89.8 120.77 33.2 69.72 33.2 150.45 0 118.39-70.85 188.37t-178.2 69.98h-60.99q-6.74 0-9.96 3.87-3.22 3.87-3.22 9.3 0 10.87 11.61 26.59t11.61 41.89q0 54.52-33.72 86.43-33.72 31.92-85.5 31.92ZM309.78-457q17-17 17-43t-17-43q-17-17-43-17t-43 17q-17 17-17 43t17 43q17 17 43 17t43-17Zm116.61-154.91q17-17 17-43t-17-43q-17-17-43-17t-43 17q-17 17-17 43t17 43q17 17 43 17t43-17Zm193.22 0q17-17 17-43t-17-43q-17-17-43-17t-43 17q-17 17-17 43t17 43q17 17 43 17t43-17ZM734.52-457q17-17 17-43t-17-43q-17-17-43-17t-43 17q-17 17-17 43t17 43q17 17 43 17t43-17Z"/></svg>)exd",
	R"exd(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24"><path fill="#ffffff" d="m220.78-220.78-69.39 69.39q-25.22 25.22-57.91 11.61-32.7-13.61-32.7-49.09v-604.35q0-44.3 30.85-75.15 30.85-30.85 75.15-30.85h626.44q44.3 0 75.15 30.85 30.85 30.85 30.85 75.15v466.44q0 44.3-30.85 75.15-30.85 30.85-75.15 30.85H220.78ZM280-400h240q17 0 28.5-11.5T560-440q0-17-11.5-28.5T520-480H280q-17 0-28.5 11.5T240-440q0 17 11.5 28.5T280-400Zm0-120h400q17 0 28.5-11.5T720-560q0-17-11.5-28.5T680-600H280q-17 0-28.5 11.5T240-560q0 17 11.5 28.5T280-520Zm0-120h400q17 0 28.5-11.5T720-680q0-17-11.5-28.5T680-720H280q-17 0-28.5 11.5T240-680q0 17 11.5 28.5T280-640Z"/></svg>)exd",
	R"exd(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24"><path fill="#ffffff" d="m600.74-159.04-22.13 63.65q-5.13 15.52-18.81 25.06-13.67 9.55-30.19 9.55-26.78 0-42.96-21.87-16.17-21.87-6.48-48.09l144.09-398.04q6.7-17.22 21.5-27.61t33.02-10.39h41.87q18.22 0 33.02 10.39 14.81 10.39 21.51 27.61l144.08 397.91q9.7 25.78-6.26 47.94-15.96 22.15-43.3 22.15-16.96 0-30.7-9.55-13.74-9.54-19.87-26.06l-21.56-62.65H600.74Zm-252.31-213.7L188-212.74q-15.52 15.52-36.83 15.46-21.3-.07-37.26-16.02-15.52-15.53-15.52-37.33 0-21.8 15.52-37.33l161-161q-40.65-41.78-70.56-87.06-29.92-45.28-51.66-97.2h116.79q15.48 29.96 34.91 56.13 19.44 26.18 45.17 53.48 31.87-34.13 64.83-88.83 32.96-54.69 49.26-100.78H70.39q-22.09 0-37.54-15.45-15.46-15.46-15.46-37.55 0-22.08 15.46-37.54 15.45-15.46 37.54-15.46h227v-27q0-22.08 15.46-37.54 15.45-15.46 37.54-15.46t37.54 15.46q15.46 15.46 15.46 37.54v27h227q22.09 0 37.54 15.46 15.46 15.46 15.46 37.54 0 22.09-15.46 37.55-15.45 15.45-37.54 15.45H575.3q-22.13 69.18-64.97 144.61-42.85 75.44-87.24 121.09l68.87 70.3-39.61 110.83-103.92-106.35Zm285.22 117.7h130.44l-65.22-189.31-65.22 189.31Z"/></svg>)exd",
	R"exd(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24"><path fill="#ffffff" d="M246.78-62.48q-43.72 0-74.86-31.14-31.14-31.13-31.14-74.86v-386.43q0-43.73 31.14-74.87 31.14-31.13 74.86-31.13h24.74v-63.61q0-87.52 60.76-148.85T480-934.7q86.96 0 147.72 61.33 60.76 61.33 60.76 148.85v63.61h24.74q43.72 0 74.86 31.13 31.14 31.14 31.14 74.87v386.43q0 43.73-31.14 74.86-31.14 31.14-74.86 31.14H246.78ZM536.5-305.2q23.5-23.5 23.5-56.5t-23.5-56.5Q513-441.7 480-441.7t-56.5 23.5Q400-394.7 400-361.7t23.5 56.5q23.5 23.5 56.5 23.5t56.5-23.5ZM377.52-660.91h204.96v-63.61q0-43.41-29.63-73.79Q523.22-828.7 480-828.7t-72.85 30.39q-29.63 30.38-29.63 73.79v63.61Z"/></svg>)exd",
	R"exd(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24"><path fill="#ffffff" d="M409-149q-29-29-29-71t29-71q29-29 71-29t71 29q29 29 29 71t-29 71q-29 29-71 29t-71-29Zm209.83-388.13q65.52 22.87 119.95 62.61 22.26 16.13 23.05 44.02.78 27.89-19.48 48.15-19.83 19.83-48.22 21.18-28.39 1.34-51.78-13.79-35.18-22.6-75.81-35.06T480-422.48q-45.35 0-85.7 12.18-40.34 12.17-75.52 34.78-23.95 15.13-52.63 13.85-28.67-1.29-48.5-21.11-19.82-20.26-19.26-47.87.57-27.61 22.83-43.74 54.43-39.74 119.95-62.68Q406.7-560 480-560q73.3 0 138.83 22.87Zm92.43-223q108.52 39.87 195.96 112.04 22.82 18.7 24.39 46.81 1.56 28.11-19.26 48.93-19.83 19.83-48.22 20.61-28.39.78-51.78-16.91-69.74-54.48-153.59-84.15-83.85-29.68-178.76-29.68T301.24-632.8q-83.85 29.67-153.59 84.15-23.39 17.69-51.5 16.91-28.11-.78-47.93-20.61-20.83-20.82-19.55-48.93 1.29-28.11 24.11-46.81 87.44-72.17 195.96-112.04Q357.26-800 480-800t231.26 39.87Z"/></svg>)exd",
	R"exd(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24"><path fill="#ffffff" d="M193.78-100.78q-39.08 0-66.32-27.24-27.24-27.24-27.24-66.33v-121.61q0-15.52 10.54-26.84 10.54-11.33 26.07-15.03 36.73-9.69 61.47-36.78 24.74-27.09 24.74-64.26 0-37.3-24.45-63.89-24.46-26.59-61.2-36.72-14.96-4.13-25.78-15.74-10.83-11.61-10.83-26.56v-124.44q0-38.65 27.18-65.82 27.17-27.18 66.39-27.18h114.08q10.96-51 53.07-87.19 42.11-36.2 97.37-36.2 58.22 0 100.89 36.2 42.68 36.19 50.11 87.19h115.78q38.65 0 66.11 27.18 27.46 27.17 27.46 65.82v131.74q36 20.26 58.85 56.52 22.84 36.26 22.84 79.09 0 44.96-22.84 82.07-22.85 37.1-58.85 53.54v128.91q0 39.09-27.46 66.33-27.46 27.24-66.11 27.24H193.78Z"/></svg>)exd",
	R"exd(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24"><path fill="#ffffff" d="M480.57-100.78q-78.4 0-147.57-29.91-69.17-29.92-120.5-81.24-51.33-51.33-81.24-120.5-29.91-69.18-29.91-147.57 0-78.39 29.91-147.57 29.91-69.17 81.24-120.5 51.33-51.32 120.5-81.24 69.17-29.91 147.57-29.91 83.13 0 157.19 33.87 74.07 33.87 127.89 96.74v-41.56q0-18.7 12.92-31.33 12.91-12.63 31.6-12.63 18.7 0 31.33 12.63 12.63 12.63 12.63 31.33V-613q0 22.09-15.46 37.54Q823.22-560 801.13-560H643.96q-18.7 0-31.89-13.2-13.2-13.19-13.2-31.89 0-18.69 12.91-31.61 12.92-12.91 31.61-12.91h49.65q-39.3-49.22-94.78-76.41-55.48-27.2-117.69-27.2-114.18 0-193.7 79.52-79.52 79.53-79.52 193.7 0 114.17 79.52 193.7 79.52 79.52 193.7 79.52 89.91 0 160.67-52.2 70.76-52.19 97.59-135.41 6.69-19.96 24.5-30.5 17.8-10.54 38.89-7.41 22.08 3.13 35.48 19.3 13.39 16.17 7.69 35.7-32.95 121.26-133.91 198.89-100.96 77.63-230.91 77.63ZM524.52-496l94.91 94.35q12.7 12.69 12.7 31.39 0 18.69-12.7 31.39-12.69 12.7-31.39 12.7-18.69 0-31.39-12.7L451.43-444.09q-7.69-7.69-11.54-17.17-3.85-9.48-3.85-20.31v-154.47q0-18.7 12.92-31.33Q461.87-680 480.57-680q18.69 0 31.32 12.63 12.63 12.63 12.63 31.33V-496Z"/></svg>)exd",
	R"exd(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24"><path fill="#ffffff" d="M441.61-137.02q-18.74-6.98-33.7-21.37l-70.13-64.13Q232.91-317.83 146.85-415.3 60.78-512.78 60.78-634q0-102.48 68.09-171.13 68.09-68.65 170.56-68.65 51.87 0 98.31 19.95 46.43 19.96 81.69 56.7 35.27-36.74 81.7-56.7 46.44-19.95 98.3-19.95 102.48 0 171.42 68.65 68.93 68.65 68.93 171.13 0 121.22-86.41 219.2-86.41 97.97-193.28 193.84l-68.57 63.13q-14.95 14.4-33.98 21.09-19.02 6.7-38.11 6.7-19.08 0-37.82-6.98Z"/></svg>)exd",
};

struct CategoryCard {
	rpl::producer<QString> (*title)();
	rpl::producer<QString> (*about)();
	const style::icon *icon = nullptr;
	Type (*target)() = nullptr;
	QColor color;
};

void AddCategoryGrid(SectionBuilder &builder) {
	const auto controller = builder.controller();
	if (!controller) {
		return;
	}
	const auto showOther = builder.showOther();
	builder.add([=](const WidgetContext &ctx) {
		const auto cards = std::make_shared<std::vector<CategoryCard>>(
			std::vector<CategoryCard>{
			{ [] { return tr::lng_extera_appearance(); },
				[] { return tr::lng_exd_cat_appearance_about(); },
				&st::menuIconPalette,
				ExteraAppearanceId,
				QColor(0x8E, 0x6C, 0xEF) },
			{ [] { return tr::lng_exd_cat_messages(); },
				[] { return tr::lng_exd_cat_messages_about(); },
				&st::exteraIconChats,
				ExteraGeneralId,
				QColor(0x3A, 0x9B, 0xF0) },
			{ [] { return tr::lng_exd_cat_translate(); },
				[] { return tr::lng_exd_cat_translate_about(); },
				&st::exteraIconTranslate,
				ExteraChatsId,
				QColor(0x26, 0xB0, 0xA2) },
			{ [] { return tr::lng_exd_cat_privacy(); },
				[] { return tr::lng_exd_cat_privacy_about(); },
				&st::menuIconLock,
				ExteraPrivacyId,
				QColor(0x5B, 0x6B, 0xE8) },
			{ [] { return tr::lng_exd_cat_network(); },
				[] { return tr::lng_exd_cat_network_about(); },
				&st::menuIconNetwork,
				ExteraNetworkId,
				QColor(0xF0, 0x8A, 0x3A) },
			{ [] { return tr::lng_exd_cat_plugins(); },
				[] { return tr::lng_exd_cat_plugins_about(); },
				&st::exteraIconPlugins,
				ExteraPluginsId,
				QColor(0xE2, 0x55, 0x7B) },
			{ [] { return tr::lng_exd_cat_updates(); },
				[] { return tr::lng_exd_cat_updates_about(); },
				&st::menuIconRestore,
				ExteraUpdatesId,
				QColor(0x3C, 0xB3, 0x71) },
			{ [] { return tr::lng_exd_cat_support(); },
				[] { return tr::lng_exd_cat_support_about(); },
				&st::menuIconLike,
				ExteraSupportId,
				QColor(0xF2, 0x54, 0x5B) },
		});
		const auto count = int(cards->size());
		const auto rows = (count + 1) / 2;
		const auto cardHeight = std::max(
			st::exteraCategoryCardHeight,
			(st::exteraCategoryCardPadding * 3 / 2
				+ st::semiboldFont->height
				+ st::exteraCategoryAboutSkip
				+ 2 * st::exteraCategoryAboutFont->height));
		const auto skip = st::exteraCategoryCardSkip;
		auto result = object_ptr<Ui::FixedHeightWidget>(
			ctx.container,
			rows * cardHeight + (rows - 1) * skip);
		const auto grid = result.data();
		struct State {
			int over = -1;
			int pressed = -1;
			std::vector<QString> titles;
			std::vector<QString> abouts;
			std::vector<QImage> icons;
		};
		const auto state = grid->lifetime().make_state<State>();
		for (const auto &card : *cards) {
			state->titles.push_back(QString());
			state->abouts.push_back(QString());
		}
		for (auto i = 0; i != count; ++i) {
			(*cards)[i].title() | rpl::on_next([=](const QString &text) {
				state->titles[i] = text;
				grid->update();
			}, grid->lifetime());
			(*cards)[i].about() | rpl::on_next([=](const QString &text) {
				state->abouts[i] = text;
				grid->update();
			}, grid->lifetime());
		}
		const auto cardRect = [=](int index) {
			const auto width = (grid->width() - skip) / 2;
			return QRect(
				(index % 2) * (width + skip),
				(index / 2) * (cardHeight + skip),
				width,
				cardHeight);
		};
		const auto cardAt = [=](QPoint point) {
			for (auto i = 0; i != count; ++i) {
				if (cardRect(i).contains(point)) {
					return i;
				}
			}
			return -1;
		};
		grid->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(grid);
			auto hq = PainterHighQualityEnabler(p);
			const auto radius = st::exteraCategoryCardRadius;
			const auto tile = st::exteraCategoryTileSize;
			const auto padding = st::exteraCategoryCardPadding;
			for (auto i = 0; i != count; ++i) {
				const auto rect = cardRect(i);
				p.setPen(Qt::NoPen);
				p.setBrush(st::windowBgOver);
				p.drawRoundedRect(rect, radius, radius);
				if (i == state->over) {
					auto over = st::windowFg->c;
					over.setAlphaF(0.07);
					p.setBrush(over);
					p.drawRoundedRect(rect, radius, radius);
				}
				const auto tileRect = QRect(
					rect.x() + padding,
					rect.y() + (rect.height() - tile) / 2,
					tile,
					tile);
				p.setBrush((*cards)[i].color);
				p.drawRoundedRect(
					tileRect,
					st::exteraCategoryTileRadius,
					st::exteraCategoryTileRadius);
				const auto iconSize = tile * 3 / 5;
				const auto ratio = style::DevicePixelRatio();
				if (state->icons.size() <= size_t(i)) {
					state->icons.resize(count);
				}
				auto &image = state->icons[i];
				if (image.width() != iconSize * ratio) {
					image = QImage(
						QSize(iconSize, iconSize) * ratio,
						QImage::Format_ARGB32_Premultiplied);
					image.fill(Qt::transparent);
					image.setDevicePixelRatio(ratio);
					auto q = QPainter(&image);
					QSvgRenderer(QByteArray(kCardSvgs[i])).render(
						&q,
						QRectF(0, 0, iconSize, iconSize));
				}
				p.drawImage(
					QRect(
						tileRect.x() + (tile - iconSize) / 2,
						tileRect.y() + (tile - iconSize) / 2,
						iconSize,
						iconSize),
					image);
				const auto textLeft = tileRect.x() + tile + st::exteraCategoryTitleSkip;
				const auto textWidth = rect.x() + rect.width() - padding - textLeft;
				const auto &aboutFont = st::exteraCategoryAboutFont;
				const auto aboutText = [&] {
					const auto &text = state->abouts[i];
					if (aboutFont->width(text) <= textWidth) {
						return QStringList{ text };
					}
					auto first = QString();
					const auto words = text.split(' ', Qt::SkipEmptyParts);
					auto used = 0;
					for (; used != int(words.size()); ++used) {
						const auto next = first.isEmpty()
							? words[used]
							: (first + ' ' + words[used]);
						if (aboutFont->width(next) > textWidth) {
							break;
						}
						first = next;
					}
					if (first.isEmpty()) {
						return QStringList{ aboutFont->elided(text, textWidth) };
					}
					const auto rest = QStringList(
						words.begin() + used,
						words.end()).join(' ');
					return QStringList{
						first,
						aboutFont->elided(rest, textWidth),
					};
				}();
				const auto aboutLines = int(aboutText.size());
				const auto textHeight = st::semiboldFont->height
					+ st::exteraCategoryAboutSkip
					+ aboutLines * aboutFont->height;
				auto top = rect.y() + (rect.height() - textHeight) / 2;
				p.setPen(st::windowBoldFg);
				p.setFont(st::semiboldFont);
				p.drawText(
					QRect(textLeft, top, textWidth, st::semiboldFont->height),
					Qt::AlignLeft | Qt::AlignTop,
					st::semiboldFont->elided(state->titles[i], textWidth));
				top += st::semiboldFont->height + st::exteraCategoryAboutSkip;
				p.setPen(st::windowSubTextFg);
				p.setFont(aboutFont);
				p.drawText(
					QRect(
						textLeft,
						top,
						textWidth,
						aboutLines * aboutFont->height),
					Qt::AlignLeft | Qt::AlignTop,
					aboutText.join(QChar('\n')));
			}
		}, grid->lifetime());
		grid->setMouseTracking(true);
		grid->events(
		) | rpl::on_next([=](not_null<QEvent*> e) {
			const auto type = e->type();
			if (type == QEvent::MouseMove) {
				const auto over = cardAt(
					static_cast<QMouseEvent*>(e.get())->pos());
				if (state->over != over) {
					state->over = over;
					grid->setCursor((over >= 0)
						? style::cur_pointer
						: style::cur_default);
					grid->update();
				}
			} else if (type == QEvent::Leave) {
				state->over = -1;
				grid->update();
			} else if (type == QEvent::MouseButtonPress) {
				state->pressed = cardAt(
					static_cast<QMouseEvent*>(e.get())->pos());
			} else if (type == QEvent::MouseButtonRelease) {
				const auto index = cardAt(
					static_cast<QMouseEvent*>(e.get())->pos());
				const auto pressed = std::exchange(state->pressed, -1);
				if (index >= 0 && index == pressed) {
					const auto target = (*cards)[index].target();
					crl::on_main(controller, [=] {
						showOther(target);
					});
				}
			}
		}, grid->lifetime());
		return SectionBuilder::WidgetToAdd{
			.widget = std::move(result),
			.margin = st::exteraCategoryGridPadding,
		};
	});
}



// Appearance


// Chats


// Plugins


void BuildPluginsEngine(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_plugins_settings());
	builder.card([&] {
		AddToggle(
			builder,
			u"plugins_developer_mode"_q,
			tr::lng_extera_plugins_developer());
		AddToggle(
			builder,
			u"plugins_compact_view"_q,
			tr::lng_extera_plugins_compact());
		const auto unknownStates = std::make_shared<rpl::event_stream<bool>>();
		const auto unknown = builder.addButton({
			.id = u"extera/plugins_unknown_sources"_q,
			.title = tr::lng_extera_plugins_unknown(),
			.st = &st::settingsButtonNoIcon,
			.toggled = unknownStates->events_starting_with(
				Extera::Bool(u"plugins_unknown_sources"_q)),
		});
		if (unknown) {
			unknown->toggledChanges(
			) | rpl::filter([](bool value) {
				return value != Extera::Bool(u"plugins_unknown_sources"_q);
			}) | rpl::on_next([=](bool value) {
				if (!value) {
					Config().setBool(u"plugins_unknown_sources"_q, false);
					return;
				}
				Extera::Plugins::ShowUnknownSourcesWarning(
					controller,
					[=](bool allowed) {
						if (allowed) {
							Config().setBool(
								u"plugins_unknown_sources"_q,
								true);
						} else {
							unknownStates->fire(false);
						}
					});
			}, unknown->lifetime());
		}
		AddToggle(
			builder,
			u"plugins_safe_mode"_q,
			tr::lng_extera_plugins_safe());
		AddToggle(
			builder,
			u"plugins_no_auto_safe"_q,
			tr::lng_exd_no_auto_safe());
		builder.addButton({
			.id = u"extera/plugins_export"_q,
			.title = tr::lng_exd_plugins_export(),
			.st = &st::settingsButtonNoIcon,
			.onClick = [=] {
				FileDialog::GetFolder(
					Core::App().getFileDialogParent(),
					tr::lng_exd_plugins_export(tr::now),
					QString(),
					[=](QString &&folder) {
						const auto count = Extera::Plugins::ExportAll(folder);
						controller->showToast(tr::lng_exd_plugins_exported(
							tr::now,
							lt_count,
							count));
					});
			},
		});
	});
	AddAbout(builder, tr::lng_extera_plugins_safe_about());


	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_exd_plugins_sdk());
	builder.card([&] {
		builder.addButton({
			.id = u"extera/plugins_sdk"_q,
			.title = tr::lng_exd_plugins_sdk_version(),
			.st = &st::settingsButtonNoIcon,
			.label = rpl::single(QString::fromLatin1(Extera::Plugins::kSdkVersion)),
		});
		builder.addButton({
			.id = u"extera/plugins_runtime"_q,
			.title = tr::lng_exd_plugins_engine(),
			.st = &st::settingsButtonNoIcon,
			.label = rpl::single(Extera::Plugins::Started()
				? tr::lng_extera_plugins_runtime_running(tr::now)
				: tr::lng_extera_plugins_runtime_stopped(tr::now)),
		});
	});
	AddAbout(builder, tr::lng_extera_plugins_runtime_about());

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_links());
	builder.card([&] {
		AddLink(
			builder,
			u"plugins_docs"_q,
			tr::lng_extera_plugins_docs(),
			QString(),
			u"https://plugins.exteragram.app/docs"_q,
			{ &st::menuIconFaq });
		for (const auto &source : Extera::PluginTrust::Sources()) {
			const auto bare = source.id;
			const auto username = source.username;
			const auto channel = controller
				? controller->session().data().channelLoaded(ChannelId(bare))
				: nullptr;
			builder.addButton({
				.id = u"extera/trusted_"_q + QString::number(bare),
				.title = rpl::single(channel
					? channel->name()
					: !username.isEmpty()
					? (u"@"_q + username)
					: tr::lng_exd_trusted_channel(
						tr::now,
						lt_id,
						QString::number(bare))),
				.icon = { &st::menuIconExdPuzzle },
				.onClick = [=] {
					if (channel) {
						controller->showPeerHistory(
							channel,
							Window::SectionShow::Way::Forward);
					} else if (!username.isEmpty()) {
						UrlClickHandler::Open(u"https://t.me/"_q + username);
					}
				},
			});
		}
	});
	AddAbout(builder, tr::lng_exd_trusted_sources_about());
	PartPluginsGuard(builder);
}

// AI Chat

void BuildAiChat(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.add([=](const WidgetContext &ctx) {
		auto header = object_ptr<Ui::VerticalLayout>(ctx.container);
		const auto raw = header.data();
		const auto aiTitle = raw->add(
			object_ptr<Ui::FlatLabel>(
				raw,
				tr::lng_extera_ai_chat(),
				st::exteraInstallName),
			st::exteraAiHeaderTitlePadding,
			style::al_top);
		{
			const auto badge = Ui::NewBadge::CreateNewBadge(
				raw,
				tr::lng_exd_beta());
			aiTitle->geometryValue(
			) | rpl::on_next([=](QRect geometry) {
				badge->move(
					geometry.x() + geometry.width() + st::normalFont->spacew,
					geometry.y()
						+ (geometry.height() - badge->height()) / 2);
			}, badge->lifetime());
		}
		raw->add(
			object_ptr<Ui::FlatLabel>(
				raw,
				tr::lng_extera_ai_header_about(),
				st::exteraAppIconAuthor),
			st::exteraAiHeaderAboutPadding,
			style::al_top);
		return SectionBuilder::WidgetToAdd{ .widget = std::move(header) };
	});
	builder.addDivider();

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_ai_general());
	builder.card([&] {
		builder.addButton({
			.id = u"extera/ai_open"_q,
			.title = tr::lng_extera_ai_open(),
			.icon = { &st::menuIconBotCommands },
			.onClick = [=] {
				if (controller) {
					Extera::ShowAiChat(controller);
				}
			},
			.keywords = { u"ai"_q, u"gemini"_q },
		});
		builder.addButton({
			.id = u"extera/ai_service"_q,
			.title = tr::lng_extera_ai_service(),
			.icon = { &st::exteraIconWebsite },
			.label = rpl::single(u"Google Gemini"_q),
		});
		builder.addButton({
			.id = u"extera/ai_key"_q,
			.title = tr::lng_extera_ai_key(),
			.icon = { &st::menuIconLock },
			.label = Config().stringChanges(
				u"ai_chat_key"_q
			) | rpl::map([](const QString &key) {
				return key.trimmed().isEmpty()
					? tr::lng_extera_ai_key_missing(tr::now)
					: tr::lng_extera_ai_key_set(tr::now);
			}),
			.onClick = [=] {
				if (controller) {
					Extera::ShowKeyBox(controller);
				}
			},
		});
		AddChoice(
			builder,
			u"ai_chat_model"_q,
			tr::lng_extera_ai_model,
			{
				&tr::lng_extera_ai_model_flash,
				&tr::lng_extera_ai_model_pro,
				&tr::lng_extera_ai_model_lite,
			});
		AddChoice(
			builder,
			u"ai_chat_role"_q,
			tr::lng_extera_ai_role,
			{
				&tr::lng_extera_ai_role_assistant,
				&tr::lng_extera_ai_role_translator,
				&tr::lng_extera_ai_role_editor,
				&tr::lng_extera_ai_role_summary,
			});
		AddChoice(
			builder,
			u"ai_chat_history"_q,
			tr::lng_extera_ai_history,
			{
				&tr::lng_extera_ai_history_none,
				&tr::lng_extera_ai_history_10,
				&tr::lng_extera_ai_history_30,
				&tr::lng_extera_ai_history_all,
			});
	});
	AddAbout(builder, tr::lng_extera_ai_history_about());

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_ai_generation());
	builder.card([&] {
		AddToggle(
			builder,
			u"ai_chat_streaming"_q,
			tr::lng_extera_ai_streaming(),
			{},
			tr::lng_extera_ai_streaming_about());
	});
	builder.addDivider();

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_ai_temperature());
	builder.card([&] {
		auto values = std::vector<int>();
		for (auto i = 0; i <= 20; ++i) {
			values.push_back(i);
		}
		AddSlider(
			builder,
			u"ai_chat_temperature"_q,
			tr::lng_extera_ai_temperature,
			std::move(values),
			{ &tr::lng_extera_ai_temperature_min, &tr::lng_extera_ai_temperature_max },
			[](int value) {
				return QString::number(value / 10.0, 'f', 1);
			},
			false);
	});
	AddAbout(builder, tr::lng_extera_ai_temperature_about());
	AddAbout(builder, tr::lng_extera_ai_about());
}

// Navigation

void BuildNavigation(SectionBuilder &builder) {
	builder.addSkip();
	builder.add([=](const WidgetContext &ctx) {
		auto list = object_ptr<Ui::VerticalLayout>(ctx.container);
		Extera::Navigation::FillSettings(list.data());
		return SectionBuilder::WidgetToAdd{ .widget = std::move(list) };
	});
}

// Other


// Main


void BuildMessages(SectionBuilder &builder) {
	AddCategorySearch(builder, { ExteraGeneralId() }, [&] {
		PartChats4(builder);
		PartChats5(builder);
		PartChats3(builder);
		PartChats2(builder);
		PartChats6(builder);
		PartChats7(builder);
	});
}

void PartPremium(SectionBuilder &builder);

void BuildAppearance(SectionBuilder &builder) {
	AddCategorySearch(builder, {
		ExteraAppearanceId(),
		ExteraNavigationId(),
	}, [&] {
		PartAppearance0(builder);
		PartAppearance1(builder);
		PartAppearance2(builder);
		PartAppearance3(builder);
		PartAppearance4(builder);
		PartGeneral3(builder);
		PartChats0(builder);
		PartPremium(builder);
		builder.addSkip();
		builder.card([&] {
			builder.addSectionButton({
				.title = tr::lng_extera_nav_title(),
				.targetSection = ExteraNavigationId(),
				.icon = { &st::menuIconReorder },
			});
		});
	});
}

void BuildTranslate(SectionBuilder &builder) {
	AddCategorySearch(builder, { ExteraChatsId() }, [&] {
		PartGeneral2(builder);
	});
}

void BuildPlugins(SectionBuilder &builder) {
	AddCategorySearch(builder, {
		ExteraPluginsId(),
		ExteraPluginsEngineId(),
		ExteraAiChatId(),
	}, [&] {
		PartPlugins0(builder);
		PartPlugins1(builder);
		PartPlugins2(builder);
		if (!Extera::Bool(u"ai_chat_main"_q)) {
			PartChats1(builder);
		}
	});
}

void PartAyu(SectionBuilder &builder) {
	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_exd_ayu_ghost());
	builder.card([&] {
		AddSwitchGroup(
			builder,
			u"ghost_mode"_q,
			tr::lng_exd_ayu_ghost_mode,
			{
				{ u"ghost_read"_q, &tr::lng_exd_ayu_ghost_read },
				{ u"ghost_stories"_q, &tr::lng_exd_ayu_ghost_stories },
				{ u"ghost_online"_q, &tr::lng_exd_ayu_ghost_online },
				{ u"ghost_typing"_q, &tr::lng_exd_ayu_ghost_typing },
				{ u"ghost_offline"_q, &tr::lng_exd_ayu_ghost_offline },
			});
		AddToggle(
			builder,
			u"ayu_ghost_read_after"_q,
			tr::lng_exd_ayu_ghost_read_after());
	});
	AddAbout(builder, tr::lng_exd_ayu_ghost_about());

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_exd_ayu_spy());
	builder.card([&] {
		AddToggle(
			builder,
			u"ayu_keep_deleted"_q,
			tr::lng_exd_ayu_save_deleted(),
			{ u"deleted"_q });
		AddToggle(
			builder,
			u"ayu_edit_history"_q,
			tr::lng_exd_ayu_edit_history(),
			{ u"edit"_q, u"history"_q });
		AddToggle(
			builder,
			u"ayu_save_bots"_q,
			tr::lng_exd_ayu_save_bots());
		AddToggle(
			builder,
			u"translucent_deleted"_q,
			tr::lng_exd_translucent_deleted());
		AddPreview(
			builder,
			u"deleted_mark"_q,
			st::exteraPreviewBubble + st::exteraPreviewSkip * 2,
			[](QPainter &p, QRect frame, int mark) {
				const auto skip = st::exteraPreviewSkip;
				const auto bubble = QRectF(
					frame.x() + skip,
					frame.y() + skip,
					frame.width() * 0.6,
					st::exteraPreviewBubble);
				p.setPen(Qt::NoPen);
				p.setBrush(st::msgInBg);
				p.drawRoundedRect(
					bubble,
					st::bubbleRadiusLarge,
					st::bubbleRadiusLarge);
				PaintTextBars(
					p,
					QRectF(
						bubble.x() + 12,
						bubble.y() + 8,
						bubble.width() * 0.55,
						bubble.height() - 16),
					st::msgInDateFg,
					1);
				const auto &font = st::msgDateFont;
				const auto time = QLocale().toString(
					QTime(12, 30),
					QLocale::ShortFormat);
				auto right = int(bubble.right()) - 10;
				const auto baseline = int(bubble.bottom()) - 8;
				p.setFont(font);
				p.setPen(st::msgInDateFg);
				right -= font->width(time);
				p.drawText(right, baseline, time);
				if (mark == 1) {
					const auto text = tr::lng_exd_message_deleted(tr::now)
						+ ' ';
					right -= font->width(text);
					p.drawText(right, baseline, text);
				} else if (mark >= 2) {
					const auto &icon = (mark == 3)
						? st::exteraDeletedCrossIconEmoji.icon
						: (mark == 4)
						? st::exteraDeletedEyeIconEmoji.icon
						: st::exteraDeletedIconEmoji.icon;
					right -= icon.width() + 4;
					icon.paint(
						p,
						right,
						baseline - font->ascent + (font->ascent - icon.height()) / 2 + 1,
						frame.width(),
						st::msgInDateFg->c);
				}
			});
		AddChoice(
			builder,
			u"deleted_mark"_q,
			tr::lng_exd_deleted_mark,
			{
				&tr::lng_exd_deleted_mark_none,
				&tr::lng_exd_deleted_mark_text,
				&tr::lng_exd_deleted_mark_trash,
				&tr::lng_exd_deleted_mark_cross,
				&tr::lng_exd_deleted_mark_eye,
			},
			{ u"deleted"_q, u"mark"_q });
		const auto controller = builder.controller();
		builder.addButton({
			.id = u"extera/ayu_clear_deleted"_q,
			.title = tr::lng_exd_ayu_clear_deleted(),
			.st = &st::settingsAttentionButton,
			.onClick = [=] {
				if (!controller) {
					return;
				}
				controller->show(Ui::MakeConfirmBox({
					.text = tr::lng_exd_ayu_clear_deleted_sure(),
					.confirmed = [=](Fn<void()> close) {
						controller->session().data().exteraClearDeleted();
						close();
					},
					.confirmText = tr::lng_exd_ayu_clear_deleted_button(),
					.confirmStyle = &st::attentionBoxButton,
				}));
			},
		});
	});
	AddAbout(builder, tr::lng_exd_ayu_about());
}

void PartPremium(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto settings = &Core::App().settings();
	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_exd_premium_title());
	builder.card([&] {
		AddToggle(
			builder,
			u"local_premium"_q,
			tr::lng_exd_premium_limits(),
			{ u"premium"_q },
			tr::lng_exd_premium_limits_about(),
			true);
		AddToggle(
			builder,
			u"premium_no_ads"_q,
			tr::lng_exd_premium_no_ads(),
			{ u"premium"_q, u"ads"_q });
		AddToggle(
			builder,
			u"local_premium_emoji"_q,
			tr::lng_extera_local_premium(),
			{ u"premium"_q, u"emoji"_q },
			tr::lng_extera_local_premium_about(),
			true);
		AddCoreToggle(
			builder,
			u"premium_translate_chats"_q,
			tr::lng_exd_premium_translate(),
			[=] { return settings->translateChatEnabled(); },
			[=](bool v) { settings->setTranslateChatEnabled(v); });
		AddChoice(
			builder,
			u"transcribe_provider"_q,
			tr::lng_exd_premium_transcribe,
			{
				&tr::lng_exd_premium_transcribe_telegram,
				&tr::lng_exd_premium_transcribe_gemini,
			},
			{ u"voice"_q, u"transcribe"_q });
		builder.addButton({
			.id = u"extera/premium_gemini_key"_q,
			.title = tr::lng_exd_premium_gemini_key(),
			.st = &st::settingsButtonNoIcon,
			.label = Config().stringChanges(
				u"ai_chat_key"_q
			) | rpl::map([](const QString &key) {
				return key.trimmed().isEmpty()
					? tr::lng_exd_streamer_hotkey_none(tr::now)
					: tr::lng_exd_premium_gemini_set(tr::now);
			}),
			.onClick = [=] {
				if (controller) {
					Extera::ShowKeyBox(controller);
				}
			},
		});
	});
	AddAbout(builder, tr::lng_exd_premium_about());
}

void BuildPrivacy(SectionBuilder &builder) {
	AddCategorySearch(builder, { ExteraPrivacyId() }, [&] {
		PartGeneral0(builder);
		PartGeneral6(builder);
		PartGeneral7(builder);
		PartAyu(builder);
	});
}

void BuildNetwork(SectionBuilder &builder) {
	AddCategorySearch(builder, { ExteraNetworkId() }, [&] {
		PartGeneral1(builder);
		PartGeneral4(builder);
		PartGeneral5(builder);
	});
}

void BuildUpdates(SectionBuilder &builder) {
	AddCategorySearch(builder, { ExteraUpdatesId() }, [&] {
		PartOther2(builder);
		PartOther3(builder);
	});
}

void BuildSupport(SectionBuilder &builder) {
	AddCategorySearch(builder, { ExteraSupportId() }, [&] {
		PartOther0(builder);
		PartOther1(builder);
		PartMain2(builder);
	});
}

void BuildOther(SectionBuilder &builder) {
	if (!builder.controller()) {
		return;
	}
	PartOther0(builder);
	PartOther1(builder);
	PartOther2(builder);
	PartOther3(builder);
}

void BuildMain(SectionBuilder &builder) {
	AddHeader(builder);
	AddCategorySearch(builder, AllCategorySections(), [&] {
		AddCategoryGrid(builder);
		builder.addSkip();
		builder.card([&] {
			builder.addSectionButton({
				.title = tr::lng_extera_other(),
				.targetSection = ExteraOtherId(),
				.icon = { &st::exteraIconOther },
			});
		});
	});
}

template <int Index>
class ExteraSection : public Section<ExteraSection<Index>> {
public:
	ExteraSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

};

using ExteraMain = ExteraSection<0>;
using ExteraGeneral = ExteraSection<1>;
using ExteraAppearance = ExteraSection<2>;
using ExteraChats = ExteraSection<3>;
using ExteraPlugins = ExteraSection<4>;
using ExteraOther = ExteraSection<5>;
using ExteraPluginsEngine = ExteraSection<6>;
using ExteraNavigation = ExteraSection<7>;
using ExteraAiChat = ExteraSection<8>;
using ExteraPrivacy = ExteraSection<9>;
using ExteraNetwork = ExteraSection<10>;
using ExteraUpdates = ExteraSection<11>;
using ExteraSupport = ExteraSection<12>;

const auto kMainMeta = BuildHelper({
	.id = ExteraMain::Id(),
	.parentId = MainId(),
	.title = &tr::lng_extera_title,
	.icon = &st::exteraIconLogo,
}, BuildMain);

const auto kGeneralMeta = BuildHelper({
	.id = ExteraGeneral::Id(),
	.parentId = ExteraMain::Id(),
	.title = &tr::lng_exd_cat_messages,
	.icon = &st::exteraIconChats,
}, BuildMessages);

const auto kAppearanceMeta = BuildHelper({
	.id = ExteraAppearance::Id(),
	.parentId = ExteraMain::Id(),
	.title = &tr::lng_extera_appearance,
	.icon = &st::exteraIconAppearance,
}, BuildAppearance);

const auto kChatsMeta = BuildHelper({
	.id = ExteraChats::Id(),
	.parentId = ExteraMain::Id(),
	.title = &tr::lng_exd_cat_translate,
	.icon = &st::exteraIconTranslate,
}, BuildTranslate);

const auto kPluginsMeta = BuildHelper({
	.id = ExteraPlugins::Id(),
	.parentId = ExteraMain::Id(),
	.title = &tr::lng_exd_cat_plugins,
	.icon = &st::exteraIconPlugins,
}, BuildPlugins);

const auto kOtherMeta = BuildHelper({
	.id = ExteraOther::Id(),
	.parentId = ExteraMain::Id(),
	.title = &tr::lng_extera_other,
	.icon = &st::exteraIconOther,
}, BuildOther);

const auto kPluginsEngineMeta = BuildHelper({
	.id = ExteraPluginsEngine::Id(),
	.parentId = ExteraPlugins::Id(),
	.title = &tr::lng_extera_plugins_engine_title,
	.icon = &st::menuIconSettings,
}, BuildPluginsEngine);

const auto kNavigationMeta = BuildHelper({
	.id = ExteraNavigation::Id(),
	.parentId = ExteraAppearance::Id(),
	.title = &tr::lng_extera_nav_title,
	.icon = &st::menuIconReorder,
}, BuildNavigation);

const auto kAiChatMeta = BuildHelper({
	.id = ExteraAiChat::Id(),
	.parentId = ExteraPlugins::Id(),
	.title = &tr::lng_extera_ai_chat,
	.icon = &st::menuIconBotCommands,
}, BuildAiChat);

const auto kPrivacyMeta = BuildHelper({
	.id = ExteraPrivacy::Id(),
	.parentId = ExteraMain::Id(),
	.title = &tr::lng_exd_cat_privacy,
	.icon = &st::menuIconLock,
}, BuildPrivacy);

const auto kNetworkMeta = BuildHelper({
	.id = ExteraNetwork::Id(),
	.parentId = ExteraMain::Id(),
	.title = &tr::lng_exd_cat_network,
	.icon = &st::menuIconNetwork,
}, BuildNetwork);

const auto kUpdatesMeta = BuildHelper({
	.id = ExteraUpdates::Id(),
	.parentId = ExteraMain::Id(),
	.title = &tr::lng_exd_cat_updates,
	.icon = &st::menuIconRestore,
}, BuildUpdates);

const auto kSupportMeta = BuildHelper({
	.id = ExteraSupport::Id(),
	.parentId = ExteraMain::Id(),
	.title = &tr::lng_exd_cat_support,
	.icon = &st::menuIconLike,
}, BuildSupport);

[[nodiscard]] const BuildHelper &Meta(int index) {
	switch (index) {
	case 0: return kMainMeta;
	case 1: return kGeneralMeta;
	case 2: return kAppearanceMeta;
	case 3: return kChatsMeta;
	case 4: return kPluginsMeta;
	case 5: return kOtherMeta;
	case 6: return kPluginsEngineMeta;
	case 7: return kNavigationMeta;
	case 8: return kAiChatMeta;
	case 9: return kPrivacyMeta;
	case 10: return kNetworkMeta;
	case 11: return kUpdatesMeta;
	case 12: return kSupportMeta;
	}
	Unexpected("Index in Extera Meta.");
}

[[nodiscard]] rpl::producer<QString> Title(int index) {
	switch (index) {
	case 0: return tr::lng_extera_title();
	case 1: return tr::lng_exd_cat_messages();
	case 2: return tr::lng_extera_appearance();
	case 3: return tr::lng_exd_cat_translate();
	case 4: return tr::lng_exd_cat_plugins();
	case 5: return tr::lng_extera_other();
	case 6: return tr::lng_extera_plugins_engine_title();
	case 7: return tr::lng_extera_nav_title();
	case 8: return tr::lng_extera_ai_chat();
	case 9: return tr::lng_exd_cat_privacy();
	case 10: return tr::lng_exd_cat_network();
	case 11: return tr::lng_exd_cat_updates();
	case 12: return tr::lng_exd_cat_support();
	}
	Unexpected("Index in Extera Title.");
}

template <int Index>
ExteraSection<Index>::ExteraSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section<ExteraSection<Index>>(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	this->build(content, Meta(Index).build);
	Ui::ResizeFitChild(this, content);
}

template <int Index>
rpl::producer<QString> ExteraSection<Index>::title() {
	return Title(Index);
}

} // namespace

void ShowExteraAppIconBox(not_null<Window::SessionController*> controller) {
	ShowAppIconBox(controller);
}

void ShowExteraSupportBox(not_null<Window::SessionController*> controller) {
	ShowBadgeInfoBox(controller);
}

Type ExteraId() {
	return ExteraMain::Id();
}

Type ExteraGeneralId() {
	return ExteraGeneral::Id();
}

Type ExteraAppearanceId() {
	return ExteraAppearance::Id();
}

Type ExteraChatsId() {
	return ExteraChats::Id();
}

Type ExteraPluginsId() {
	return ExteraPlugins::Id();
}

Type ExteraOtherId() {
	return ExteraOther::Id();
}

Type ExteraPluginsEngineId() {
	return ExteraPluginsEngine::Id();
}

Type ExteraNavigationId() {
	return ExteraNavigation::Id();
}

Type ExteraAiChatId() {
	return ExteraAiChat::Id();
}

Type ExteraPrivacyId() {
	return ExteraPrivacy::Id();
}

Type ExteraNetworkId() {
	return ExteraNetwork::Id();
}

Type ExteraUpdatesId() {
	return ExteraUpdates::Id();
}

Type ExteraSupportId() {
	return ExteraSupport::Id();
}

} // namespace Settings
