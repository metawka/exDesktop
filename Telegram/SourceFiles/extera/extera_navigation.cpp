#include "extera/extera_navigation.h"

#include "extera/extera_config.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/vertical_layout_reorder.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_wallet.h"
#include "styles/style_window.h"
#include "styles/style_window_main_menu.h"

namespace Extera::Navigation {
namespace {

constexpr auto kProperty = "extera_menu_id";
const auto kDivider = u"divider"_q;
const auto kSettings = u"settings"_q;

struct Item {
	QString id;
	tr::phrase<> title;
	const style::icon *icon = nullptr;
};

[[nodiscard]] const std::vector<Item> &Items() {
	static const auto result = std::vector<Item>{
		{ u"profile"_q, tr::lng_menu_my_profile, &st::menuIconProfile },
		{ u"new_group"_q, tr::lng_create_group_title, &st::menuIconGroups },
		{ u"new_channel"_q, tr::lng_create_channel_title, &st::menuIconChannel },
		{ u"contacts"_q, tr::lng_menu_contacts, &st::menuIconUserShow },
		{ u"calls"_q, tr::lng_menu_calls, &st::menuIconPhone },
		{ u"saved"_q, tr::lng_saved_messages, &st::menuIconSavedMessages },
		{ u"feed"_q, tr::lng_extera_feed, &st::menuIconChannel },
		{ u"plugins"_q, tr::lng_extera_plugins, &st::exteraIconPlugins },
		{ u"wallet"_q, tr::lng_wallet_menu, &st::walletMenuIcon },
		{ u"settings"_q, tr::lng_menu_settings, &st::menuIconSettings },
		{ u"streamer"_q, tr::lng_extera_streamer_mode, &st::menuIconUserHide },
		{ u"night"_q, tr::lng_menu_night_mode, &st::menuIconNightMode },
	};
	return result;
}

[[nodiscard]] const Item *FindItem(const QString &id) {
	for (const auto &item : Items()) {
		if (item.id == id) {
			return &item;
		}
	}
	return nullptr;
}

[[nodiscard]] QStringList Hidden() {
	return String(u"menu_hidden"_q).split(',', Qt::SkipEmptyParts);
}

[[nodiscard]] QStringList Order() {
	auto result = String(u"menu_order"_q).split(',', Qt::SkipEmptyParts);
	const auto hidden = Hidden();
	result.erase(std::remove_if(result.begin(), result.end(), [&](
			const QString &id) {
		return (id != kDivider) && (!FindItem(id) || hidden.contains(id));
	}), result.end());
	auto previous = QString();
	for (const auto &item : Items()) {
		if (!result.contains(item.id) && !hidden.contains(item.id)) {
			const auto after = previous.isEmpty()
				? -1
				: result.indexOf(previous);
			if (after >= 0) {
				result.insert(after + 1, item.id);
			} else {
				result.push_back(item.id);
			}
		}
		if (result.contains(item.id)) {
			previous = item.id;
		}
	}
	return result;
}

void Save(const QStringList &order, const QStringList &hidden) {
	Config::Instance().setString(u"menu_order"_q, order.join(','));
	Config::Instance().setString(u"menu_hidden"_q, hidden.join(','));
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeDivider(QWidget *parent) {
	return object_ptr<Ui::PlainShadow>(parent);
}

void AddDragHandle(not_null<Ui::RpWidget*> button) {
	const auto handle = Ui::CreateChild<Ui::RpWidget>(button.get());
	const auto &icon = st::usernamesReorderIcon;
	handle->resize(icon.width(), icon.height());
	handle->setAttribute(Qt::WA_TransparentForMouseEvents);
	handle->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(handle);
		icon.paint(p, 0, 0, handle->width());
	}, handle->lifetime());
	button->sizeValue() | rpl::on_next([=](QSize size) {
		handle->moveToRight(
			st::settingsButtonNoIcon.padding.right(),
			(size.height() - handle->height()) / 2,
			size.width());
	}, handle->lifetime());
}

} // namespace

void ApplyMenuLayout(
		not_null<Ui::VerticalLayout*> menu,
		not_null<QWidget*> holder) {
	auto tagged = base::flat_map<QString, Ui::RpWidget*>();
	auto first = -1;
	for (auto i = 0; i != menu->count(); ++i) {
		const auto widget = menu->widgetAt(i);
		const auto id = widget->property(kProperty).toString();
		if (!id.isEmpty()) {
			tagged.emplace(id, widget.get());
			if (first < 0) {
				first = i;
			}
		}
	}
	if (first < 0) {
		return;
	}
	for (const auto &id : Hidden()) {
		if (id == kSettings) {
			continue;
		}
		const auto i = tagged.find(id);
		if (i != end(tagged)) {
			i->second->hide();
			i->second->setParent(holder);
			tagged.erase(i);
		}
	}
	const auto indexOf = [&](not_null<Ui::RpWidget*> widget) {
		for (auto i = 0; i != menu->count(); ++i) {
			if (menu->widgetAt(i) == widget) {
				return i;
			}
		}
		return -1;
	};
	auto position = first;
	for (const auto &id : Order()) {
		if (id == kDivider) {
			if (position <= menu->count()) {
				menu->insert(
					position++,
					MakeDivider(menu),
					{ 0, st::mainMenuSkip, 0, st::mainMenuSkip });
			}
			continue;
		}
		const auto i = tagged.find(id);
		if (i == end(tagged)) {
			continue;
		}
		const auto index = indexOf(i->second);
		if (index >= 0 && position < menu->count()) {
			menu->reorderRows(index, position++);
		}
		tagged.erase(i);
	}
}

void FillSettings(not_null<Ui::VerticalLayout*> container) {
	const auto rebuild = std::make_shared<Fn<void()>>();
	*rebuild = [=] {
		container->clear();
		auto order = Order();
		auto hidden = Hidden();

		Ui::AddSubsectionTitle(container, tr::lng_extera_nav_items());
		const auto list = container->add(
			object_ptr<Ui::VerticalLayout>(container));
		const auto dragging = list->lifetime().make_state<bool>(false);
		for (const auto &id : order) {
			const auto item = FindItem(id);
			const auto button = list->add(item
				? Settings::CreateButtonWithIcon(
					list,
					item->title(),
					st::settingsButton,
					{ item->icon })
				: Settings::CreateButtonWithIcon(
					list,
					tr::lng_extera_nav_divider(),
					st::settingsButton,
					{ &st::menuIconRemove }));
			button->setProperty(kProperty, id);
			AddDragHandle(button);
			button->setClickedCallback([=] {
				if (*dragging) {
					return;
				}
				auto order = Order();
				auto hidden = Hidden();
				const auto index = [&] {
					for (auto i = 0; i != list->count(); ++i) {
						if (list->widgetAt(i) == button) {
							return i;
						}
					}
					return -1;
				}();
				if (index >= 0 && index < order.size()) {
					if (id == kSettings) {
						return;
					}
					order.removeAt(index);
					if (id != kDivider) {
						hidden.push_back(id);
					}
					Save(order, hidden);
					crl::on_main(container, [=] { (*rebuild)(); });
				}
			});
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
			if (data.state != State::Applied) {
				return;
			}
			auto order = QStringList();
			for (auto i = 0; i != list->count(); ++i) {
				order.push_back(
					list->widgetAt(i)->property(kProperty).toString());
			}
			Save(order, Hidden());
		}, list->lifetime());
		reorder->start();

		container->add(Settings::CreateButtonWithIcon(
			container,
			tr::lng_extera_nav_add_divider(),
			st::settingsButtonActive,
			{
				&st::settingsIconAdd,
				Settings::IconType::Round,
				&st::windowBgActive,
			}
		))->setClickedCallback([=] {
			auto order = Order();
			order.push_back(kDivider);
			Save(order, Hidden());
			crl::on_main(container, [=] { (*rebuild)(); });
		});
		Ui::AddDividerText(container, tr::lng_extera_nav_items_about());

		if (!hidden.isEmpty()) {
			Ui::AddSubsectionTitle(container, tr::lng_extera_nav_hidden());
			for (const auto &id : hidden) {
				const auto item = FindItem(id);
				if (!item) {
					continue;
				}
				container->add(Settings::CreateButtonWithIcon(
					container,
					item->title(),
					st::settingsButton,
					{ item->icon }
				))->setClickedCallback([=] {
					auto order = Order();
					auto hidden = Hidden();
					hidden.removeAll(id);
					order.push_back(id);
					Save(order, hidden);
					crl::on_main(container, [=] { (*rebuild)(); });
				});
			}
			Ui::AddDividerText(container, tr::lng_extera_nav_hidden_about());
		}
		container->resizeToWidth(container->width());
	};
	(*rebuild)();
}

} // namespace Extera::Navigation
