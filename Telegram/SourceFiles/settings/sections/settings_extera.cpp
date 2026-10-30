#include "settings/sections/settings_extera.h"

#include "core/application.h"
#include "core/file_utilities.h"
#include "core/version.h"
#include "ui/style/style_core_font.h"
#include "core/core_settings.h"
#include "ui/boxes/choose_language_box.h"
#include "boxes/translate_box.h"
#include "boxes/download_path_box.h"
#include "extera/extera_config.h"
#include "extera/extera_badges.h"
#include "data/data_user.h"
#include "extera/extera_menu.h"
#include "history/view/media/history_view_sticker.h"
#include "extera/extera_navigation.h"
#include "extera/extera_streamer.h"
#include "extera/extera_ai_chat.h"
#include "extera/extera_welcome.h"
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
		button->toggledValue(
		) | rpl::on_next([=](bool value) {
			Config().setBool(key, value);
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

[[nodiscard]] int StickerPreviewHeight() {
	return HistoryView::Sticker::Size().height()
		+ st::exteraPreviewBubble
		+ 3 * st::exteraPreviewSkip;
}

void PaintStickerPreview(QPainter &p, QRect frame, int value) {
	static const auto image = QImage(u":/gui/art/exteradesktop.png"_q);
	const auto skip = st::exteraPreviewSkip;
	const auto side = HistoryView::Sticker::Size().width();
	const auto bubble = QRectF(
		frame.x() + skip,
		frame.y() + skip,
		frame.width() * 0.4,
		st::exteraPreviewBubble);
	p.setBrush(st::msgInBg);
	p.drawRoundedRect(bubble, st::bubbleRadiusLarge, st::bubbleRadiusLarge);
	p.drawImage(
		QRect(
			frame.x() + frame.width() - skip - side,
			frame.y() + frame.height() - skip - side,
			side,
			side),
		image);
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
			row->add(
				object_ptr<Ui::FlatLabel>(
					row,
					std::move(text),
					st::exteraBadgeStepText),
				st::exteraBadgeStepLabelPadding);
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
			tr::lng_exd_support_step1_text(tr::rich));
		step(
			&st::menuIconPhoto,
			tr::lng_exd_support_step2_title(),
			tr::lng_exd_support_step2_text(tr::rich));
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
			p.setBrush(st::msgInBg);
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

void BuildGeneral(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto settings = &Core::App().settings();

	builder.addSkip();
	builder.card([&] {
		AddStreamerRow(builder);
	});
	builder.addDivider();

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
				return Ui::LanguageName(id);
			}),
			.onClick = [=] {
				controller->show(Ui::ChooseTranslateToBox(
					settings->translateTo(),
					[=](LanguageId id) {
						settings->setTranslateTo(id);
						Core::App().saveSettingsDelayed();
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
		AddToggle(
			builder,
			u"disable_notification_delay"_q,
			tr::lng_extera_disable_notify_delay());
	});
	AddAbout(builder, tr::lng_extera_speed_about());

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

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_profile());
	builder.card([&] {
		AddToggle(
			builder,
			u"relative_last_seen"_q,
			tr::lng_extera_relative_last_seen(),
			{},
			tr::lng_extera_lastseen_days(lt_count, rpl::single(2.)));
		AddToggle(
			builder,
			u"hide_phone"_q,
			tr::lng_extera_hide_phone());
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

// Appearance

void BuildAppearance(SectionBuilder &builder) {
	const auto controller = builder.controller();

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

	builder.addDivider();

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
			});
		AddToggle(
			builder,
			u"custom_chat_themes"_q,
			tr::lng_extera_custom_themes());
	});
	AddAbout(builder, tr::lng_extera_custom_themes_about());
}

// Chats

void BuildChats(SectionBuilder &builder) {
	const auto actions = std::vector<const Phrase*>{
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
		AddPreview(
			builder,
			u"sticker_size"_q,
			st::exteraPreviewStickerHeight,
			PaintStickerPreview,
			StickerPreviewHeight);
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

	builder.addSkip();
	builder.card([&] {
		builder.addSectionButton({
			.title = tr::lng_extera_ai_chat(),
			.targetSection = ExteraAiChatId(),
			.icon = { &st::menuIconBotCommands },
		});
	});
	AddAbout(builder, tr::lng_extera_ai_settings_about());

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
		AddToggle(
			builder,
			u"local_premium_emoji"_q,
			tr::lng_extera_local_premium(),
			{ u"premium"_q, u"emoji"_q },
			tr::lng_extera_local_premium_about(),
			true);
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
	});
	AddAbout(builder, tr::lng_extera_double_tap_about());

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

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_photos());
	builder.card([&] {
		AddToggle(builder, u"always_send_hd"_q, tr::lng_extera_send_hd());
	});
	builder.addDivider();

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

// Plugins

void BuildPlugins(SectionBuilder &builder) {
	const auto controller = builder.controller();

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
	if (!Extera::Plugins::Available()) {
		AddAbout(builder, tr::lng_extera_plugins_runtime_missing());
	} else if (!Extera::Plugins::RuntimeError().isEmpty()) {
		AddAbout(builder, rpl::single(Extera::Plugins::RuntimeError()));
	} else {
		builder.addDivider();
	}

	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_plugins());
	builder.add([=](const WidgetContext &ctx) {
		auto list = object_ptr<Ui::VerticalLayout>(ctx.container);
		Extera::Plugins::FillPluginsList(list.data(), controller);
		return SectionBuilder::WidgetToAdd{ .widget = std::move(list) };
	});
}

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
				const auto confirmed = std::make_shared<bool>(false);
				controller->show(Ui::MakeConfirmBox({
					.text = tr::lng_extera_plugins_unknown_warning(
						tr::now,
						tr::rich),
					.confirmed = [=](Fn<void()> close) {
						*confirmed = true;
						Config().setBool(u"plugins_unknown_sources"_q, true);
						close();
					},
					.cancelled = [=](Fn<void()> close) {
						unknownStates->fire(false);
						close();
					},
					.confirmText = tr::lng_extera_plugins_unknown_allow(),
					.confirmStyle = &st::attentionBoxButton,
					.title = tr::lng_extera_plugins_unknown(),
				}));
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
	builder.addSubsectionTitle(tr::lng_extera_plugins_runtime());
	builder.card([&] {
		builder.addButton({
			.id = u"extera/plugins_runtime"_q,
			.title = tr::lng_extera_plugins_runtime_python(),
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
		AddLink(
			builder,
			u"plugins_trusted"_q,
			tr::lng_extera_plugins_trusted(),
			QString(),
			u"https://t.me/exteraPlugins"_q,
			{ &st::menuIconStar });
	});
	AddAbout(builder, tr::lng_extera_plugins_powered());
}

// AI Chat

void BuildAiChat(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.add([=](const WidgetContext &ctx) {
		auto header = object_ptr<Ui::VerticalLayout>(ctx.container);
		const auto raw = header.data();
		raw->add(
			object_ptr<Ui::FlatLabel>(
				raw,
				tr::lng_extera_ai_chat(),
				st::exteraInstallName),
			st::exteraAiHeaderTitlePadding,
			style::al_top);
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

void BuildOther(SectionBuilder &builder) {
	const auto controller = builder.controller();

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
				.label = rpl::single(address.left(6)
					+ QString::fromUtf8("\xE2\x80\xA6")
					+ address.right(4)),
				.onClick = [=] {
					QGuiApplication::clipboard()->setText(address);
					controller->showToast(
						tr::lng_exd_donate_copied(tr::now));
				},
				.keywords = { u"donate"_q, u"crypto"_q },
			});
		};
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

// Main

void BuildMain(SectionBuilder &builder) {
	const auto showOther = builder.showOther();

	AddHeader(builder);
	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_extera_categories());
	builder.card([&] {
		const auto category = [&](
				Type target,
				rpl::producer<QString> title,
				const style::icon *icon) {
			builder.addSectionButton({
				.title = std::move(title),
				.targetSection = target,
				.icon = { icon },
			});
		};
		category(
			ExteraGeneralId(),
			tr::lng_extera_general(),
			&st::exteraIconGeneral);
		category(
			ExteraAppearanceId(),
			tr::lng_extera_appearance(),
			&st::exteraIconAppearance);
		category(
			ExteraNavigationId(),
			tr::lng_extera_nav_title(),
			&st::menuIconReorder);
		category(
			ExteraChatsId(),
			tr::lng_extera_chats(),
			&st::exteraIconChats);
		category(
			ExteraPluginsId(),
			tr::lng_extera_plugins(),
			&st::exteraIconPlugins);
		category(
			ExteraOtherId(),
			tr::lng_extera_other(),
			&st::exteraIconOther);
	});
	builder.addDivider();

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

const auto kMainMeta = BuildHelper({
	.id = ExteraMain::Id(),
	.parentId = MainId(),
	.title = &tr::lng_extera_title,
	.icon = &st::exteraIconLogo,
}, BuildMain);

const auto kGeneralMeta = BuildHelper({
	.id = ExteraGeneral::Id(),
	.parentId = ExteraMain::Id(),
	.title = &tr::lng_extera_general,
	.icon = &st::exteraIconGeneral,
}, BuildGeneral);

const auto kAppearanceMeta = BuildHelper({
	.id = ExteraAppearance::Id(),
	.parentId = ExteraMain::Id(),
	.title = &tr::lng_extera_appearance,
	.icon = &st::exteraIconAppearance,
}, BuildAppearance);

const auto kChatsMeta = BuildHelper({
	.id = ExteraChats::Id(),
	.parentId = ExteraMain::Id(),
	.title = &tr::lng_extera_chats,
	.icon = &st::exteraIconChats,
}, BuildChats);

const auto kPluginsMeta = BuildHelper({
	.id = ExteraPlugins::Id(),
	.parentId = ExteraMain::Id(),
	.title = &tr::lng_extera_plugins,
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
	.parentId = ExteraMain::Id(),
	.title = &tr::lng_extera_nav_title,
	.icon = &st::menuIconReorder,
}, BuildNavigation);

const auto kAiChatMeta = BuildHelper({
	.id = ExteraAiChat::Id(),
	.parentId = ExteraChats::Id(),
	.title = &tr::lng_extera_ai_chat,
	.icon = &st::menuIconBotCommands,
}, BuildAiChat);

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
	}
	Unexpected("Index in Extera Meta.");
}

[[nodiscard]] rpl::producer<QString> Title(int index) {
	switch (index) {
	case 0: return tr::lng_extera_title();
	case 1: return tr::lng_extera_general();
	case 2: return tr::lng_extera_appearance();
	case 3: return tr::lng_extera_chats();
	case 4: return tr::lng_extera_plugins();
	case 5: return tr::lng_extera_other();
	case 6: return tr::lng_extera_plugins_engine_title();
	case 7: return tr::lng_extera_nav_title();
	case 8: return tr::lng_extera_ai_chat();
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

} // namespace Settings
