#include "extera/extera_menu.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "base/event_filter.h"
#include "data/data_chat_participant_status.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "extera/extera_config.h"
#include "ui/widgets/menu/menu_common.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/painter.h"
#include "ui/abstract_button.h"
#include "styles/style_chat.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include "extera/extera_utils.h"
#include "extera/extera_plugins_ui.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/format_values.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"

namespace Extera {
namespace {

constexpr auto kMaxQuickActions = 5;


class DetailAction final : public Ui::Menu::ItemBase {
public:
	DetailAction(
		not_null<Ui::Menu::Menu*> parent,
		const style::Menu &st,
		const style::icon *icon,
		const QString &title,
		const QString &value);

	bool isEnabled() const override;
	not_null<QAction*> action() const override;

protected:
	int contentHeight() const override;
	void paintEvent(QPaintEvent *e) override;

private:
	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	const style::icon *_icon = nullptr;
	const QString _title;
	const QString _value;
	int _height = 0;

};

DetailAction::DetailAction(
	not_null<Ui::Menu::Menu*> parent,
	const style::Menu &st,
	const style::icon *icon,
	const QString &title,
	const QString &value)
: ItemBase(parent, st)
, _dummyAction(Ui::CreateChild<QAction>(parent.get()))
, _st(st)
, _icon(icon)
, _title(title)
, _value(value) {
	_height = _st.itemPadding.top()
		+ _st.itemStyle.font->height
		+ st::normalFont->height
		+ _st.itemPadding.bottom();
	setMinWidth(_st.itemPadding.left()
		+ std::max(
			_st.itemStyle.font->width(_title),
			st::normalFont->width(_value))
		+ _st.itemPadding.right());
	fitToMenuWidth();
	enableMouseSelecting();
	setActionTriggered([=] {
		QGuiApplication::clipboard()->setText(_value);
	});
}

bool DetailAction::isEnabled() const {
	return true;
}

not_null<QAction*> DetailAction::action() const {
	return _dummyAction;
}

int DetailAction::contentHeight() const {
	return _height;
}

void DetailAction::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto selected = isSelected();
	p.fillRect(rect(), selected ? _st.itemBgOver : _st.itemBg);
	RippleButton::paintRipple(p, 0, 0);
	if (_icon) {
		_icon->paint(
			p,
			_st.itemIconPosition.x(),
			(_height - _icon->height()) / 2,
			width());
	}
	const auto left = _st.itemPadding.left();
	const auto available = width() - left - _st.itemPadding.right();
	auto top = _st.itemPadding.top();
	p.setFont(_st.itemStyle.font);
	p.setPen(selected ? _st.itemFgOver : _st.itemFg);
	p.drawTextLeft(
		left,
		top,
		width(),
		_st.itemStyle.font->elided(_title, available));
	top += _st.itemStyle.font->height;
	p.setFont(st::normalFont);
	p.setPen(selected ? _st.itemFgShortcutOver : _st.itemFgShortcut);
	p.drawTextLeft(
		left,
		top,
		width(),
		st::normalFont->elided(_value, available));
}

struct QuickEntry {
	QString id;
	std::vector<tr::phrase<>> labels;
	const style::icon *icon = nullptr;
};

[[nodiscard]] const std::vector<QuickEntry> &QuickEntries() {
	static const auto result = std::vector<QuickEntry>{
		{ u"reply"_q, { tr::lng_context_reply_msg }, &st::menuIconReply },
		{ u"delete"_q, { tr::lng_context_delete_msg }, &st::menuIconDelete },
		{
			u"copy"_q,
			{
				tr::lng_context_copy_text,
				tr::lng_context_copy_selected,
				tr::lng_context_copy_image,
			},
			&st::menuIconCopy,
		},
		{ u"edit"_q, { tr::lng_context_edit_msg }, &st::menuIconEdit },
		{
			u"forward"_q,
			{ tr::lng_context_forward_msg },
			&st::menuIconForward,
		},
		{
			u"pin"_q,
			{ tr::lng_context_pin_msg, tr::lng_context_unpin_msg },
			&st::menuIconPin,
		},
		{ u"select"_q, { tr::lng_context_select_msg }, &st::menuIconSelect },
	};
	return result;
}

class QuickRow final : public Ui::Menu::ItemBase {
public:
	QuickRow(
		not_null<Ui::Menu::Menu*> parent,
		const style::Menu &st,
		Fn<void()> hide);

	void addButton(
		not_null<QAction*> action,
		const style::icon *icon,
		const QString &label);

	bool isEnabled() const override;
	not_null<QAction*> action() const override;

protected:
	int contentHeight() const override;
	void paintEvent(QPaintEvent *e) override;

private:
	void layout();
	void updateHovered();
	[[nodiscard]] int dividerHeight() const;

	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	const Fn<void()> _hide;
	std::vector<not_null<Ui::AbstractButton*>> _buttons;
	int _hovered = -1;

};

QuickRow::QuickRow(
	not_null<Ui::Menu::Menu*> parent,
	const style::Menu &st,
	Fn<void()> hide)
: ItemBase(parent, st)
, _dummyAction(Ui::CreateChild<QAction>(parent.get()))
, _st(st)
, _hide(std::move(hide)) {
	fitToMenuWidth();
	widthValue() | rpl::on_next([=] {
		layout();
	}, lifetime());
	base::install_event_filter(this, qApp, [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseMove
			|| type == QEvent::Leave
			|| type == QEvent::Enter) {
			updateHovered();
		}
		return base::EventFilterResult::Continue;
	});
}

void QuickRow::updateHovered() {
	const auto local = mapFromGlobal(QCursor::pos());
	auto hovered = -1;
	if (isVisible() && rect().contains(local)) {
		for (auto i = 0; i != int(_buttons.size()); ++i) {
			if (_buttons[i]->geometry().contains(local)) {
				hovered = i;
				break;
			}
		}
	}
	if (_hovered != hovered) {
		_hovered = hovered;
		for (const auto button : _buttons) {
			button->update();
		}
	}
}

void QuickRow::addButton(
		not_null<QAction*> action,
		const style::icon *icon,
		const QString &label) {
	action->setParent(this);
	const auto button = Ui::CreateChild<Ui::AbstractButton>(this);
	const auto index = int(_buttons.size());
	button->setAccessibleName(label);
	button->setPointerCursor(true);
	button->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(button);
		if (_hovered == index || button->isDown()) {
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(_st.itemBgOver);
			const auto radius = st::roundRadiusLarge;
			p.drawRoundedRect(button->rect(), radius, radius);
		}
		icon->paintInCenter(p, button->rect());
	}, button->lifetime());
	button->setClickedCallback([=] {
		const auto weak = base::make_weak(this);
		action->trigger();
		if (weak) {
			_hide();
		}
	});
	_buttons.push_back(button);
	setMinWidth(int(_buttons.size()) * st::exteraQuickButtonWidth
		+ 2 * st::exteraQuickPadding);
	layout();
}

void QuickRow::layout() {
	if (_buttons.empty()) {
		return;
	}
	const auto padding = st::exteraQuickPadding;
	const auto available = width() - 2 * padding;
	const auto count = int(_buttons.size());
	const auto each = available / count;
	for (auto i = 0; i != count; ++i) {
		_buttons[i]->setGeometry(
			padding + i * each,
			dividerHeight() + padding,
			each,
			st::exteraQuickHeight - 2 * padding);
	}
}

bool QuickRow::isEnabled() const {
	return false;
}

not_null<QAction*> QuickRow::action() const {
	return _dummyAction;
}

int QuickRow::dividerHeight() const {
	const auto &divider = st::expandedMenuSeparator;
	return divider.padding.top() + divider.width + divider.padding.bottom();
}

int QuickRow::contentHeight() const {
	return dividerHeight() + st::exteraQuickHeight;
}

void QuickRow::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto &divider = st::expandedMenuSeparator;
	p.fillRect(
		divider.padding.left(),
		divider.padding.top(),
		width() - divider.padding.left() - divider.padding.right(),
		divider.width,
		divider.fg);
}

[[nodiscard]] QString FormatFullDate(TimeId date) {
	const auto parsed = base::unixtime::parse(date);
	return tr::lng_mediaview_date_time(
		tr::now,
		lt_date,
		QLocale().toString(parsed.date(), u"dd.MM.yyyy"_q),
		lt_time,
		QLocale().toString(parsed.time(), u"HH:mm:ss"_q));
}

void FillDetails(
		not_null<Ui::PopupMenu*> menu,
		not_null<HistoryItem*> item) {
	const auto add = [&](
			const style::icon *icon,
			const QString &title,
			const QString &value) {
		menu->addAction(base::make_unique_q<DetailAction>(
			menu->menu(),
			menu->st().menu,
			icon,
			title,
			value));
	};
	add(
		&st::menuIconInfo,
		tr::lng_extera_details_id(tr::now),
		QString::number(item->id.bare));
	add(
		&st::menuIconChats,
		tr::lng_extera_details_chat(tr::now),
		QString::number(
			item->history()->peer->id.value & PeerId::kChatTypeMask));
	if (const auto from = item->from()) {
		add(
			&st::menuIconProfile,
			tr::lng_extera_details_sender(tr::now),
			QString::number(from->id.value & PeerId::kChatTypeMask));
	}
	add(
		&st::menuIconSchedule,
		tr::lng_extera_details_date(tr::now),
		FormatFullDate(item->date()));
	if (const auto edited = item->Get<HistoryMessageEdited>()) {
		if (edited->date) {
			add(
				&st::menuIconEdit,
				tr::lng_extera_details_edited(tr::now),
				FormatFullDate(edited->date));
		}
	}
	if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		if (forwarded->originalDate) {
			add(
				&st::menuIconForward,
				tr::lng_extera_details_forwarded(tr::now),
				FormatFullDate(forwarded->originalDate));
		}
	}
	if (const auto media = item->media()) {
		if (const auto document = media->document()) {
			if (!document->filename().isEmpty()) {
				add(
					&st::menuIconFile,
					tr::lng_extera_details_file(tr::now),
					document->filename());
			}
			add(
				&st::menuIconDownload,
				tr::lng_extera_details_size(tr::now),
				Ui::FormatSizeText(document->size));
		}
	}
}

} // namespace

void AddMessageMenuActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<HistoryItem*> item,
		not_null<Window::SessionController*> controller) {
	if (!item->isRegular()) {
		return;
	}
	const auto itemId = item->fullId();
	const auto session = &item->history()->session();
	if (Bool(u"menu_repeat"_q)
		&& item->allowsForward()
		&& Data::CanSendAnything(item->history())) {
		menu->addAction(tr::lng_extera_menu_repeat(tr::now), [=] {
			if (const auto item = session->data().message(itemId)) {
				session->api().forwardMessages(
					Data::ResolvedForwardDraft{
						.items = { item },
						.options = Data::ForwardOptions::NoSenderNames,
					},
					Api::SendAction(item->history()));
			}
		}, &st::menuIconRestore);
	}
	Plugins::AddMessageMenuItems(menu, item);
	if (Bool(u"menu_details"_q)) {
		const auto action = menu->addAction(
			tr::lng_extera_menu_details(tr::now),
			[] {},
			&st::menuIconInfo);
		action->setMenu(Ui::CreateChild<QMenu>(menu->menu().get()));
		FillDetails(
			menu->ensureSubmenu(action, st::popupMenuWithIcons),
			item);
	}
}

QStringList QuickActionIds() {
	auto result = QStringList();
	for (const auto &entry : QuickEntries()) {
		result.push_back(entry.id);
	}
	return result;
}

QString QuickActionTitle(const QString &id) {
	for (const auto &entry : QuickEntries()) {
		if (entry.id == id) {
			return entry.labels.front()(tr::now);
		}
	}
	return id;
}

const style::icon *QuickActionIcon(const QString &id) {
	for (const auto &entry : QuickEntries()) {
		if (entry.id == id) {
			return entry.icon;
		}
	}
	return nullptr;
}

void ApplyQuickActions(not_null<Ui::PopupMenu*> menu) {
	if (!Bool(u"menu_quick_row"_q)) {
		return;
	}
	const auto ids = String(u"menu_quick_items"_q).split(
		',',
		Qt::SkipEmptyParts);
	struct Found {
		QAction *action = nullptr;
		const style::icon *icon = nullptr;
		QString label;
	};
	auto found = std::vector<Found>();
	for (const auto &id : ids) {
		if (found.size() >= kMaxQuickActions) {
			break;
		}
		const auto entry = ranges::find(QuickEntries(), id, &QuickEntry::id);
		if (entry == end(QuickEntries())) {
			continue;
		}
		for (const auto action : menu->actions()) {
			if (action->isSeparator() || action->menu()) {
				continue;
			}
			const auto text = action->text();
			const auto matches = ranges::any_of(entry->labels, [&](
					const tr::phrase<> &label) {
				return text == label(tr::now);
			});
			if (matches) {
				found.push_back({ action.get(), entry->icon, text });
				break;
			}
		}
	}
	if (found.empty()) {
		return;
	}
	const auto weak = base::make_weak(menu.get());
	auto row = base::make_unique_q<QuickRow>(
		menu->menu(),
		menu->st().menu,
		[=] {
			if (const auto strong = weak.get()) {
				strong->hideMenu();
			}
		});
	for (const auto &entry : found) {
		row->addButton(entry.action, entry.icon, entry.label);
	}
	for (const auto &entry : found) {
		const auto &actions = menu->actions();
		const auto i = ranges::find_if(actions, [&](not_null<QAction*> a) {
			return a.get() == entry.action;
		});
		if (i != end(actions)) {
			menu->removeAction(int(i - begin(actions)));
		}
	}
	auto previousSeparator = true;
	for (auto i = 0; i < int(menu->actions().size());) {
		const auto separator = menu->actions()[i]->isSeparator();
		if (separator && previousSeparator) {
			menu->removeAction(i);
			continue;
		}
		previousSeparator = separator;
		++i;
	}
	while (!menu->empty() && menu->actions().back()->isSeparator()) {
		menu->removeAction(int(menu->actions().size()) - 1);
	}
	auto lastSeparator = -1;
	for (auto i = 0; i != int(menu->actions().size()); ++i) {
		if (menu->actions()[i]->isSeparator()) {
			lastSeparator = i;
		}
	}
	if (lastSeparator > 0) {
		menu->insertAction(lastSeparator, std::move(row));
	} else {
		menu->addAction(std::move(row));
	}
}

} // namespace Extera
