#include "extera/extera_plugins_ui.h"

#include "base/unixtime.h"

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
#include "ui/layers/layer_manager.h"
#include "ui/layers/show.h"
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
#include "settings/sections/settings_extera.h"
#include "mainwindow.h"
#include "ui/text/text_options.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "extera/extera_plugin_trust.h"
#include "extera/exd_plugin_guard.h"
#include "extera/exd_plugin_porter.h"
#include "data/data_channel.h"
#include "data/data_peer_values.h"
#include "boxes/translate_box.h"
#include "data/data_user.h"
#include "ui/wrap/slide_wrap.h"
#include "extera/extera_config.h"
#include "base/timer.h"
#include "settings.h"
#include "core/click_handler_types.h"
#include "ui/effects/animations.h"
#include "ui/boxes/confirm_box.h"
#include "styles/style_layers.h"
#include "data/stickers/data_custom_emoji.h"
#include "ui/emoji_config.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_info.h"

#include <QtCore/QJsonArray>
#include <QtCore/QFile>
#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>
#include <QtGui/QCursor>
#include <QtSvg/QSvgRenderer>
#include <QtCore/QFileInfo>
#include <QtCore/QDir>
#include <QtCore/QRegularExpression>
#include <QtCore/QCryptographicHash>
#include <QtCore/QJsonObject>

namespace Extera::Plugins {
namespace {

std::weak_ptr<Ui::Show> PwShowOverride;

void PwShowBox(
		not_null<Window::SessionController*> controller,
		object_ptr<Ui::BoxContent> box,
		Ui::LayerOptions options = Ui::LayerOption::KeepOther) {
	if (const auto show = PwShowOverride.lock()) {
		show->showBox(std::move(box), options);
	} else {
		controller->show(std::move(box), options);
	}
}

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
	PwShowBox(controller, Box([=](not_null<Ui::GenericBox*> box) {
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
	if (items.isEmpty()) {
		auto enabled = false;
		for (const auto &entry : Call(u"list"_q).toArray()) {
			const auto info = entry.toObject();
			if (info.value(u"id"_q).toString() == id) {
				enabled = info.value(u"enabled"_q).toBool();
			}
		}
		Ui::AddSkip(container);
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				(enabled
					? tr::lng_exd_plugin_no_settings()
					: tr::lng_exd_plugin_settings_off()),
				st::exteraAbout),
			st::exteraAboutPadding);
		return;
	}
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
				PwShowBox(controller, Box([=](not_null<Ui::GenericBox*> box) {
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

[[nodiscard]] QString DesktopOsName(QString os) {
	if (os.isEmpty()) {
#if defined Q_OS_WIN
		os = u"windows"_q;
#elif defined Q_OS_MAC
		os = u"macos"_q;
#else
		os = u"linux"_q;
#endif
	}
	if (os == u"windows"_q) {
		return u"Windows"_q;
	} else if (os == u"macos"_q) {
		return u"macOS"_q;
	} else if (os == u"linux"_q) {
		return u"Linux"_q;
	}
	return os;
}

[[nodiscard]] QString PortedLabel(const QJsonObject &meta) {
	const auto os = meta.value(u"desktop_os"_q).toString();
	return os.isEmpty()
		? tr::lng_exd_plugin_ported(tr::now)
		: tr::lng_exd_plugin_ported_os(tr::now, lt_os, DesktopOsName(os));
}

[[nodiscard]] QJsonObject PluginPermissions(const QString &id) {
	return SandboxQuery(u"permissions"_q, id).toObject();
}

[[nodiscard]] bool HasPermissions(const QJsonObject &permissions) {
	return !permissions.value(u"net"_q).toArray().isEmpty()
		|| !permissions.value(u"paths"_q).toArray().isEmpty();
}

void ShowPartialStyled(
	not_null<Window::SessionController*> controller,
	const QJsonObject &info,
	const QStringList &items);
void AddColorPill(
	not_null<Ui::GenericBox*> box,
	const QString &text,
	const style::icon &icon,
	Fn<QColor()> color);
[[nodiscard]] not_null<Ui::RoundButton*> AddPortButton(
	not_null<Ui::GenericBox*> box,
	rpl::producer<QString> text,
	bool primary,
	bool last);

void ShowPartialBox(
		not_null<Window::SessionController*> controller,
		const QJsonObject &info,
		const QStringList &items) {
	ShowPartialStyled(controller, info, items);
}

[[nodiscard]] QString LeakCategoryTitle(const QString &category) {
	if (category == u"session"_q) {
		return tr::lng_exd_leak_session(tr::now);
	} else if (category == u"code"_q) {
		return tr::lng_exd_leak_code(tr::now);
	} else if (category == u"phone"_q) {
		return tr::lng_exd_leak_phone(tr::now);
	} else if (category == u"contacts"_q) {
		return tr::lng_exd_leak_contacts(tr::now);
	} else if (category == u"keys"_q) {
		return tr::lng_exd_leak_keys(tr::now);
	} else if (category == u"seed"_q) {
		return tr::lng_exd_leak_seed(tr::now);
	} else if (category == u"browser"_q) {
		return tr::lng_exd_leak_browser(tr::now);
	} else if (category == u"device"_q) {
		return tr::lng_exd_leak_device(tr::now);
	} else if (category == u"tokens"_q) {
		return tr::lng_exd_leak_tokens(tr::now);
	}
	return category;
}

void AddPermissionToggle(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> title,
		bool value,
		Fn<void(bool)> changed) {
	const auto button = container->add(object_ptr<Ui::SettingsButton>(
		container,
		std::move(title),
		st::settingsButtonNoIcon));
	button->toggleOn(rpl::single(value));
	button->toggledChanges(
	) | rpl::on_next([=](bool enabled) {
		changed(enabled);
	}, button->lifetime());
}

[[nodiscard]] QString JournalTime(int64 stamp) {
	const auto parsed = base::unixtime::parse(TimeId(stamp));
	const auto time = QLocale().toString(parsed.time(), QLocale::ShortFormat);
	return (parsed.date() == QDate::currentDate())
		? time
		: (QLocale().toString(parsed.date(), u"dd.MM"_q) + ' ' + time);
}

[[nodiscard]] std::vector<QJsonObject> JournalSorted(const QJsonArray &list) {
	auto result = std::vector<QJsonObject>();
	for (const auto &entry : list) {
		result.push_back(entry.toObject());
	}
	ranges::sort(result, ranges::greater(), [](const QJsonObject &o) {
		return o.value(u"last"_q).toDouble();
	});
	if (result.size() > 20) {
		result.resize(20);
	}
	return result;
}

void FillJournal(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		const QString &id,
		Fn<void()> refresh) {
	const auto journal = SandboxQuery(u"journal"_q, id).toObject();
	const auto net = JournalSorted(journal.value(u"net"_q).toArray());
	const auto files = JournalSorted(journal.value(u"files"_q).toArray());
	const auto blocked = JournalSorted(
		journal.value(u"blocked"_q).toArray());
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, tr::lng_exd_plugin_journal());
	if (net.empty() && files.empty() && blocked.empty()) {
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				tr::lng_exd_plugin_journal_empty(),
				st::exteraAbout),
			st::exteraAboutPadding);
	}
	for (const auto &item : net) {
		const auto key = item.value(u"key"_q).toString();
		const auto count = int(item.value(u"count"_q).toDouble());
		const auto last = int64(item.value(u"last"_q).toDouble());
		const auto button = Settings::AddButtonWithLabel(
			container,
			rpl::single(key),
			rpl::single(QString::number(count)
				+ QString::fromUtf8(" \xC2\xB7 ")
				+ JournalTime(last)),
			st::settingsButtonNoIcon);
		button->setClickedCallback([=] {
			PwShowBox(controller, Ui::MakeConfirmBox({
				.text = tr::lng_exd_plugin_journal_block_sure(
					tr::now,
					lt_target,
					key),
				.confirmed = [=](Fn<void()> close) {
					SandboxQuery(
						u"set_rule"_q,
						id,
						u"net"_q,
						key + u"=deny"_q);
					close();
					refresh();
				},
				.confirmText = tr::lng_exd_plugin_journal_block(),
				.confirmStyle = &st::attentionBoxButton,
			}));
		});
	}
	for (const auto &item : files) {
		const auto key = item.value(u"key"_q).toString();
		Settings::AddButtonWithLabel(
			container,
			rpl::single(QDir::toNativeSeparators(key)),
			rpl::single(JournalTime(int64(item.value(u"last"_q).toDouble()))),
			st::settingsButtonNoIcon);
	}
	for (const auto &item : blocked) {
		const auto key = item.value(u"key"_q).toString();
		auto text = key;
		if (key.startsWith(u"leak:"_q)) {
			text = LeakCategoryTitle(key.section(':', 1, 1))
				+ u": "_q
				+ key.section(':', 2);
		} else if (key.startsWith(u"path:"_q)) {
			text = tr::lng_exd_plugin_journal_path(
				tr::now,
				lt_target,
				QDir::toNativeSeparators(key.mid(5)));
		} else if (key.startsWith(u"net:"_q)) {
			text = tr::lng_exd_plugin_journal_conn(
				tr::now,
				lt_target,
				key.mid(4));
		} else if (key.startsWith(u"registry:"_q)) {
			text = tr::lng_exd_plugin_journal_registry(
				tr::now,
				lt_target,
				key.mid(9));
		} else if (key.startsWith(u"native:"_q)) {
			text = tr::lng_exd_plugin_journal_native(tr::now);
		} else if (key.startsWith(u"process:"_q)) {
			text = tr::lng_exd_plugin_journal_process(tr::now);
		} else if (key == u"msg_send"_q) {
			text = tr::lng_exd_plugin_perm_msg_send(tr::now);
		} else if (key == u"msg_edit"_q) {
			text = tr::lng_exd_plugin_perm_msg_edit(tr::now);
		} else if (key == u"msg_delete"_q) {
			text = tr::lng_exd_plugin_perm_msg_delete(tr::now);
		}
		Settings::AddButtonWithLabel(
			container,
			rpl::single(text),
			rpl::single(JournalTime(int64(item.value(u"last"_q).toDouble()))),
			st::settingsAttentionButton);
	}
	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_exd_plugin_journal_about());
}

void FillPermissions(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		const QString &id,
		Fn<void()> refresh) {
	const auto permissions = PluginPermissions(id);
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, tr::lng_exd_plugin_permissions());
	auto internet = true;
	auto extra = std::map<QString, QString>();
	for (const auto &entry : permissions.value(u"net"_q).toArray()) {
		const auto item = entry.toObject();
		if (item.value(u"key"_q).toString() == u"*"_q) {
			internet = (item.value(u"rule"_q).toString() != u"deny"_q);
		}
	}
	for (const auto &entry : permissions.value(u"extra"_q).toArray()) {
		const auto item = entry.toObject();
		extra[item.value(u"key"_q).toString()]
			= item.value(u"rule"_q).toString();
	}
	const auto setExtra = [=](const QString &key, const QString &value) {
		SandboxQuery(u"set_rule"_q, id, u"extra"_q, key + '=' + value);
	};
	AddPermissionToggle(
		container,
		tr::lng_exd_plugin_perm_internet(),
		internet,
		[=](bool enabled) {
			SandboxQuery(
				u"set_rule"_q,
				id,
				u"net"_q,
				enabled ? u"*="_q : u"*=deny"_q);
		});
	const auto level = Extera::Int(u"plugins_guard_level"_q);
	const auto addDenyToggle = [&](
			rpl::producer<QString> title,
			const QString &key) {
		AddPermissionToggle(
			container,
			std::move(title),
			extra[key] != u"deny"_q,
			[=](bool enabled) {
				setExtra(key, enabled ? QString() : u"deny"_q);
			});
	};
	addDenyToggle(tr::lng_exd_plugin_perm_files_read(), u"files_read"_q);
	addDenyToggle(tr::lng_exd_plugin_perm_files_write(), u"files_write"_q);
	addDenyToggle(tr::lng_exd_plugin_perm_msg_send(), u"msg_send"_q);
	addDenyToggle(tr::lng_exd_plugin_perm_msg_edit(), u"msg_edit"_q);
	addDenyToggle(tr::lng_exd_plugin_perm_msg_delete(), u"msg_delete"_q);
	const auto addLevelToggle = [&](
			rpl::producer<QString> title,
			const QString &key,
			int minimum) {
		if (level < minimum) {
			Settings::AddButtonWithLabel(
				container,
				std::move(title),
				tr::lng_exd_plugin_perm_level_locked(),
				st::settingsButtonNoIcon);
			return;
		}
		AddPermissionToggle(
			container,
			std::move(title),
			extra[key] == u"allow"_q,
			[=](bool enabled) {
				setExtra(key, enabled ? u"allow"_q : u"deny"_q);
			});
	};
	addLevelToggle(tr::lng_exd_plugin_perm_process(), u"process"_q, 1);
	addLevelToggle(tr::lng_exd_plugin_perm_native(), u"native"_q, 2);
	AddPermissionToggle(
		container,
		tr::lng_exd_plugin_perm_clipboard(),
		extra[u"clipboard"_q] != u"deny"_q,
		[=](bool enabled) {
			setExtra(u"clipboard"_q, enabled ? QString() : u"deny"_q);
		});
	AddPermissionToggle(
		container,
		tr::lng_exd_plugin_perm_notify(),
		extra[u"notify"_q] != u"deny"_q,
		[=](bool enabled) {
			setExtra(u"notify"_q, enabled ? QString() : u"deny"_q);
		});
	AddPermissionToggle(
		container,
		tr::lng_exd_plugin_perm_links(),
		extra[u"links"_q] == u"allow"_q,
		[=](bool enabled) {
			setExtra(u"links"_q, enabled ? u"allow"_q : QString());
		});
	const auto revoke = [=](QString kind, QString key, QString target) {
		PwShowBox(controller, Ui::MakeConfirmBox({
			.text = tr::lng_exd_plugin_perm_revoke_sure(
				tr::now,
				lt_target,
				target),
			.confirmed = [=](Fn<void()> close) {
				SandboxQuery(u"revoke"_q, id, kind, key);
				close();
				refresh();
				notifyListChanged();
			},
			.confirmText = tr::lng_exd_plugin_perm_reset(),
		}));
	};
	for (const auto &entry : permissions.value(u"net"_q).toArray()) {
		const auto item = entry.toObject();
		const auto key = item.value(u"key"_q).toString();
		if (key == u"*"_q) {
			continue;
		}
		const auto allowed = (item.value(u"rule"_q).toString() == u"allow"_q);
		const auto shown = key.startsWith(u"udp:"_q)
			? tr::lng_exd_plugin_perm_udp(tr::now, lt_target, key.mid(4))
			: key.startsWith(u"leak:"_q)
			? tr::lng_exd_plugin_perm_leak(
				tr::now,
				lt_what,
				LeakCategoryTitle(key.mid(5).section('|', 0, 0)),
				lt_target,
				key.mid(5).section('|', 1))
			: key;
		const auto button = Settings::AddButtonWithLabel(
			container,
			rpl::single(shown),
			(allowed
				? tr::lng_exd_plugin_perm_allowed()
				: tr::lng_exd_plugin_perm_denied()),
			st::settingsButtonNoIcon);
		button->setClickedCallback([=] {
			revoke(u"net"_q, key, shown);
		});
	}
	for (const auto &entry : permissions.value(u"paths"_q).toArray()) {
		const auto key = entry.toObject().value(u"key"_q).toString();
		const auto shown = QDir::toNativeSeparators(key);
		const auto button = Settings::AddButtonWithLabel(
			container,
			rpl::single(QFileInfo(key).fileName().isEmpty()
				? shown
				: QFileInfo(key).fileName()),
			tr::lng_exd_plugin_perm_folder(),
			st::settingsButtonNoIcon);
		button->setClickedCallback([=] {
			revoke(u"path"_q, key, shown);
		});
	}
	if (HasPermissions(permissions)
		|| !permissions.value(u"extra"_q).toArray().isEmpty()) {
		const auto all = container->add(object_ptr<Ui::SettingsButton>(
			container,
			tr::lng_exd_plugin_perm_reset_all(),
			st::settingsAttentionButton));
		all->setClickedCallback([=] {
			PwShowBox(controller, Ui::MakeConfirmBox({
				.text = tr::lng_exd_plugin_perm_reset_all_sure(),
				.confirmed = [=](Fn<void()> close) {
					SandboxQuery(u"revoke"_q, id, u"all"_q);
					close();
					refresh();
					notifyListChanged();
				},
				.confirmText = tr::lng_exd_plugin_perm_reset(),
				.confirmStyle = &st::attentionBoxButton,
			}));
		});
	}
	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_exd_plugin_perm_about());
	FillJournal(container, controller, id, refresh);
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
	PwShowBox(controller, Window::PrepareChooseRecipientBox(session, [=](
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
	}));
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
	const auto code = QString::fromUtf8(file.readAll());
	PwShowBox(controller, Box([=](not_null<Ui::GenericBox*> box) {
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
	auto result = TextUtilities::ParseEntities(
		text,
		TextParseLinks
			| TextParseMentions
			| TextParseHashtags
			| TextParseMarkdown);
	static const auto kLink = QRegularExpression(
		u"\\[([^\\]\\n]+)\\]\\(((?:https?://|tg://)[^\\s)]+)\\)"_q);
	auto matches = std::vector<QRegularExpressionMatch>();
	auto iterator = kLink.globalMatch(result.text);
	while (iterator.hasNext()) {
		matches.push_back(iterator.next());
	}
	for (auto i = int(matches.size()) - 1; i >= 0; --i) {
		const auto &match = matches[i];
		const auto start = int(match.capturedStart());
		const auto end = int(match.capturedEnd());
		const auto label = match.captured(1);
		const auto url = match.captured(2);
		const auto delta = int(label.size()) - (end - start);
		auto entities = EntitiesInText();
		for (const auto &entity : result.entities) {
			const auto from = entity.offset();
			const auto till = from + entity.length();
			if (from >= start && till <= end) {
				continue;
			} else if (from >= end) {
				entities.push_back(EntityInText(
					entity.type(),
					from + delta,
					entity.length(),
					entity.data()));
			} else if (from <= start && till >= end) {
				entities.push_back(EntityInText(
					entity.type(),
					from,
					entity.length() + delta,
					entity.data()));
			} else if (till <= start) {
				entities.push_back(entity);
			}
		}
		result.text.replace(start, end - start, label);
		entities.push_back(EntityInText(
			EntityType::CustomUrl,
			start,
			int(label.size()),
			url));
		std::sort(entities.begin(), entities.end(), [](
				const EntityInText &a,
				const EntityInText &b) {
			return a.offset() < b.offset();
		});
		result.entities = std::move(entities);
	}
	return result;
}

void SetupLinks(
		not_null<Ui::FlatLabel*> label,
		not_null<Ui::GenericBox*> box) {
	label->setClickHandlerFilter([=](const ClickHandlerPtr &, Qt::MouseButton) {
		box->closeBox();
		return true;
	});
}

[[nodiscard]] QString ExdIconFramePath(const QString &key) {
	const auto hash = QCryptographicHash::hash(
		key.toUtf8(),
		QCryptographicHash::Md5).toHex();
	return cWorkingDir()
		+ u"tdata/exd_plugin_icons/"_q
		+ QString::fromLatin1(hash)
		+ u".png"_q;
}

[[nodiscard]] base::flat_map<QString, QImage> &ExdIconFrames() {
	static auto result = base::flat_map<QString, QImage>();
	return result;
}

[[nodiscard]] QImage ExdLoadIconFrame(const QString &key) {
	auto &frames = ExdIconFrames();
	if (const auto i = frames.find(key); i != end(frames)) {
		return i->second;
	}
	auto image = QImage(ExdIconFramePath(key));
	if (!image.isNull()) {
		image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(style::DevicePixelRatio());
		frames.emplace(key, image);
	}
	return image;
}

void ExdStoreIconFrame(const QString &key, QImage image) {
	ExdIconFrames()[key] = image;
	const auto path = ExdIconFramePath(key);
	crl::async([=] {
		QDir().mkpath(QFileInfo(path).absolutePath());
		image.save(path, "PNG");
	});
}

void SetupPluginIcon(
		not_null<Ui::AbstractButton*> widget,
		not_null<Window::SessionController*> controller,
		const QString &icon,
		int size,
		int badge,
		Fn<QColor()> ring = nullptr) {
	struct State {
		std::unique_ptr<Ui::Text::CustomEmoji> emoji;
		StickerSetIdentifier set;
		QImage image;
		QImage puzzle;
		Ui::Animations::Simple appear;
		bool failed = false;
		bool shown = false;
		bool captured = false;
	};
	widget->resize(size + badge / 3, size + badge / 3);
	const auto state = widget->lifetime().make_state<State>();
	const auto slash = icon.lastIndexOf('/');
	state->failed = (slash <= 0);
	const auto frameKey = icon + u"@"_q + QString::number(size);
	if (!state->failed) {
		state->image = ExdLoadIconFrame(frameKey);
		if (!state->image.isNull()) {
			state->shown = true;
			state->captured = true;
		}
	}
	const auto startAppear = [=] {
		if (state->shown) {
			return;
		}
		state->shown = true;
		state->appear.start(
			[=] { widget->update(); },
			0.,
			1.,
			st::exteraInstallIconDuration,
			anim::easeOutCirc);
	};
	if (state->failed) {
		auto length = 0;
		const auto trimmed = icon.trimmed();
		const auto emoji = Ui::Emoji::Find(trimmed, &length);
		if (emoji && length == trimmed.size()) {
			const auto ratio = style::DevicePixelRatio();
			auto image = QImage(
				QSize(size, size) * ratio,
				QImage::Format_ARGB32_Premultiplied);
			image.setDevicePixelRatio(ratio);
			image.fill(Qt::transparent);
			{
				auto q = QPainter(&image);
				const auto large = Ui::Emoji::GetSizeLarge();
				const auto drawn = large / ratio;
				const auto scale = size * 0.8 / drawn;
				q.setRenderHint(QPainter::SmoothPixmapTransform);
				q.translate(size / 2., size / 2.);
				q.scale(scale, scale);
				Ui::Emoji::Draw(q, emoji, large, -drawn / 2, -drawn / 2);
			}
			state->image = std::move(image);
			state->failed = false;
			state->shown = true;
		}
	}
	widget->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(widget);
		auto hq = PainterHighQualityEnabler(p);
		const auto left = (widget->width() - size - badge / 3) / 2;
		const auto rect = QRect(left, 0, size, size);
		const auto radius = st::exteraInstallIconRadius;
		p.setPen(Qt::NoPen);
		const auto emojiReady = state->emoji && state->emoji->ready();
		if (emojiReady) {
			startAppear();
			if (!state->captured) {
				state->captured = true;
				const auto ratio = style::DevicePixelRatio();
				auto frame = QImage(
					QSize(size, size) * ratio,
					QImage::Format_ARGB32_Premultiplied);
				frame.setDevicePixelRatio(ratio);
				frame.fill(Qt::transparent);
				{
					auto q = QPainter(&frame);
					auto fhq = PainterHighQualityEnabler(q);
					const auto real = std::max(
						size,
						int(st::exteraInstallIcon));
					q.translate(size / 2., size / 2.);
					if (real != size) {
						q.scale(size / float64(real), size / float64(real));
					}
					state->emoji->paint(q, {
						.textColor = st::windowFg->c,
						.now = crl::now(),
						.position = QPoint(-real / 2, -real / 2),
					});
				}
				ExdStoreIconFrame(frameKey, frame);
			}
		}
		if (state->failed && state->image.isNull() && !emojiReady) {
			p.setBrush(st::windowBgActive);
			p.drawRoundedRect(rect, radius, radius);
			st::exteraIconPlugins.paintInCenter(
				p,
				rect,
				st::windowFgActive->c);
		} else {
			const auto shown = (state->image.isNull() && !emojiReady)
				? 0.
				: state->appear.value(1.);
			if (shown < 1.) {
				p.setOpacity(1. - shown);
				p.setBrush(st::windowBgOver);
				p.drawRoundedRect(rect, radius, radius);
				p.setOpacity(1.);
			}
			if (shown > 0.) {
				const auto scale = 0.85 + 0.15 * shown;
				p.setOpacity(shown);
				p.translate(rect.center());
				p.scale(scale, scale);
				if (emojiReady) {
					const auto real = std::max(size, int(st::exteraInstallIcon));
					if (real != size) {
						p.scale(size / float64(real), size / float64(real));
					}
					state->emoji->paint(p, {
						.textColor = st::windowFg->c,
						.now = crl::now(),
						.position = QPoint(-real / 2, -real / 2),
					});
				} else {
					const auto real = state->image.size()
						/ state->image.devicePixelRatio();
					p.drawImage(
						QPoint(-real.width() / 2, -real.height() / 2),
						state->image);
				}
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
			p.setBrush(ring ? ring() : st::boxBg->c);
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
		PwShowBox(controller, Box<StickerSetBox>(
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
	static auto Cache = base::flat_map<QString, DocumentId>();
	const auto useDocument = [=](not_null<DocumentData*> document) {
		state->emoji = session->data().customEmojiManager().create(
			document,
			[=] { widget->update(); },
			Data::CustomEmojiManager::SizeTag::Isolated,
			std::max(size, int(st::exteraInstallIcon)));
		widget->update();
	};
	if (const auto i = Cache.find(icon); i != end(Cache)) {
		const auto document = session->data().document(i->second);
		if (document->sticker()) {
			state->set = document->sticker()->set;
			useDocument(document);
			return;
		}
	}
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
			Cache[icon] = document->id;
			useDocument(document);
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

void AddInstallIconTo(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		const QString &icon) {
	const auto widget = container->add(
		object_ptr<Ui::AbstractButton>(container),
		st::exteraInstallIconPadding);
	SetupPluginIcon(
		widget,
		controller,
		icon,
		st::exteraInstallIcon,
		st::exteraInstallBadge);
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
	PwShowBox(controller, Box([=](not_null<Ui::GenericBox*> box) {
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

void ShowPluginPermissions(
		not_null<Window::SessionController*> controller,
		const QString &id,
		const QString &name) {
	PwShowBox(controller, Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(name));
		box->setWidth(st::boxWideWidth);
		const auto content = box->verticalLayout();
		const auto perms = content->add(
			object_ptr<Ui::VerticalLayout>(content));
		const auto refreshPerms = box->lifetime().make_state<Fn<void()>>();
		*refreshPerms = [=] {
			perms->clear();
			FillPermissions(perms, controller, id, [=] {
				(*refreshPerms)();
			});
			perms->resizeToWidth(content->width());
		};
		FillPermissions(perms, controller, id, [=] {
			(*refreshPerms)();
		});
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

void RunGuard(
	not_null<Window::SessionController*> controller,
	const QString &path,
	const QString &id);

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
				const auto about = card->add(
					object_ptr<Ui::FlatLabel>(card, st::boxLabel),
					st::exteraPluginDescriptionPadding);
				about->setMarkedText(WithLinks(description));
			}
			const auto unsupported = info.value(u"unsupported"_q).toArray();
			const auto desktopMark = info.value(u"desktop"_q).toBool();
			if (desktopMark || !unsupported.isEmpty()) {
				auto items = QStringList();
				for (const auto &value : unsupported) {
					items.push_back(value.toString());
				}
				auto text = TextWithEntities();
				if (desktopMark) {
					text.append(info.value(u"ported_from"_q).toString().isEmpty()
						? tr::lng_exd_plugin_desktop(tr::now)
						: PortedLabel(info));
				} else {
					text.append(Ui::Text::Link(
						tr::lng_exd_plugin_partial(tr::now)));
				}
				const auto marks = card->add(
					object_ptr<Ui::FlatLabel>(
						card,
						rpl::single(text),
						st::exteraSubtitle),
					st::exteraPluginDescriptionPadding);
				marks->setClickHandlerFilter([=](
						const ClickHandlerPtr &,
						Qt::MouseButton) {
					ShowPartialBox(controller, info, items);
					return false;
				});
			}
			const auto error = info.value(u"error"_q).toString();
			const auto developer = Bool(u"plugins_developer_mode"_q);
			if (info.value(u"installing"_q).toBool()) {
				card->add(
					object_ptr<Ui::FlatLabel>(
						card,
						tr::lng_exd_plugin_installing_deps(),
						st::exteraAbout),
					st::exteraPluginDescriptionPadding);
			} else if (!error.isEmpty()) {
				const auto label = card->add(
					object_ptr<Ui::FlatLabel>(
						card,
						rpl::single(developer
							? error.trimmed()
							: error.trimmed().split('\n').last()),
						st::exteraAbout),
					st::exteraPluginDescriptionPadding);
				label->setSelectable(developer);
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
			addAction(st::menuIconSettings, [=] {
				ShowPluginSettings(controller, id);
			});
			const auto isPinned = pinned.contains(id);
			addAction(
				isPinned ? st::menuIconUnpin : st::menuIconPin,
				[=] {
					SetPluginPinned(id, !isPinned);
					notifyListChanged();
				});
			const auto guard = (Exd::PluginGuard::Mode() != 2);
			const auto suggested = Exd::PluginPorter::Suggested(info);
			const auto menu = card->lifetime().make_state<
				base::unique_qptr<Ui::PopupMenu>>();
			const auto more = Ui::CreateChild<Ui::IconButton>(
				actions,
				st::exteraPluginAction);
			more->setIconOverride(&st::exteraIconMore, &st::exteraIconMore);
			more->setClickedCallback([=] {
				*menu = base::make_unique_q<Ui::PopupMenu>(
					more,
					st::popupMenuWithIcons);
				const auto raw = menu->get();
				raw->addAction(tr::lng_exd_plugin_share(tr::now), [=] {
					SharePluginFile(controller, path);
				}, &st::menuIconShare);
				raw->addAction(tr::lng_exd_plugin_view_code(tr::now), [=] {
					ShowPluginCode(controller, name, path);
				}, &st::exteraIconOpenCode);
				raw->addAction(
					tr::lng_exd_plugin_permissions_menu(tr::now),
					[=] { ShowPluginPermissions(controller, id, name); },
					&st::menuIconLock);
				if (guard) {
					raw->addAction(tr::lng_exd_guard_run(tr::now), [=] {
						RunGuard(controller, path, id);
					}, &st::menuIconAntispam);
				}
				if (suggested) {
					raw->addAction(tr::lng_exd_plugin_port(tr::now), [=] {
						Exd::PluginPorter::Start(controller, path);
					}, &st::menuIconTranslate);
				}
				if (developer) {
					raw->addAction(tr::lng_exd_plugin_reload(tr::now), [=] {
						crl::on_main(controller, [=] {
							Call(u"reload"_q, QJsonObject{ { u"id"_q, id } });
							notifyListChanged();
							controller->showToast(tr::lng_exd_plugin_reloaded(
								tr::now,
								lt_name,
								name));
						});
					}, &st::menuIconRestore);
				}
				raw->popup(QCursor::pos());
			});
			const auto remove = Ui::CreateChild<Ui::IconButton>(
				actions,
				st::exteraPluginAction);
			remove->setIconOverride(
				&st::menuIconDeleteAttention,
				&st::menuIconDeleteAttention);
			remove->setClickedCallback([=] {
				PwShowBox(controller, Ui::MakeConfirmBox({
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
				more->moveToRight(remove->width(), 0, width);
			}, actions->lifetime());
			Ui::AddSkip(card, st::exteraPluginCardSkip);
		}
		container->showChildren();
		for (const auto child : container->findChildren<QWidget*>()) {
			if (!child->testAttribute(Qt::WA_WState_ExplicitShowHide)) {
				child->show();
			}
		}
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

[[nodiscard]] not_null<Ui::VerticalLayout*> AddCardTo(
		not_null<Ui::VerticalLayout*> container) {
	const auto card = container->add(
		object_ptr<Ui::VerticalLayout>(container),
		st::exdInstallColumnCardMargin,
		style::al_justify);
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
	PwShowBox(controller, Box([=](not_null<Ui::GenericBox*> box) {
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
		std::vector<Threat> threats,
		Ui::VerticalLayout *container = nullptr) {
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
	const auto card = container ? AddCardTo(container) : AddCard(box);
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
		const Ui::Text::MarkedContext &context,
		Ui::VerticalLayout *container = nullptr) {
	const auto card = container ? AddCardTo(container) : AddCard(box);
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

Fn<void(Fn<void()>)> AddTopActions(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		const QString &path,
		const QString &description,
		Ui::VerticalLayout *container = nullptr) {
	const auto size = st::exteraTopActionSize;
	const auto count = description.isEmpty() ? 1 : 2;
	const auto row = container
		? container->add(
			object_ptr<Ui::RpWidget>(container),
			st::exdInstallActionsMargin)
		: nullptr;
	if (row) {
		row->resize(row->width(), size);
	}
	const auto actions = row
		? Ui::CreateChild<Ui::RpWidget>(row)
		: Ui::CreateChild<Ui::RpWidget>(box.get());
	actions->resize(size * count, size);
	actions->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(actions);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(actions->rect(), size / 2., size / 2.);
	}, actions->lifetime());
	const auto addButton = [=](int index, const style::icon &icon, Fn<void()> callback) {
		const auto button = Ui::CreateChild<Ui::AbstractButton>(actions);
		button->setGeometry(index * size, 0, size, size);
		button->paintRequest() | rpl::on_next([=, &icon] {
			auto p = QPainter(button);
			icon.paintInCenter(p, button->rect());
		}, button->lifetime());
		button->setClickedCallback(std::move(callback));
		button->show();
	};
	auto index = 0;
	if (!description.isEmpty()) {
		addButton(index++, st::exteraTopActionTranslate, [=] {
			PwShowBox(controller, 
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
	if (row) {
		row->widthValue() | rpl::on_next([=](int width) {
			actions->moveToLeft((width - actions->width()) / 2, 0, width);
		}, actions->lifetime());
	} else {
		box->widthValue() | rpl::on_next([=](int width) {
			const auto &margin = st::exteraTopActionsMargin;
			actions->moveToLeft(
				width - margin.right() - actions->width(),
				margin.top(),
				width);
			actions->raise();
		}, actions->lifetime());
	}
	crl::on_main(actions, [=] { actions->raise(); });
	const auto reported = actions->lifetime().make_state<bool>(false);
	const auto weak = base::make_weak(actions);
	return [=](Fn<void()> report) {
		if (!weak || *reported) {
			return;
		}
		*reported = true;
		actions->resize(size * (count + 1), size);
		addButton(count, st::exteraTopActionReport, std::move(report));
		if (row) {
			const auto width = row->width();
			actions->moveToLeft((width - actions->width()) / 2, 0, width);
		} else {
			const auto width = box->width();
			const auto &margin = st::exteraTopActionsMargin;
			actions->moveToLeft(
				width - margin.right() - actions->width(),
				margin.top(),
				width);
		}
		actions->update();
		actions->raise();
	};
}

} // namespace

void AddDesktopPillTo(
		not_null<Ui::VerticalLayout*> container,
		QString ported) {
	const auto text = tr::lng_exd_plugin_desktop(tr::now);
	const auto &font = st::exteraInstallPillFont;
	const auto &icon = st::exteraInstallPuzzle;
	const auto padding = st::exteraInstallPillPadding;
	const auto width = padding.left()
		+ icon.width()
		+ st::exteraInstallPillSkip
		+ font->width(text)
		+ padding.right();
	const auto widget = container->add(
		object_ptr<Ui::RpWidget>(container),
		st::exteraInstallPillMargin);
	widget->resize(width, st::exteraInstallPillHeight);
	widget->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(widget);
		auto hq = PainterHighQualityEnabler(p);
		const auto color = st::boxTextFgGood->c;
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
	if (!ported.isEmpty()) {
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				rpl::single(ported),
				st::exteraInstallVersion),
			st::exteraInstallVersionPadding,
			style::al_top);
	}
}

void AddDesktopPill(not_null<Ui::GenericBox*> box, QString ported) {
	const auto text = tr::lng_exd_plugin_desktop(tr::now);
	const auto &font = st::exteraInstallPillFont;
	const auto &icon = st::exteraInstallPuzzle;
	const auto padding = st::exteraInstallPillPadding;
	const auto width = padding.left()
		+ icon.width()
		+ st::exteraInstallPillSkip
		+ font->width(text)
		+ padding.right();
	const auto widget = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		st::exteraInstallPillMargin);
	widget->resize(width, st::exteraInstallPillHeight);
	widget->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(widget);
		auto hq = PainterHighQualityEnabler(p);
		const auto color = st::boxTextFgGood->c;
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
	if (!ported.isEmpty()) {
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(ported),
				st::exteraInstallVersion),
			st::exteraInstallVersionPadding,
			style::al_top);
	}
}

void AddSourcePill(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		bool trusted,
		Ui::VerticalLayout *container = nullptr) {
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
	const auto widget = container
		? container->add(
			object_ptr<Ui::AbstractButton>(container),
			st::exteraInstallPillMargin)
		: box->addRow(
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

void SendGuardReport(
		not_null<Window::SessionController*> controller,
		const QString &path,
		const Exd::PluginGuard::Report &report) {
	const auto meta = Call(u"metadata"_q, QJsonObject{
		{ u"path"_q, path },
	}).toObject();
	auto text = QStringList{
		tr::lng_exd_guard_report_header(tr::now),
		tr::lng_exd_guard_report_name(tr::now)
			+ u": "_q + meta.value(u"name"_q).toString(),
		tr::lng_exd_guard_report_version(tr::now)
			+ u": "_q + meta.value(u"version"_q).toString(),
		tr::lng_exd_guard_report_author(tr::now)
			+ u": "_q + meta.value(u"author"_q).toString(),
		u"SHA-256: "_q + report.sha256,
		tr::lng_exd_guard_report_score(tr::now)
			+ u": "_q + QString::number(report.score) + u"/100"_q,
		QString(),
		Exd::PluginGuard::Summary(report),
	};
	const auto message = text.join('\n');
	const auto session = &controller->session();
	const auto weak = base::make_weak(controller);
	const auto send = [=](not_null<PeerData*> peer) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		const auto history = session->data().history(peer);
		auto list = Storage::PrepareMediaList(
			QStringList{ path },
			st::sendMediaPreviewSize,
			session->premium());
		if (list.error == Ui::PreparedList::Error::None
			&& !list.files.empty()) {
			session->api().sendFiles(
				std::move(list),
				SendMediaType::File,
				nullptr,
				Api::SendAction(history));
		}
		auto action = Api::SendAction(history);
		auto toSend = Api::MessageToSend(action);
		toSend.textWithTags = TextWithTags{ message };
		session->api().sendMessage(std::move(toSend));
		strong->showToast(tr::lng_exd_guard_report_sent(tr::now));
	};
	session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(u"exDesktopPlugins"_q),
		MTP_string()
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		const auto &data = result.data();
		session->data().processUsers(data.vusers());
		session->data().processChats(data.vchats());
		const auto peer = session->data().peerLoaded(peerFromMTP(data.vpeer()));
		const auto channel = peer ? peer->asChannel() : nullptr;
		const auto monoforum = channel ? channel->monoforumLink() : nullptr;
		if (monoforum && monoforum->amMonoforumAdmin()) {
			if (const auto strong = weak.get()) {
				QGuiApplication::clipboard()->setText(message);
				strong->showToast(tr::lng_exd_guard_report_admin(tr::now));
			}
		} else if (monoforum && monoforum->isLoaded()) {
			send(monoforum);
		} else if (const auto strong = weak.get()) {
			QGuiApplication::clipboard()->setText(message);
			strong->showToast(tr::lng_exd_guard_report_copied(tr::now));
			if (channel) {
				strong->showPeerHistory(channel);
			}
		}
	}).fail([=] {
		if (const auto strong = weak.get()) {
			strong->showToast(tr::lng_exd_guard_report_failed(tr::now));
		}
	}).send();
}

[[nodiscard]] QColor GuardLevelColor(const QString &level) {
	if (level == u"high"_q) {
		return ThreatColor(ThreatLevel::Danger);
	} else if (level == u"medium"_q) {
		return ThreatColor(ThreatLevel::Attention);
	} else if (level == u"low"_q) {
		return st::historyPeer6UserpicBg->c;
	}
	return st::historyPeer2UserpicBg->c;
}

[[nodiscard]] QString FindingLevelTitle(const QString &level) {
	if (level == u"CRITICAL"_q) {
		return tr::lng_exd_guard_finding_critical(tr::now);
	} else if (level == u"HIGH"_q) {
		return tr::lng_exd_guard_finding_high(tr::now);
	}
	return tr::lng_exd_guard_finding_warning(tr::now);
}

[[nodiscard]] QColor FindingColor(const QString &level) {
	return (level == u"CRITICAL"_q)
		? ThreatColor(ThreatLevel::Danger)
		: (level == u"HIGH"_q)
		? ThreatColor(ThreatLevel::Attention)
		: st::windowSubTextFg->c;
}

[[nodiscard]] QString VectorTitle(const QString &verdict) {
	if (verdict == u"MALICIOUS"_q) {
		return tr::lng_exd_guard_vector_malicious(tr::now);
	} else if (verdict == u"ANOMALY"_q) {
		return tr::lng_exd_guard_vector_anomaly(tr::now);
	} else if (verdict == u"CLEAN"_q) {
		return tr::lng_exd_guard_vector_clean(tr::now);
	}
	return tr::lng_exd_guard_vector_unavailable(tr::now);
}

[[nodiscard]] QString LlmTitle(const Exd::PluginGuard::Report &report) {
	if (!report.llmUsed) {
		return report.llmError.isEmpty()
			? tr::lng_exd_guard_llm_off(tr::now)
			: tr::lng_exd_guard_llm_error(tr::now);
	} else if (report.llmVerdict == u"MALICIOUS"_q) {
		return tr::lng_exd_guard_llm_malicious(tr::now);
	} else if (report.llmVerdict == u"SUSPICIOUS"_q) {
		return tr::lng_exd_guard_llm_suspicious(tr::now);
	}
	return tr::lng_exd_guard_llm_safe(tr::now);
}

class GuardChip final : public Ui::RpWidget {
public:
	GuardChip(QWidget *parent, QString text, QColor color)
	: RpWidget(parent)
	, _text(std::move(text))
	, _color(color) {
		const auto &font = st::normalFont;
		resize(
			font->width(_text) + style::ConvertScale(16),
			font->height + style::ConvertScale(6));
	}

	void setContent(QString text, QColor color) {
		if (_text == text && _color == color) {
			return;
		}
		_text = std::move(text);
		_color = color;
		const auto &font = st::normalFont;
		resize(
			font->width(_text) + style::ConvertScale(16),
			font->height + style::ConvertScale(6));
		update();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		auto bg = _color;
		bg.setAlphaF(0.16);
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		p.drawRoundedRect(rect(), height() / 2., height() / 2.);
		p.setPen(_color);
		p.setFont(st::normalFont);
		p.drawText(rect(), Qt::AlignCenter, _text);
	}

private:
	QString _text;
	QColor _color;

};

void AddGuardCard(
		not_null<Ui::GenericBox*> box,
		const QString &title,
		const QString &chip,
		QColor color,
		QStringList lines) {
	const auto card = AddCard(box);
	const auto head = card->add(
		object_ptr<Ui::FixedHeightWidget>(card, style::ConvertScale(36)),
		QMargins(st::exteraInstallCardPadding.left(), 6, st::exteraInstallCardPadding.left(), 0));
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		head,
		title,
		st::exteraThreatTitle);
	const auto badge = chip.isEmpty()
		? nullptr
		: Ui::CreateChild<GuardChip>(head, chip, color);
	head->widthValue() | rpl::on_next([=](int width) {
		const auto chipWidth = badge ? (badge->width() + 8) : 0;
		label->resizeToWidth(width - chipWidth);
		label->moveToLeft(0, (head->height() - label->height()) / 2, width);
		if (badge) {
			badge->moveToRight(0, (head->height() - badge->height()) / 2, width);
		}
	}, head->lifetime());
	for (const auto &line : lines) {
		const auto text = card->add(
			object_ptr<Ui::FlatLabel>(
				card,
				rpl::single(line),
				st::exteraThreatAbout),
			QMargins(st::exteraInstallCardPadding.left(), 2, st::exteraInstallCardPadding.left(), 2));
		text->setSelectable(true);
	}
	Ui::AddSkip(card, 8);
}

void ShowGuardAnalysis(
		not_null<Window::SessionController*> controller,
		const QString &path,
		Exd::PluginGuard::Report report) {
	using namespace Exd::PluginGuard;
	const auto meta = Call(u"metadata"_q, QJsonObject{
		{ u"path"_q, path },
	}).toObject();
	PwShowBox(controller, Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_exd_guard_analysis());
		box->setWidth(st::exteraInstallWidth);
		const auto color = GuardLevelColor(report.level);
		const auto score = box->addRow(
			object_ptr<Ui::FixedHeightWidget>(box, style::ConvertScale(92)),
			st::exteraInstallCardMargin);
		const auto name = meta.value(u"name"_q).toString();
		const auto level = LevelTitle(report.level);
		score->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(score);
			auto hq = PainterHighQualityEnabler(p);
			const auto side = score->height() - style::ConvertScale(12);
			const auto circle = QRectF(6, 6, side, side);
			auto pen = QPen(color);
			pen.setWidthF(style::ConvertScale(5.));
			auto back = color;
			back.setAlphaF(0.18);
			p.setPen(QPen(back, pen.widthF()));
			p.drawEllipse(circle.marginsRemoved(QMarginsF(3, 3, 3, 3)));
			p.setPen(pen);
			p.drawArc(
				circle.marginsRemoved(QMarginsF(3, 3, 3, 3)),
				90 * 16,
				-int(360 * 16 * report.score / 100.));
			p.setFont(st::semiboldFont->f);
			auto big = st::semiboldFont->f;
			big.setPixelSize(style::ConvertScale(22));
			p.setFont(big);
			p.drawText(circle, Qt::AlignCenter, QString::number(report.score));
			const auto left = int(circle.right()) + style::ConvertScale(14);
			p.setPen(st::windowBoldFg);
			p.setFont(st::semiboldFont);
			p.drawText(
				left,
				score->height() / 2 - style::ConvertScale(4),
				name);
			p.setPen(color);
			p.setFont(st::normalFont);
			p.drawText(
				left,
				score->height() / 2 + st::normalFont->height,
				level + u" \u00B7 "_q + QString::number(report.score) + u"/100"_q);
		}, score->lifetime());

		auto vectorLines = QStringList();
		if (report.modelUsed) {
			vectorLines.push_back(tr::lng_exd_guard_vector_similarity(
				tr::now,
				lt_mean,
				QString::number(report.meanSimilarity, 'f', 3),
				lt_min,
				QString::number(report.minSimilarity, 'f', 3)));
			vectorLines.push_back(tr::lng_exd_guard_vector_malware_sim(
				tr::now,
				lt_amount,
				QString::number(report.maxMalwareSimilarity, 'f', 3)));
			vectorLines.push_back(tr::lng_exd_guard_vector_chunks(
				tr::now,
				lt_amount,
				QString::number(report.scannedChunks),
				lt_count_malware,
				QString::number(report.malwareChunks),
				lt_count_suspicious,
				QString::number(report.suspiciousChunks)));
			vectorLines.push_back(tr::lng_exd_guard_vector_base(
				tr::now,
				lt_clean,
				QString::number(report.cleanVectors),
				lt_malware,
				QString::number(report.malwareVectors)));
		} else {
			vectorLines.push_back(tr::lng_exd_guard_no_model(tr::now));
		}
		AddGuardCard(
			box,
			tr::lng_exd_guard_vector_title(tr::now)
				+ (report.modelVersion.isEmpty()
					? QString()
					: (u" v"_q + report.modelVersion)),
			VectorTitle(report.vectorVerdict),
			(report.vectorVerdict == u"MALICIOUS"_q)
				? ThreatColor(ThreatLevel::Danger)
				: (report.vectorVerdict == u"ANOMALY"_q)
				? ThreatColor(ThreatLevel::Attention)
				: (report.vectorVerdict == u"CLEAN"_q)
				? st::historyPeer2UserpicBg->c
				: st::windowSubTextFg->c,
			vectorLines);

		auto llmLines = QStringList();
		if (report.llmUsed) {
			llmLines.push_back(tr::lng_exd_guard_llm_confidence(
				tr::now,
				lt_amount,
				QString::number(int(std::round(report.llmConfidence * 100)))));
			if (!report.llmReason.isEmpty()) {
				llmLines.push_back(report.llmReason);
			}
		} else if (!report.llmError.isEmpty()) {
			llmLines.push_back(report.llmError.left(300));
		} else {
			llmLines.push_back(tr::lng_exd_guard_llm_off_about(tr::now));
		}
		AddGuardCard(
			box,
			tr::lng_exd_guard_llm(tr::now),
			LlmTitle(report),
			!report.llmUsed
				? st::windowSubTextFg->c
				: (report.llmVerdict == u"MALICIOUS"_q)
				? ThreatColor(ThreatLevel::Danger)
				: (report.llmVerdict == u"SUSPICIOUS"_q)
				? ThreatColor(ThreatLevel::Attention)
				: st::historyPeer2UserpicBg->c,
			llmLines);

		const auto threats = AddCard(box);
		const auto threatsTitle = threats->add(
			object_ptr<Ui::FlatLabel>(
				threats,
				tr::lng_exd_guard_threats(
					tr::now,
					lt_amount,
					QString::number(report.findings.size())),
				st::exteraThreatTitle),
			QMargins(st::exteraInstallCardPadding.left(), 12, st::exteraInstallCardPadding.left(), 4));
		(void)threatsTitle;
		if (report.findings.empty()) {
			threats->add(
				object_ptr<Ui::FlatLabel>(
					threats,
					tr::lng_exd_guard_threats_none(),
					st::exteraThreatAbout),
				QMargins(st::exteraInstallCardPadding.left(), 0, st::exteraInstallCardPadding.left(), 0));
		}
		for (const auto &finding : report.findings) {
			const auto row = threats->add(
				object_ptr<Ui::VerticalLayout>(threats),
				QMargins(st::exteraInstallCardPadding.left(), 6, st::exteraInstallCardPadding.left(), 0));
			const auto base = style::ConvertScale(26);
			const auto head = row->add(object_ptr<Ui::RpWidget>(row));
			head->resize(head->width(), base);
			const auto chip = Ui::CreateChild<GuardChip>(
				head,
				FindingLevelTitle(finding.level),
				FindingColor(finding.level));
			const auto title = Ui::CreateChild<Ui::FlatLabel>(
				head,
				RuleTitle(finding.rule),
				st::exteraThreatTitle);
			head->widthValue() | rpl::on_next([=](int width) {
				const auto left = chip->width() + 8;
				title->resizeToWidth(std::max(width - left, 1));
				const auto line = st::exteraThreatTitle.style.font->height;
				const auto top = (base - line) / 2;
				chip->moveToLeft(0, (base - chip->height()) / 2, width);
				title->moveToLeft(left, top, width);
				const auto height = std::max(base, top + title->height() + top);
				if (head->height() != height) {
					head->resize(width, height);
				}
			}, head->lifetime());
			auto details = finding.detail;
			if (finding.line > 0) {
				details += u" \u00B7 "_q + tr::lng_exd_guard_line(
					tr::now,
					lt_amount,
					QString::number(finding.line));
			}
			if (!details.trimmed().isEmpty()) {
				const auto label = row->add(object_ptr<Ui::FlatLabel>(
					row,
					rpl::single(details),
					st::exteraThreatAbout));
				label->setSelectable(true);
			}
		}
		Ui::AddSkip(threats, 10);

		if (!report.endpoints.isEmpty()) {
			AddGuardCard(
				box,
				tr::lng_exd_guard_network(tr::now),
				QString::number(report.endpoints.size()),
				st::windowSubTextFg->c,
				report.endpoints);
		}
		auto capabilities = QStringList();
		for (const auto &threat : CollectThreats(
				meta.value(u"capabilities"_q).toObject())) {
			capabilities.push_back(threat.title);
		}
		if (!capabilities.isEmpty()) {
			AddGuardCard(
				box,
				tr::lng_exd_guard_capabilities(tr::now),
				QString::number(capabilities.size()),
				st::windowSubTextFg->c,
				capabilities);
		}
		AddGuardCard(
			box,
			u"SHA-256"_q,
			QString(),
			QColor(),
			{ report.sha256 });

		if (report.level != u"clean"_q) {
			box->addLeftButton(tr::lng_exd_guard_report(), [=] {
				SendGuardReport(controller, path, report);
				box->closeBox();
			});
		}
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

void FillGuardSummary(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		const QString &path,
		const Exd::PluginGuard::Report *report,
		Fn<void()> startManual) {
	using namespace Exd::PluginGuard;
	container->clear();
	const auto column = container->property("exdColumn").toBool();
	const auto card = container->add(
		object_ptr<Ui::VerticalLayout>(container),
		column ? st::exdInstallColumnCardMargin : st::exteraInstallCardMargin,
		column ? style::al_justify : style::al_left);
	PaintCardBackground(card);
	if (!report) {
		const auto row = card->add(object_ptr<ThreatRow>(
			card,
			st::windowSubTextFg->c,
			st::exteraThreatOk,
			startManual
				? tr::lng_exd_guard_run(tr::now)
				: tr::lng_exd_guard_checking(tr::now),
			tr::lng_exd_guard_checking_about(tr::now),
			startManual ? Chevron::Right : Chevron::None));
		if (startManual) {
			row->setClickedCallback(startManual);
		} else {
			row->setAttribute(Qt::WA_TransparentForMouseEvents);
		}
	} else if (!report->ok) {
		const auto row = card->add(object_ptr<ThreatRow>(
			card,
			st::windowSubTextFg->c,
			st::exteraThreatAlert,
			tr::lng_exd_guard_failed(tr::now),
			report->error,
			Chevron::None));
		row->setAttribute(Qt::WA_TransparentForMouseEvents);
	} else {
		const auto about = LevelTitle(report->level)
			+ u" \u00B7 "_q
			+ tr::lng_exd_guard_vector_short(tr::now)
			+ u": "_q
			+ VectorTitle(report->vectorVerdict).toLower()
			+ u" \u00B7 "_q
			+ tr::lng_exd_guard_llm_short(tr::now)
			+ u": "_q
			+ LlmTitle(*report).toLower();
		const auto row = card->add(object_ptr<ThreatRow>(
			card,
			GuardLevelColor(report->level),
			(report->level == u"clean"_q || report->level == u"low"_q)
				? st::exteraThreatOk
				: st::exteraThreatAlert,
			tr::lng_exd_guard_result(
				tr::now,
				lt_level,
				tr::lng_exd_guard_name(tr::now),
				lt_score,
				QString::number(report->score)),
			about,
			Chevron::Right));
		const auto copy = *report;
		const auto weak = base::make_weak(controller);
		row->setClickedCallback([=] {
			if (const auto strong = weak.get()) {
				ShowGuardAnalysis(strong, path, copy);
			}
		});
	}
	container->resizeToWidth(container->width());
	container->showChildren();
	for (const auto child : container->findChildren<QWidget*>()) {
		if (!child->testAttribute(Qt::WA_WState_ExplicitShowHide)) {
			child->show();
		}
	}
}

void ShowInstallBoxResolved(
		not_null<Window::SessionController*> controller,
		const QString &path,
		bool trusted,
		std::optional<QString> blocked = std::nullopt,
		bool runGuard = false) {
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
	PwShowBox(controller, Box([=](not_null<Ui::GenericBox*> box) {
		box->setStyle(st::exteraInstallBox);
		box->setWidth(st::exteraInstallWidth);
		box->setNoContentMargin(true);
		const auto context = Core::TextContext({
			.session = &controller->session(),
		});
		const auto columns = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			st::exdInstallColumnsPadding,
			style::al_justify);
		const auto leftWidth = st::exdInstallLeftWidth;
		const auto right = columns->add(
			object_ptr<Ui::VerticalLayout>(columns),
			style::margins(leftWidth, st::exdInstallRightTop, 0, 0),
			style::al_justify);
		const auto spacer = columns->add(object_ptr<Ui::RpWidget>(columns));
		const auto left = Ui::CreateChild<Ui::VerticalLayout>(
			columns);
		left->resizeToWidth(leftWidth);
		left->moveToLeft(0, 0);
		rpl::combine(
			left->heightValue(),
			right->heightValue()
		) | rpl::on_next([=](int leftHeight, int rightHeight) {
			const auto rightFull = rightHeight + st::exdInstallRightTop;
			spacer->resize(
				spacer->width(),
				std::max(leftHeight - rightFull, 0));
		}, spacer->lifetime());
		AddInstallIconTo(left, controller, meta.value(u"icon"_q).toString());
		left->add(
			object_ptr<Ui::FlatLabel>(
				left,
				rpl::single(meta.value(u"name"_q).toString()),
				st::exdInstallSideName),
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
			const auto label = left->add(
				object_ptr<Ui::FlatLabel>(left, st::exdInstallSideVersion),
				st::exteraInstallVersionPadding,
				style::al_top);
			label->setMarkedText(line, context);
			SetupLinks(label, box);
		}
		AddSourcePill(box, controller, trusted, left);
		if (meta.value(u"desktop"_q).toBool()) {
			AddDesktopPillTo(
				left,
				meta.value(u"ported_from"_q).toString().isEmpty()
					? QString()
					: PortedLabel(meta));
		}
		left->resizeToWidth(leftWidth);

		const auto description = meta.value(u"description"_q).toString();
		const auto addReport = AddTopActions(
			box,
			controller,
			path,
			description,
			left);
		left->resizeToWidth(leftWidth);
		if (!description.isEmpty()) {
			AddDescription(box, WithLinks(description), context, right);
		}
		AddThreatsSummary(
			box,
			controller,
			path,
			CollectThreats(meta.value(u"capabilities"_q).toObject()),
			right);
		const auto guardMode = Exd::PluginGuard::Mode();
		const auto guardBox = (guardMode == 2)
			? nullptr
			: right->add(
				object_ptr<Ui::VerticalLayout>(right),
				style::margins(),
				style::al_justify);
		if (guardBox) {
			guardBox->setProperty("exdColumn", true);
		}
		const auto installText = box->lifetime().make_state<
			rpl::variable<QString>>(exists
				? tr::lng_extera_plugins_update(tr::now)
				: tr::lng_extera_plugins_install(tr::now));
		const auto installButton = box->lifetime().make_state<
			QPointer<Ui::RoundButton>>();
		const auto reportSlot = box->lifetime().make_state<
			QPointer<Ui::VerticalLayout>>();
		const auto startGuard = std::make_shared<Fn<void()>>();
		*startGuard = [=] {
			if (!guardBox) {
				return;
			}
			FillGuardSummary(guardBox, controller, path, nullptr, nullptr);
			if (*installButton) {
				(*installButton)->setDisabled(true);
				*installText = tr::lng_exd_guard_checking(tr::now);
			}
			const auto weakBox = base::make_weak(box.get());
			Exd::PluginGuard::Scan(path, [=](Exd::PluginGuard::Report report) {
				if (!weakBox) {
					return;
				}
				FillGuardSummary(guardBox, controller, path, &report, nullptr);
				const auto high = report.ok && (report.level == u"high"_q);
				if (high) {
					Exd::PluginGuard::MarkBlocked(report.sha256);
				} else if (report.ok) {
					Exd::PluginGuard::Unblock(report.sha256);
				}
				if (*installButton) {
					(*installButton)->setDisabled(high);
					*installText = high
						? tr::lng_exd_guard_install_blocked(tr::now)
						: exists
						? tr::lng_extera_plugins_update(tr::now)
						: tr::lng_extera_plugins_install(tr::now);
				}
				if (high || report.level == u"medium"_q) {
					addReport([=] {
						SendGuardReport(controller, path, report);
						box->closeBox();
					});
				}
			});
		};
		if (guardBox) {
			if (runGuard) {
				FillGuardSummary(guardBox, controller, path, nullptr, nullptr);
			} else {
				FillGuardSummary(
					guardBox,
					controller,
					path,
					nullptr,
					[=] { (*startGuard)(); });
			}
		}
		(void)reportSlot;

		if (blocked) {
			box->addRow(
				object_ptr<Ui::FlatLabel>(
					box,
					tr::lng_exd_plugin_danger_title(),
					st::exteraBlockedTitle),
				st::exteraInstallVersionPadding,
				style::al_top);
			box->addRow(
				object_ptr<Ui::FlatLabel>(
					box,
					rpl::single(*blocked),
					st::exteraBlockedText),
				st::exteraInstallVersionPadding,
				style::al_top);
			const auto close = box->addRow(
				object_ptr<Ui::RoundButton>(
					box,
					tr::lng_close(),
					st::exteraInstallCancel),
				style::margins(
					st::exteraInstallButtonMargin.left(),
					st::exteraInstallButtonMargin.top(),
					st::exteraInstallButtonMargin.right(),
					st::exteraInstallCancelMargin.bottom()));
			close->setFullRadius(true);
			close->setTextTransform(
				Ui::RoundButtonTextTransform::NoTransform);
			box->widthValue() | rpl::on_next([=](int width) {
				const auto margin = st::exteraInstallButtonMargin;
				close->setFullWidth(width - margin.left() - margin.right());
			}, close->lifetime());
			close->setClickedCallback([=] { box->closeBox(); });
			if (guardBox && runGuard) {
				(*startGuard)();
			}
			return;
		}
		const auto buttons = box->addRow(
			object_ptr<Ui::FixedHeightWidget>(
				box,
				st::exteraInstallButton.height),
			style::margins(
				st::exteraInstallButtonMargin.left(),
				st::exteraInstallButtonMargin.top(),
				st::exteraInstallButtonMargin.right(),
				st::exteraInstallCancelMargin.bottom()));
		const auto cancel = Ui::CreateChild<Ui::RoundButton>(
			buttons,
			tr::lng_cancel(),
			st::exteraInstallCancel);
		const auto port = Exd::PluginPorter::Suggested(meta)
			? Ui::CreateChild<Ui::RoundButton>(
				buttons,
				tr::lng_exd_port_action(),
				st::exteraInstallCancel)
			: nullptr;
		const auto install = Ui::CreateChild<Ui::RoundButton>(
			buttons,
			installText->value(),
			st::exteraInstallButton);
		*installButton = install;
		for (const auto button : { install, cancel }) {
			button->setFullRadius(true);
			button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		}
		if (port) {
			port->setFullRadius(true);
			port->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
			port->setClickedCallback([=] {
				const auto target = path;
				const auto weakController = base::make_weak(controller);
				crl::on_main([=] {
					if (const auto strong = weakController.get()) {
						Exd::PluginPorter::Start(strong, target);
					}
				});
				box->closeBox();
			});
		}
		buttons->widthValue() | rpl::on_next([=](int width) {
			const auto gap = st::exteraInstallCancelMargin.top();
			const auto count = port ? 3 : 2;
			const auto each = (width - gap * (count - 1)) / count;
			auto left = 0;
			for (const auto button : { cancel, port, install }) {
				if (!button) {
					continue;
				}
				button->setFullWidth(each);
				button->moveToLeft(left, 0);
				left += each + gap;
			}
		}, buttons->lifetime());
		cancel->setClickedCallback([=] { box->closeBox(); });
		if (guardBox && runGuard) {
			(*startGuard)();
		}
		install->setClickedCallback([=] {
			if (install->isDisabled()) {
				return;
			}
			const auto name = meta.value(u"name"_q).toString();
			const auto info = Call(u"install"_q, QJsonObject{
				{ u"path"_q, path },
			}).toObject();
			const auto window = controller;
			const auto updated = exists;
			box->closeBox();
			notifyListChanged();
			window->showToast(info.value(u"installing"_q).toBool()
				? tr::lng_exd_plugin_installing_deps_toast(tr::now, lt_name, name)
				: info.value(u"error"_q).toString().isEmpty()
				? (updated
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
	auto source = Call(u"guard_preview"_q, QJsonObject{
		{ u"path"_q, path },
	}).toString();
	if (source.isEmpty()) {
		source = Call(u"source"_q, QJsonObject{
			{ u"path"_q, path },
		}).toString();
	}
	PwShowBox(controller, Box([=](not_null<Ui::GenericBox*> box) {
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

void ShowUnknownSourcesWarning(
		not_null<Window::SessionController*> controller,
		Fn<void(bool)> done) {
	PwShowBox(controller, Box([=](not_null<Ui::GenericBox*> box) {
		struct State {
			rpl::variable<int> left = 30;
			base::Timer timer;
			Ui::Animations::Simple shake;
			bool confirmed = false;
			int baseX = 0;
		};
		const auto state = box->lifetime().make_state<State>();
		box->setStyle(st::exteraInstallBox);
		box->setWidth(st::exteraInstallWidth);
		box->setNoContentMargin(true);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_extera_plugins_unknown(),
				st::exteraInstallName),
			QMargins(22, 28, 22, 0),
			style::al_top);
		AddColorPill(
			box,
			tr::lng_extera_plugins_unknown_source(tr::now),
			st::exteraInstallShield,
			[] { return st::historyPeer1UserpicBg->c; });
		AddGuardCard(
			box,
			tr::lng_exd_unknown_risk_title(tr::now),
			QString(),
			st::historyPeer1UserpicBg->c,
			{
				tr::lng_exd_unknown_risk_account(tr::now),
				tr::lng_exd_unknown_risk_check(tr::now),
			});
		AddGuardCard(
			box,
			tr::lng_exd_unknown_protect_title(tr::now),
			QString(),
			st::boxTextFgGood->c,
			{
				QString::fromUtf8("\xE2\x80\xA2 ") + tr::lng_exd_unknown_protect_guard(tr::now),
				QString::fromUtf8("\xE2\x80\xA2 ") + tr::lng_exd_unknown_protect_sandbox(tr::now),
				QString::fromUtf8("\xE2\x80\xA2 ") + tr::lng_exd_unknown_protect_permissions(tr::now),
			});
		const auto confirm = AddPortButton(
			box,
			state->left.value() | rpl::map([](int left) {
				return (left > 0)
					? tr::lng_exd_unknown_allow_wait(
						tr::now,
						lt_seconds,
						QString::number(left))
					: tr::lng_extera_plugins_unknown_allow(tr::now);
			}),
			true,
			false);
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
		AddPortButton(box, tr::lng_cancel(), false, true)->setClickedCallback([=] {
			box->closeBox();
		});
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

void ShowGuardLevelConfirm(
		not_null<Window::SessionController*> controller,
		int level) {
	PwShowBox(controller, Box([=](not_null<Ui::GenericBox*> box) {
		struct State {
			rpl::variable<int> left = 30;
			base::Timer timer;
			Ui::Animations::Simple shake;
			int baseX = 0;
		};
		const auto state = box->lifetime().make_state<State>();
		const auto bullet = QString::fromUtf8("\xE2\x80\xA2 ");
		box->setStyle(st::exteraInstallBox);
		box->setWidth(st::exteraInstallWidth);
		box->setNoContentMargin(true);
		const auto circle = st::exteraGuardLevelCircle;
		const auto icon = box->addRow(
			object_ptr<Ui::FixedHeightWidget>(box, circle),
			QMargins(22, 28, 22, 0));
		icon->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(icon);
			PainterHighQualityEnabler hq(p);
			auto color = st::attentionButtonFg->c;
			const auto rect = QRect(
				(icon->width() - circle) / 2,
				0,
				circle,
				circle);
			color.setAlphaF(0.16);
			p.setPen(Qt::NoPen);
			p.setBrush(color);
			p.drawEllipse(rect);
			const auto &glyph = st::exteraGuardLevelIcon;
			glyph.paint(
				p,
				rect.x() + (circle - glyph.width()) / 2,
				rect.y() + (circle - glyph.height()) / 2,
				icon->width(),
				st::attentionButtonFg->c);
		}, icon->lifetime());
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_exd_guard_lower_title(),
				st::exteraInstallName),
			QMargins(22, 14, 22, 0),
			style::al_top);
		AddColorPill(
			box,
			(level == 2)
				? tr::lng_exd_guard_level_optimal(tr::now)
				: tr::lng_exd_guard_level_standard(tr::now),
			st::exteraInstallShield,
			[] { return st::historyPeer1UserpicBg->c; });
		auto allowed = QStringList{
			bullet + tr::lng_exd_guard_item_process_ask(tr::now),
		};
		const auto same = QStringList{
			bullet + tr::lng_exd_guard_item_telegram(tr::now),
			bullet + tr::lng_exd_guard_item_own_files(tr::now),
			bullet + tr::lng_exd_guard_item_folders(tr::now),
		};
		auto denied = QStringList();
		if (level == 2) {
			allowed.push_back(bullet + tr::lng_exd_guard_item_native_ask(tr::now));
		} else {
			denied.push_back(bullet + tr::lng_exd_guard_item_native_off(tr::now));
		}
		denied.push_back(bullet + tr::lng_exd_guard_item_secrets(tr::now));
		denied.push_back(bullet + tr::lng_exd_guard_item_malware(tr::now));
		auto risks = QStringList{ tr::lng_exd_guard_risk_process(tr::now) };
		if (level == 2) {
			risks.push_back(tr::lng_exd_guard_risk_native(tr::now));
		}
		risks.push_back(tr::lng_exd_guard_risk_prompt(tr::now));
		AddGuardCard(
			box,
			tr::lng_exd_guard_lower_allowed(tr::now),
			QString(),
			st::boxTextFgGood->c,
			allowed);
		AddGuardCard(
			box,
			tr::lng_exd_guard_lower_denied(tr::now),
			QString(),
			st::attentionButtonFg->c,
			denied);
		AddGuardCard(
			box,
			tr::lng_exd_guard_lower_same(tr::now),
			QString(),
			st::windowSubTextFg->c,
			same);
		AddGuardCard(
			box,
			tr::lng_exd_guard_lower_risk(tr::now),
			QString(),
			st::historyPeer1UserpicBg->c,
			risks);
		const auto confirm = AddPortButton(
			box,
			state->left.value() | rpl::map([](int left) {
				return (left > 0)
					? tr::lng_exd_guard_lower_wait(
						tr::now,
						lt_seconds,
						QString::number(left))
					: tr::lng_exd_guard_lower_confirm(tr::now);
			}),
			true,
			false);
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
			Extera::Config::Instance().setInt(u"plugins_guard_level"_q, level);
			box->closeBox();
		});
		AddPortButton(box, tr::lng_cancel(), false, true)->setClickedCallback([=] {
			box->closeBox();
		});
		state->timer.setCallback([=] {
			const auto left = state->left.current() - 1;
			state->left = std::max(left, 0);
			if (left <= 0) {
				state->timer.cancel();
			}
		});
		state->timer.callEach(1000);
	}));
}

void ShowBoxInPluginsWindow(
		not_null<Window::SessionController*> controller,
		object_ptr<Ui::BoxContent> box) {
	PwShowBox(controller, std::move(box));
}

void ShowGuardLevelBox(not_null<Window::SessionController*> controller) {
	PwShowBox(controller, Box([=](not_null<Ui::GenericBox*> box) {
		const auto current = Extera::Int(u"plugins_guard_level"_q);
		SingleChoiceBox(box, {
			.title = tr::lng_exd_guard_level_title(),
			.options = {
				tr::lng_exd_guard_level_max(tr::now),
				tr::lng_exd_guard_level_standard(tr::now),
				tr::lng_exd_guard_level_optimal(tr::now),
			},
			.initialSelection = std::clamp(current, 0, 2),
			.callback = [=](int index) {
				if (index <= Extera::Int(u"plugins_guard_level"_q)) {
					Extera::Config::Instance().setInt(
						u"plugins_guard_level"_q,
						index);
					return;
				}
				crl::on_main(controller, [=] {
					ShowGuardLevelConfirm(controller, index);
				});
			},
		});
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(tr::lng_exd_guard_level_max(tr::now)
					+ u": "_q
					+ tr::lng_exd_guard_level_max_about(tr::now)
					+ u"\n"_q
					+ tr::lng_exd_guard_level_standard(tr::now)
					+ u": "_q
					+ tr::lng_exd_guard_level_standard_about(tr::now)
					+ u"\n"_q
					+ tr::lng_exd_guard_level_optimal(tr::now)
					+ u": "_q
					+ tr::lng_exd_guard_level_optimal_about(tr::now)
					+ u"\n\n"_q
					+ tr::lng_exd_guard_level_about(tr::now)),
				st::exteraAbout),
			st::boxRowPadding);
	}));
}

void ShowGuarded(
		not_null<Window::SessionController*> controller,
		const QString &path,
		bool trusted) {
	ShowInstallBoxResolved(
		controller,
		path,
		trusted,
		std::nullopt,
		Exd::PluginGuard::AutoOnInstall());
}

void RunGuard(
		not_null<Window::SessionController*> controller,
		const QString &path,
		const QString &id) {
	controller->showToast(tr::lng_exd_guard_checking(tr::now));
	const auto weak = base::make_weak(controller);
	Exd::PluginGuard::Scan(path, [=](Exd::PluginGuard::Report report) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		if (!report.ok) {
			strong->showToast(tr::lng_exd_guard_failed(tr::now));
			return;
		}
		if (report.level == u"high"_q) {
			Exd::PluginGuard::MarkBlocked(report.sha256);
			Call(u"set_enabled"_q, QJsonObject{
				{ u"id"_q, id },
				{ u"enabled"_q, false },
			});
			notifyListChanged();
			strong->showToast(tr::lng_exd_guard_disabled(tr::now));
		} else {
			Exd::PluginGuard::Unblock(report.sha256);
		}
		ShowGuardAnalysis(strong, path, report);
	});
}

void ShowInstallBox(
		not_null<Window::SessionController*> controller,
		const QString &path,
		bool trusted) {
	if (const auto hash = Exd::PluginGuard::FileHash(path);
		Exd::PluginGuard::IsBlocked(hash)) {
		const auto weak = base::make_weak(controller);
		Exd::PluginGuard::Scan(path, [=](Exd::PluginGuard::Report report) {
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			if (report.ok && report.level != u"high"_q) {
				Exd::PluginGuard::Unblock(hash);
				ShowInstallBox(strong, path, trusted);
				return;
			}
			ShowInstallBoxResolved(
				strong,
				path,
				trusted,
				tr::lng_exd_guard_blocked_before(tr::now));
		});
		return;
	}
	const auto meta = Call(u"metadata"_q, QJsonObject{
		{ u"path"_q, path },
	}).toObject();
	if (meta.isEmpty()) {
		controller->showToast(tr::lng_extera_plugins_bad_file(tr::now));
		return;
	}
	const auto capabilities = meta.value(u"capabilities"_q).toObject();
	if (!capabilities.value(u"obfuscation"_q).toArray().isEmpty()) {
		ShowInstallBoxResolved(
			controller,
			path,
			trusted,
			tr::lng_exd_plugin_danger_text(tr::now),
			true);
		return;
	}
	if (trusted) {
		Call(u"trust"_q, QJsonObject{ { u"path"_q, path } });
		ShowGuarded(controller, path, true);
		return;
	}
	if (Call(u"is_trusted"_q, QJsonObject{ { u"path"_q, path } }).toBool()) {
		ShowGuarded(controller, path, true);
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
			ShowGuarded(strong, path, true);
		} else if (Extera::Bool(u"plugins_unknown_sources"_q)) {
			ShowGuarded(strong, path, false);
		} else {
			const auto again = base::make_weak(strong);
			ShowUnknownSourcesWarning(strong, [=](bool allowed) {
				const auto controller = again.get();
				if (!allowed || !controller) {
					return;
				}
				Config::Instance().setBool(u"plugins_unknown_sources"_q, true);
				Call(u"unknown_sources"_q, QJsonObject{
					{ u"enabled"_q, true },
				});
				ShowGuarded(controller, path, false);
			});
		}
	});
}

void ChooseAndInstall(not_null<Window::SessionController*> controller) {
	FileDialog::GetOpenPath(
		Core::App().getFileDialogParent(),
		tr::lng_extera_plugins_install(tr::now),
		u"exDesktop Plugin (*.plugin *.elyx *.eaf);;Python (*.py)"_q,
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

void AddProfileMenuItems(
		const Fn<void(QString, Fn<void()>)> &add,
		not_null<PeerData*> peer) {
	if (!Started()) {
		return;
	}
	const auto dialog = DialogId(peer);
	for (const auto &item : MenuItems(u"profile_action_menu"_q)) {
		const auto id = item.id;
		add(item.text, [=] {
			ClickMenuItem(id, QJsonObject{
				{ u"peer"_q, double(dialog) },
			});
		});
	}
}

void AddDrawerMenuItems(const Fn<void(QString, Fn<void()>)> &add) {
	if (!Started()) {
		return;
	}
	for (const auto type : { u"drawer_menu"_q, u"main_menu"_q }) {
		for (const auto &item : MenuItems(type)) {
			const auto id = item.id;
			add(item.text, [=] {
				ClickMenuItem(id, QJsonObject());
			});
		}
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

namespace {

// Port

[[nodiscard]] QString PortServiceName(int provider) {
	using Phrase = tr::phrase<>;
	static const auto names = std::array<const Phrase*, 10>{
		&tr::lng_exd_port_provider_none,
		&tr::lng_exd_port_provider_openai,
		&tr::lng_exd_port_provider_anthropic,
		&tr::lng_exd_port_provider_openrouter,
		&tr::lng_exd_port_provider_groq,
		&tr::lng_exd_port_provider_deepseek,
		&tr::lng_exd_port_provider_ollama,
		&tr::lng_exd_port_provider_lmstudio,
		&tr::lng_exd_port_provider_custom,
		&tr::lng_exd_port_provider_gemini,
	};
	return (provider >= 0 && provider < int(names.size()))
		? (*names[provider])(tr::now)
		: QString();
}

[[nodiscard]] QString PortSeconds(int ms) {
	return tr::lng_exd_port_sec(
		tr::now,
		lt_seconds,
		QString::number(std::max(ms, 0) / 1000));
}

void AddColorPill(
		not_null<Ui::GenericBox*> box,
		const QString &text,
		const style::icon &icon,
		Fn<QColor()> color) {
	const auto &font = st::exteraInstallPillFont;
	const auto padding = st::exteraInstallPillPadding;
	const auto width = padding.left()
		+ icon.width()
		+ st::exteraInstallPillSkip
		+ font->width(text)
		+ padding.right();
	const auto target = dynamic_cast<Ui::VerticalLayout*>(
		qvariant_cast<QObject*>(box->property("exdPillTarget")));
	box->setProperty("exdPillTarget", QVariant());
	const auto widget = target
		? target->add(
			object_ptr<Ui::RpWidget>(target),
			style::margins(0, st::exdPortHeaderPillSkip, 0, 0))
		: box->addRow(
			object_ptr<Ui::RpWidget>(box),
			st::exteraInstallPillMargin);
	if (target) {
		widget->setNaturalWidth(width);
	}
	widget->resize(width, st::exteraInstallPillHeight);
	widget->paintRequest() | rpl::on_next([=, &icon] {
		auto p = QPainter(widget);
		auto hq = PainterHighQualityEnabler(p);
		const auto fg = color();
		auto bg = fg;
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
			fg);
		p.setPen(fg);
		p.setFont(font);
		p.drawText(
			iconLeft + icon.width() + st::exteraInstallPillSkip,
			(rect.height() - font->height) / 2 + font->ascent,
			text);
	}, widget->lifetime());
}

void AddPortHeader(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		const QJsonObject &meta) {
	box->setStyle(st::exteraInstallBox);
	box->setWidth(st::exteraInstallWidth);
	box->setNoContentMargin(true);
	const auto header = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		st::exdPortHeaderPadding,
		style::al_justify);
	const auto icon = Ui::CreateChild<Ui::AbstractButton>(header);
	SetupPluginIcon(
		icon,
		controller,
		meta.value(u"icon"_q).toString(),
		st::exdPortHeaderIcon,
		st::exdPortHeaderBadge);
	icon->resize(
		st::exdPortHeaderIcon + 2 * (st::exdPortHeaderBadge / 3),
		icon->height());
	const auto text = Ui::CreateChild<Ui::VerticalLayout>(header);
	box->setProperty(
		"exdPillTarget",
		QVariant::fromValue(static_cast<QObject*>(text)));
	const auto layout = [=] {
		const auto width = header->width();
		if (width <= 0) {
			return;
		}
		const auto skip = st::exdPortHeaderIconSkip;
		const auto available = std::max(width - icon->width() - skip, 1);
		const auto natural = text->naturalWidth();
		const auto textWidth = (natural > 0)
			? std::min(natural, available)
			: available;
		if (text->width() != textWidth) {
			text->resizeToWidth(textWidth);
		}
		const auto group = icon->width() + skip + textWidth;
		const auto left = std::max((width - group) / 2, 0);
		const auto height = std::max(icon->height(), text->height());
		icon->moveToLeft(left, (height - icon->height()) / 2, width);
		text->moveToLeft(
			left + icon->width() + skip,
			(height - text->height()) / 2,
			width);
		if (header->height() != height) {
			header->resize(width, height);
		}
	};
	rpl::merge(
		header->widthValue() | rpl::to_empty,
		text->naturalWidthValue() | rpl::to_empty,
		text->heightValue() | rpl::to_empty
	) | rpl::on_next(layout, header->lifetime());
	text->add(
		object_ptr<Ui::FlatLabel>(
			text,
			rpl::single(meta.value(u"name"_q).toString()),
			st::exdSideTitle),
		style::margins());
	auto line = QStringList();
	if (const auto version = meta.value(u"version"_q).toString();
		!version.isEmpty()) {
		line.push_back(tr::lng_extera_plugins_version(tr::now)
			+ ' '
			+ version);
	}
	if (const auto author = meta.value(u"author"_q).toString();
		!author.isEmpty()) {
		line.push_back(author);
	}
	if (!line.isEmpty()) {
		text->add(
			object_ptr<Ui::FlatLabel>(
				text,
				rpl::single(line.join(QString::fromUtf8(" \xE2\x80\xA2 "))),
				st::exdSideSubtitle),
			style::margins(0, st::exdSideSubtitleSkip, 0, 0));
	}
}

[[nodiscard]] not_null<Ui::RoundButton*> AddPortButton(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> text,
		bool primary,
		bool last) {
	const auto button = box->addRow(
		object_ptr<Ui::RoundButton>(
			box,
			std::move(text),
			primary ? st::exteraInstallButton : st::exteraInstallCancel),
		primary
			? st::exteraInstallButtonMargin
			: last
			? st::exteraInstallCancelMargin
			: style::margins(
				st::exteraInstallCancelMargin.left(),
				st::exteraInstallCancelMargin.top(),
				st::exteraInstallCancelMargin.right(),
				0));
	button->setFullRadius(true);
	button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	box->widthValue() | rpl::on_next([=](int width) {
		const auto margin = st::exteraInstallButtonMargin;
		button->setFullWidth(width - margin.left() - margin.right());
	}, button->lifetime());
	return button;
}

void AddPortStep(
		not_null<Ui::VerticalLayout*> card,
		const QString &title,
		const QString &status,
		QColor color,
		const QStringList &details) {
	const auto head = card->add(
		object_ptr<Ui::FixedHeightWidget>(card, style::ConvertScale(34)),
		QMargins(st::exteraInstallCardPadding.left(), 4, st::exteraInstallCardPadding.left(), 0));
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		head,
		title,
		st::exteraThreatTitle);
	const auto chip = status.isEmpty()
		? nullptr
		: Ui::CreateChild<GuardChip>(head, status, color);
	head->widthValue() | rpl::on_next([=](int width) {
		const auto chipWidth = chip ? (chip->width() + 8) : 0;
		label->resizeToWidth(width - chipWidth);
		label->moveToLeft(0, (head->height() - label->height()) / 2, width);
		if (chip) {
			chip->moveToRight(0, (head->height() - chip->height()) / 2, width);
		}
	}, head->lifetime());
	label->show();
	if (chip) {
		chip->show();
	}
	head->show();
	for (const auto &line : details) {
		const auto text = card->add(
			object_ptr<Ui::FlatLabel>(
				card,
				rpl::single(line),
				st::exteraThreatAbout),
			QMargins(st::exteraInstallCardPadding.left(), 0, st::exteraInstallCardPadding.left(), 4));
		text->setSelectable(true);
		text->show();
	}
}

struct PortStepView {
	GuardChip *chip = nullptr;
	Ui::SlideWrap<Ui::FlatLabel> *details = nullptr;
	std::shared_ptr<rpl::variable<QString>> text;
};

struct PortStepsView {
	PortStepView prepare;
	PortStepView model;
	PortStepView check;
};

[[nodiscard]] PortStepView AddPortStepView(
		not_null<Ui::VerticalLayout*> card,
		const QString &title) {
	auto result = PortStepView{
		.text = std::make_shared<rpl::variable<QString>>(),
	};
	const auto left = st::exteraInstallCardPadding.left();
	const auto head = card->add(
		object_ptr<Ui::FixedHeightWidget>(card, style::ConvertScale(34)),
		QMargins(left, 4, left, 0));
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		head,
		title,
		st::exteraThreatTitle);
	const auto chip = Ui::CreateChild<GuardChip>(
		head,
		tr::lng_exd_port_step_pending(tr::now),
		st::windowSubTextFg->c);
	rpl::combine(
		head->widthValue(),
		chip->widthValue()
	) | rpl::on_next([=](int width, int chipWidth) {
		label->resizeToWidth(width - chipWidth - 8);
		label->moveToLeft(0, (head->height() - label->height()) / 2, width);
		chip->moveToRight(0, (head->height() - chip->height()) / 2, width);
	}, head->lifetime());
	label->show();
	chip->show();
	head->show();
	const auto details = card->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			card,
			object_ptr<Ui::FlatLabel>(
				card,
				result.text->value(),
				st::exteraThreatAbout),
			QMargins(left, 0, left, 4)));
	details->entity()->setSelectable(true);
	details->toggle(false, anim::type::instant);
	result.chip = chip;
	result.details = details;
	return result;
}

void UpdatePortStep(
		const PortStepView &view,
		const QString &status,
		QColor color,
		const QStringList &lines) {
	view.chip->setContent(status, color);
	const auto text = lines.join(u"\n"_q);
	if (view.text->current() != text) {
		*view.text = text;
	}
	view.details->toggle(!text.isEmpty(), anim::type::instant);
}

void UpdatePortSteps(
		const PortStepsView &views,
		const Exd::PluginPorter::Progress &progress,
		int waiting) {
	using Stage = Exd::PluginPorter::Progress::Stage;
	const auto good = st::boxTextFgGood->c;
	const auto active = st::windowActiveTextFg->c;
	const auto muted = st::windowSubTextFg->c;
	UpdatePortStep(
		views.prepare,
		tr::lng_exd_port_step_done(tr::now),
		good,
		{ tr::lng_exd_port_step_prepare_info(
			tr::now,
			lt_hints,
			QString::number(progress.hints.size()),
			lt_sections,
			progress.sections.isEmpty()
				? u"0"_q
				: progress.sections.join(u", "_q)) });
	const auto requesting = (progress.stage == Stage::Requesting)
		|| (progress.stage == Stage::Fixing);
	auto model = QStringList();
	if (progress.attempt > 0) {
		model.push_back(tr::lng_exd_port_step_attempt(
			tr::now,
			lt_attempt,
			QString::number(progress.attempt),
			lt_total,
			QString::number(progress.attempts)));
	}
	if (requesting) {
		model.push_back(tr::lng_exd_port_step_waiting_for(
			tr::now,
			lt_time,
			PortSeconds(waiting)));
	} else if (progress.elapsed > 0) {
		model.push_back(tr::lng_exd_port_step_answered(
			tr::now,
			lt_time,
			PortSeconds(progress.elapsed)));
	}
	if (!progress.requested.isEmpty()) {
		model.push_back(tr::lng_exd_port_docs_requested(
			tr::now,
			lt_list,
			progress.requested.join(u", "_q)));
	}
	UpdatePortStep(
		views.model,
		requesting
			? tr::lng_exd_port_step_active(tr::now)
			: tr::lng_exd_port_step_done(tr::now),
		requesting ? active : good,
		model);
	auto check = QStringList();
	if (progress.fixed > 0) {
		check.push_back(tr::lng_exd_port_fixed(
			tr::now,
			lt_value,
			QString::number(progress.fixed)));
	}
	for (const auto &error : progress.errors.mid(0, 3)) {
		check.push_back(QString::fromUtf8("\xE2\x80\xA2 ") + error);
	}
	const auto checking = (progress.stage == Stage::Checking);
	const auto fixing = (progress.stage == Stage::Fixing);
	UpdatePortStep(
		views.check,
		checking
			? tr::lng_exd_port_step_active(tr::now)
			: fixing
			? tr::lng_exd_port_step_fixing(tr::now)
			: tr::lng_exd_port_step_pending(tr::now),
		(checking || fixing) ? active : muted,
		check);
}

void ShowPortResult(
		not_null<Window::SessionController*> controller,
		const QString &path,
		const QJsonObject &meta,
		const Exd::PluginPorter::Progress &progress,
		const Exd::PluginPorter::Info &info) {
	using Stage = Exd::PluginPorter::Progress::Stage;
	PwShowBox(controller, Box([=](not_null<Ui::GenericBox*> box) {
		AddPortHeader(box, controller, meta);
		const auto good = st::boxTextFgGood->c;
		const auto attention = st::historyPeer3UserpicBg->c;
		const auto bad = st::historyPeer1UserpicBg->c;
		const auto stats = tr::lng_exd_port_stats(
			tr::now,
			lt_attempts,
			QString::number(std::max(progress.attempt, 1)),
			lt_time,
			PortSeconds(progress.total),
			lt_model,
			PortServiceName(info.provider)
				+ (info.model.isEmpty() ? QString() : (u" / "_q + info.model)));
		const auto list = [&](const QString &key) {
			auto result = QStringList();
			for (const auto &value : progress.report.value(key).toArray()) {
				result.push_back(QString::fromUtf8("\xE2\x80\xA2 ") + value.toString());
			}
			return result;
		};
		if (progress.stage == Stage::Done) {
			AddColorPill(
				box,
				tr::lng_exd_plugin_ported_os(tr::now, lt_os, DesktopOsName(QString())),
				st::exteraInstallTranslate,
				[] { return st::boxTextFgGood->c; });
			AddGuardCard(
				box,
				tr::lng_exd_port_done(tr::now),
				QString(),
				good,
				QStringList{
					tr::lng_exd_port_done_about(tr::now),
					stats,
				} + (progress.library
					? QStringList{ tr::lng_exd_port_library(tr::now) }
					: QStringList()));
			const auto kept = list(u"kept"_q);
			const auto changed = list(u"changed"_q);
			const auto dropped = list(u"dropped"_q);
			if (!kept.isEmpty()) {
				AddGuardCard(
					box,
					tr::lng_exd_port_kept(tr::now),
					QString::number(kept.size()),
					good,
					kept);
			}
			if (!changed.isEmpty()) {
				AddGuardCard(
					box,
					tr::lng_exd_port_changed(tr::now),
					QString::number(changed.size()),
					st::windowActiveTextFg->c,
					changed);
			}
			if (!dropped.isEmpty()) {
				AddGuardCard(
					box,
					tr::lng_exd_port_dropped(tr::now),
					QString::number(dropped.size()),
					attention,
					dropped);
			}
			const auto install = AddPortButton(
				box,
				tr::lng_extera_plugins_install(),
				true,
				false);
			const auto saved = progress.path;
			const auto sourceTrusted = Call(u"is_trusted"_q, QJsonObject{
				{ u"path"_q, path },
			}).toBool();
			install->setClickedCallback([=] {
				const auto target = saved;
				const auto weakController = base::make_weak(controller);
				if (sourceTrusted) {
					Call(u"trust"_q, QJsonObject{ { u"path"_q, target } });
				}
				crl::on_main([=] {
					if (const auto strong = weakController.get()) {
						ShowInstallBox(strong, target, false);
					}
				});
				box->closeBox();
			});
			AddPortButton(box, tr::lng_close(), false, true)->setClickedCallback([=] {
				box->closeBox();
			});
			return;
		}
		const auto nothing = (progress.stage == Stage::Nothing);
		AddColorPill(
			box,
			nothing
				? tr::lng_exd_port_nothing_title(tr::now)
				: tr::lng_exd_port_failed(tr::now),
			st::exteraInstallShield,
			[=] { return nothing ? st::historyPeer3UserpicBg->c : st::historyPeer1UserpicBg->c; });
		if (nothing) {
			auto lines = QStringList{ tr::lng_exd_port_nothing(tr::now) };
			AddGuardCard(
				box,
				tr::lng_exd_port_nothing_title(tr::now),
				QString(),
				attention,
				lines);
			const auto dropped = list(u"dropped"_q);
			if (!dropped.isEmpty()) {
				AddGuardCard(
					box,
					tr::lng_exd_port_dropped(tr::now),
					QString::number(dropped.size()),
					attention,
					dropped);
			}
		} else {
			auto lines = QStringList();
			if (progress.limit) {
				lines.push_back(tr::lng_exd_port_error_limit(tr::now));
			}
			if (!progress.error.isEmpty()) {
				lines.push_back(progress.error.left(600));
			}
			if (!progress.errors.isEmpty()) {
				lines.push_back(tr::lng_exd_port_failed_checks(tr::now));
				for (const auto &error : progress.errors.mid(0, 8)) {
					lines.push_back(QString::fromUtf8("\xE2\x80\xA2 ") + error);
				}
			}
			AddGuardCard(
				box,
				tr::lng_exd_port_failed(tr::now),
				QString(),
				bad,
				lines);
			if (info.local || progress.limit) {
				AddGuardCard(
					box,
					tr::lng_exd_port_advice(tr::now),
					QString(),
					st::windowActiveTextFg->c,
					{ tr::lng_exd_port_advice_text(tr::now) });
			}
		}
		AddGuardCard(box, tr::lng_exd_port_service(tr::now), QString(), st::windowSubTextFg->c, { stats });
		AddPortButton(box, tr::lng_exd_port_open_settings(), false, false)->setClickedCallback([=] {
			box->closeBox();
			UrlClickHandler::Open(u"tg://settings/extera/plugins/engine"_q);
		});
		AddPortButton(box, tr::lng_close(), false, true)->setClickedCallback([=] {
			box->closeBox();
		});
	}));
}

[[nodiscard]] QString PartialArea(const QString &hook) {
	const auto cls = hook.section('.', 0, 0);
	const auto has = [&](std::initializer_list<const char*> names) {
		for (const auto name : names) {
			if (cls.contains(QString::fromLatin1(name))) {
				return true;
			}
		}
		return false;
	};
	if (has({ "ChatMessageCell", "ChatActionCell", "Cell" })) {
		return tr::lng_exd_partial_area_cells(tr::now);
	} else if (has({ "ChatActivityEnterView", "EditText" })) {
		return tr::lng_exd_partial_area_input(tr::now);
	} else if (has({ "ChatActivity", "ChatAvatarContainer" })) {
		return tr::lng_exd_partial_area_chat(tr::now);
	} else if (has({ "DialogsActivity", "Dialog" })) {
		return tr::lng_exd_partial_area_dialogs(tr::now);
	} else if (has({ "Profile" })) {
		return tr::lng_exd_partial_area_profile(tr::now);
	} else if (has({ "PhotoViewer", "Camera", "ChatAttachAlert", "Story", "Stories" })) {
		return tr::lng_exd_partial_area_media(tr::now);
	} else if (has({ "ActionBar", "Theme", "View", "Layout", "Drawer", "Launch" })) {
		return tr::lng_exd_partial_area_ui(tr::now);
	} else if (has({ "UserConfig", "MessagesController", "Controller", "Premium" })) {
		return tr::lng_exd_partial_area_data(tr::now);
	}
	return tr::lng_exd_partial_area_other(tr::now);
}

void ShowPartialStyled(
		not_null<Window::SessionController*> controller,
		const QJsonObject &info,
		const QStringList &items) {
	PwShowBox(controller, Box([=](not_null<Ui::GenericBox*> box) {
		AddPortHeader(box, controller, info);
		const auto attention = st::historyPeer3UserpicBg->c;
		AddColorPill(
			box,
			tr::lng_exd_plugin_partial(tr::now),
			st::exteraInstallShield,
			[] { return st::historyPeer3UserpicBg->c; });
		AddGuardCard(
			box,
			tr::lng_exd_partial_why(tr::now),
			QString(),
			st::windowActiveTextFg->c,
			{ tr::lng_exd_plugin_partial_about(tr::now) });
		auto groups = std::vector<std::pair<QString, QStringList>>();
		for (const auto &item : items) {
			const auto area = PartialArea(item);
			auto found = ranges::find(groups, area, &std::pair<QString, QStringList>::first);
			if (found == groups.end()) {
				groups.push_back({ area, {} });
				found = groups.end() - 1;
			}
			found->second.push_back(QString::fromUtf8("\xE2\x80\xA2 ") + item);
		}
		for (const auto &[area, lines] : groups) {
			AddGuardCard(
				box,
				area,
				QString::number(lines.size()),
				attention,
				lines);
		}
		auto works = QStringList();
		if (info.value(u"has_settings"_q).toBool()) {
			works.push_back(QString::fromUtf8("\xE2\x80\xA2 ") + tr::lng_exd_partial_works_settings(tr::now));
		}
		works.push_back(QString::fromUtf8("\xE2\x80\xA2 ") + tr::lng_exd_partial_works_api(tr::now));
		AddGuardCard(
			box,
			tr::lng_exd_partial_works(tr::now),
			QString(),
			st::boxTextFgGood->c,
			works);
		const auto path = info.value(u"path"_q).toString();
		if (Exd::PluginPorter::Suggested(info)) {
			AddPortButton(box, tr::lng_exd_port_action(), true, false)->setClickedCallback([=] {
				const auto target = path;
				const auto weakController = base::make_weak(controller);
				crl::on_main([=] {
					if (const auto strong = weakController.get()) {
						Exd::PluginPorter::Start(strong, target);
					}
				});
				box->closeBox();
			});
		}
		AddPortButton(box, tr::lng_close(), false, true)->setClickedCallback([=] {
			box->closeBox();
		});
	}));
}

struct PortTask {
	QString path;
	QJsonObject meta;
	Exd::PluginPorter::Info info;
	std::shared_ptr<Exd::PluginPorter::Job> job;
	Exd::PluginPorter::Progress progress;
	base::weak_ptr<Window::SessionController> controller;
	base::Timer timer;
	crl::time requested = 0;
	bool finished = false;
	bool boxOpen = false;
	rpl::event_stream<> updates;
};

[[nodiscard]] std::shared_ptr<PortTask> &CurrentPort() {
	static auto &result = *new std::shared_ptr<PortTask>();
	return result;
}

[[nodiscard]] rpl::variable<PortBarState> &PortBarVariable() {
	static auto &result = *new rpl::variable<PortBarState>();
	return result;
}

[[nodiscard]] int PortWaiting(const std::shared_ptr<PortTask> &task) {
	return task->requested ? int(crl::now() - task->requested) : 0;
}

[[nodiscard]] QString PortTaskName(const std::shared_ptr<PortTask> &task) {
	const auto name = task->meta.value(u"name"_q).toString();
	return name.isEmpty() ? QFileInfo(task->path).completeBaseName() : name;
}

[[nodiscard]] float64 PortFraction(
		const Exd::PluginPorter::Progress &progress) {
	using Stage = Exd::PluginPorter::Progress::Stage;
	const auto total = std::max(progress.attempts, 1);
	const auto done = std::clamp(progress.attempt - 1, 0, total);
	const auto part = (progress.stage == Stage::Checking) ? 0.8 : 0.3;
	return std::clamp((done + part) / total, 0.05, 0.95);
}

void RefreshPortBar() {
	using Stage = Exd::PluginPorter::Progress::Stage;
	const auto task = CurrentPort();
	if (!task) {
		PortBarVariable() = PortBarState();
		return;
	}
	const auto &progress = task->progress;
	auto state = PortBarState{
		.name = PortTaskName(task),
		.active = true,
		.finished = task->finished,
	};
	if (task->finished) {
		state.failed = (progress.stage == Stage::Failed);
		state.progress = 1.;
		state.status = (progress.stage == Stage::Done)
			? tr::lng_exd_port_bar_done(tr::now)
			: (progress.stage == Stage::Nothing)
			? tr::lng_exd_port_bar_nothing(tr::now)
			: tr::lng_exd_port_bar_failed(tr::now);
	} else {
		state.progress = PortFraction(progress);
		auto parts = QStringList();
		if (progress.stage == Stage::Checking) {
			parts.push_back(tr::lng_exd_port_step_check(tr::now));
		} else {
			if (progress.attempt > 0) {
				parts.push_back(tr::lng_exd_port_step_attempt(
					tr::now,
					lt_attempt,
					QString::number(progress.attempt),
					lt_total,
					QString::number(progress.attempts)));
			}
			if (progress.stage == Stage::Fixing) {
				parts.push_back(tr::lng_exd_port_step_fixing(tr::now));
			}
			parts.push_back(PortSeconds(PortWaiting(task)));
		}
		state.status = parts.join(QString::fromUtf8(" \xE2\x80\xA2 "));
	}
	PortBarVariable() = std::move(state);
}

void ClearPort() {
	CurrentPort() = nullptr;
	RefreshPortBar();
}

void ShowTaskResult(
		not_null<Window::SessionController*> controller,
		std::shared_ptr<PortTask> task) {
	const auto path = task->path;
	const auto meta = task->meta;
	const auto progress = task->progress;
	const auto info = task->info;
	if (CurrentPort() == task) {
		ClearPort();
	}
	ShowPortResult(controller, path, meta, progress, info);
}

void FinishPort(const std::shared_ptr<PortTask> &task) {
	task->finished = true;
	task->timer.cancel();
	RefreshPortBar();
	task->updates.fire({});
	if (!task->boxOpen) {
		if (const auto strong = task->controller.get()) {
			strong->showToast(tr::lng_exd_port_finished_toast(
				tr::now,
				lt_name,
				PortTaskName(task)));
		}
	}
}

void OpenPortBox(
		not_null<Window::SessionController*> controller,
		std::shared_ptr<PortTask> task) {
	if (task->finished) {
		ShowTaskResult(controller, task);
		return;
	}
	const auto meta = task->meta;
	const auto info = task->info;
	const auto weakTask = std::weak_ptr<PortTask>(task);
	PwShowBox(controller, Box([=](not_null<Ui::GenericBox*> box) {
		AddPortHeader(box, controller, meta);
		AddColorPill(
			box,
			PortServiceName(info.provider)
				+ (info.model.isEmpty() ? QString() : (u" / "_q + info.model)),
			st::exteraInstallPuzzle,
			[] { return st::windowActiveTextFg->c; });
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_exd_port_title(),
				st::exteraThreatSectionTitle),
			st::exteraThreatSectionPadding);
		const auto card = AddCard(box);
		Ui::AddSkip(card, 6);
		const auto views = std::make_shared<PortStepsView>();
		views->prepare = AddPortStepView(
			card,
			tr::lng_exd_port_step_prepare(tr::now));
		views->model = AddPortStepView(
			card,
			tr::lng_exd_port_step_model(tr::now));
		views->check = AddPortStepView(
			card,
			tr::lng_exd_port_step_check(tr::now));
		Ui::AddSkip(card, 8);
		const auto hints = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			style::margins());
		if (info.local) {
			AddGuardCard(
				box,
				tr::lng_exd_port_local_title(tr::now),
				QString(),
				st::historyPeer3UserpicBg->c,
				{ tr::lng_exd_port_warning(tr::now) });
		}
		AddPortButton(
			box,
			tr::lng_exd_port_minimize(),
			true,
			false
		)->setClickedCallback([=] {
			box->closeBox();
		});
		AddPortButton(
			box,
			tr::lng_exd_port_cancel(),
			false,
			true
		)->setClickedCallback([=] {
			if (const auto strong = weakTask.lock()) {
				if (CurrentPort() == strong) {
					Exd::PluginPorter::Cancel(strong->job);
					strong->finished = true;
					ClearPort();
				}
			}
			box->closeBox();
		});
		const auto hintsShown = std::make_shared<bool>(false);
		const auto refresh = [=] {
			const auto strong = weakTask.lock();
			if (!strong) {
				return;
			}
			UpdatePortSteps(*views, strong->progress, PortWaiting(strong));
			if (!*hintsShown && !strong->progress.hints.isEmpty()) {
				*hintsShown = true;
				auto lines = QStringList();
				for (const auto &hint : strong->progress.hints) {
					lines.push_back(QString::fromUtf8("\xE2\x80\xA2 ") + hint);
				}
				const auto inner = hints->add(
					object_ptr<Ui::VerticalLayout>(hints),
					st::exteraInstallCardMargin);
				PaintCardBackground(inner);
				AddPortStep(
					inner,
					tr::lng_exd_port_hints(tr::now),
					QString::number(lines.size()),
					st::windowActiveTextFg->c,
					lines);
				Ui::AddSkip(inner, 8);
				inner->show();
				hints->resizeToWidth(box->width());
			}
		};
		task->boxOpen = true;
		task->updates.events(
		) | rpl::on_next([=] {
			const auto strong = weakTask.lock();
			if (!strong) {
				return;
			}
			if (strong->finished) {
				const auto weak = base::make_weak(controller);
				crl::on_main([=] {
					if (const auto window = weak.get()) {
						if (CurrentPort() == strong) {
							ShowTaskResult(window, strong);
						}
					}
				});
				box->closeBox();
				return;
			}
			refresh();
		}, box->lifetime());
		box->boxClosing() | rpl::on_next([=] {
			const auto strong = weakTask.lock();
			if (!strong || !strong->boxOpen) {
				return;
			}
			strong->boxOpen = false;
			if (!strong->finished && CurrentPort() == strong) {
				controller->showToast(tr::lng_exd_port_background(tr::now));
			}
		}, box->lifetime());
		refresh();
	}));
}

} // namespace

void ShowPortBox(
		not_null<Window::SessionController*> controller,
		const QString &path) {
	using Stage = Exd::PluginPorter::Progress::Stage;
	if (const auto task = CurrentPort(); task && !task->finished) {
		if (task->path != path) {
			controller->showToast(tr::lng_exd_port_busy(tr::now));
		}
		OpenPortBox(controller, task);
		return;
	}
	auto task = std::make_shared<PortTask>();
	task->path = path;
	task->meta = Call(u"metadata"_q, QJsonObject{
		{ u"path"_q, path },
	}).toObject();
	task->info = Exd::PluginPorter::CurrentInfo();
	task->controller = base::make_weak(controller);
	const auto weak = std::weak_ptr<PortTask>(task);
	auto error = QString();
	task->job = Exd::PluginPorter::Run(path, [=](
			const Exd::PluginPorter::Progress &progress) {
		const auto strong = weak.lock();
		if (!strong || strong->finished) {
			return;
		}
		const auto restart = (progress.stage == Stage::Requesting
			|| progress.stage == Stage::Fixing)
			&& (progress.attempt != strong->progress.attempt
				|| strong->progress.stage == Stage::Checking
				|| !strong->requested);
		strong->progress = progress;
		if (restart) {
			strong->requested = crl::now();
		}
		if (progress.stage == Stage::Done
			|| progress.stage == Stage::Nothing
			|| progress.stage == Stage::Failed) {
			FinishPort(strong);
			return;
		}
		RefreshPortBar();
		strong->updates.fire({});
	}, &error);
	if (!task->job) {
		controller->showToast((error == u"format"_q)
			? tr::lng_exd_port_error_format(tr::now)
			: (error == u"size"_q)
			? tr::lng_exd_port_error_size(tr::now)
			: tr::lng_extera_plugins_bad_file(tr::now));
		return;
	}
	task->timer.setCallback([=] {
		if (const auto strong = weak.lock()) {
			RefreshPortBar();
			strong->updates.fire({});
		}
	});
	task->timer.callEach(1000);
	CurrentPort() = task;
	RefreshPortBar();
	OpenPortBox(controller, task);
}

rpl::producer<PortBarState> PortBarValue() {
	return PortBarVariable().value();
}

void OpenPortTask(not_null<Window::SessionController*> controller) {
	if (const auto task = CurrentPort()) {
		OpenPortBox(controller, task);
	}
}

void ShowPortSetupBox(
		not_null<Window::SessionController*> controller,
		const QString &path) {
	if (const auto task = CurrentPort(); task && !task->finished) {
		OpenPortBox(controller, task);
		return;
	}
	const auto meta = Call(u"metadata"_q, QJsonObject{
		{ u"path"_q, path },
	}).toObject();
	const auto prepared = Call(u"port_prepare"_q, QJsonObject{
		{ u"path"_q, path },
	}).toObject();
	if (const auto error = prepared.value(u"error"_q).toString();
		!error.isEmpty()) {
		controller->showToast((error == u"format"_q)
			? tr::lng_exd_port_error_format(tr::now)
			: (error == u"size"_q)
			? tr::lng_exd_port_error_size(tr::now)
			: tr::lng_extera_plugins_bad_file(tr::now));
		return;
	}
	PwShowBox(controller, Box([=](not_null<Ui::GenericBox*> box) {
		AddPortHeader(box, controller, meta);
		AddColorPill(
			box,
			tr::lng_exd_plugin_ported_os(tr::now, lt_os, DesktopOsName(QString())),
			st::exteraInstallTranslate,
			[] { return st::windowActiveTextFg->c; });

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_exd_port_setup_plan(),
				st::exteraThreatSectionTitle),
			st::exteraThreatSectionPadding);
		const auto plan = AddCard(box);
		Ui::AddSkip(plan, 6);
		const auto hints = prepared.value(u"hints"_q).toArray();
		const auto sections = prepared.value(u"sections"_q).toArray();
		auto names = QStringList();
		for (const auto &value : sections) {
			names.push_back(value.toString());
		}
		const auto size = prepared.value(u"system"_q).toString().size()
			+ prepared.value(u"user"_q).toString().size();
		auto lines = QStringList{
			tr::lng_exd_port_step_prepare_info(
				tr::now,
				lt_hints,
				QString::number(hints.size()),
				lt_sections,
				names.isEmpty() ? u"0"_q : names.join(u", "_q)),
			tr::lng_exd_port_setup_size(
				tr::now,
				lt_value,
				QString::number(std::max(1, int(size / 4000)))),
		};
		if (prepared.value(u"library"_q).toBool()) {
			lines.push_back(tr::lng_exd_port_setup_library(tr::now));
		}
		for (const auto &hint : hints) {
			lines.push_back(QString::fromUtf8("\xE2\x80\xA2 ") + hint.toString());
		}
		AddPortStep(
			plan,
			tr::lng_exd_port_setup_request(tr::now),
			QString(),
			st::windowActiveTextFg->c,
			lines);
		Ui::AddSkip(plan, 8);

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_exd_port_setup_service(),
				st::exteraThreatSectionTitle),
			st::exteraThreatSectionPadding);
		const auto service = AddCard(box);
		const auto provider = std::make_shared<rpl::variable<int>>(
			std::max(Int(u"port_llm_provider"_q), 0));
		const auto model = std::make_shared<rpl::variable<QString>>(
			String(u"port_llm_model"_q));
		const auto key = std::make_shared<rpl::variable<QString>>(
			String(u"port_llm_key"_q));
		const auto agent = std::make_shared<rpl::variable<bool>>(
			Int(u"port_llm_mode"_q) == 0);
		auto options = std::vector<QString>();
		for (auto i = 1; i < Exd::PluginPorter::ProvidersCount(); ++i) {
			options.push_back(PortServiceName(i));
		}
		const auto serviceButton = Settings::AddButtonWithLabel(
			service,
			tr::lng_exd_port_setup_provider(),
			provider->value() | rpl::map([](int value) {
				return value ? PortServiceName(value) : tr::lng_exd_port_provider_none(tr::now);
			}),
			st::exdCardSettingsButton);
		serviceButton->setClickedCallback([=] {
			PwShowBox(controller, Box([=](not_null<Ui::GenericBox*> inner) {
				SingleChoiceBox(inner, {
					.title = tr::lng_exd_port_setup_provider(),
					.options = options,
					.initialSelection = std::max(provider->current() - 1, 0),
					.callback = [=](int value) {
						if (provider->current() != value + 1) {
							*provider = value + 1;
							*model = QString();
						}
					},
				});
			}));
		});
		const auto modelButton = Settings::AddButtonWithLabel(
			service,
			tr::lng_exd_port_setup_model(),
			rpl::combine(
				provider->value(),
				model->value()
			) | rpl::map([](int value, const QString &name) {
				const auto fallback = Exd::PluginPorter::DefaultModel(value);
				return !name.isEmpty()
					? name
					: !fallback.isEmpty()
					? fallback
					: tr::lng_exd_port_setup_model_none(tr::now);
			}),
			st::exdCardSettingsButton);
		modelButton->setClickedCallback([=] {
			const auto current = model->current().isEmpty()
				? Exd::PluginPorter::DefaultModel(provider->current())
				: model->current();
			ShowInput(
				controller,
				tr::lng_exd_port_setup_model(tr::now),
				current,
				false,
				[=](QString value) { *model = value; });
		});
		const auto keyButton = Settings::AddButtonWithLabel(
			service,
			tr::lng_exd_port_setup_key(),
			rpl::combine(
				provider->value(),
				key->value()
			) | rpl::map([](int value, const QString &text) {
				return (value > 0 && Exd::PluginPorter::ProviderLocal(value))
					? tr::lng_exd_port_setup_key_local(tr::now)
					: text.isEmpty()
					? tr::lng_exd_port_setup_key_empty(tr::now)
					: tr::lng_exd_port_setup_key_set(tr::now);
			}),
			st::exdCardSettingsButton);
		keyButton->setClickedCallback([=] {
			ShowInput(
				controller,
				tr::lng_exd_port_setup_key(tr::now),
				key->current(),
				false,
				[=](QString value) { *key = value; });
		});
		const auto modeButton = service->add(object_ptr<Ui::SettingsButton>(
			service,
			tr::lng_exd_port_setup_agent(),
			st::exdCardSettingsButton));
		modeButton->toggleOn(rpl::single(agent->current()));
		modeButton->toggledChanges(
		) | rpl::on_next([=](bool value) {
			*agent = value;
		}, modeButton->lifetime());
		Ui::AddSkip(service, 8);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_exd_port_setup_about(),
				st::exteraInstallVersion),
			st::exteraInstallVersionPadding,
			style::al_top);

		AddPortButton(
			box,
			tr::lng_exd_port_setup_start(),
			true,
			false
		)->setClickedCallback([=] {
			auto &config = Config::Instance();
			config.setInt(u"port_llm_provider"_q, provider->current());
			config.setString(u"port_llm_model"_q, model->current().trimmed());
			config.setString(u"port_llm_key"_q, key->current().trimmed());
			config.setInt(u"port_llm_mode"_q, agent->current() ? 0 : 1);
			if (!Exd::PluginPorter::Configured()) {
				controller->showToast(tr::lng_exd_port_not_configured(tr::now));
				return;
			}
			const auto window = controller;
			const auto target = path;
			box->closeBox();
			crl::on_main(window, [=] {
				ShowPortBox(window, target);
			});
		});
		AddPortButton(
			box,
			tr::lng_cancel(),
			false,
			true
		)->setClickedCallback([=] {
			box->closeBox();
		});
	}));
}

void ShowPermissionAlert(
		not_null<Window::SessionController*> controller,
		const QJsonObject &data,
		Fn<void(QJsonValue)> answer) {
	const auto extra = data.value(u"exd"_q).toObject();
	const auto kind = extra.value(u"kind"_q).toString();
	const auto net = (kind == u"net"_q) || (kind == u"udp"_q);
	const auto leak = (kind == u"leak"_q);
	const auto process = (kind == u"process"_q);
	const auto system = process || (kind == u"native"_q);
	const auto plugin = extra.value(u"plugin"_q).toString();
	const auto target = extra.value(u"target"_q).toString();
	const auto leakCategory = target.section('|', 0, 0);
	const auto leakTarget = target.section('|', 1);
	const auto message = leak
		? tr::lng_exd_perm_leak_text(tr::now, lt_target, leakTarget)
		: data.value(u"message"_q).toString();
	auto meta = QJsonObject();
	for (const auto &entry : Call(u"list"_q).toArray()) {
		const auto info = entry.toObject();
		if (info.value(u"id"_q).toString() == plugin) {
			meta = info;
			break;
		}
	}
	if (meta.value(u"name"_q).toString().isEmpty()) {
		meta.insert(u"name"_q, plugin);
	}
	auto ordered = std::vector<std::pair<int, QString>>();
	const auto buttons = data.value(u"buttons"_q).toArray();
	for (const auto which : { -1, -3, -4, -2 }) {
		for (const auto &value : buttons) {
			const auto entry = value.toObject();
			if (entry.value(u"which"_q).toInt() == which) {
				ordered.emplace_back(which, entry.value(u"text"_q).toString());
			}
		}
	}
	PwShowBox(controller, Box([=](not_null<Ui::GenericBox*> box) {
		AddPortHeader(box, controller, meta);
		const auto color = [=] {
			return (leak || system)
				? st::attentionButtonFg->c
				: net
				? st::windowActiveTextFg->c
				: st::historyPeer3UserpicBg->c;
		};
		AddColorPill(
			box,
			leak
				? tr::lng_exd_perm_request_leak(tr::now)
				: process
				? tr::lng_exd_perm_request_process(tr::now)
				: system
				? tr::lng_exd_perm_request_native(tr::now)
				: (kind == u"udp"_q)
				? tr::lng_exd_perm_request_udp(tr::now)
				: net
				? tr::lng_exd_perm_request_lan(tr::now)
				: tr::lng_exd_perm_request_files(tr::now),
			st::exteraInstallPuzzle,
			color);
		const auto card = AddCard(box);
		Ui::AddSkip(card, 6);
		AddPortStep(
			card,
			leak
				? LeakCategoryTitle(leakCategory)
				: process
				? tr::lng_exd_plugin_perm_process(tr::now)
				: system
				? tr::lng_exd_plugin_perm_native(tr::now)
				: net
				? tr::lng_exd_perm_device(tr::now)
				: tr::lng_exd_perm_folder(tr::now),
			leak
				? tr::lng_exd_perm_leak_chip(tr::now)
				: tr::lng_exd_perm_chip(tr::now),
			color(),
			system
				? QStringList{ message }
				: QStringList{
				leak
					? leakTarget
					: net
					? (target.startsWith(u"udp:"_q) ? target.mid(4) : target)
					: QDir::toNativeSeparators(target),
				message,
			});
		Ui::AddSkip(card, 8);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_exd_perm_request_about(),
				st::exteraInstallVersion),
			st::exteraInstallVersionPadding,
			style::al_top);
		for (auto i = 0; i != int(ordered.size()); ++i) {
			const auto which = ordered[i].first;
			AddPortButton(
				box,
				rpl::single(ordered[i].second),
				(i == 0),
				(i + 1 == int(ordered.size()))
			)->setClickedCallback([=] {
				answer(which);
				box->closeBox();
			});
		}
		box->boxClosing() | rpl::on_next([=] {
			answer(u"dismiss"_q);
		}, box->lifetime());
	}));
}

namespace {

// Window

enum class PwSection {
	All,
	Enabled,
	Disabled,
	Errors,
	Pinned,
};

enum class PwSource {
	None,
	Trusted,
	Ported,
	Other,
};

enum class PwSort {
	Pinned,
	Name,
	Enabled,
	Date,
};

struct PwEntry {
	QJsonObject info;
	QString id;
	QString name;
	QString meta;
	QString description;
	QString error;
	QString path;
	bool enabled = false;
	bool trusted = false;
	bool desktop = false;
	bool ported = false;
	bool partial = false;
	bool settings = false;
	bool pinned = false;
	int pinIndex = -1;
	QDateTime installed;
	qint64 size = 0;
};

struct PwState {
	std::vector<PwEntry> entries;
	PwSection section = PwSection::All;
	PwSource source = PwSource::None;
	PwSort sort = PwSort::Pinned;
	bool grid = true;
	bool chipDesktop = false;
	bool chipPartial = false;
	bool chipSettings = false;
	QString query;
	QString selected;
	int page = 0;
	bool engineOpen = false;
	rpl::event_stream<> changes;
};

[[nodiscard]] QColor PwMix(QColor a, QColor b, float64 ratio) {
	return anim::color(a, b, ratio);
}

[[nodiscard]] QColor PwSurface(float64 level = 0.) {
	return PwMix(st::windowBg->c, st::windowFg->c, 0.05 + level);
}

[[nodiscard]] int Px(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] std::vector<PwEntry> PwLoad() {
	auto result = std::vector<PwEntry>();
	const auto pinned = PinnedPlugins();
	for (const auto &value : Call(u"list"_q).toArray()) {
		const auto info = value.toObject();
		auto entry = PwEntry();
		entry.info = info;
		entry.id = info.value(u"id"_q).toString();
		entry.name = info.value(u"name"_q).toString();
		entry.meta = Subtitle(info);
		entry.description = info.value(u"description"_q).toString();
		entry.error = info.value(u"error"_q).toString().trimmed();
		entry.path = info.value(u"path"_q).toString();
		entry.enabled = info.value(u"enabled"_q).toBool()
			&& !Extera::Bool(u"plugins_safe_mode"_q);
		entry.trusted = info.value(u"trusted"_q).toBool();
		entry.desktop = info.value(u"desktop"_q).toBool();
		entry.ported = !info.value(u"ported_from"_q).toString().isEmpty();
		entry.partial = !info.value(u"unsupported"_q).toArray().isEmpty();
		entry.settings = info.value(u"has_settings"_q).toBool();
		entry.pinIndex = pinned.indexOf(entry.id);
		entry.pinned = (entry.pinIndex >= 0);
		const auto file = QFileInfo(entry.path);
		entry.installed = file.lastModified();
		entry.size = file.size();
		result.push_back(std::move(entry));
	}
	return result;
}

[[nodiscard]] bool PwMatchSection(const PwEntry &entry, PwSection section) {
	switch (section) {
	case PwSection::All: return true;
	case PwSection::Enabled: return entry.enabled;
	case PwSection::Disabled: return !entry.enabled;
	case PwSection::Errors: return !entry.error.isEmpty();
	case PwSection::Pinned: return entry.pinned;
	}
	return true;
}

[[nodiscard]] bool PwMatchSource(const PwEntry &entry, PwSource source) {
	switch (source) {
	case PwSource::None: return true;
	case PwSource::Trusted: return entry.trusted;
	case PwSource::Ported: return entry.ported;
	case PwSource::Other: return !entry.trusted;
	}
	return true;
}

[[nodiscard]] std::vector<const PwEntry*> PwFiltered(
		const PwState &state) {
	auto result = std::vector<const PwEntry*>();
	const auto query = state.query.trimmed().toLower();
	for (const auto &entry : state.entries) {
		if (!PwMatchSection(entry, state.section)
			|| !PwMatchSource(entry, state.source)
			|| (state.chipDesktop && !entry.desktop)
			|| (state.chipPartial && !entry.partial)
			|| (state.chipSettings && !entry.settings)) {
			continue;
		}
		if (!query.isEmpty()) {
			const auto haystack = (entry.name
				+ ' '
				+ entry.meta
				+ ' '
				+ entry.description).toLower();
			if (!haystack.contains(query)) {
				continue;
			}
		}
		result.push_back(&entry);
	}
	const auto byName = [](const PwEntry *a, const PwEntry *b) {
		return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
	};
	switch (state.sort) {
	case PwSort::Pinned:
		ranges::stable_sort(result, [&](const PwEntry *a, const PwEntry *b) {
			if (a->pinned != b->pinned) {
				return a->pinned;
			} else if (a->pinned) {
				return a->pinIndex < b->pinIndex;
			} else if (a->error.isEmpty() != b->error.isEmpty()) {
				return !a->error.isEmpty();
			}
			return byName(a, b);
		});
		break;
	case PwSort::Name:
		ranges::stable_sort(result, byName);
		break;
	case PwSort::Enabled:
		ranges::stable_sort(result, [&](const PwEntry *a, const PwEntry *b) {
			if (a->enabled != b->enabled) {
				return a->enabled;
			}
			return byName(a, b);
		});
		break;
	case PwSort::Date:
		ranges::stable_sort(result, [&](const PwEntry *a, const PwEntry *b) {
			return a->installed > b->installed;
		});
		break;
	}
	return result;
}

struct PwTag {
	QString text;
	QColor fg;
	QColor bg;
};

[[nodiscard]] std::vector<PwTag> PwTags(const PwEntry &entry) {
	auto result = std::vector<PwTag>();
	const auto sub = st::windowSubTextFg->c;
	const auto neutral = PwSurface(0.06);
	if (!entry.error.isEmpty()) {
		auto bg = st::attentionButtonFg->c;
		bg.setAlphaF(0.16);
		result.push_back({
			tr::lng_exd_pw_tag_error(tr::now),
			st::attentionButtonFg->c,
			bg,
		});
	}
	if (entry.desktop) {
		auto fg = QColor(0x5f, 0xbf, 0x7f);
		auto bg = fg;
		bg.setAlphaF(0.16);
		result.push_back({ tr::lng_exd_pw_tag_desktop(tr::now), fg, bg });
	}
	if (entry.ported) {
		result.push_back({ tr::lng_exd_pw_tag_ported(tr::now), sub, neutral });
	}
	if (entry.partial) {
		auto fg = QColor(0xe0, 0xa8, 0x4a);
		auto bg = fg;
		bg.setAlphaF(0.16);
		result.push_back({ tr::lng_exd_pw_tag_partial(tr::now), fg, bg });
	}
	return result;
}

int PwPaintTags(
		QPainter &p,
		const std::vector<PwTag> &tags,
		int left,
		int top,
		int right) {
	const auto &font = st::normalFont;
	const auto height = font->height + Px(4);
	const auto padding = Px(8);
	auto x = left;
	for (const auto &tag : tags) {
		const auto width = font->width(tag.text) + padding * 2;
		if (x + width > right) {
			break;
		}
		p.setPen(Qt::NoPen);
		p.setBrush(tag.bg);
		p.drawRoundedRect(
			QRect(x, top, width, height),
			height / 2.,
			height / 2.);
		p.setPen(tag.fg);
		p.setFont(font);
		p.drawText(
			QRect(x, top, width, height),
			Qt::AlignCenter,
			tag.text);
		x += width + Px(6);
	}
	return height;
}

class PwToggle final : public Ui::AbstractButton {
public:
	PwToggle(QWidget *parent, bool checked)
	: AbstractButton(parent)
	, _view(st::defaultToggle, checked, [=] { update(); }) {
		const auto size = _view.getSize();
		resize(size.width() + Px(8), size.height() + Px(8));
		setClickedCallback([=] {
			_view.setChecked(!_view.checked(), anim::type::normal);
			_changes.fire_copy(_view.checked());
		});
	}

	[[nodiscard]] rpl::producer<bool> changes() const {
		return _changes.events();
	}

private:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		_view.paint(p, Px(4), Px(4), width());
	}

	Ui::ToggleView _view;
	rpl::event_stream<bool> _changes;

};

class PwNavButton final : public Ui::AbstractButton {
public:
	PwNavButton(
		QWidget *parent,
		const style::icon &icon,
		QString text,
		int count,
		Fn<bool()> active)
	: AbstractButton(parent)
	, _icon(icon)
	, _text(std::move(text))
	, _count(count)
	, _active(std::move(active)) {
		resize(width(), Px(38));
	}

	void setCount(int count) {
		_count = count;
		update();
	}

private:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto active = _active();
		const auto rect = this->rect();
		if (active || isOver()) {
			auto bg = active
				? st::windowActiveTextFg->c
				: PwSurface(0.05);
			if (active) {
				bg.setAlphaF(0.16);
			}
			p.setPen(Qt::NoPen);
			p.setBrush(bg);
			p.drawRoundedRect(rect, rect.height() / 2., rect.height() / 2.);
		}
		const auto fg = active ? st::windowActiveTextFg->c : st::windowFg->c;
		_icon.paint(
			p,
			Px(12),
			(height() - _icon.height()) / 2,
			width(),
			active ? fg : st::menuIconColor->c);
		p.setPen(fg);
		p.setFont(st::normalFont);
		const auto textLeft = Px(12) + _icon.width() + Px(10);
		const auto count = (_count >= 0) ? QString::number(_count) : QString();
		const auto countWidth = st::normalFont->width(count);
		p.drawText(
			QRect(
				textLeft,
				0,
				width() - textLeft - countWidth - Px(24),
				height()),
			Qt::AlignVCenter | Qt::AlignLeft,
			st::normalFont->elided(
				_text,
				width() - textLeft - countWidth - Px(24)));
		p.setPen(active ? fg : st::windowSubTextFg->c);
		p.drawText(
			QRect(0, 0, width() - Px(14), height()),
			Qt::AlignVCenter | Qt::AlignRight,
			count);
	}

	const style::icon &_icon;
	QString _text;
	int _count = 0;
	Fn<bool()> _active;

};

class PwChip final : public Ui::AbstractButton {
public:
	PwChip(QWidget *parent, QString text, Fn<bool()> active)
	: AbstractButton(parent)
	, _text(std::move(text))
	, _active(std::move(active)) {
		resize(
			st::normalFont->width(_text) + Px(24),
			st::normalFont->height + Px(12));
	}

private:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto active = _active();
		const auto rect = QRectF(this->rect()).marginsRemoved(
			{ 0.5, 0.5, 0.5, 0.5 });
		if (active) {
			p.setPen(Qt::NoPen);
			p.setBrush(PwSurface(0.1));
		} else {
			p.setPen(QPen(PwSurface(0.12), 1.));
			p.setBrush(isOver() ? PwSurface(0.02) : Qt::transparent);
		}
		p.drawRoundedRect(rect, rect.height() / 2., rect.height() / 2.);
		p.setPen(active ? st::windowFg->c : st::windowSubTextFg->c);
		p.setFont(st::normalFont);
		p.drawText(this->rect(), Qt::AlignCenter, _text);
	}

	QString _text;
	Fn<bool()> _active;

};

class PwPillButton final : public Ui::AbstractButton {
public:
	PwPillButton(
		QWidget *parent,
		rpl::producer<QString> text,
		const style::icon *icon,
		bool primary)
	: AbstractButton(parent)
	, _icon(icon)
	, _primary(primary) {
		std::move(text) | rpl::on_next([=](QString value) {
			_text = std::move(value);
			const auto iconWidth = _icon ? (_icon->width() + Px(6)) : 0;
			resize(
				st::semiboldFont->width(_text) + iconWidth + Px(32),
				Px(38));
			update();
		}, lifetime());
	}

private:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		auto bg = _primary ? st::windowActiveTextFg->c : PwSurface(0.08);
		if (isOver()) {
			bg = _primary
				? PwMix(bg, st::windowFg->c, 0.1)
				: PwSurface(0.12);
		}
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		p.drawRoundedRect(rect(), height() / 2., height() / 2.);
		const auto fg = _primary ? st::windowBg->c : st::windowFg->c;
		const auto iconWidth = _icon ? (_icon->width() + Px(6)) : 0;
		const auto textWidth = st::semiboldFont->width(_text);
		auto x = (width() - textWidth - iconWidth) / 2;
		if (_icon) {
			_icon->paint(
				p,
				x,
				(height() - _icon->height()) / 2,
				width(),
				fg);
			x += iconWidth;
		}
		p.setPen(fg);
		p.setFont(st::semiboldFont);
		p.drawText(
			QRect(x, 0, textWidth + 1, height()),
			Qt::AlignVCenter | Qt::AlignLeft,
			_text);
	}

	QString _text;
	const style::icon *_icon = nullptr;
	bool _primary = false;

};

class PwIconButton final : public Ui::AbstractButton {
public:
	PwIconButton(QWidget *parent, const style::icon &icon, int size = 34)
	: AbstractButton(parent)
	, _icon(&icon) {
		resize(Px(size), Px(size));
	}

	void setIcon(const style::icon &icon) {
		_icon = &icon;
		update();
	}

	void setActiveColor(std::optional<QColor> color) {
		_color = color;
		update();
	}

private:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		if (isOver()) {
			p.setPen(Qt::NoPen);
			p.setBrush(PwSurface(0.1));
			p.drawEllipse(rect());
		}
		const auto color = _color.value_or(st::menuIconColor->c);
		_icon->paint(
			p,
			(width() - _icon->width()) / 2,
			(height() - _icon->height()) / 2,
			width(),
			color);
	}

	const style::icon *_icon = nullptr;
	std::optional<QColor> _color;

};

class PwViewButton final : public Ui::AbstractButton {
public:
	PwViewButton(QWidget *parent, bool grid)
	: AbstractButton(parent)
	, _grid(grid) {
		resize(Px(38), Px(38));
	}

	void setActiveColor(std::optional<QColor> color) {
		_color = color;
		update();
	}

private:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		if (_color) {
			p.setPen(Qt::NoPen);
			p.setBrush(PwSurface(0.14));
			p.drawRoundedRect(rect(), height() / 2., height() / 2.);
		}
		const auto color = _color
			? st::windowFg->c
			: isOver()
			? st::windowFg->c
			: st::windowSubTextFg->c;
		const auto size = Px(16);
		const auto left = (width() - size) / 2.;
		const auto top = (height() - size) / 2.;
		if (_grid) {
			p.setPen(Qt::NoPen);
			p.setBrush(color);
			const auto cell = (size - Px(2)) / 2.;
			for (auto i = 0; i != 4; ++i) {
				p.drawRoundedRect(
					QRectF(
						left + (i % 2) * (cell + Px(2)),
						top + (i / 2) * (cell + Px(2)),
						cell,
						cell),
					Px(2),
					Px(2));
			}
		} else {
			auto pen = QPen(color, Px(2));
			pen.setCapStyle(Qt::RoundCap);
			p.setPen(pen);
			for (auto i = 0; i != 3; ++i) {
				const auto y = top + Px(2) + i * (size - Px(4)) / 2.;
				p.drawLine(QPointF(left + Px(1), y), QPointF(left + size - Px(1), y));
			}
		}
	}

	const bool _grid = true;
	std::optional<QColor> _color;

};

class PwCard final : public Ui::AbstractButton {
public:
	PwCard(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		const PwEntry &entry,
		bool grid,
		bool selected,
		Fn<void(QString)> select,
		Fn<void(QString, bool)> setEnabled,
		Fn<void(QString)> togglePin,
		Fn<void(QWidget*, QString)> showMenu,
		Fn<void(QString)> openSettings)
	: AbstractButton(parent)
	, _id(entry.id)
	, _grid(grid)
	, _selected(selected)
	, _enabled(entry.enabled)
	, _name(entry.name)
	, _meta(entry.meta)
	, _error(entry.error.split('\n').last())
	, _tags(PwTags(entry)) {
		_description.setMarkedText(
			st::defaultTextStyle,
			WithLinks(entry.description),
			Ui::ItemTextDefaultOptions());
		const auto iconSize = _grid ? Px(44) : Px(36);
		_iconSize = iconSize;
		_icon = Ui::CreateChild<Ui::AbstractButton>(this);
		_icon->setAttribute(Qt::WA_TransparentForMouseEvents);
		SetupPluginIcon(
			_icon,
			controller,
			entry.info.value(u"icon"_q).toString(),
			iconSize,
			entry.trusted ? (iconSize * 4 / 10) : 0,
			[=] { return isOver() ? PwSurface(0.08) : PwSurface(0.04); });
		_toggle = Ui::CreateChild<PwToggle>(this, entry.enabled);
		_toggle->changes(
		) | rpl::on_next([=](bool value) {
			setEnabled(_id, value);
		}, _toggle->lifetime());
		if (_grid) {
			_settings = Ui::CreateChild<PwIconButton>(
				this,
				st::menuIconSettings);
			_settings->setClickedCallback([=] {
				openSettings(_id);
			});
			_pin = Ui::CreateChild<PwIconButton>(
				this,
				entry.pinned ? st::menuIconUnpin : st::menuIconPin);
			if (entry.pinned) {
				_pin->setActiveColor(st::windowActiveTextFg->c);
			}
			_pin->setClickedCallback([=] { togglePin(_id); });
			_more = Ui::CreateChild<PwIconButton>(this, st::exteraIconMore);
			_more->setClickedCallback([=] { showMenu(_more, _id); });
		}
		setClickedCallback([=] { select(_id); });
		setAcceptBoth(true);
		clicks(
		) | rpl::filter([](Qt::MouseButton button) {
			return (button == Qt::RightButton);
		}) | rpl::on_next([=] {
			showMenu(this, _id);
		}, lifetime());
		resize(width(), _grid ? Px(180) : Px(58));
	}

	void layout() {
		const auto padding = _grid ? Px(14) : Px(12);
		const auto iconSize = _iconSize;
		if (_grid) {
			_icon->moveToLeft(padding, padding);
			_toggle->moveToLeft(
				width() - padding - _toggle->width() + Px(4),
				padding + (iconSize - _toggle->height()) / 2);
			const auto bottom = height() - padding + Px(4) - _settings->height();
			_settings->moveToLeft(padding - Px(6), bottom);
			_pin->moveToLeft(padding - Px(6) + _settings->width(), bottom);
			_more->moveToLeft(
				width() - padding + Px(6) - _more->width(),
				bottom);
		} else {
			_icon->moveToLeft(padding, (height() - iconSize) / 2);
			_toggle->moveToLeft(
				width() - padding - _toggle->width() + Px(4),
				(height() - _toggle->height()) / 2);
		}
	}

protected:
	void resizeEvent(QResizeEvent *e) override {
		layout();
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = _grid ? Px(18) : Px(14);
		p.setPen(Qt::NoPen);
		p.setBrush(isOver() ? PwSurface(0.08) : PwSurface(0.04));
		p.drawRoundedRect(rect(), radius, radius);
		if (_selected) {
			p.setPen(QPen(st::windowActiveTextFg->c, Px(2)));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(
				QRectF(rect()).marginsRemoved({ 1., 1., 1., 1. }),
				radius,
				radius);
		}
		const auto padding = _grid ? Px(14) : Px(12);
		const auto iconSize = _iconSize;
		const auto textLeft = padding + iconSize + Px(12);
		const auto textRight = _toggle->x() - Px(8);
		const auto nameWidth = std::max(textRight - textLeft, 0);
		if (!_enabled) {
			p.setOpacity(0.75);
		}
		const auto nameTop = _grid
			? (padding + (iconSize - st::semiboldFont->height - st::normalFont->height) / 2)
			: ((height() - st::semiboldFont->height - st::normalFont->height) / 2);
		if (_grid) {
			p.setPen(st::windowFg);
			p.setFont(st::semiboldFont);
			p.drawText(
				textLeft,
				nameTop + st::semiboldFont->ascent,
				st::semiboldFont->elided(_name, nameWidth));
			p.setPen(st::windowSubTextFg);
			p.setFont(st::normalFont);
			p.drawText(
				textLeft,
				nameTop + st::semiboldFont->height + st::normalFont->ascent,
				st::normalFont->elided(_meta, nameWidth));
			const auto descTop = padding + iconSize + Px(10);
			const auto descWidth = width() - padding * 2;
			if (!_error.isEmpty()) {
				p.setPen(st::attentionButtonFg);
				p.setFont(st::normalFont);
				p.drawText(
					QRect(padding, descTop, descWidth, st::normalFont->height * 2),
					Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
					_error);
			} else {
				p.setPen(st::windowFg);
				_description.draw(p, {
					.position = { padding, descTop },
					.availableWidth = descWidth,
					.palette = &st::defaultTextPalette,
					.now = crl::now(),
					.elisionLines = _tags.empty() ? 3 : 2,
				});
			}
			PwPaintTags(
				p,
				_tags,
				padding,
				descTop + st::defaultTextStyle.font->height * 2 + Px(10),
				width() - padding);
		} else {
			const auto nameColumn = std::min(nameWidth, Px(220));
			p.setPen(st::windowFg);
			p.setFont(st::semiboldFont);
			p.drawText(
				textLeft,
				nameTop + st::semiboldFont->ascent,
				st::semiboldFont->elided(_name, nameColumn));
			p.setPen(st::windowSubTextFg);
			p.setFont(st::normalFont);
			p.drawText(
				textLeft,
				nameTop + st::semiboldFont->height + st::normalFont->ascent,
				st::normalFont->elided(_meta, nameColumn));
			const auto descLeft = textLeft + nameColumn + Px(16);
			auto tagsWidth = 0;
			for (const auto &tag : _tags) {
				tagsWidth += st::normalFont->width(tag.text) + Px(22);
			}
			const auto descRight = textRight - tagsWidth;
			if (descRight - descLeft > Px(60)) {
				const auto line = _error.isEmpty()
					? _description.toString()
					: _error;
				p.setPen(_error.isEmpty()
					? st::windowSubTextFg
					: st::attentionButtonFg);
				p.setFont(st::normalFont);
				p.drawText(
					QRect(descLeft, 0, descRight - descLeft, height()),
					Qt::AlignVCenter | Qt::AlignLeft,
					st::normalFont->elided(
						line.simplified(),
						descRight - descLeft));
			}
			PwPaintTags(
				p,
				_tags,
				std::max(descRight, descLeft),
				(height() - st::normalFont->height - Px(4)) / 2,
				textRight);
		}
		p.setOpacity(1.);
	}

private:
	const QString _id;
	const bool _grid = true;
	const bool _selected = false;
	const bool _enabled = false;
	QString _name;
	QString _meta;
	QString _error;
	std::vector<PwTag> _tags;
	Ui::Text::String _description;
	Ui::AbstractButton *_icon = nullptr;
	int _iconSize = 0;
	PwToggle *_toggle = nullptr;
	PwIconButton *_settings = nullptr;
	PwIconButton *_pin = nullptr;
	PwIconButton *_more = nullptr;

};

class PwGrid final : public Ui::RpWidget {
public:
	PwGrid(QWidget *parent, bool grid)
	: RpWidget(parent)
	, _grid(grid) {
	}

	void add(not_null<PwCard*> card) {
		_cards.push_back(card);
	}

protected:
	int resizeGetHeight(int newWidth) override {
		if (_cards.empty()) {
			return 0;
		}
		const auto gap = _grid ? Px(10) : Px(3);
		const auto columns = _grid
			? std::max(1, (newWidth + gap) / (Px(250) + gap))
			: 1;
		const auto cardWidth = (newWidth - gap * (columns - 1)) / columns;
		auto top = 0;
		auto rowHeight = 0;
		for (auto i = 0; i != int(_cards.size()); ++i) {
			const auto column = i % columns;
			if (column == 0 && i > 0) {
				top += rowHeight + gap;
			}
			const auto card = _cards[i];
			card->resize(cardWidth, card->height());
			card->moveToLeft(column * (cardWidth + gap), top);
			rowHeight = card->height();
		}
		return top + rowHeight;
	}

private:
	const bool _grid = true;
	std::vector<not_null<PwCard*>> _cards;

};

[[nodiscard]] QString PwSize(qint64 size) {
	if (size < 1024 * 1024) {
		return QString::number(std::max<qint64>(size / 1024, 1)) + u" KB"_q;
	}
	return QString::number(size / (1024. * 1024.), 'f', 1) + u" MB"_q;
}

} // namespace

void ShowPluginsWindow(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto computeSize = [=] {
			const auto outer = controller->widget()->size();
			const auto width = std::max(
				std::min({
					outer.width() * 92 / 100,
					outer.width() - Px(24),
					Px(1180) }),
				Px(320));
			const auto height = std::max(
				std::min(
					std::max(width * 9 / 16, Px(520)),
					outer.height() * 90 / 100),
				Px(300));
			return QSize(width, height);
		};
		const auto width = computeSize().width();
		const auto height = computeSize().height();
		box->setStyle(st::exteraInstallBox);
		box->setWidth(width);
		box->setNoContentMargin(true);
		box->setMaxHeight(height);

		const auto state = box->lifetime().make_state<PwState>();
		state->entries = PwLoad();
		state->sort = PwSort(std::clamp(
			Extera::Int(u"plugins_window_sort"_q),
			0,
			3));
		state->grid = !Extera::Bool(u"plugins_compact_view"_q);

		const auto root = box->addRow(
			object_ptr<Ui::RpWidget>(box),
			style::margins());
		root->resize(width, height);
		controller->widget()->sizeValue(
		) | rpl::skip(1) | rpl::on_next([=] {
			const auto size = computeSize();
			box->setWidth(size.width());
			box->setMaxHeight(size.height());
			root->resize(size);
		}, root->lifetime());
		const auto layers = box->lifetime().make_state<Ui::LayerManager>(
			root);
		layers->setHideByBackgroundClick(true);
		PwShowOverride = layers->uiShow();
		box->lifetime().add([] { PwShowOverride.reset(); });

		const auto detailWidthValue = box->lifetime().make_state<int>(Px(330));

		// Side
		const auto side = Ui::CreateChild<Ui::RpWidget>(root);
		side->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(side);
			p.fillRect(side->rect(), PwSurface(0.02));
		}, side->lifetime());
		const auto sideScroll = Ui::CreateChild<Ui::ScrollArea>(
			side,
			st::defaultSolidScroll);
		const auto sideInner = sideScroll->setOwnedWidget(
			object_ptr<Ui::VerticalLayout>(sideScroll));

		const auto title = sideInner->add(
			object_ptr<Ui::FlatLabel>(
				sideInner,
				tr::lng_extera_plugins(),
				st::exdUpdateCardTitle),
			style::margins(Px(20), Px(22), Px(20), Px(14)));
		title->setTextColorOverride(st::windowFg->c);

		const auto engine = sideInner->add(
			object_ptr<Ui::RpWidget>(sideInner),
			style::margins(Px(12), 0, Px(12), Px(12)));
		engine->resize(engine->width(), Px(60));
		const auto engineOn = Extera::Bool(u"plugins_enabled"_q);
		const auto engineToggle = Ui::CreateChild<PwToggle>(engine, engineOn);
		engine->widthValue() | rpl::on_next([=](int w) {
			engineToggle->moveToLeft(
				w - engineToggle->width() - Px(10),
				(engine->height() - engineToggle->height()) / 2);
		}, engine->lifetime());
		engine->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(engine);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(PwSurface(0.07));
			p.drawRoundedRect(engine->rect(), Px(18), Px(18));
			const auto status = !Extera::Plugins::Available()
				? tr::lng_exd_pw_engine_missing(tr::now)
				: (Extera::Bool(u"plugins_enabled"_q)
					&& Extera::Plugins::Started())
				? tr::lng_exd_pw_engine_on(tr::now)
				: tr::lng_exd_pw_engine_off(tr::now);
			const auto textWidth = engineToggle->x() - Px(22);
			p.setPen(st::windowFg);
			p.setFont(st::semiboldFont);
			p.drawText(
				Px(14),
				Px(10) + st::semiboldFont->ascent,
				st::semiboldFont->elided(
					tr::lng_exd_pw_engine(tr::now),
					textWidth));
			p.setPen(st::windowSubTextFg);
			p.setFont(st::normalFont);
			p.drawText(
				Px(14),
				Px(12) + st::semiboldFont->height + st::normalFont->ascent,
				st::normalFont->elided(status, textWidth));
		}, engine->lifetime());
		engineToggle->changes(
		) | rpl::on_next([=](bool value) {
			Extera::Config::Instance().setBool(u"plugins_enabled"_q, value);
			PwShowBox(controller, Ui::MakeConfirmBox({
				.text = tr::lng_extera_restart_required(),
				.confirmed = [] { Core::Restart(); },
				.confirmText = tr::lng_extera_restart(),
			}));
		}, engineToggle->lifetime());

		const auto engineSettings = sideInner->add(
			object_ptr<PwNavButton>(
				sideInner,
				st::menuIconSettings,
				tr::lng_exd_pw_engine_settings(tr::now),
				-1,
				[=] { return state->engineOpen; }),
			style::margins(Px(12), 0, Px(12), Px(10)));
		const auto enginePage = Ui::CreateChild<Ui::RpWidget>(root);
		enginePage->hide();
		enginePage->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(enginePage);
			p.fillRect(enginePage->rect(), st::boxBg);
		}, enginePage->lifetime());
		const auto engineHeader = Ui::CreateChild<Ui::RpWidget>(enginePage);
		engineHeader->resize(engineHeader->width(), Px(56));
		const auto engineBack = Ui::CreateChild<PwIconButton>(
			engineHeader,
			st::infoTopBarBackIcon,
			40);
		const auto engineTitle = Ui::CreateChild<Ui::FlatLabel>(
			engineHeader,
			tr::lng_exd_pw_engine_settings(),
			st::exdUpdateCardTitle);
		engineTitle->setTextColorOverride(st::windowFg->c);
		const auto engineScroll = Ui::CreateChild<Ui::ScrollArea>(
			enginePage,
			st::defaultSolidScroll);
		const auto engineInner = engineScroll->setOwnedWidget(
			object_ptr<Ui::VerticalLayout>(engineScroll));
		const auto engineFilled = box->lifetime().make_state<bool>(false);
		enginePage->sizeValue() | rpl::on_next([=](QSize size) {
			engineHeader->setGeometry(0, 0, size.width(), Px(56));
			engineBack->moveToLeft(Px(8), Px(8));
			engineTitle->resizeToWidth(std::max(size.width() - Px(64), 1));
			engineTitle->moveToLeft(
				Px(56),
				(Px(56) - engineTitle->height()) / 2);
			engineScroll->setGeometry(
				0,
				Px(56),
				size.width(),
				size.height() - Px(56));
			const auto inner = std::min(size.width() - Px(32), Px(1100));
			engineInner->resizeToWidth(inner);
			engineInner->moveToLeft((size.width() - inner) / 2, 0);
		}, enginePage->lifetime());
		const auto showEngine = [=](bool show) {
			if (show && !*engineFilled) {
				*engineFilled = true;
				::Settings::FillExteraPluginsEngine(engineInner, controller);
				for (const auto child : engineInner->findChildren<QWidget*>()) {
					if (!child->testAttribute(Qt::WA_WState_ExplicitShowHide)) {
						child->show();
					}
				}
				engineInner->resizeToWidth(engineInner->width());
			}
			state->engineOpen = show;
			state->changes.fire({});
		};
		engineBack->setClickedCallback([=] { showEngine(false); });
		engineSettings->setClickedCallback([=] { showEngine(true); });
		root->sizeValue() | rpl::on_next([=](QSize size) {
			const auto left = (size.width() < Px(640))
				? 0
				: (size.width() < Px(900))
				? Px(196)
				: Px(232);
			enginePage->setGeometry(
				left,
				0,
				size.width() - left,
				size.height());
		}, enginePage->lifetime());

		auto navButtons = std::vector<std::pair<
			not_null<PwNavButton*>,
			Fn<int()>>>();
		const auto addNav = [&](
				const style::icon &icon,
				QString text,
				Fn<int()> count,
				Fn<bool()> active,
				Fn<void()> click) {
			const auto button = sideInner->add(
				object_ptr<PwNavButton>(
					sideInner,
					icon,
					std::move(text),
					count(),
					std::move(active)),
				style::margins(Px(12), 0, Px(12), Px(2)));
			button->setClickedCallback(std::move(click));
			navButtons.push_back({ button, std::move(count) });
		};
		const auto countIf = [=](auto predicate) {
			return [=] {
				return int(ranges::count_if(state->entries, predicate));
			};
		};
		const auto addSection = [&](
				const style::icon &icon,
				QString text,
				PwSection section) {
			addNav(
				icon,
				std::move(text),
				countIf([=](const PwEntry &e) {
					return PwMatchSection(e, section);
				}),
				[=] {
					return !state->engineOpen
						&& (state->source == PwSource::None)
						&& (state->section == section);
				},
				[=] {
					state->engineOpen = false;
					state->section = section;
					state->source = PwSource::None;
					state->changes.fire({});
				});
		};
		const auto addSource = [&](
				const style::icon &icon,
				QString text,
				PwSource source) {
			addNav(
				icon,
				std::move(text),
				countIf([=](const PwEntry &e) {
					return PwMatchSource(e, source);
				}),
				[=] {
					return !state->engineOpen && (state->source == source);
				},
				[=] {
					state->source = (state->source == source)
						? PwSource::None
						: source;
					state->section = PwSection::All;
					state->engineOpen = false;
					state->changes.fire({});
				});
		};
		addSection(
			st::exteraIconPlugins,
			tr::lng_exd_pw_all(tr::now),
			PwSection::All);
		addSection(
			st::menuIconSelect,
			tr::lng_exd_pw_enabled(tr::now),
			PwSection::Enabled);
		addSection(
			st::menuIconBlock,
			tr::lng_exd_pw_disabled(tr::now),
			PwSection::Disabled);
		addSection(
			st::menuIconReport,
			tr::lng_exd_pw_errors(tr::now),
			PwSection::Errors);
		addSection(
			st::menuIconPin,
			tr::lng_exd_pw_pinned(tr::now),
			PwSection::Pinned);
		sideInner->add(
			object_ptr<Ui::FlatLabel>(
				sideInner,
				tr::lng_exd_pw_sources(),
				st::defaultSubsectionTitle),
			style::margins(Px(22), Px(14), Px(22), Px(6)));
		addSource(
			st::menuIconAntispam,
			tr::lng_exd_pw_trusted(tr::now),
			PwSource::Trusted);
		addSource(
			st::menuIconTranslate,
			tr::lng_exd_pw_ported(tr::now),
			PwSource::Ported);
		addSource(
			st::menuIconDownload,
			tr::lng_exd_pw_other(tr::now),
			PwSource::Other);
		const auto navList = box->lifetime().make_state<std::vector<
			std::pair<not_null<PwNavButton*>, Fn<int()>>>>(
				std::move(navButtons));

		// Main
		const auto main = Ui::CreateChild<Ui::RpWidget>(root);
		const auto toolbar = Ui::CreateChild<Ui::RpWidget>(main);
		toolbar->resize(toolbar->width(), Px(62));
		const auto searchWrap = Ui::CreateChild<Ui::RpWidget>(toolbar);
		searchWrap->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(searchWrap);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(PwSurface(0.05));
			p.drawRoundedRect(
				searchWrap->rect(),
				searchWrap->height() / 2.,
				searchWrap->height() / 2.);
			st::menuIconSearch.paint(
				p,
				Px(10),
				(searchWrap->height() - st::menuIconSearch.height()) / 2,
				searchWrap->width(),
				st::windowSubTextFg->c);
		}, searchWrap->lifetime());
		const auto search = Ui::CreateChild<Ui::InputField>(
			searchWrap,
			st::defaultMultiSelectSearchField,
			tr::lng_exd_pw_search());
		search->changes() | rpl::on_next([=] {
			state->query = search->getLastText();
			state->changes.fire({});
		}, search->lifetime());

		const auto sortLabel = [=] {
			switch (state->sort) {
			case PwSort::Name: return tr::lng_exd_pw_sort_name(tr::now);
			case PwSort::Enabled: return tr::lng_exd_pw_sort_enabled(tr::now);
			case PwSort::Date: return tr::lng_exd_pw_sort_date(tr::now);
			default: return tr::lng_exd_pw_sort_pinned(tr::now);
			}
		};
		const auto sortText = box->lifetime().make_state<
			rpl::variable<QString>>(sortLabel());
		const auto sort = Ui::CreateChild<PwPillButton>(
			toolbar,
			sortText->value(),
			nullptr,
			false);
		const auto sortMenu = box->lifetime().make_state<
			base::unique_qptr<Ui::PopupMenu>>();
		sort->setClickedCallback([=] {
			*sortMenu = base::make_unique_q<Ui::PopupMenu>(
				sort,
				st::popupMenuWithIcons);
			const auto add = [&](PwSort value, QString text) {
				(*sortMenu)->addAction(text, [=] {
					state->sort = value;
					Extera::Config::Instance().setInt(
						u"plugins_window_sort"_q,
						int(value));
					*sortText = sortLabel();
					state->changes.fire({});
				}, (state->sort == value) ? &st::menuIconSelect : nullptr);
			};
			add(PwSort::Pinned, tr::lng_exd_pw_sort_pinned(tr::now));
			add(PwSort::Name, tr::lng_exd_pw_sort_name(tr::now));
			add(PwSort::Enabled, tr::lng_exd_pw_sort_enabled(tr::now));
			add(PwSort::Date, tr::lng_exd_pw_sort_date(tr::now));
			(*sortMenu)->popup(QCursor::pos());
		});

		const auto viewSwitch = Ui::CreateChild<Ui::RpWidget>(toolbar);
		viewSwitch->resize(Px(38) * 2 + Px(6), Px(38));
		viewSwitch->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(viewSwitch);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(PwSurface(0.05));
			p.drawRoundedRect(
				viewSwitch->rect(),
				viewSwitch->height() / 2.,
				viewSwitch->height() / 2.);
		}, viewSwitch->lifetime());
		const auto gridButton = Ui::CreateChild<PwViewButton>(viewSwitch, true);
		const auto listButton = Ui::CreateChild<PwViewButton>(viewSwitch, false);
		gridButton->resize(Px(36), Px(32));
		listButton->resize(Px(36), Px(32));
		gridButton->moveToLeft(Px(3), Px(3));
		listButton->moveToLeft(Px(3) + Px(36) + Px(4), Px(3));
		const auto viewAnimate = box->lifetime().make_state<Fn<void()>>();
		const auto refreshView = [=] {
			gridButton->setActiveColor(state->grid
				? std::make_optional(st::windowActiveTextFg->c)
				: std::nullopt);
			listButton->setActiveColor(!state->grid
				? std::make_optional(st::windowActiveTextFg->c)
				: std::nullopt);
		};
		refreshView();
		gridButton->setClickedCallback([=] {
			if (state->grid) {
				return;
			}
			if (*viewAnimate) {
				(*viewAnimate)();
			}
			state->grid = true;
			Extera::Config::Instance().setBool(
				u"plugins_compact_view"_q,
				false);
			refreshView();
			state->changes.fire({});
		});
		listButton->setClickedCallback([=] {
			if (!state->grid) {
				return;
			}
			if (*viewAnimate) {
				(*viewAnimate)();
			}
			state->grid = false;
			Extera::Config::Instance().setBool(
				u"plugins_compact_view"_q,
				true);
			refreshView();
			state->changes.fire({});
		});
		const auto install = Ui::CreateChild<PwPillButton>(
			toolbar,
			tr::lng_exd_pw_install(),
			&st::menuIconDownload,
			true);
		install->setClickedCallback([=] {
			if (Extera::Plugins::Started()) {
				ChooseAndInstall(controller);
			} else {
				controller->showToast(
					tr::lng_extera_plugins_not_running(tr::now));
			}
		});
		toolbar->widthValue() | rpl::on_next([=](int w) {
			const auto top = Px(18);
			const auto h = Px(38);
			auto right = w - Px(20);
			install->moveToLeft(right - install->width(), top);
			right = install->x() - Px(10);
			viewSwitch->moveToLeft(right - viewSwitch->width(), top);
			right = viewSwitch->x() - Px(10);
			sort->moveToLeft(right - sort->width(), top);
			right = sort->x() - Px(10);
			const auto compact = (right - Px(20) < Px(200));
			if (compact) {
				searchWrap->setGeometry(
					Px(20),
					top + h + Px(10),
					w - Px(40),
					h);
			} else {
				searchWrap->setGeometry(Px(20), top, right - Px(20), h);
			}
			const auto wanted = compact ? (Px(62) + h + Px(10)) : Px(62);
			if (toolbar->height() != wanted) {
				toolbar->resize(w, wanted);
			}
			search->setGeometry(
				Px(36),
				(h - search->height()) / 2,
				searchWrap->width() - Px(44),
				search->height());
		}, toolbar->lifetime());
		sortText->value() | rpl::on_next([=] {
			toolbar->resize(toolbar->width() + 1, toolbar->height());
			toolbar->resize(toolbar->width() - 1, toolbar->height());
		}, toolbar->lifetime());

		const auto chips = Ui::CreateChild<Ui::RpWidget>(main);
		chips->resize(chips->width(), Px(40));
		auto chipList = std::vector<not_null<PwChip*>>();
		const auto addChip = [&](QString text, bool PwState::*field) {
			const auto chip = Ui::CreateChild<PwChip>(
				chips,
				std::move(text),
				[=] { return state->*field; });
			chip->setClickedCallback([=] {
				state->*field = !(state->*field);
				chip->update();
				state->changes.fire({});
			});
			chipList.push_back(chip);
		};
		addChip(tr::lng_exd_pw_chip_desktop(tr::now), &PwState::chipDesktop);
		addChip(tr::lng_exd_pw_chip_partial(tr::now), &PwState::chipPartial);
		addChip(tr::lng_exd_pw_chip_settings(tr::now), &PwState::chipSettings);
		chips->widthValue() | rpl::on_next([=](int) {
			auto left = Px(20);
			for (const auto chip : chipList) {
				chip->moveToLeft(left, 0);
				left += chip->width() + Px(8);
			}
		}, chips->lifetime());

		const auto scroll = Ui::CreateChild<Ui::ScrollArea>(
			main,
			st::defaultSolidScroll);
		const auto list = scroll->setOwnedWidget(
			object_ptr<Ui::VerticalLayout>(scroll));
		*viewAnimate = [=] {
			if (anim::Disabled() || scroll->width() <= 0) {
				return;
			}
			const auto shot = Ui::GrabWidget(main, scroll->geometry());
			const auto cover = Ui::CreateChild<Ui::RpWidget>(main);
			cover->setAttribute(Qt::WA_TransparentForMouseEvents);
			cover->setGeometry(scroll->geometry());
			const auto animation = cover->lifetime().make_state<
				Ui::Animations::Simple>();
			cover->paintRequest() | rpl::on_next([=] {
				const auto progress = animation->value(1.);
				auto p = QPainter(cover);
				p.setOpacity(1. - progress);
				p.drawPixmap(0, -int(base::SafeRound(Px(12) * progress)), shot);
			}, cover->lifetime());
			cover->show();
			cover->raise();
			animation->start([=] {
				cover->update();
				if (!animation->animating()) {
					crl::on_main(cover, [=] { delete cover; });
				}
			}, 0., 1., st::slideDuration, anim::easeOutCubic);
		};

		// Detail
		const auto detail = Ui::CreateChild<Ui::RpWidget>(root);
		detail->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(detail);
			p.fillRect(detail->rect(), PwSurface(0.02));
		}, detail->lifetime());
		const auto detailScroll = Ui::CreateChild<Ui::ScrollArea>(
			detail,
			st::defaultSolidScroll);
		const auto detailInner = detailScroll->setOwnedWidget(
			object_ptr<Ui::VerticalLayout>(detailScroll));
		const auto detailShown = box->lifetime().make_state<
			Ui::Animations::Simple>();
		const auto detailOpen = box->lifetime().make_state<bool>(false);

		const auto relayout = [=] {
			const auto w = root->width();
			const auto h = root->height();
			const auto sideWidth = (w < Px(640))
				? 0
				: (w < Px(900))
				? Px(196)
				: Px(232);
			const auto detailWidth = std::min(Px(330), w - sideWidth);
			*detailWidthValue = detailWidth;
			const auto overlay = (w - sideWidth - detailWidth < Px(420));
			const auto shown = detailShown->value(*detailOpen ? 1. : 0.);
			const auto dw = int(base::SafeRound(detailWidth * shown));
			side->setVisible(sideWidth > 0);
			side->setGeometry(0, 0, sideWidth, h);
			sideScroll->setGeometry(0, 0, sideWidth, h);
			if (sideWidth > 0) {
				sideInner->resizeToWidth(sideWidth);
			}
			main->setGeometry(
				sideWidth,
				0,
				w - sideWidth - (overlay ? 0 : dw),
				h);
			detail->setGeometry(w - dw, 0, dw, h);
			detail->setVisible(dw > 0);
			detail->raise();
			detailScroll->setGeometry(0, 0, detailWidth, h);
			detailInner->resizeToWidth(*detailWidthValue);
			toolbar->setGeometry(0, 0, main->width(), toolbar->height());
			chips->setGeometry(0, toolbar->height(), main->width(), chips->height());
			const auto top = toolbar->height() + chips->height();
			scroll->setGeometry(0, top, main->width(), h - top);
			list->resizeToWidth(main->width());
		};

		toolbar->heightValue(
		) | rpl::skip(1) | rpl::on_next([=] {
			relayout();
		}, toolbar->lifetime());

		const auto entryById = [=](const QString &id) -> const PwEntry* {
			for (const auto &entry : state->entries) {
				if (entry.id == id) {
					return &entry;
				}
			}
			return nullptr;
		};
		const auto reload = [=] {
			state->entries = PwLoad();
			if (!state->selected.isEmpty() && !entryById(state->selected)) {
				state->selected = QString();
			}
			state->changes.fire({});
		};
		const auto setEnabled = [=](QString id, bool enabled) {
			if (enabled && Extera::Bool(u"plugins_safe_mode"_q)) {
				controller->showToast(tr::lng_exd_safe_mode_blocked(tr::now));
				crl::on_main(root, reload);
				return;
			}
			Call(u"set_enabled"_q, QJsonObject{
				{ u"id"_q, id },
				{ u"enabled"_q, enabled },
			});
			crl::on_main(root, reload);
		};
		const auto togglePin = [=](QString id) {
			if (const auto entry = entryById(id)) {
				SetPluginPinned(id, !entry->pinned);
				notifyListChanged();
			}
		};
		const auto removePlugin = [=](QString id) {
			PwShowBox(controller, Ui::MakeConfirmBox({
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
		};
		const auto openPage = [=](QString id, int page) {
			state->selected = id;
			state->page = page;
			state->changes.fire({});
		};
		const auto menu = box->lifetime().make_state<
			base::unique_qptr<Ui::PopupMenu>>();
		const auto showMenu = [=](QWidget *parent, QString id) {
			const auto entry = entryById(id);
			if (!entry) {
				return;
			}
			const auto info = entry->info;
			const auto name = entry->name;
			const auto path = entry->path;
			const auto pinned = entry->pinned;
			*menu = base::make_unique_q<Ui::PopupMenu>(
				parent,
				st::popupMenuWithIcons);
			const auto raw = menu->get();
			if (entry->settings) {
				raw->addAction(tr::lng_exd_pw_settings(tr::now), [=] {
					openPage(id, 1);
				}, &st::menuIconSettings);
			}
			raw->addAction(
				(pinned
					? tr::lng_context_unpin_from_top
					: tr::lng_context_pin_to_top)(tr::now),
				[=] { togglePin(id); },
				pinned ? &st::menuIconUnpin : &st::menuIconPin);
			raw->addAction(
				tr::lng_exd_plugin_permissions_menu(tr::now),
				[=] { openPage(id, 2); },
				&st::menuIconLock);
			raw->addAction(tr::lng_exd_plugin_view_code(tr::now), [=] {
				ShowPluginCode(controller, name, path);
			}, &st::exteraIconOpenCode);
			raw->addAction(tr::lng_exd_plugin_share(tr::now), [=] {
				SharePluginFile(controller, path);
			}, &st::menuIconShare);
			if (Exd::PluginGuard::Mode() != 2) {
				raw->addAction(tr::lng_exd_guard_run(tr::now), [=] {
					RunGuard(controller, path, id);
				}, &st::menuIconAntispam);
			}
			if (Exd::PluginPorter::Suggested(info)) {
				raw->addAction(tr::lng_exd_plugin_port(tr::now), [=] {
					Exd::PluginPorter::Start(controller, path);
				}, &st::menuIconTranslate);
			}
			raw->addSeparator();
			raw->addAction(
				tr::lng_exd_pw_delete(tr::now),
				[=] { removePlugin(id); },
				&st::menuIconDeleteAttention);
			raw->popup(QCursor::pos());
		};

		const auto showAllChildren = [](not_null<QWidget*> parent) {
			for (const auto child : parent->findChildren<QWidget*>()) {
				if (!child->testAttribute(Qt::WA_WState_ExplicitShowHide)) {
					child->show();
				}
			}
		};
		const auto fillDetail = box->lifetime().make_state<Fn<void()>>();
		const auto fillPage = box->lifetime().make_state<
			Fn<void(const PwEntry*)>>();
		*fillPage = [=](const PwEntry *entry) {
			const auto id = entry->id;
			const auto header = detailInner->add(
				object_ptr<Ui::RpWidget>(detailInner),
				style::margins(Px(8), Px(12), Px(12), Px(4)));
			header->resize(header->width(), Px(40));
			const auto back = Ui::CreateChild<PwIconButton>(
				header,
				st::infoTopBarBackIcon,
				40);
			back->moveToLeft(0, 0);
			back->setClickedCallback([=] {
				state->page = 0;
				state->changes.fire({});
			});
			const auto title = Ui::CreateChild<Ui::FlatLabel>(
				header,
				(state->page == 1)
					? tr::lng_exd_pw_settings()
					: tr::lng_exd_pw_permissions(),
				st::exdUpdateCardTitle);
			title->setTextColorOverride(st::windowFg->c);
			header->widthValue() | rpl::on_next([=](int w) {
				title->resizeToWidth(std::max(w - Px(48), 1));
				title->moveToLeft(Px(48), (Px(40) - title->height()) / 2);
			}, title->lifetime());
			detailInner->add(
				object_ptr<Ui::FlatLabel>(
					detailInner,
					rpl::single(entry->name),
					st::exdUpdateCardText),
				style::margins(Px(20), 0, Px(20), Px(8)));
			const auto content = detailInner->add(
				object_ptr<Ui::VerticalLayout>(detailInner));
			const auto refill = [=] {
				content->clear();
				if (state->page == 1) {
					FillSettings(content, controller, id);
				} else {
					FillPermissions(content, controller, id, [=] {
						crl::on_main(content, [=] { (*fillDetail)(); });
					});
				}
				content->resizeToWidth(*detailWidthValue);
				showAllChildren(content);
			};
			refill();
			if (state->page == 1) {
				settingsChanges(
				) | rpl::filter([=](const QString &changed) {
					return changed == id;
				}) | rpl::on_next([=] {
					crl::on_main(content, refill);
				}, content->lifetime());
			}
			Ui::AddSkip(detailInner, Px(20));
			detailInner->resizeToWidth(*detailWidthValue);
			showAllChildren(detailInner);
		};
		*fillDetail = [=] {
			detailInner->clear();
			const auto entry = entryById(state->selected);
			if (!entry) {
				return;
			}
			if (state->page != 0) {
				(*fillPage)(entry);
				return;
			}
			const auto id = entry->id;
			const auto name = entry->name;
			const auto path = entry->path;
			const auto padding = style::margins(Px(20), 0, Px(20), 0);
			const auto header = detailInner->add(
				object_ptr<Ui::RpWidget>(detailInner),
				style::margins(Px(20), Px(20), Px(20), Px(12)));
			header->resize(header->width(), Px(72));
			const auto bigIcon = Ui::CreateChild<Ui::AbstractButton>(header);
			bigIcon->setAttribute(Qt::WA_TransparentForMouseEvents);
			SetupPluginIcon(
				bigIcon,
				controller,
				entry->info.value(u"icon"_q).toString(),
				Px(72),
				entry->trusted ? Px(24) : 0);
			bigIcon->moveToLeft(0, 0);
			const auto close = Ui::CreateChild<PwIconButton>(
				header,
				st::boxTitleClose.icon);
			header->widthValue() | rpl::on_next([=](int w) {
				close->moveToLeft(w - close->width() + Px(6), -Px(6));
			}, close->lifetime());
			close->setClickedCallback([=] {
				state->selected = QString();
				state->changes.fire({});
			});
			const auto nameLabel = detailInner->add(
				object_ptr<Ui::FlatLabel>(
					detailInner,
					rpl::single(name),
					st::exdUpdateCardTitle),
				padding);
			nameLabel->setTextColorOverride(st::windowFg->c);
			detailInner->add(
				object_ptr<Ui::FlatLabel>(
					detailInner,
					rpl::single(entry->meta),
					st::exdUpdateCardText),
				padding);
			const auto tags = PwTags(*entry);
			if (!tags.empty()) {
				const auto row = detailInner->add(
					object_ptr<Ui::RpWidget>(detailInner),
					style::margins(Px(20), Px(10), Px(20), 0));
				row->resize(row->width(), st::normalFont->height + Px(4));
				row->paintRequest() | rpl::on_next([=] {
					auto p = QPainter(row);
					auto hq = PainterHighQualityEnabler(p);
					PwPaintTags(p, tags, 0, 0, row->width());
				}, row->lifetime());
			}
			if (!entry->description.isEmpty()) {
				const auto about = detailInner->add(
					object_ptr<Ui::FlatLabel>(detailInner, st::boxLabel),
					style::margins(Px(20), Px(12), Px(20), 0));
				about->setMarkedText(WithLinks(entry->description));
			}
			if (!entry->error.isEmpty()) {
				const auto developer = Extera::Bool(
					u"plugins_developer_mode"_q);
				const auto error = detailInner->add(
					object_ptr<Ui::FlatLabel>(
						detailInner,
						rpl::single(developer
							? entry->error
							: entry->error.split('\n').last()),
						st::exteraAbout),
					style::margins(Px(20), Px(12), Px(20), 0));
				error->setTextColorOverride(st::attentionButtonFg->c);
				error->setSelectable(true);
			}
			const auto addBlock = [&] {
				const auto block = detailInner->add(
					object_ptr<Ui::VerticalLayout>(detailInner),
					style::margins(Px(12), Px(14), Px(12), 0));
				block->paintRequest() | rpl::on_next([=] {
					auto p = QPainter(block);
					auto hq = PainterHighQualityEnabler(p);
					p.setPen(Qt::NoPen);
					p.setBrush(PwSurface(0.07));
					p.drawRoundedRect(block->rect(), Px(18), Px(18));
				}, block->lifetime());
				return block;
			};
			const auto addRow = [&](
					not_null<Ui::VerticalLayout*> block,
					rpl::producer<QString> text,
					const style::icon *icon,
					QString value,
					Fn<void()> click) {
				const auto button = block->add(
					object_ptr<Ui::SettingsButton>(
						block,
						std::move(text),
						st::exdCardSettingsButton));
				if (icon) {
					auto padding = st::exdCardSettingsButton.padding;
					padding.setLeft(Px(50));
					button->setPaddingOverride(padding);
					const auto iconWidget = Ui::CreateChild<Ui::RpWidget>(button);
					iconWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
					iconWidget->resize(icon->size());
					button->heightValue() | rpl::on_next([=](int h) {
						iconWidget->moveToLeft(
							Px(16),
							(h - icon->height()) / 2);
					}, iconWidget->lifetime());
					iconWidget->paintRequest() | rpl::on_next([=] {
						auto p = QPainter(iconWidget);
						icon->paint(p, 0, 0, iconWidget->width());
					}, iconWidget->lifetime());
				}
				if (!value.isEmpty()) {
					const auto label = Ui::CreateChild<Ui::FlatLabel>(
						button,
						value,
						st::exdUpdateCardText);
					label->setAttribute(Qt::WA_TransparentForMouseEvents);
					rpl::combine(
						button->sizeValue(),
						label->sizeValue()
					) | rpl::on_next([=](QSize outer, QSize inner) {
						label->moveToLeft(
							outer.width() - inner.width() - Px(16),
							(outer.height() - inner.height()) / 2);
					}, label->lifetime());
				}
				if (click) {
					button->setClickedCallback(std::move(click));
				} else {
					button->setAttribute(Qt::WA_TransparentForMouseEvents);
				}
				return button;
			};

			const auto main = addBlock();
			Ui::AddSkip(main, Px(4));
			const auto enabledRow = main->add(
				object_ptr<Ui::SettingsButton>(
					main,
					tr::lng_exd_pw_enabled_row(),
					st::exdCardSettingsButton));
			enabledRow->toggleOn(rpl::single(entry->enabled));
			enabledRow->toggledChanges(
			) | rpl::on_next([=](bool value) {
				setEnabled(id, value);
			}, enabledRow->lifetime());
			if (entry->settings) {
				addRow(
					main,
					tr::lng_exd_pw_settings(),
					&st::menuIconSettings,
					QString(),
					[=] { openPage(id, 1); });
			}
			const auto permissions = entry->info.value(
				u"permissions"_q).toArray().size();
			addRow(
				main,
				tr::lng_exd_pw_permissions(),
				&st::menuIconLock,
				permissions
					? QString::number(permissions)
					: tr::lng_exd_pw_permissions_none(tr::now),
				[=] { openPage(id, 2); });
			Ui::AddSkip(main, Px(4));

			detailInner->add(
				object_ptr<Ui::FlatLabel>(
					detailInner,
					tr::lng_exd_pw_info(),
					st::defaultSubsectionTitle),
				style::margins(Px(22), Px(14), Px(22), 0));
			const auto info = addBlock();
			Ui::AddSkip(info, Px(4));
			addRow(
				info,
				tr::lng_exd_pw_source(),
				&st::menuIconAntispam,
				entry->trusted
					? tr::lng_exd_pw_source_trusted(tr::now)
					: tr::lng_exd_pw_source_other(tr::now),
				nullptr);
			if (entry->size > 0) {
				addRow(
					info,
					tr::lng_exd_pw_size(),
					&st::menuIconDownload,
					PwSize(entry->size),
					nullptr);
			}
			if (entry->installed.isValid()) {
				addRow(
					info,
					tr::lng_exd_pw_installed(),
					&st::menuIconInfo,
					QLocale().toString(
						entry->installed.date(),
						QLocale::ShortFormat),
					nullptr);
			}
			Ui::AddSkip(info, Px(4));

			const auto actions = addBlock();
			Ui::AddSkip(actions, Px(4));
			addRow(
				actions,
				tr::lng_exd_plugin_view_code(),
				&st::exteraIconOpenCode,
				QString(),
				[=] { ShowPluginCode(controller, name, path); });
			addRow(
				actions,
				tr::lng_exd_plugin_share(),
				&st::menuIconShare,
				QString(),
				[=] { SharePluginFile(controller, path); });
			if (Exd::PluginGuard::Mode() != 2) {
				addRow(
					actions,
					tr::lng_exd_guard_run(),
					&st::menuIconAntispam,
					QString(),
					[=] { RunGuard(controller, path, id); });
			}
			if (Exd::PluginPorter::Suggested(entry->info)) {
				addRow(
					actions,
					tr::lng_exd_plugin_port(),
					&st::menuIconTranslate,
					QString(),
					[=] { Exd::PluginPorter::Start(controller, path); });
			}
			const auto remove = addRow(
				actions,
				tr::lng_exd_pw_delete(),
				&st::menuIconDeleteAttention,
				QString(),
				[=] { removePlugin(id); });
			remove->setColorOverride(st::attentionButtonFg->c);
			Ui::AddSkip(actions, Px(4));
			Ui::AddSkip(detailInner, Px(20));
			detailInner->resizeToWidth(*detailWidthValue);
			showAllChildren(detailInner);
		};

		const auto fillList = [=] {
			list->clear();
			const auto engineEnabled = Extera::Bool(u"plugins_enabled"_q);
			const auto addText = [&](rpl::producer<QString> text) {
				const auto label = list->add(
					object_ptr<Ui::FlatLabel>(
						list,
						std::move(text),
						st::exteraAbout),
					style::margins(Px(20), Px(60), Px(20), Px(20)),
					style::al_top);
				label->setTextColorOverride(st::windowSubTextFg->c);
			};
			if (!engineEnabled) {
				addText(tr::lng_exd_pw_engine_disabled());
			} else if (!Extera::Plugins::Available()) {
				addText(tr::lng_extera_plugins_runtime_missing());
			} else if (!Extera::Plugins::RuntimeError().isEmpty()) {
				addText(rpl::single(Extera::Plugins::RuntimeError()));
			}
			const auto filtered = PwFiltered(*state);
			if (filtered.empty()) {
				if (engineEnabled) {
					addText(state->entries.empty()
						? tr::lng_extera_plugins_empty()
						: tr::lng_exd_pw_empty());
				}
			} else {
				const auto split = (state->sort == PwSort::Pinned);
				auto pinnedList = std::vector<const PwEntry*>();
				auto restList = std::vector<const PwEntry*>();
				for (const auto entry : filtered) {
					((split && entry->pinned) ? pinnedList : restList)
						.push_back(entry);
				}
				const auto addGroup = [&](
						const std::vector<const PwEntry*> &items,
						QString title) {
					if (items.empty()) {
						return;
					}
					if (!title.isEmpty()) {
						const auto label = list->add(
							object_ptr<Ui::FlatLabel>(
								list,
								rpl::single(title),
								st::exdUpdateCardText),
							style::margins(Px(24), Px(4), Px(20), Px(8)));
						label->setTextColorOverride(st::windowSubTextFg->c);
					}
					const auto grid = list->add(
						object_ptr<PwGrid>(list, state->grid),
						style::margins(Px(20), 0, Px(20), Px(14)));
					for (const auto entry : items) {
						grid->add(Ui::CreateChild<PwCard>(
							grid,
							controller,
							*entry,
							state->grid,
							(state->selected == entry->id),
							[=](QString id) {
								state->selected = (state->selected == id
										&& !state->page)
									? QString()
									: id;
								state->page = 0;
								state->changes.fire({});
							},
							setEnabled,
							togglePin,
							showMenu,
							[=](QString id) { openPage(id, 1); }));
					}
				};
				if (!pinnedList.empty()) {
					addGroup(pinnedList, tr::lng_exd_pw_pinned(tr::now));
					addGroup(restList, tr::lng_exd_pw_section_other(tr::now));
				} else {
					addGroup(restList, QString());
				}
			}
			list->resizeToWidth(main->width());
			list->showChildren();
			for (const auto child : list->findChildren<QWidget*>()) {
				if (!child->testAttribute(Qt::WA_WState_ExplicitShowHide)) {
					child->show();
				}
			}
		};

		const auto refresh = [=] {
			for (const auto &[button, count] : *navList) {
				button->setCount(count());
				button->update();
			}
			engine->update();
			const auto open = !state->selected.isEmpty();
			if (*detailOpen != open) {
				*detailOpen = open;
				detailShown->start(
					relayout,
					open ? 0. : 1.,
					open ? 1. : 0.,
					st::slideDuration,
					anim::easeOutCubic);
			}
			fillList();
			(*fillDetail)();
			relayout();
			enginePage->setVisible(state->engineOpen);
			if (state->engineOpen) {
				enginePage->raise();
			}
			engineSettings->update();
		};
		state->changes.events(
		) | rpl::on_next(refresh, root->lifetime());
		listChanges(
		) | rpl::on_next([=] {
			crl::on_main(root, reload);
		}, root->lifetime());
		root->sizeValue() | rpl::on_next([=] {
			relayout();
		}, root->lifetime());
		refresh();
		search->setFocusFast();
	}));
}

} // namespace Extera::Plugins
