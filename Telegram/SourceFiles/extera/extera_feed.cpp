#include "extera/extera_feed.h"

#include "apiwrap.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_folder.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "extera/extera_config.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_list_widget.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/chat_theme.h"
#include "ui/layers/generic_box.h"
#include "ui/effects/panel_animation.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/vertical_layout.h"
#include "window/section_memento.h"
#include "window/section_widget.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"

namespace Extera {
namespace {

constexpr auto kMaxPosts = 150;
constexpr auto kMaxChannels = 30;
constexpr auto kPerChannel = 10;

std::vector<FullMsgId> &Fetched() {
	static auto result = std::vector<FullMsgId>();
	return result;
}

[[nodiscard]] base::flat_set<uint64> HiddenChannels() {
	auto result = base::flat_set<uint64>();
	for (const auto &part : String(u"feed_hidden"_q).split(',')) {
		if (const auto value = part.trimmed().toULongLong()) {
			result.emplace(value);
		}
	}
	return result;
}

void SetChannelHidden(not_null<PeerData*> peer, bool hidden) {
	auto list = HiddenChannels();
	if (hidden) {
		list.emplace(peer->id.value);
	} else {
		list.remove(peer->id.value);
	}
	auto parts = QStringList();
	for (const auto value : list) {
		parts.push_back(QString::number(value));
	}
	Config::Instance().setString(u"feed_hidden"_q, parts.join(','));
}

void CollectChannels(
		not_null<Dialogs::IndexedList*> list,
		std::vector<not_null<History*>> &result) {
	for (const auto &row : list->all()) {
		if (const auto history = row->history()) {
			if (history->peer->isBroadcast()) {
				result.push_back(history);
			}
		}
	}
}

[[nodiscard]] std::vector<not_null<History*>> AllChannels(
		not_null<Data::Session*> owner) {
	auto result = std::vector<not_null<History*>>();
	CollectChannels(owner->chatsList()->indexed(), result);
	if (Bool(u"feed_include_archived"_q)) {
		if (const auto folder = owner->folderLoaded(Data::Folder::kId)) {
			CollectChannels(folder->chatsList()->indexed(), result);
		}
	}
	return result;
}

[[nodiscard]] std::vector<not_null<History*>> FeedChannels(
		not_null<Data::Session*> owner) {
	const auto hidden = HiddenChannels();
	auto result = AllChannels(owner);
	result.erase(ranges::remove_if(result, [&](not_null<History*> history) {
		return hidden.contains(history->peer->id.value);
	}), end(result));
	return result;
}

[[nodiscard]] bool GoodPost(not_null<HistoryItem*> item) {
	return item->isRegular() && !item->isService();
}

[[nodiscard]] std::vector<FullMsgId> Gather(not_null<Data::Session*> owner) {
	const auto channels = FeedChannels(owner);
	const auto allowed = [&](not_null<History*> history) {
		return ranges::contains(channels, history);
	};
	auto items = std::vector<not_null<HistoryItem*>>();
	for (const auto history : channels) {
		auto added = false;
		for (const auto &block : history->blocks) {
			for (const auto &view : block->messages) {
				if (GoodPost(view->data())) {
					items.push_back(view->data());
					added = true;
				}
			}
		}
		if (!added) {
			if (const auto last = history->lastMessage()) {
				if (GoodPost(last)) {
					items.push_back(last);
				}
			}
		}
	}
	for (const auto &id : Fetched()) {
		if (const auto item = owner->message(id)) {
			if (allowed(item->history())) {
				items.push_back(item);
			}
		}
	}
	ranges::sort(items, ranges::less(), [](not_null<HistoryItem*> item) {
		return item->position();
	});
	items.erase(ranges::unique(items), end(items));
	if (int(items.size()) > kMaxPosts) {
		items.erase(begin(items), end(items) - kMaxPosts);
	}
	auto result = std::vector<FullMsgId>();
	result.reserve(items.size());
	for (const auto item : items) {
		result.push_back(item->fullId());
	}
	return result;
}

[[nodiscard]] int UnreadCount(not_null<Data::Session*> owner) {
	auto result = 0;
	for (const auto history : FeedChannels(owner)) {
		result += history->unreadCount();
	}
	return result;
}

void Request(
		not_null<Main::Session*> session,
		const std::vector<not_null<History*>> &channels,
		Fn<void()> done) {
	const auto count = std::min(int(channels.size()), kMaxChannels);
	if (!count) {
		done();
		return;
	}
	const auto left = std::make_shared<int>(count);
	const auto finish = [=] {
		if (!--*left) {
			done();
		}
	};
	for (auto i = 0; i != count; ++i) {
		const auto history = channels[i];
		session->api().request(MTPmessages_GetHistory(
			history->peer->input(),
			MTP_int(0),
			MTP_int(0),
			MTP_int(0),
			MTP_int(kPerChannel),
			MTP_int(0),
			MTP_int(0),
			MTP_long(0)
		)).done([=](const MTPmessages_Messages &result) {
			const auto owner = &session->data();
			result.match([](const MTPDmessages_messagesNotModified &) {
			}, [&](const auto &data) {
				owner->processUsers(data.vusers());
				owner->processChats(data.vchats());
				for (const auto &message : data.vmessages().v) {
					if (!DateFromMessage(message)
						|| !owner->peerLoaded(PeerFromMessage(message))) {
						continue;
					}
					const auto item = owner->addNewMessage(
						message,
						MessageFlags(),
						NewMessageType::Existing);
					if (item
						&& item->isRegular()
						&& !item->isService()
						&& !ranges::contains(Fetched(), item->fullId())) {
						if (Fetched().size() >= kMaxPosts * 2) {
							Fetched().erase(Fetched().begin(), Fetched().begin() + kMaxPosts);
						}
						Fetched().push_back(item->fullId());
					}
				}
			});
			finish();
		}).fail(finish).send();
	}
}



class FeedMemento;

class FeedWidget final
	: public Window::SectionWidget
	, private HistoryView::WindowListDelegate {
public:
	FeedWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	bool hasTopBarShadow() const override {
		return true;
	}
	QPixmap grabForShowAnimation(
		const Window::SectionSlideParams &params) override;
	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	std::shared_ptr<Window::SectionMemento> createMemento() override;
	bool showMessage(
		PeerId peerId,
		const Window::SectionShow &params,
		MsgId messageId) override {
		return false;
	}
	Window::SectionActionResult sendBotCommand(
			Bot::SendCommandRequest request) override {
		return Window::SectionActionResult::Fallback;
	}

	void setInternalState(
		const QRect &geometry,
		not_null<FeedMemento*> memento);

	bool floatPlayerHandleWheelEvent(QEvent *e) override {
		return _scroll->viewportEvent(e);
	}
	QRect floatPlayerAvailableRect() override {
		return mapToGlobal(_scroll->geometry());
	}

	HistoryView::Context listContext() override {
		return HistoryView::Context::Pinned;
	}
	bool listScrollTo(int top, bool syntetic = true) override;
	void listCancelRequest() override {
		controller()->showBackFromStack();
	}
	void listDeleteRequest() override {
	}
	void listTryProcessKeyInput(not_null<QKeyEvent*> e) override {
	}
	rpl::producer<Data::MessagesSlice> listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) override;
	bool listAllowsMultiSelect() override {
		return false;
	}
	bool listIsItemGoodForSelection(not_null<HistoryItem*> item) override {
		return false;
	}
	bool listIsLessInOrder(
			not_null<HistoryItem*> first,
			not_null<HistoryItem*> second) override {
		return first->position() < second->position();
	}
	void listSelectionChanged(
		HistoryView::SelectedItems &&items) override {
	}
	void listMarkReadTill(not_null<HistoryItem*> item) override {
	}
	void listMarkContentsRead(
		const base::flat_set<not_null<HistoryItem*>> &items) override {
	}
	HistoryView::MessagesBarData listMessagesBar(
			const std::vector<not_null<HistoryView::Element*>> &elements,
			bool markLastAsRead) override {
		return {};
	}
	void listContentRefreshed() override {
	}
	void listUpdateDateLink(
		ClickHandlerPtr &link,
		not_null<HistoryView::Element*> view) override {
	}
	bool listElementHideReply(
			not_null<const HistoryView::Element*> view) override {
		return false;
	}
	bool listElementShownUnread(
			not_null<const HistoryView::Element*> view) override {
		return view->data()->unread(view->data()->history());
	}
	bool listIsGoodForAroundPosition(
			not_null<const HistoryView::Element*> view) override {
		return view->data()->isRegular();
	}
	void listSendBotCommand(
		const QString &command,
		const FullMsgId &context) override {
	}
	void listSearch(
			const QString &query,
			const FullMsgId &context) override {
		controller()->searchMessages(query, Dialogs::Key());
	}
	void listHandleViaClick(not_null<UserData*> bot) override {
	}
	not_null<Ui::ChatTheme*> listChatTheme() override {
		return _theme.get();
	}
	HistoryView::CopyRestrictionType listCopyRestrictionType(
			HistoryItem *item) override {
		return item
			? HistoryView::CopyRestrictionTypeFor(item->history()->peer, item)
			: HistoryView::CopyRestrictionType::None;
	}
	HistoryView::CopyRestrictionType listCopyMediaRestrictionType(
			not_null<HistoryItem*> item) override {
		return HistoryView::CopyMediaRestrictionTypeFor(
			item->history()->peer,
			item);
	}
	HistoryView::CopyRestrictionType listSelectRestrictionType() override {
		return HistoryView::CopyRestrictionType::None;
	}
	auto listAllowedReactionsValue()
	-> rpl::producer<Data::AllowedReactions> override {
		return rpl::single(Data::AllowedReactions{
			.type = Data::AllowedReactionsType::All,
		});
	}
	void listShowPremiumToast(not_null<DocumentData*> document) override {
	}
	void listOpenPhoto(
			not_null<PhotoData*> photo,
			FullMsgId context) override {
		controller()->openPhoto(photo, { .id = context });
	}
	void listOpenDocument(
			not_null<DocumentData*> document,
			FullMsgId context,
			bool showInMediaView) override {
		controller()->openDocument(
			document,
			showInMediaView,
			{ .id = context });
	}
	void listPaintEmpty(
		Painter &p,
		const Ui::ChatPaintContext &context) override;
	QString listElementAuthorRank(
			not_null<const HistoryView::Element*> view) override {
		return {};
	}
	bool listElementHideTopicButton(
			not_null<const HistoryView::Element*> view) override {
		return true;
	}
	History *listTranslateHistory() override {
		return nullptr;
	}
	void listAddTranslatedItems(
		not_null<HistoryView::TranslateTracker*> tracker) override {
	}
	Ui::ElasticScroll *listScrollArea() const override {
		return _scroll.get();
	}
	bool listThanosEffectEnabled() const override {
		return false;
	}

private:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void showFinishedHook() override {
		_inner->showFinished();
	}
	void doSetInnerFocus() override {
		_inner->setFocus();
	}
	void checkActivation() override {
		_inner->checkActivation();
	}

	void setupTopBar();
	void showMenu();
	void updateControlsGeometry();
	void updateInnerVisibleArea();
	void refresh();

	const not_null<Data::Session*> _owner;
	std::shared_ptr<Ui::ChatTheme> _theme;
	object_ptr<Ui::RpWidget> _topBar;
	object_ptr<Ui::PlainShadow> _topBarShadow;
	std::unique_ptr<Ui::ElasticScroll> _scroll;
	QPointer<HistoryView::ListWidget> _inner;
	base::unique_qptr<Ui::PopupMenu> _menu;
	Ui::IconButton *_menuToggle = nullptr;
	rpl::event_stream<> _refreshes;
	bool _skipScrollEvent = false;

};

class FeedMemento final : public Window::SectionMemento {
public:
	object_ptr<Window::SectionWidget> createWidget(
			QWidget *parent,
			not_null<Window::SessionController*> controller,
			Window::Column column,
			const QRect &geometry) override {
		if (column == Window::Column::Third) {
			return nullptr;
		}
		auto result = object_ptr<FeedWidget>(parent, controller);
		result->setInternalState(geometry, this);
		return result;
	}

	[[nodiscard]] not_null<HistoryView::ListMemento*> list() {
		return &_list;
	}

private:
	HistoryView::ListMemento _list;

};

FeedWidget::FeedWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Window::SectionWidget(parent, controller)
, WindowListDelegate(controller)
, _owner(&controller->session().data())
, _theme(controller->defaultChatTheme())
, _topBar(this)
, _topBarShadow(this)
, _scroll(std::make_unique<Ui::ElasticScroll>(
	this,
	controller->chatStyle()->value(lifetime(), st::historyScroll))) {
	controller->setChatStyleTheme(_theme);
	setupTopBar();

	_scroll->setHandleTouch(false);
	_inner = _scroll->setOwnedWidget(object_ptr<HistoryView::ListWidget>(
		this,
		&controller->session(),
		static_cast<HistoryView::ListDelegate*>(this)));
	_inner->lower();
	_scroll->show();
	_scroll->setOverscrollBg(QColor(0, 0, 0, 0));
	_scroll->scrolls(
	) | rpl::on_next([=] {
		if (!_skipScrollEvent) {
			updateInnerVisibleArea();
		}
	}, lifetime());
	_inner->scrollKeyEvents(
	) | rpl::on_next([=](not_null<QKeyEvent*> e) {
		_scroll->keyPressEvent(e);
	}, lifetime());

	controller->session().data().newItemAdded(
	) | rpl::filter([](not_null<HistoryItem*> item) {
		return item->history()->peer->isBroadcast();
	}) | rpl::on_next([=] {
		refresh();
	}, lifetime());

	const auto weak = base::make_weak(this);
	Request(&controller->session(), FeedChannels(_owner), [=] {
		if (weak) {
			refresh();
		}
	});
}

void FeedWidget::setupTopBar() {
	_topBar->resize(width(), st::topBarHeight);
	_topBar->paintRequest() | rpl::on_next([=](QRect clip) {
		auto p = QPainter(_topBar.data());
		p.fillRect(clip, st::topBarBg);
		p.setPen(st::dialogsNameFg);
		p.setFont(st::semiboldFont);
		const auto left = st::historyTopBarBack.width;
		p.drawText(
			left,
			(_topBar->height() - st::semiboldFont->height) / 2
				+ st::semiboldFont->ascent,
			tr::lng_extera_feed(tr::now));
	}, _topBar->lifetime());
	const auto back = Ui::CreateChild<Ui::IconButton>(
		_topBar.data(),
		st::historyTopBarBack);
	back->setClickedCallback([=] { controller()->showBackFromStack(); });
	back->moveToLeft(0, 0);
	_menuToggle = Ui::CreateChild<Ui::IconButton>(
		_topBar.data(),
		st::topBarMenuToggle);
	_menuToggle->setClickedCallback([=] { showMenu(); });
	_menuToggle->show();
	_topBar->show();
	_topBarShadow->show();
}

void FeedWidget::showMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuExpandedSeparator);
	const auto window = controller();
	const auto owner = _owner;
	_menu->addAction(tr::lng_extera_feed_mark_read(tr::now), [=] {
		window->show(Ui::MakeConfirmBox({
			.text = tr::lng_extera_feed_mark_read_sure(),
			.confirmed = [=](Fn<void()> close) {
				for (const auto history : FeedChannels(owner)) {
					if (history->unreadCount() > 0) {
						owner->histories().readInbox(history);
					}
				}
				close();
				window->showToast(
					tr::lng_extera_feed_mark_read_done(tr::now));
			},
			.confirmText = tr::lng_extera_feed_mark_read(),
		}));
	}, &st::menuIconMarkRead);
	_menu->addAction(tr::lng_extera_feed_settings(tr::now), [=] {
		ShowFeedSettings(window);
	}, &st::menuIconSettings);
	_menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
	_menu->popup(mapToGlobal(QPoint(
		width() - st::topBarMenuToggle.width / 2,
		st::topBarHeight)));
}

void FeedWidget::refresh() {
	_refreshes.fire({});
}

rpl::producer<Data::MessagesSlice> FeedWidget::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	return rpl::single(
		rpl::empty
	) | rpl::then(
		_refreshes.events()
	) | rpl::map([=] {
		auto result = Data::MessagesSlice();
		result.ids = Gather(_owner);
		result.fullCount = int(result.ids.size());
		result.skippedBefore = 0;
		result.skippedAfter = 0;
		if (!result.ids.empty()) {
			result.nearestToAround = result.ids.back();
		}
		return result;
	});
}

bool FeedWidget::listScrollTo(int top, bool syntetic) {
	top = std::clamp(top, 0, _scroll->scrollTopMax());
	if (_scroll->scrollTop() == top) {
		updateInnerVisibleArea();
		return false;
	}
	_scroll->scrollToY(top);
	return true;
}

void FeedWidget::listPaintEmpty(
		Painter &p,
		const Ui::ChatPaintContext &context) {
}

QPixmap FeedWidget::grabForShowAnimation(
		const Window::SectionSlideParams &params) {
	if (params.withTopBarShadow) {
		_topBarShadow->hide();
	}
	auto result = Ui::GrabWidget(this);
	if (params.withTopBarShadow) {
		_topBarShadow->show();
	}
	return result;
}

bool FeedWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (const auto feed = dynamic_cast<FeedMemento*>(memento.get())) {
		_inner->restoreState(feed->list());
		return true;
	}
	return false;
}

std::shared_ptr<Window::SectionMemento> FeedWidget::createMemento() {
	auto result = std::make_shared<FeedMemento>();
	_inner->saveState(result->list());
	return result;
}

void FeedWidget::setInternalState(
		const QRect &geometry,
		not_null<FeedMemento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	_inner->restoreState(memento->list());
}

void FeedWidget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}
	controller()->adaptive().setChatLayout(
		(width() < st::adaptiveChatWideWidth)
			? Window::Adaptive::ChatLayout::Normal
			: Window::Adaptive::ChatLayout::Wide);
	updateControlsGeometry();
}

void FeedWidget::updateControlsGeometry() {
	const auto contentWidth = width();
	_topBar->resize(contentWidth, st::topBarHeight);
	_topBar->moveToLeft(0, 0);
	_topBar->raise();
	if (_menuToggle) {
		_menuToggle->moveToRight(0, 0, contentWidth);
		_menuToggle->raise();
	}
	_topBarShadow->resize(contentWidth, st::lineWidth);
	_topBarShadow->moveToLeft(0, _topBar->height());
	const auto top = _topBar->height();
	const auto size = QSize(contentWidth, height() - top);
	if (_scroll->size() != size) {
		_skipScrollEvent = true;
		_scroll->resize(size);
		_inner->resizeToWidth(size.width(), _scroll->height());
		_skipScrollEvent = false;
	}
	_scroll->move(0, top);
	updateInnerVisibleArea();
}

void FeedWidget::updateInnerVisibleArea() {
	const auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
}

void FeedWidget::paintEvent(QPaintEvent *e) {
	if (animatingShow()) {
		SectionWidget::paintEvent(e);
		return;
	} else if (controller()->contentOverlapped(this, e)) {
		return;
	}
	const auto above = _topBar->height();
	const auto bg = e->rect().intersected(
		QRect(0, above, width(), height() - above));
	SectionWidget::PaintBackground(controller(), _theme.get(), this, bg);
}

void AddSettingsToggle(
		not_null<Ui::VerticalLayout*> container,
		const QString &key,
		rpl::producer<QString> title) {
	const auto button = container->add(
		object_ptr<Ui::SettingsButton>(
			container,
			std::move(title),
			st::settingsButtonNoIcon));
	button->toggleOn(rpl::single(Bool(key)));
	button->toggledChanges(
	) | rpl::on_next([=](bool value) {
		Config::Instance().setBool(key, value);
	}, button->lifetime());
}

} // namespace

void ShowChannelFeed(not_null<Window::SessionController*> controller) {
	controller->showSection(std::make_shared<FeedMemento>());
}

int FeedUnreadCount(not_null<Main::Session*> session) {
	return Bool(u"feed_unread_counter"_q) ? UnreadCount(&session->data()) : 0;
}

void ShowFeedSettings(not_null<Window::SessionController*> controller) {
	const auto owner = &controller->session().data();
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_extera_feed_settings());
		box->setWidth(st::boxWideWidth);
		const auto content = box->verticalLayout();
		AddSettingsToggle(
			content,
			u"feed_in_menu"_q,
			tr::lng_extera_feed_in_menu());
		AddSettingsToggle(
			content,
			u"feed_wide_posts"_q,
			tr::lng_extera_feed_wide());
		AddSettingsToggle(
			content,
			u"feed_unread_counter"_q,
			tr::lng_extera_feed_counter());
		AddSettingsToggle(
			content,
			u"feed_include_archived"_q,
			tr::lng_extera_feed_archived());
		Ui::AddDividerText(content, tr::lng_extera_feed_archived_about());
		Ui::AddSubsectionTitle(content, tr::lng_extera_feed_shown());
		const auto hidden = HiddenChannels();
		for (const auto history : AllChannels(owner)) {
			const auto peer = history->peer;
			const auto checkbox = content->add(
				object_ptr<Ui::Checkbox>(
					content,
					peer->name(),
					!hidden.contains(peer->id.value),
					st::defaultBoxCheckbox),
				st::boxRowPadding + QMargins(0, 6, 0, 6));
			checkbox->checkedChanges(
			) | rpl::on_next([=](bool checked) {
				SetChannelHidden(peer, !checked);
			}, checkbox->lifetime());
		}
		Ui::AddDividerText(content, tr::lng_extera_feed_channels_about());
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

} // namespace Extera
