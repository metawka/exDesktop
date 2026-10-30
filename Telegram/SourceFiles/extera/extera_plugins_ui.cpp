#include "extera/extera_plugins_ui.h"

#include "apiwrap.h"
#include "boxes/sticker_set_box.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_stickers_set.h"
#include "main/main_session.h"
#include "ui/image/image.h"
#include "ui/text/text_entity.h"
#include "core/application.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_boxes.h"
#include "ui/chat/attach/attach_prepare.h"
#include "api/api_common.h"
#include "window/window_peer_menu.h"
#include "storage/storage_media_prepare.h"
#include "storage/localimageloader.h"
#include "ui/effects/animations.h"
#include "core/ui_integration.h"
#include "core/file_utilities.h"
#include "extera/extera_plugins.h"
#include "ui/painter.h"
#include "extera/extera_config.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "extera/extera_plugin_trust.h"
#include "boxes/translate_box.h"
#include "data/data_user.h"
#include "ui/wrap/slide_wrap.h"
#include "extera/extera_config.h"
#include "base/timer.h"
#include "core/click_handler_types.h"
#include "ui/effects/animations.h"
#include "ui/boxes/confirm_box.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtCore/QJsonArray>
#include <QtCore/QFile>
#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>
#include <QtSvg/QSvgRenderer>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonObject>

namespace Extera::Plugins {
namespace {

[[nodiscard]] QString Subtitle(const QJsonObject &info) {
	auto parts = QStringList();
	const auto version = info.value(u"version"_q).toString();
	const auto author = info.value(u"author"_q).toString();
	if (!version.isEmpty()) {
		parts.push_back(u"v"_q + version);
	}
	if (!author.isEmpty()) {
		parts.push_back(author);
	}
	return parts.join(QString::fromUtf8(" \xE2\x80\xA2 "));
}

void ShowInput(
		not_null<Window::SessionController*> controller,
		const QString &title,
		const QString &value,
		bool multiline,
		Fn<void(QString)> done) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(title));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			multiline
				? st::defaultInputField
				: st::defaultInputField,
			multiline
				? Ui::InputField::Mode::MultiLine
				: Ui::InputField::Mode::SingleLine,
			rpl::single(QString()),
			value));
		box->setFocusCallback([=] { field->setFocusFast(); });
		const auto submit = [=] {
			done(field->getLastText().trimmed());
			box->closeBox();
		};
		field->submits() | rpl::on_next(submit, field->lifetime());
		box->addButton(tr::lng_settings_save(), submit);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void FillSettings(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		const QString &id) {
	const auto items = Call(u"settings"_q, QJsonObject{
		{ u"id"_q, id },
	}).toArray();
	const auto changed = [=](int index, QJsonValue value) {
		Call(u"setting_changed"_q, QJsonObject{
			{ u"id"_q, id },
			{ u"index"_q, index },
			{ u"value"_q, value },
		});
	};
	Ui::AddSkip(container);
	for (const auto &entry : items) {
		const auto item = entry.toObject();
		const auto kind = item.value(u"kind"_q).toString();
		const auto index = item.value(u"index"_q).toInt();
		const auto text = item.value(u"text"_q).toString();
		const auto subtext = item.value(u"subtext"_q).toString();
		if (kind == u"header"_q) {
			Ui::AddSkip(container);
			Ui::AddSubsectionTitle(container, rpl::single(text));
		} else if (kind == u"divider"_q) {
			if (text.isEmpty()) {
				Ui::AddSkip(container);
				Ui::AddDivider(container);
			} else {
				Ui::AddDividerText(container, rpl::single(text));
			}
		} else if (kind == u"switch"_q) {
			const auto button = container->add(
				object_ptr<Ui::SettingsButton>(
					container,
					rpl::single(text),
					st::settingsButtonNoIcon));
			button->toggleOn(rpl::single(item.value(u"value"_q).toBool()));
			button->toggledChanges(
			) | rpl::on_next([=](bool value) {
				changed(index, value);
			}, button->lifetime());
			if (!subtext.isEmpty()) {
				container->add(
					object_ptr<Ui::FlatLabel>(
						container,
						rpl::single(subtext),
						st::exteraSubtitle),
					st::exteraPluginSubtextPadding);
			}
		} else if (kind == u"selector"_q) {
			auto options = std::vector<QString>();
			for (const auto &option : item.value(u"items"_q).toArray()) {
				options.push_back(option.toString());
			}
			const auto current = std::make_shared<rpl::variable<int>>(
				item.value(u"value"_q).toInt());
			const auto label = current->value(
			) | rpl::map([=](int value) {
				return (value >= 0 && value < int(options.size()))
					? options[value]
					: QString();
			});
			const auto button = Settings::AddButtonWithLabel(
				container,
				rpl::single(text),
				label,
				st::settingsButtonNoIcon);
			button->setClickedCallback([=] {
				controller->show(Box([=](not_null<Ui::GenericBox*> box) {
					SingleChoiceBox(box, {
						.title = rpl::single(text),
						.options = options,
						.initialSelection = current->current(),
						.callback = [=](int value) {
							*current = value;
							changed(index, value);
						},
					});
				}));
			});
		} else if (kind == u"input"_q || kind == u"edit_text"_q) {
			const auto title = (kind == u"edit_text"_q)
				? item.value(u"hint"_q).toString()
				: text;
			const auto current = std::make_shared<rpl::variable<QString>>(
				item.value(u"value"_q).toString());
			const auto secret = item.value(u"key"_q).toString().contains(
				u"key"_q,
				Qt::CaseInsensitive);
			const auto label = current->value(
			) | rpl::map([=](const QString &value) {
				return (secret && !value.isEmpty())
					? QString(std::min(int(value.size()), 12), QChar(0x2022))
					: value.left(40);
			});
			const auto button = Settings::AddButtonWithLabel(
				container,
				rpl::single(title),
				label,
				st::settingsButtonNoIcon);
			const auto multiline = item.value(u"multiline"_q).toBool();
			button->setClickedCallback([=] {
				ShowInput(controller, title, current->current(), multiline, [=](
						QString value) {
					*current = value;
					changed(index, value);
				});
			});
			if (!subtext.isEmpty()) {
				container->add(
					object_ptr<Ui::FlatLabel>(
						container,
						rpl::single(subtext),
						st::exteraSubtitle),
					st::exteraPluginSubtextPadding);
			}
		} else if (kind == u"slider"_q) {
			const auto min = item.value(u"min"_q).toDouble();
			const auto max = item.value(u"max"_q).toDouble();
			const auto step = std::max(
				item.value(u"step"_q).toDouble(),
				0.001);
			if (max > min) {
				const auto count = std::clamp(
					int(std::round((max - min) / step)) + 1,
					2,
					2000);
				auto values = std::vector<float>();
				values.reserve(count);
				for (auto i = 0; i != count; ++i) {
					values.push_back(float(min + i * step));
				}
				values.back() = float(max);
				auto current = item.value(u"value"_q).toDouble();
				if (current < min || current > max) {
					current = item.value(u"default"_q).toDouble();
				}
				auto index = 0;
				for (auto i = 0; i != int(values.size()); ++i) {
					if (std::abs(values[i] - current)
						< std::abs(values[index] - current)) {
						index = i;
					}
				}
				const auto format = [=](float value) {
					const auto rounded = int(std::round(value));
					return (std::abs(value - rounded) < 0.001f)
						? QString::number(rounded)
						: QString::number(value, 'f', 1);
				};
				container->add(
					object_ptr<Ui::FlatLabel>(
						container,
						rpl::single(text),
						st::exteraSubtitle),
					st::exteraPluginSubtextPadding);
				auto sliderWithLabel = Settings::MakeSliderWithLabel(
					container,
					st::settingsScale,
					st::settingsScaleLabel,
					st::normalFont->spacew * 2,
					st::settingsScaleLabel.style.font->width(QString(15, '0')),
					true);
				container->add(
					std::move(sliderWithLabel.widget),
					st::settingsBigScalePadding);
				const auto slider = sliderWithLabel.slider;
				const auto label = sliderWithLabel.label;
				label->setText(format(values[index]));
				slider->setPseudoDiscrete(
					int(values.size()),
					[=](int i) { return values[std::clamp(
						i, 0, int(values.size()) - 1)]; },
					values[index],
					[=](float value) { label->setText(format(value)); },
					[=](float value) {
						label->setText(format(value));
						changed(index, value);
					});
			}
		} else if (kind == u"text"_q) {
			const auto red = item.value(u"red"_q).toBool();
			const auto accent = item.value(u"accent"_q).toBool();
			const auto button = container->add(
				object_ptr<Ui::SettingsButton>(
					container,
					rpl::single(text),
					red
						? st::settingsAttentionButton
						: accent
						? st::settingsButtonLightNoIcon
						: st::settingsButtonNoIcon));
			if (item.value(u"clickable"_q).toBool()) {
				button->setClickedCallback([=] {
					Call(u"setting_click"_q, QJsonObject{
						{ u"id"_q, id },
						{ u"index"_q, index },
					});
				});
			} else {
				button->setAttribute(Qt::WA_TransparentForMouseEvents);
			}
			if (!subtext.isEmpty()) {
				container->add(
					object_ptr<Ui::FlatLabel>(
						container,
						rpl::single(subtext),
						st::exteraSubtitle),
					st::exteraPluginSubtextPadding);
			}
		}
	}
	Ui::AddSkip(container);
}

[[nodiscard]] QStringList PinnedPlugins() {
	return String(u"plugins_pinned"_q).split(',', Qt::SkipEmptyParts);
}

void SetPluginPinned(const QString &id, bool pinned) {
	auto list = PinnedPlugins();
	list.removeAll(id);
	if (pinned) {
		list.push_front(id);
	}
	Config::Instance().setString(u"plugins_pinned"_q, list.join(','));
}

void SharePluginFile(
		not_null<Window::SessionController*> controller,
		const QString &path) {
	if (path.isEmpty() || !QFile::exists(path)) {
		controller->showToast(tr::lng_extera_plugins_bad_file(tr::now));
		return;
	}
	const auto session = &controller->session();
	Window::ShowChooseRecipientBox(controller, [=](
			not_null<Data::Thread*> thread) {
		auto list = Storage::PrepareMediaList(
			QStringList{ path },
			st::sendMediaPreviewSize,
			session->premium());
		if (list.error != Ui::PreparedList::Error::None
			|| list.files.empty()) {
			controller->showToast(tr::lng_extera_plugins_bad_file(tr::now));
			return true;
		}
		session->api().sendFiles(
			std::move(list),
			SendMediaType::File,
			nullptr,
			Api::SendAction(thread));
		controller->showToast(tr::lng_extera_plugins_shared(tr::now));
		return true;
	});
}

void ShowPluginCode(
		not_null<Window::SessionController*> controller,
		const QString &name,
		const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		controller->showToast(tr::lng_extera_plugins_bad_file(tr::now));
		return;
	}
	constexpr auto kMaxCodeSize = 512 * 1024;
	const auto bytes = file.read(kMaxCodeSize);
	auto code = QString::fromUtf8(bytes);
	if (!file.atEnd()) {
		code += u"\n\n# ... [truncated]"_q;
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(name));
		box->setWidth(st::exteraPluginCodeWidth);
		auto text = TextWithEntities{ code };
		text.entities.push_back(
			EntityInText(EntityType::Pre, 0, code.size(), u"python"_q));
		const auto label = box->addRow(
			object_ptr<Ui::FlatLabel>(box, st::exteraPluginCode));
		label->setMarkedText(text);
		label->setSelectable(true);
		box->addButton(tr::lng_extera_plugins_copy_code(), [=] {
			QGuiApplication::clipboard()->setText(code);
			controller->showToast(tr::lng_text_copied(tr::now));
		});
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

constexpr auto kPuzzleSvg = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'><path fill='#ffffff' d='M20.5 11H19V7c0-1.1-.9-2-2-2h-4V3.5C13 2.12 11.88 1 10.5 1S8 2.12 8 3.5V5H4c-1.1 0-1.99.9-1.99 2v3.8H3.5c1.49 0 2.7 1.21 2.7 2.7s-1.21 2.7-2.7 2.7H2V20c0 1.1.9 2 2 2h3.8v-1.5c0-1.49 1.21-2.7 2.7-2.7 1.49 0 2.7 1.21 2.7 2.7V22H17c1.1 0 2-.9 2-2v-4h1.5c1.38 0 2.5-1.12 2.5-2.5S21.88 11 20.5 11z'/></svg>";

[[nodiscard]] QImage PuzzleImage(int size, QColor color) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		QSize(size, size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		QSvgRenderer(QByteArray(kPuzzleSvg)).render(
			&p,
			QRectF(0, 0, size * ratio, size * ratio));
	}
	result = style::colorizeImage(result, color);
	result.setDevicePixelRatio(ratio);
	return result;
}

[[nodiscard]] TextWithEntities WithLinks(const QString &text) {
	return TextUtilities::ParseEntities(
		text,
		TextParseLinks | TextParseMentions | TextParseHashtags);
}

void SetupLinks(
		not_null<Ui::FlatLabel*> label,
		not_null<Ui::GenericBox*> box) {
	label->setClickHandlerFilter([=](const ClickHandlerPtr &, Qt::MouseButton) {
		box->closeBox();
		return true;
	});
}

void SetupPluginIcon(
		not_null<Ui::AbstractButton*> widget,
		not_null<Window::SessionController*> controller,
		const QString &icon,
		int size,
		int badge) {
	struct State {
		std::shared_ptr<Data::DocumentMedia> media;
		StickerSetIdentifier set;
		QImage image;
		QImage puzzle;
		Ui::Animations::Simple appear;
		bool failed = false;
	};
	widget->resize(size, size + badge / 3);
	const auto state = widget->lifetime().make_state<State>();
	const auto slash = icon.lastIndexOf('/');
	state->failed = (slash <= 0);
	const auto ready = [=] {
		if (!state->image.isNull() || !state->media) {
			return;
		}
		const auto large = state->media->getStickerLarge();
		if (!large) {
			return;
		}
		const auto ratio = style::DevicePixelRatio();
		state->image = large->original().scaled(
			QSize(size, size) * ratio,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
		state->image.setDevicePixelRatio(ratio);
		state->appear.start(
			[=] { widget->update(); },
			0.,
			1.,
			st::exteraInstallIconDuration,
			anim::easeOutCirc);
	};
	widget->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(widget);
		auto hq = PainterHighQualityEnabler(p);
		const auto left = (widget->width() - size) / 2;
		const auto rect = QRect(left, 0, size, size);
		const auto radius = st::exteraInstallIconRadius;
		p.setPen(Qt::NoPen);
		if (state->failed && state->image.isNull()) {
			p.setBrush(st::windowBgActive);
			p.drawRoundedRect(rect, radius, radius);
			st::exteraIconPlugins.paintInCenter(
				p,
				rect,
				st::windowFgActive->c);
		} else {
			const auto shown = state->image.isNull()
				? 0.
				: state->appear.value(1.);
			if (shown < 1.) {
				p.setOpacity(1. - shown);
				p.setBrush(st::windowBgOver);
				p.drawRoundedRect(rect, radius, radius);
				p.setOpacity(1.);
			}
			if (shown > 0.) {
				const auto real = state->image.size()
					/ state->image.devicePixelRatio();
				const auto scale = 0.85 + 0.15 * shown;
				p.setOpacity(shown);
				p.translate(rect.center());
				p.scale(scale, scale);
				p.drawImage(
					QPoint(-real.width() / 2, -real.height() / 2),
					state->image);
				p.resetTransform();
				p.setOpacity(1.);
			}
		}
		if (badge > 0) {
			const auto circle = QRect(
				rect.x() + rect.width() - badge * 2 / 3,
				rect.y() + rect.height() - badge * 2 / 3,
				badge,
				badge);
			p.setBrush(st::boxBg);
			p.drawEllipse(circle);
			const auto stroke = st::exteraInstallBadgeStroke;
			const auto inner = circle.marginsRemoved(
				{ stroke, stroke, stroke, stroke });
			p.setBrush(st::windowSubTextFg);
			p.drawEllipse(inner);
			const auto glyph = st::exteraInstallBadgeGlyph;
			if (state->puzzle.isNull()) {
				state->puzzle = PuzzleImage(glyph, st::boxBg->c);
			}
			p.drawImage(
				QPoint(
					inner.x() + (inner.width() - glyph) / 2,
					inner.y() + (inner.height() - glyph) / 2),
				state->puzzle);
		}
	}, widget->lifetime());
	widget->setClickedCallback([=] {
		if (state->set.shortName.isEmpty() && !state->set.id) {
			return;
		}
		controller->show(Box<StickerSetBox>(
			controller->uiShow(),
			state->set,
			Data::StickersType::Stickers));
	});

	if (state->failed) {
		return;
	}
	const auto name = icon.left(slash);
	const auto index = icon.mid(slash + 1).toInt();
	const auto session = &controller->session();
	const auto weak = base::make_weak(widget);
	session->api().request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetShortName(MTP_string(name)),
		MTP_int(0)
	)).done([=](const MTPmessages_StickerSet &result) {
		if (!weak) {
			return;
		}
		result.match([&](const MTPDmessages_stickerSet &data) {
			const auto set = session->data().stickers().feedSetFull(data);
			state->set = set->identifier();
			if (index < 0 || index >= set->stickers.size()) {
				state->failed = true;
				widget->update();
				return;
			}
			const auto document = set->stickers[index];
			state->media = document->createMediaView();
			state->media->checkStickerLarge();
			if (!state->media->loaded()) {
				document->save(document->stickerOrGifOrigin(), QString());
			}
			ready();
			session->downloaderTaskFinished(
			) | rpl::filter([=] {
				return state->image.isNull();
			}) | rpl::on_next([=] {
				state->media->checkStickerLarge();
				ready();
			}, widget->lifetime());
		}, [&](const MTPDmessages_stickerSetNotModified &) {
			state->failed = true;
			widget->update();
		});
	}).fail([=] {
		if (weak) {
			state->failed = true;
			widget->update();
		}
	}).send();
}

void AddInstallIcon(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		const QString &icon) {
	const auto widget = box->addRow(
		object_ptr<Ui::AbstractButton>(box),
		st::exteraInstallIconPadding);
	SetupPluginIcon(
		widget,
		controller,
		icon,
		st::exteraInstallIcon,
		st::exteraInstallBadge);
}

} // namespace

void ShowPluginSettings(
		not_null<Window::SessionController*> controller,
		const QString &id) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		auto title = id;
		for (const auto &entry : Call(u"list"_q).toArray()) {
			const auto info = entry.toObject();
			if (info.value(u"id"_q).toString() == id) {
				title = info.value(u"name"_q).toString();
			}
		}
		box->setTitle(rpl::single(title));
		box->setWidth(st::boxWideWidth);
		const auto content = box->verticalLayout();
		const auto list = content->add(
			object_ptr<Ui::VerticalLayout>(content));
		FillSettings(list, controller, id);
		settingsChanges(
		) | rpl::filter([=](const QString &changed) {
			return changed == id;
		}) | rpl::on_next([=] {
			list->clear();
			FillSettings(list, controller, id);
			list->resizeToWidth(content->width());
		}, box->lifetime());
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

void FillPluginsList(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller) {
	const auto fill = [=] {
		container->clear();
		auto list = Call(u"list"_q).toArray().toVariantList();
		const auto pinned = PinnedPlugins();
		std::stable_sort(list.begin(), list.end(), [&](
				const QVariant &a,
				const QVariant &b) {
			const auto idA = a.toMap().value(u"id"_q).toString();
			const auto idB = b.toMap().value(u"id"_q).toString();
			const auto indexA = pinned.indexOf(idA);
			const auto indexB = pinned.indexOf(idB);
			if (indexA >= 0 && indexB >= 0) {
				return indexA < indexB;
			}
			return (indexA >= 0) && (indexB < 0);
		});
		if (list.isEmpty()) {
			container->add(
				object_ptr<Ui::FlatLabel>(
					container,
					tr::lng_extera_plugins_empty(),
					st::exteraAbout),
				st::exteraAboutPadding);
		}
		for (const auto &entry : list) {
			const auto info = QJsonObject::fromVariantMap(entry.toMap());
			const auto id = info.value(u"id"_q).toString();
			const auto card = container->add(
				object_ptr<Ui::VerticalLayout>(container),
				st::exteraPluginCardMargin);
			card->paintRequest() | rpl::on_next([=] {
				auto p = QPainter(card);
				auto hq = PainterHighQualityEnabler(p);
				p.setPen(Qt::NoPen);
				p.setBrush(st::windowBgOver);
				p.drawRoundedRect(
					card->rect(),
					st::exteraPluginCardRadius,
					st::exteraPluginCardRadius);
			}, card->lifetime());
			Ui::AddSkip(card, st::exteraPluginCardSkip);
			const auto button = card->add(
				object_ptr<Ui::SettingsButton>(
					card,
					rpl::single(info.value(u"name"_q).toString()),
					st::exteraPluginNameButton));
			const auto iconSize = st::exteraPluginIcon;
			const auto iconLeft = st::settingsButtonNoIcon.padding.left();
			const auto shift = iconSize + st::exteraPluginIconSkip;
			auto namePadding = st::settingsButtonNoIcon.padding;
			namePadding.setLeft(namePadding.left() + shift);
			button->setPaddingOverride(namePadding);
			const auto toggled = button->lifetime().make_state<
				rpl::variable<bool>>(info.value(u"enabled"_q).toBool()
					&& !Extera::Bool(u"plugins_safe_mode"_q));
			button->toggleOn(toggled->value());
			button->toggledChanges(
			) | rpl::on_next([=](bool enabled) {
				if (enabled && Extera::Bool(u"plugins_safe_mode"_q)) {
					controller->showToast(
						tr::lng_exd_safe_mode_blocked(tr::now));
					crl::on_main(button, [=] {
						toggled->force_assign(false);
					});
					return;
				}
				Call(u"set_enabled"_q, QJsonObject{
					{ u"id"_q, id },
					{ u"enabled"_q, enabled },
				});
			}, button->lifetime());
			const auto meta = card->add(
				object_ptr<Ui::FlatLabel>(
					card,
					rpl::single(Subtitle(info)),
					st::exteraSubtitle),
				st::exteraPluginMetaPadding + style::margins(shift, 0, 0, 0));
			const auto pluginPath = info.value(u"path"_q).toString();
			const auto code = card->add(
				object_ptr<Ui::LinkButton>(
					card,
					tr::lng_exd_plugin_view_code(tr::now)),
				st::exteraPluginMetaPadding + style::margins(shift, 0, 0, 0),
				style::al_left);
			code->setClickedCallback([=] {
				ShowSourceBox(controller, pluginPath);
			});
			const auto icon = Ui::CreateChild<Ui::AbstractButton>(card);
			SetupPluginIcon(
				icon,
				controller,
				info.value(u"icon"_q).toString(),
				iconSize,
				0);
			rpl::combine(
				button->geometryValue(),
				meta->geometryValue()
			) | rpl::on_next([=](QRect name, QRect line) {
				const auto top = name.y();
				const auto bottom = line.y() + line.height();
				icon->moveToLeft(
					iconLeft,
					top + (bottom - top - iconSize) / 2);
				icon->raise();
			}, icon->lifetime());
			const auto description = info.value(u"description"_q).toString();
			if (!description.isEmpty() && !Bool(u"plugins_compact_view"_q)) {
				card->add(
					object_ptr<Ui::FlatLabel>(
						card,
						rpl::single(description),
						st::boxLabel),
					st::exteraPluginDescriptionPadding);
			}
			const auto error = info.value(u"error"_q).toString();
			if (!error.isEmpty()) {
				card->add(
					object_ptr<Ui::FlatLabel>(
						card,
						rpl::single(error.trimmed().split('\n').last()),
						st::exteraAbout),
					st::exteraPluginDescriptionPadding);
			}
			const auto path = info.value(u"path"_q).toString();
			const auto name = info.value(u"name"_q).toString();
			const auto actions = card->add(
				object_ptr<Ui::FixedHeightWidget>(
					card,
					st::exteraPluginAction.height),
				st::exteraPluginActionsPadding);
			auto buttons = std::vector<not_null<Ui::IconButton*>>();
			const auto addAction = [&](
					const style::icon &icon,
					Fn<void()> callback) {
				const auto button = Ui::CreateChild<Ui::IconButton>(
					actions,
					st::exteraPluginAction);
				button->setIconOverride(&icon, &icon);
				button->setClickedCallback(std::move(callback));
				buttons.push_back(button);
				return button;
			};
			addAction(st::menuIconShare, [=] {
				SharePluginFile(controller, path);
			});
			addAction(st::exteraIconOpenCode, [=] {
				ShowPluginCode(controller, name, path);
			});
			const auto isPinned = pinned.contains(id);
			addAction(
				isPinned ? st::menuIconUnpin : st::menuIconPin,
				[=] {
					SetPluginPinned(id, !isPinned);
					notifyListChanged();
				});
			if (info.value(u"has_settings"_q).toBool()) {
				addAction(st::menuIconSettings, [=] {
					ShowPluginSettings(controller, id);
				});
			}
			const auto remove = Ui::CreateChild<Ui::IconButton>(
				actions,
				st::exteraPluginAction);
			remove->setIconOverride(
				&st::menuIconDeleteAttention,
				&st::menuIconDeleteAttention);
			remove->setClickedCallback([=] {
				controller->show(Ui::MakeConfirmBox({
					.text = tr::lng_extera_plugins_delete_sure(),
					.confirmed = [=](Fn<void()> close) {
						Call(u"delete"_q, QJsonObject{ { u"id"_q, id } });
						SetPluginPinned(id, false);
						notifyListChanged();
						close();
					},
					.confirmText = tr::lng_extera_plugins_delete(),
					.confirmStyle = &st::attentionBoxButton,
				}));
			});
			actions->widthValue() | rpl::on_next([=](int width) {
				auto left = 0;
				for (const auto button : buttons) {
					button->moveToLeft(left, 0, width);
					left += button->width();
				}
				remove->moveToRight(0, 0, width);
			}, actions->lifetime());
			Ui::AddSkip(card, st::exteraPluginCardSkip);
		}
		container->showChildren();
		container->resizeToWidth(container->width());
	};
	fill();
	listChanges(
	) | rpl::on_next(fill, container->lifetime());
}

namespace {

// Threats

enum class ThreatLevel {
	Danger,
	Attention,
	Other,
};

enum class Chevron {
	None,
	Right,
	Down,
	Up,
};

struct Threat {
	QString key;
	ThreatLevel level = ThreatLevel::Other;
	const style::icon *icon = nullptr;
	const style::icon *chip = nullptr;
	QString chipText;
	QString title;
	QString about;
};

[[nodiscard]] QColor ThreatColor(ThreatLevel level) {
	switch (level) {
	case ThreatLevel::Danger: return st::historyPeer1UserpicBg->c;
	case ThreatLevel::Attention: return st::historyPeer3UserpicBg->c;
	case ThreatLevel::Other: break;
	}
	return st::windowSubTextFg->c;
}

[[nodiscard]] std::vector<Threat> CollectThreats(
		const QJsonObject &capabilities) {
	auto result = std::vector<Threat>();
	const auto add = [&](
			const QString &key,
			ThreatLevel level,
			const style::icon &icon,
			const style::icon &chip,
			QString chipText,
			QString title,
			QString about) {
		if (capabilities.contains(key)) {
			result.push_back({
				.key = key,
				.level = level,
				.icon = &icon,
				.chip = &chip,
				.chipText = std::move(chipText),
				.title = std::move(title),
				.about = std::move(about),
			});
		}
	};
	using Level = ThreatLevel;
	add(
		u"native"_q,
		Level::Danger,
		st::exteraThreatCode,
		st::exteraChipCode,
		tr::lng_exd_threat_chip_code(tr::now),
		tr::lng_exd_threat_native(tr::now),
		tr::lng_exd_threat_native_about(tr::now));
	add(
		u"hooks"_q,
		Level::Attention,
		st::exteraThreatCode,
		st::exteraChipCode,
		tr::lng_exd_threat_chip_code(tr::now),
		tr::lng_exd_threat_hooks(tr::now),
		tr::lng_exd_threat_hooks_about(tr::now));
	add(
		u"files"_q,
		Level::Attention,
		st::exteraThreatFile,
		st::exteraChipFile,
		tr::lng_exd_threat_chip_files(tr::now),
		tr::lng_exd_threat_files(tr::now),
		tr::lng_exd_threat_files_about(tr::now));
	add(
		u"network"_q,
		Level::Attention,
		st::exteraThreatGlobe,
		st::exteraChipGlobe,
		tr::lng_exd_threat_chip_network(tr::now),
		tr::lng_exd_threat_network(tr::now),
		tr::lng_exd_threat_network_about(tr::now));
	add(
		u"messages.send"_q,
		Level::Attention,
		st::exteraThreatChat,
		st::exteraChipChat,
		tr::lng_exd_threat_chip_messages(tr::now),
		tr::lng_exd_threat_send(tr::now),
		tr::lng_exd_threat_send_about(tr::now));
	add(
		u"messages.read"_q,
		Level::Other,
		st::exteraThreatChat,
		st::exteraChipChat,
		tr::lng_exd_threat_chip_messages(tr::now),
		tr::lng_exd_threat_read(tr::now),
		tr::lng_exd_threat_read_about(tr::now));
	add(
		u"intents"_q,
		Level::Other,
		st::exteraThreatLink,
		st::exteraChipLink,
		tr::lng_exd_threat_chip_links(tr::now),
		tr::lng_exd_threat_intents(tr::now),
		tr::lng_exd_threat_intents_about(tr::now));
	add(
		u"settings"_q,
		Level::Other,
		st::exteraThreatTune,
		st::exteraChipTune,
		tr::lng_exd_threat_chip_settings(tr::now),
		tr::lng_exd_threat_settings(tr::now),
		tr::lng_exd_threat_settings_about(tr::now));
	add(
		u"dex"_q,
		Level::Other,
		st::exteraThreatCode,
		st::exteraChipCode,
		tr::lng_exd_threat_chip_code(tr::now),
		tr::lng_exd_threat_dex(tr::now),
		tr::lng_exd_threat_dex_about(tr::now));
	return result;
}

void PaintTile(
		QPainter &p,
		QRect rect,
		QColor color,
		const style::icon &icon) {
	auto hq = PainterHighQualityEnabler(p);
	auto bg = color;
	bg.setAlphaF(0.18);
	p.setPen(Qt::NoPen);
	p.setBrush(bg);
	p.drawRoundedRect(
		rect,
		st::exteraThreatTileRadius,
		st::exteraThreatTileRadius);
	icon.paint(
		p,
		QPoint(
			rect.x() + (rect.width() - icon.width()) / 2,
			rect.y() + (rect.height() - icon.height()) / 2),
		rect.x() * 2 + rect.width(),
		color);
}

void PaintChevron(QPainter &p, QPoint center, Chevron direction) {
	if (direction == Chevron::None) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	auto pen = QPen(st::windowSubTextFg);
	pen.setWidthF(style::ConvertScaleExact(1.8));
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	const auto a = style::ConvertScaleExact(4.);
	const auto b = style::ConvertScaleExact(2.);
	const auto c = QPointF(center);
	auto points = std::array<QPointF, 3>();
	switch (direction) {
	case Chevron::Right:
		points = { c + QPointF(-b, -a), c + QPointF(b, 0), c + QPointF(-b, a) };
		break;
	case Chevron::Down:
		points = { c + QPointF(-a, -b), c + QPointF(0, b), c + QPointF(a, -b) };
		break;
	case Chevron::Up:
		points = { c + QPointF(-a, b), c + QPointF(0, -b), c + QPointF(a, b) };
		break;
	case Chevron::None:
		break;
	}
	p.drawPolyline(points.data(), int(points.size()));
}

void PaintCardBackground(not_null<Ui::RpWidget*> card) {
	card->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(card);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(
			card->rect(),
			st::exteraInstallCardRadius,
			st::exteraInstallCardRadius);
	}, card->lifetime());
}

[[nodiscard]] not_null<Ui::VerticalLayout*> AddCard(
		not_null<Ui::GenericBox*> box) {
	const auto card = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		st::exteraInstallCardMargin);
	PaintCardBackground(card);
	return card;
}

class ThreatRow final : public Ui::AbstractButton {
public:
	ThreatRow(
		QWidget *parent,
		QColor color,
		const style::icon &icon,
		const QString &title,
		const QString &about,
		Chevron chevron)
	: AbstractButton(parent)
	, _color(color)
	, _icon(icon)
	, _chevron(chevron)
	, _title(this, title, st::exteraThreatTitle)
	, _about(this, about, st::exteraThreatAbout) {
		_title->setAttribute(Qt::WA_TransparentForMouseEvents);
		_about->setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	void setChevron(Chevron chevron) {
		_chevron = chevron;
		update();
	}

protected:
	int resizeGetHeight(int newWidth) override {
		const auto &padding = st::exteraThreatRowPadding;
		const auto tile = st::exteraThreatTile;
		const auto left = padding.left() + tile + st::exteraThreatTextSkip;
		const auto right = padding.right()
			+ (_chevron == Chevron::None ? 0 : st::exteraThreatTile / 2);
		const auto available = std::max(newWidth - left - right, 1);
		_title->resizeToWidth(available);
		_about->resizeToWidth(available);
		const auto text = _title->height() + _about->height();
		const auto inner = std::max(tile, text);
		const auto top = padding.top() + (inner - text) / 2;
		_title->moveToLeft(left, top, newWidth);
		_about->moveToLeft(left, top + _title->height(), newWidth);
		_tileTop = padding.top() + (inner - tile) / 2;
		return padding.top() + inner + padding.bottom();
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		if (isOver() || isDown()) {
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgRipple);
			p.drawRoundedRect(
				rect(),
				st::exteraInstallCardRadius,
				st::exteraInstallCardRadius);
		}
		const auto &padding = st::exteraThreatRowPadding;
		const auto tile = st::exteraThreatTile;
		PaintTile(
			p,
			QRect(padding.left(), _tileTop, tile, tile),
			_color,
			_icon);
		PaintChevron(
			p,
			QPoint(
				width() - padding.right() - st::exteraThreatTile / 4,
				height() / 2),
			_chevron);
	}

	void onStateChanged(State was, StateChangeSource source) override {
		update();
	}

private:
	const QColor _color;
	const style::icon &_icon;
	Chevron _chevron = Chevron::None;
	object_ptr<Ui::FlatLabel> _title;
	object_ptr<Ui::FlatLabel> _about;
	int _tileTop = 0;

};

struct Chip {
	const style::icon *icon = nullptr;
	QString text;
	QColor color;
};

class ChipsRow final : public Ui::RpWidget {
public:
	ChipsRow(QWidget *parent, std::vector<Chip> chips)
	: RpWidget(parent)
	, _chips(std::move(chips)) {
		setAttribute(Qt::WA_TransparentForMouseEvents);
	}

protected:
	int resizeGetHeight(int newWidth) override {
		_rects.clear();
		const auto &padding = st::exteraThreatChipsPadding;
		const auto gap = st::exteraThreatChipsGap;
		const auto height = st::exteraThreatChipHeight;
		const auto available = newWidth - padding.left() - padding.right();
		auto x = 0;
		auto y = 0;
		for (const auto &chip : _chips) {
			const auto width = chipWidth(chip);
			if (x > 0 && x + width > available) {
				x = 0;
				y += height + gap;
			}
			_rects.push_back(QRect(padding.left() + x, y, width, height));
			x += width + gap;
		}
		return _chips.empty()
			? 0
			: (y + height + padding.bottom());
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto &padding = st::exteraThreatChipPadding;
		const auto &font = st::exteraThreatChipFont;
		for (auto i = 0; i != int(_chips.size()); ++i) {
			const auto &chip = _chips[i];
			const auto rect = _rects[i];
			auto bg = chip.color;
			bg.setAlphaF(0.18);
			p.setPen(Qt::NoPen);
			p.setBrush(bg);
			p.drawRoundedRect(
				rect,
				st::exteraThreatChipRadius,
				st::exteraThreatChipRadius);
			const auto icon = chip.icon;
			icon->paint(
				p,
				QPoint(
					rect.x() + padding.left(),
					rect.y() + (rect.height() - icon->height()) / 2),
				width(),
				chip.color);
			p.setPen(chip.color);
			p.setFont(font);
			p.drawText(
				rect.x()
					+ padding.left()
					+ icon->width()
					+ st::exteraThreatChipSkip,
				rect.y() + (rect.height() - font->height) / 2 + font->ascent,
				chip.text);
		}
	}

private:
	[[nodiscard]] int chipWidth(const Chip &chip) const {
		const auto &padding = st::exteraThreatChipPadding;
		return padding.left()
			+ chip.icon->width()
			+ st::exteraThreatChipSkip
			+ st::exteraThreatChipFont->width(chip.text)
			+ padding.right();
	}

	std::vector<Chip> _chips;
	std::vector<QRect> _rects;

};

[[nodiscard]] const style::FlatLabel &CodeLabelStyle() {
	static const auto result = [] {
		auto copy = st::exteraThreatAbout;
		copy.textFg = st::windowFg;
		copy.style.font = st::exteraThreatAbout.style.font->monospace();
		return copy;
	}();
	return result;
}

[[nodiscard]] const style::FlatLabel &CodeNumberStyle() {
	static const auto result = [] {
		auto copy = st::exteraThreatAbout;
		copy.style.font = st::exteraThreatAbout.style.font->monospace();
		return copy;
	}();
	return result;
}

class CodeLines final : public Ui::RpWidget {
public:
	CodeLines(QWidget *parent, const QJsonObject &data)
	: RpWidget(parent) {
		for (const auto &value : data.value(u"lines"_q).toArray()) {
			const auto line = value.toArray();
			auto number = object_ptr<Ui::FlatLabel>(
				this,
				QString::number(line.at(0).toInt()),
				CodeNumberStyle());
			auto text = object_ptr<Ui::FlatLabel>(
				this,
				line.at(1).toString(),
				CodeLabelStyle());
			text->setSelectable(true);
			_lines.push_back({ std::move(number), std::move(text) });
		}
		const auto shown = int(_lines.size());
		const auto total = data.value(u"total"_q).toInt();
		if (total > shown) {
			_more.create(
				this,
				tr::lng_exd_threats_more(
					tr::now,
					lt_amount,
					QString::number(total - shown)),
				CodeNumberStyle());
		}
	}

protected:
	int resizeGetHeight(int newWidth) override {
		const auto &margin = st::exteraThreatCodeMargin;
		const auto &padding = st::exteraThreatCodePadding;
		const auto left = margin.left() + padding.left();
		const auto number = st::exteraThreatCodeNumber;
		const auto available = std::max(
			newWidth
				- margin.left()
				- margin.right()
				- padding.left()
				- padding.right()
				- number,
			1);
		auto top = padding.top();
		for (auto &[label, text] : _lines) {
			label->resizeToWidth(number);
			label->moveToLeft(left, top, newWidth);
			text->resizeToWidth(available);
			text->moveToLeft(left + number, top, newWidth);
			top += text->height() + st::exteraThreatCodeLineSkip;
		}
		if (_more) {
			_more->resizeToWidth(available + number);
			_more->moveToLeft(left, top, newWidth);
			top += _more->height() + st::exteraThreatCodeLineSkip;
		}
		top += padding.bottom() - st::exteraThreatCodeLineSkip;
		_block = QRect(
			margin.left(),
			0,
			newWidth - margin.left() - margin.right(),
			top);
		return top + margin.bottom();
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgRipple);
		p.drawRoundedRect(
			_block,
			st::exteraThreatChipRadius,
			st::exteraThreatChipRadius);
	}

private:
	struct Line {
		object_ptr<Ui::FlatLabel> number;
		object_ptr<Ui::FlatLabel> text;
	};
	std::vector<Line> _lines;
	object_ptr<Ui::FlatLabel> _more = { nullptr };
	QRect _block;

};

class ClampedText final : public Ui::RpWidget {
public:
	ClampedText(QWidget *parent, const style::FlatLabel &st)
	: RpWidget(parent)
	, _label(this, st)
	, _limit(st::exteraDescriptionLines
		* (st.style.lineHeight
			? st.style.lineHeight
			: st.style.font->height)) {
	}

	[[nodiscard]] not_null<Ui::FlatLabel*> label() const {
		return _label.data();
	}
	[[nodiscard]] rpl::producer<bool> clampedValue() const {
		return _clamped.value();
	}
	void expand() {
		_expanded = true;
		resizeToWidth(width());
	}

protected:
	int resizeGetHeight(int newWidth) override {
		_label->resizeToWidth(newWidth);
		_label->moveToLeft(0, 0, newWidth);
		const auto full = _label->height();
		_clamped = !_expanded && (full > _limit + _limit / 5);
		return _clamped.current() ? _limit : full;
	}

private:
	object_ptr<Ui::FlatLabel> _label;
	const int _limit = 0;
	bool _expanded = false;
	rpl::variable<bool> _clamped = false;

};

void ShowThreatsBox(
		not_null<Window::SessionController*> controller,
		const QString &path,
		std::vector<Threat> threats) {
	const auto locations = Call(u"capability_lines"_q, QJsonObject{
		{ u"path"_q, path },
	}).toObject();
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setStyle(st::exteraInstallBox);
		box->setWidth(st::exteraInstallWidth);
		box->setNoContentMargin(true);
		const auto sections = std::array{
			std::pair{ ThreatLevel::Danger, tr::lng_exd_threats_section_danger(tr::now) },
			std::pair{ ThreatLevel::Attention, tr::lng_exd_threats_section_attention(tr::now) },
			std::pair{ ThreatLevel::Other, tr::lng_exd_threats_section_other(tr::now) },
		};
		for (const auto &[level, title] : sections) {
			auto list = std::vector<Threat>();
			for (const auto &threat : threats) {
				if (threat.level == level) {
					list.push_back(threat);
				}
			}
			if (list.empty()) {
				continue;
			}
			box->addRow(
				object_ptr<Ui::FlatLabel>(
					box,
					title,
					st::exteraThreatSectionTitle),
				st::exteraThreatSectionPadding);
			const auto card = box->addRow(
				object_ptr<Ui::VerticalLayout>(box),
				style::margins(
					st::exteraInstallCardMargin.left(),
					0,
					st::exteraInstallCardMargin.right(),
					0));
			PaintCardBackground(card);
			for (const auto &threat : list) {
				const auto data = locations.value(threat.key).toObject();
				const auto expandable = !data.value(u"lines"_q)
					.toArray()
					.isEmpty();
				const auto row = card->add(object_ptr<ThreatRow>(
					card,
					ThreatColor(threat.level),
					*threat.icon,
					threat.title,
					threat.about,
					expandable ? Chevron::Down : Chevron::None));
				if (!expandable) {
					row->setAttribute(Qt::WA_TransparentForMouseEvents);
					continue;
				}
				const auto lines = card->add(
					object_ptr<Ui::SlideWrap<CodeLines>>(
						card,
						object_ptr<CodeLines>(card, data)));
				lines->hide(anim::type::instant);
				const auto opened = (threat.level == ThreatLevel::Danger);
				if (opened) {
					lines->show(anim::type::instant);
					row->setChevron(Chevron::Up);
				}
				row->setClickedCallback([=] {
					const auto shown = !lines->toggled();
					lines->toggle(shown, anim::type::normal);
					row->setChevron(shown ? Chevron::Up : Chevron::Down);
				});
			}
		}
		const auto close = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				tr::lng_close(),
				st::exteraInstallCancel),
			st::exteraInstallButtonMargin);
		close->setFullRadius(true);
		close->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		close->setClickedCallback([=] { box->closeBox(); });
		box->widthValue() | rpl::on_next([=](int width) {
			const auto margin = st::exteraInstallButtonMargin;
			close->setFullWidth(width - margin.left() - margin.right());
		}, close->lifetime());
		Ui::AddSkip(box->verticalLayout(), st::exteraInstallCancelMargin.bottom());
	}), Ui::LayerOption::KeepOther);
}

void AddThreatsSummary(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		const QString &path,
		std::vector<Threat> threats) {
	auto danger = 0;
	auto attention = 0;
	for (const auto &threat : threats) {
		if (threat.level == ThreatLevel::Danger) {
			++danger;
		} else if (threat.level == ThreatLevel::Attention) {
			++attention;
		}
	}
	const auto color = danger
		? st::attentionButtonFg->c
		: attention
		? st::historyPeer3UserpicBg->c
		: st::historyPeer2UserpicBg->c;
	const auto title = danger
		? tr::lng_exd_threats_found(tr::now)
		: attention
		? tr::lng_exd_threats_attention(tr::now)
		: tr::lng_exd_threats_none(tr::now);
	const auto about = danger
		? tr::lng_exd_threats_dangerous_amount(
			tr::now,
			lt_amount,
			QString::number(danger))
		: attention
		? tr::lng_exd_threats_attention_amount(
			tr::now,
			lt_amount,
			QString::number(attention))
		: threats.empty()
		? tr::lng_exd_threats_nothing(tr::now)
		: tr::lng_exd_threats_other_amount(
			tr::now,
			lt_amount,
			QString::number(threats.size()));
	const auto card = AddCard(box);
	const auto row = card->add(object_ptr<ThreatRow>(
		card,
		color,
		(danger || attention) ? st::exteraThreatAlert : st::exteraThreatOk,
		title,
		about,
		threats.empty() ? Chevron::None : Chevron::Right));
	if (threats.empty()) {
		row->setAttribute(Qt::WA_TransparentForMouseEvents);
		return;
	}
	auto chips = std::vector<Chip>();
	for (const auto &threat : threats) {
		const auto i = ranges::find(chips, threat.chipText, &Chip::text);
		if (i == end(chips)) {
			chips.push_back({
				.icon = threat.chip,
				.text = threat.chipText,
				.color = ThreatColor(threat.level),
			});
		}
	}
	card->add(object_ptr<ChipsRow>(card, std::move(chips)));
	const auto weak = base::make_weak(controller);
	row->setClickedCallback([=] {
		if (const auto strong = weak.get()) {
			ShowThreatsBox(strong, path, threats);
		}
	});
}

void AddDescription(
		not_null<Ui::GenericBox*> box,
		const TextWithEntities &text,
		const Ui::Text::MarkedContext &context) {
	const auto card = AddCard(box);
	const auto clamped = card->add(
		object_ptr<ClampedText>(card, st::exteraInstallDescription),
		st::exteraInstallCardPadding);
	clamped->label()->setMarkedText(text, context);
	SetupLinks(clamped->label(), box);
	const auto more = card->add(
		object_ptr<Ui::SlideWrap<Ui::LinkButton>>(
			card,
			object_ptr<Ui::LinkButton>(
				card,
				tr::lng_exd_description_more(tr::now))),
		st::exteraDescriptionMore);
	more->toggleOn(clamped->clampedValue(), anim::type::instant);
	more->entity()->setClickedCallback([=] {
		clamped->expand();
	});
}

void AddTopActions(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		const QString &path,
		const QString &description) {
	const auto size = st::exteraTopActionSize;
	const auto count = description.isEmpty() ? 1 : 2;
	const auto actions = Ui::CreateChild<Ui::RpWidget>(box.get());
	actions->resize(size * count, size);
	actions->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(actions);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(actions->rect(), size / 2., size / 2.);
	}, actions->lifetime());
	const auto addButton = [&](int index, const style::icon &icon, Fn<void()> callback) {
		const auto button = Ui::CreateChild<Ui::AbstractButton>(actions);
		button->setGeometry(index * size, 0, size, size);
		button->paintRequest() | rpl::on_next([=, &icon] {
			auto p = QPainter(button);
			icon.paintInCenter(p, button->rect());
		}, button->lifetime());
		button->setClickedCallback(std::move(callback));
	};
	auto index = 0;
	if (!description.isEmpty()) {
		addButton(index++, st::exteraTopActionTranslate, [=] {
			controller->show(
				Box(
					Ui::TranslateBox,
					controller->session().user(),
					MsgId(),
					TextWithEntities{ description },
					false),
				Ui::LayerOption::KeepOther);
		});
	}
	addButton(index, st::exteraTopActionCode, [=] {
		ShowSourceBox(controller, path);
	});
	box->widthValue() | rpl::on_next([=](int width) {
		const auto &margin = st::exteraTopActionsMargin;
		actions->moveToLeft(
			width - margin.right() - actions->width(),
			margin.top(),
			width);
		actions->raise();
	}, actions->lifetime());
	crl::on_main(actions, [=] { actions->raise(); });
}

} // namespace

void AddSourcePill(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		bool trusted) {
	const auto text = trusted
		? tr::lng_extera_plugins_trusted_source(tr::now)
		: tr::lng_extera_plugins_unknown_source(tr::now);
	const auto &font = st::exteraInstallPillFont;
	const auto &icon = trusted
		? st::exteraInstallPuzzle
		: st::exteraInstallShield;
	const auto padding = st::exteraInstallPillPadding;
	const auto width = padding.left()
		+ icon.width()
		+ st::exteraInstallPillSkip
		+ font->width(text)
		+ padding.right();
	const auto widget = box->addRow(
		object_ptr<Ui::AbstractButton>(box),
		st::exteraInstallPillMargin);
	widget->resize(width, st::exteraInstallPillHeight);
	widget->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(widget);
		auto hq = PainterHighQualityEnabler(p);
		const auto color = trusted
			? st::windowActiveTextFg->c
			: st::attentionButtonFg->c;
		auto bg = color;
		bg.setAlphaF(0.16);
		const auto left = (widget->width() - width) / 2;
		const auto rect = QRect(left, 0, width, widget->height());
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		p.drawRoundedRect(rect, rect.height() / 2., rect.height() / 2.);
		const auto iconLeft = left + padding.left();
		icon.paint(
			p,
			QPoint(iconLeft, (rect.height() - icon.height()) / 2),
			widget->width(),
			color);
		p.setPen(color);
		p.setFont(font);
		p.drawText(
			iconLeft + icon.width() + st::exteraInstallPillSkip,
			(rect.height() - font->height) / 2 + font->ascent,
			text);
	}, widget->lifetime());
	widget->setClickedCallback([=] {
		controller->showToast(trusted
			? tr::lng_extera_plugins_trusted_info(tr::now)
			: tr::lng_extera_plugins_unknown_info(tr::now));
	});
}

void ShowInstallBoxResolved(
		not_null<Window::SessionController*> controller,
		const QString &path,
		bool trusted) {
	const auto meta = Call(u"metadata"_q, QJsonObject{
		{ u"path"_q, path },
	}).toObject();
	if (meta.isEmpty()) {
		controller->showToast(tr::lng_extera_plugins_bad_file(tr::now));
		return;
	}
	const auto id = meta.value(u"id"_q).toString();
	auto installed = QString();
	auto exists = false;
	for (const auto &entry : Call(u"list"_q, QJsonObject()).toArray()) {
		const auto info = entry.toObject();
		if (info.value(u"id"_q).toString() == id) {
			exists = true;
			installed = info.value(u"version"_q).toString();
		}
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setStyle(st::exteraInstallBox);
		box->setWidth(st::exteraInstallWidth);
		box->setNoContentMargin(true);
		const auto context = Core::TextContext({
			.session = &controller->session(),
		});
		AddInstallIcon(box, controller, meta.value(u"icon"_q).toString());
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(meta.value(u"name"_q).toString()),
				st::exteraInstallName),
			st::exteraInstallNamePadding,
			style::al_top);

		const auto version = meta.value(u"version"_q).toString();
		const auto author = meta.value(u"author"_q).toString();
		auto line = TextWithEntities();
		if (!version.isEmpty()) {
			line.append(tr::lng_extera_plugins_version(tr::now) + ' ');
			if (exists && !installed.isEmpty()) {
				line.append(TextWithEntities{
					installed,
					{ EntityInText(EntityType::StrikeOut, 0, installed.size()) },
				});
				line.append(QString::fromUtf8(" \xE2\x86\x92 "));
			}
			line.append(version);
		}
		if (!author.isEmpty()) {
			if (!line.empty()) {
				line.append(QString::fromUtf8(" \xE2\x80\xA2 "));
			}
			line.append(WithLinks(author));
		}
		if (!line.empty()) {
			const auto label = box->addRow(
				object_ptr<Ui::FlatLabel>(box, st::exteraInstallVersion),
				st::exteraInstallVersionPadding,
			style::al_top);
			label->setMarkedText(line, context);
			SetupLinks(label, box);
		}
		AddSourcePill(box, controller, trusted);

		const auto description = meta.value(u"description"_q).toString();
		AddTopActions(box, controller, path, description);
		if (!description.isEmpty()) {
			AddDescription(box, WithLinks(description), context);
		}
		AddThreatsSummary(
			box,
			controller,
			path,
			CollectThreats(meta.value(u"capabilities"_q).toObject()));

		const auto install = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				exists
					? tr::lng_extera_plugins_update()
					: tr::lng_extera_plugins_install(),
				st::exteraInstallButton),
			st::exteraInstallButtonMargin);
		const auto cancel = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				tr::lng_cancel(),
				st::exteraInstallCancel),
			st::exteraInstallCancelMargin);
		for (const auto button : { install, cancel }) {
			button->setFullRadius(true);
			button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		}
		box->widthValue() | rpl::on_next([=](int width) {
			const auto margin = st::exteraInstallButtonMargin;
			const auto full = width - margin.left() - margin.right();
			install->setFullWidth(full);
			cancel->setFullWidth(full);
		}, install->lifetime());
		cancel->setClickedCallback([=] { box->closeBox(); });
		install->setClickedCallback([=] {
			const auto name = meta.value(u"name"_q).toString();
			const auto info = Call(u"install"_q, QJsonObject{
				{ u"path"_q, path },
			}).toObject();
			box->closeBox();
			notifyListChanged();
			controller->showToast(info.value(u"error"_q).toString().isEmpty()
				? (exists
					? tr::lng_extera_plugins_updated(tr::now, lt_name, name)
					: tr::lng_extera_plugins_installed_name(
						tr::now,
						lt_name,
						name))
				: tr::lng_extera_plugins_install_failed(tr::now));
		});
	}));
}

void PaintPuzzle(QPainter &p, QRectF rect, QColor color) {
	static const auto renderer = std::make_shared<QSvgRenderer>(
		QByteArray(R"exd(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512"><path d="M21.60,9.52 C21.60,9.52 21.61,9.52 21.61,9.52 C22.27,10.20 23.00,10.97 23.00,12.00 C23.00,13.03 22.27,13.80 21.61,14.48 C21.24,14.87 20.87,15.27 20.72,15.61 C20.59,15.93 20.57,16.45 20.56,16.97 C20.56,16.98 20.56,16.98 20.56,16.99 C20.55,17.93 20.53,19.03 19.77,19.77 C19.03,20.53 17.93,20.55 16.99,20.56 C16.98,20.56 16.98,20.56 16.97,20.56 C16.45,20.57 15.93,20.59 15.61,20.72 C15.27,20.87 14.87,21.23 14.48,21.60 C14.48,21.60 14.48,21.61 14.48,21.61 C13.80,22.27 13.03,23.00 12.00,23.00 C10.97,23.00 10.20,22.27 9.52,21.61 C9.13,21.24 8.73,20.87 8.39,20.72 C8.07,20.59 7.55,20.57 7.03,20.56 C7.02,20.56 7.02,20.56 7.01,20.56 C6.07,20.55 4.97,20.53 4.23,19.77 C3.47,19.03 3.45,17.93 3.44,16.99 C3.44,16.98 3.44,16.98 3.44,16.97 C3.43,16.45 3.41,15.93 3.28,15.61 C3.13,15.27 2.77,14.87 2.40,14.48 C2.40,14.48 2.39,14.48 2.39,14.48 C1.73,13.80 1.00,13.03 1.00,12.00 C1.00,10.97 1.73,10.20 2.39,9.52 C2.76,9.13 3.13,8.73 3.28,8.39 C3.41,8.07 3.43,7.55 3.44,7.03 C3.44,7.01 3.44,6.99 3.44,6.97 C3.47,6.03 3.48,4.96 4.23,4.23 C4.97,3.47 6.07,3.45 7.01,3.44 C7.02,3.44 7.02,3.44 7.03,3.44 C7.55,3.43 8.07,3.41 8.39,3.28 C8.73,3.13 9.13,2.77 9.52,2.40 C9.52,2.40 9.52,2.39 9.52,2.39 C10.20,1.73 10.97,1.00 12.00,1.00 C13.03,1.00 13.80,1.73 14.48,2.39 C14.87,2.76 15.27,3.13 15.61,3.28 C15.93,3.41 16.45,3.43 16.97,3.44 C16.98,3.44 16.98,3.44 16.99,3.44 C17.93,3.45 19.03,3.47 19.77,4.23 C20.53,4.97 20.55,6.07 20.56,7.01 C20.56,7.02 20.56,7.02 20.56,7.03 C20.57,7.55 20.59,8.07 20.72,8.39 C20.87,8.73 21.23,9.13 21.60,9.52 Z" fill="#ffffff" fill-opacity="0.3" transform="translate(256 256) scale(21.33) translate(-12 -12)"/><path d="M14.52,12.66 C14.52,14.80 17.00,14.49 17.00,14.49 C17.00,14.52 17.00,14.55 17.00,14.58 C17.00,15.84 17.00,16.47 16.61,16.86 C16.22,17.25 15.59,17.25 14.33,17.25 C12.78,17.25 11.22,17.25 9.67,17.25 C8.41,17.25 7.78,17.25 7.39,16.86 C7.00,16.47 7.00,15.84 7.00,14.58 C7.00,13.30 7.00,12.01 7.00,10.72 C7.00,9.47 7.00,8.83 7.39,8.44 C7.78,8.06 8.41,8.06 9.67,8.06 C9.82,8.06 9.98,8.06 10.13,8.06 C10.13,8.06 9.82,5.58 12.01,5.58 C14.21,5.58 13.89,8.06 13.89,8.06 C14.04,8.06 14.19,8.06 14.33,8.06 C15.59,8.06 16.22,8.06 16.61,8.44 C17.00,8.83 17.00,9.47 17.00,10.72 C17.00,10.75 17.00,10.79 17.00,10.82 C17.00,10.82 14.52,10.51 14.52,12.65 C14.52,12.65 14.52,12.65 14.52,12.66 Z" fill="none" stroke="#ffffff" stroke-width="1.322" stroke-linecap="round" stroke-linejoin="round" transform="translate(256 256) scale(14.5) translate(-12 -11.415)"/></svg>)exd"));
	static auto cache = base::flat_map<
		std::tuple<int, int, int, QRgb>,
		QImage>();
	const auto ratio = style::DevicePixelRatio();
	const auto pixels = (rect.size() * ratio).toSize();
	const auto key = std::make_tuple(
		pixels.width(),
		pixels.height(),
		ratio,
		color.rgba());
	auto i = cache.find(key);
	if (i == end(cache)) {
		if (cache.size() > 32) {
			cache.clear();
		}
		auto image = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		{
			auto q = QPainter(&image);
			auto hq = PainterHighQualityEnabler(q);
			renderer->render(&q, QRectF(QPointF(), QSizeF(pixels)));
			q.setCompositionMode(QPainter::CompositionMode_SourceIn);
			q.fillRect(image.rect(), color);
		}
		image.setDevicePixelRatio(ratio);
		i = cache.emplace(key, std::move(image)).first;
	}
	p.drawImage(rect.topLeft(), i->second);
}

void ShowSourceBox(
		not_null<Window::SessionController*> controller,
		const QString &path) {
	const auto source = Call(u"source"_q, QJsonObject{
		{ u"path"_q, path },
	}).toString();
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(QFileInfo(path).fileName()));
		box->setWidth(st::boxWideWidth * 3 / 2);
		box->setMaxHeight(st::boxWideWidth * 2);
		const auto label = box->addRow(
			object_ptr<Ui::FlatLabel>(box, st::exteraSourceText),
			st::boxRowPadding);
		if (source.isEmpty()) {
			label->setText(tr::lng_exd_plugin_code_empty(tr::now));
		} else {
			label->setMarkedText(TextWithEntities{
				source,
				{ EntityInText(EntityType::Code, 0, int(source.size())) },
			});
		}
		label->setSelectable(true);
		box->addButton(tr::lng_exd_plugin_code_copy(), [=] {
			QGuiApplication::clipboard()->setText(source);
			controller->showToast(tr::lng_exd_plugin_code_copied(tr::now));
		});
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

void ShowDangerBox(not_null<Window::SessionController*> controller) {
	controller->show(Ui::MakeInformBox({
		.text = tr::lng_exd_plugin_danger_text(),
		.title = tr::lng_exd_plugin_danger_title(),
	}));
}

void ShowUnknownBlockedBox(
		not_null<Window::SessionController*> controller) {
	controller->show(Ui::MakeConfirmBox({
		.text = tr::lng_exd_plugin_unknown_blocked(),
		.confirmed = [=](Fn<void()> close) {
			close();
			UrlClickHandler::Open(u"tg://settings/extera/plugins"_q);
		},
		.confirmText = tr::lng_extera_safe_mode_open(),
		.title = tr::lng_extera_plugins_unknown_source(),
	}));
}

void ShowUnknownSourcesWarning(
		not_null<Window::SessionController*> controller,
		Fn<void(bool)> done) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		struct State {
			rpl::variable<int> left = 30;
			base::Timer timer;
			Ui::Animations::Simple shake;
			bool confirmed = false;
			int baseX = 0;
		};
		const auto state = box->lifetime().make_state<State>();
		box->setTitle(tr::lng_extera_plugins_unknown());
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_exd_unknown_warning(tr::rich),
				st::boxLabel),
			st::boxRowPadding);
		const auto confirm = box->addButton(
			state->left.value() | rpl::map([](int left) {
				return (left > 0)
					? tr::lng_exd_unknown_allow_wait(
						tr::now,
						lt_seconds,
						QString::number(left))
					: tr::lng_extera_plugins_unknown_allow(tr::now);
			}),
			[=] {},
			st::attentionBoxButton);
		confirm->setClickedCallback([=] {
			if (state->left.current() > 0) {
				if (!state->shake.animating()) {
					state->baseX = confirm->x();
					state->shake.start([=] {
						const auto progress = state->shake.value(1.);
						const auto offset = std::sin(progress * 3.14159265 * 6.)
							* (1. - progress)
							* style::ConvertScale(10);
						confirm->move(
							state->baseX + int(offset),
							confirm->y());
					}, 0., 1., 500);
				}
				return;
			}
			state->confirmed = true;
			done(true);
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		state->timer.setCallback([=] {
			const auto left = state->left.current() - 1;
			state->left = std::max(left, 0);
			if (left <= 0) {
				state->timer.cancel();
			}
		});
		state->timer.callEach(1000);
		box->boxClosing() | rpl::on_next([=] {
			if (!state->confirmed) {
				done(false);
			}
		}, box->lifetime());
	}));
}

void ShowInstallBox(
		not_null<Window::SessionController*> controller,
		const QString &path,
		bool trusted) {
	const auto meta = Call(u"metadata"_q, QJsonObject{
		{ u"path"_q, path },
	}).toObject();
	if (meta.isEmpty()) {
		controller->showToast(tr::lng_extera_plugins_bad_file(tr::now));
		return;
	}
	const auto capabilities = meta.value(u"capabilities"_q).toObject();
	if (!capabilities.value(u"obfuscation"_q).toArray().isEmpty()) {
		ShowDangerBox(controller);
		return;
	}
	if (trusted) {
		Call(u"trust"_q, QJsonObject{ { u"path"_q, path } });
		ShowInstallBoxResolved(controller, path, true);
		return;
	}
	if (Call(u"is_trusted"_q, QJsonObject{ { u"path"_q, path } }).toBool()) {
		ShowInstallBoxResolved(controller, path, true);
		return;
	}
	controller->showToast(tr::lng_exd_plugin_checking_source(tr::now));
	const auto weak = base::make_weak(controller);
	Extera::PluginTrust::Verify(&controller->session(), path, [=](bool ok) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		if (ok) {
			Call(u"trust"_q, QJsonObject{ { u"path"_q, path } });
			ShowInstallBoxResolved(strong, path, true);
		} else if (Extera::Bool(u"plugins_unknown_sources"_q)) {
			ShowInstallBoxResolved(strong, path, false);
		} else {
			ShowUnknownBlockedBox(strong);
		}
	});
}

void ChooseAndInstall(not_null<Window::SessionController*> controller) {
	FileDialog::GetOpenPath(
		Core::App().getFileDialogParent(),
		tr::lng_extera_plugins_install(tr::now),
		u"exDesktop Plugin (*.plugin);;Python (*.py)"_q,
		[=](FileDialog::OpenResult &&result) {
			if (!result.paths.isEmpty()) {
				ShowInstallBox(controller, result.paths.front(), false);
			}
		});
}

void AddChatMenuItems(
		const Fn<void(QString, Fn<void()>)> &add,
		not_null<PeerData*> peer) {
	if (!Started()) {
		return;
	}
	const auto dialog = DialogId(peer);
	for (const auto &item : MenuItems(u"chat_action_menu"_q)) {
		const auto id = item.id;
		add(item.text, [=] {
			ClickMenuItem(id, QJsonObject{
				{ u"peer"_q, double(dialog) },
			});
		});
	}
}

void AddMessageMenuItems(
		not_null<Ui::PopupMenu*> menu,
		not_null<HistoryItem*> item) {
	if (!Started()) {
		return;
	}
	const auto dialog = DialogId(item->history()->peer);
	const auto messageId = item->id.bare;
	const auto text = item->originalText().text;
	for (const auto &entry : MenuItems(u"message_context_menu"_q)) {
		const auto id = entry.id;
		menu->addAction(entry.text, [=] {
			ClickMenuItem(id, QJsonObject{
				{ u"peer"_q, double(dialog) },
				{ u"message_id"_q, double(messageId) },
				{ u"message_text"_q, text },
			});
		}, &st::menuIconBotCommands);
	}
}

} // namespace Extera::Plugins
