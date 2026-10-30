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
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtCore/QJsonArray>
#include <QtCore/QFile>
#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>
#include <QtSvg/QSvgRenderer>
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
			button->toggleOn(rpl::single(info.value(u"enabled"_q).toBool()));
			button->toggledChanges(
			) | rpl::on_next([=](bool enabled) {
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

void AddSourcePill(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		bool trusted) {
	const auto text = trusted
		? tr::lng_extera_plugins_trusted_source(tr::now)
		: tr::lng_extera_plugins_unknown_source(tr::now);
	const auto &font = st::exteraInstallPillFont;
	const auto &icon = st::exteraInstallShield;
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
			? QColor(0x5c, 0xb8, 0x5f)
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
			iconLeft,
			(rect.height() - icon.height()) / 2,
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
		if (!description.isEmpty()) {
			const auto card = box->addRow(
				object_ptr<Ui::VerticalLayout>(box),
				st::exteraInstallCardMargin);
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
			const auto label = card->add(
				object_ptr<Ui::FlatLabel>(card, st::exteraInstallDescription),
				st::exteraInstallCardPadding);
			label->setMarkedText(WithLinks(description), context);
			SetupLinks(label, box);
		}

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
