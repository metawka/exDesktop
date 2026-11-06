#include "extera/exd_ai_chat_section.h"

#include "extera/extera_ai_chat.h"
#include <QtCore/QLocale>
#include "base/unixtime.h"
#include "lang/lang_keys.h"
#include "settings/sections/settings_extera.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/controls/send_button.h"
#include "ui/painter.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_options.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/section_memento.h"
#include "window/section_widget.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Exd {
namespace {

// Bubble

class Bubble final : public Ui::RpWidget {
public:
	Bubble(
		QWidget *parent,
		Extera::AiChatMessage message,
		bool typing);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	[[nodiscard]] QRect bubbleRect() const;

	const Extera::AiChatMessage _message;
	const bool _typing = false;
	Ui::Text::String _text;
	Ui::Text::String _author;
	QString _time;
	int _textWidth = 0;
	int _textHeight = 0;
	int _timeExtra = 0;
	base::unique_qptr<Ui::PopupMenu> _menu;

};

Bubble::Bubble(
	QWidget *parent,
	Extera::AiChatMessage message,
	bool typing)
: RpWidget(parent)
, _message(std::move(message))
, _typing(typing) {
	auto text = TextUtilities::ParseEntities(
		_message.text,
		TextParseLinks | TextParseMarkdown);
	_text.setMarkedText(
		st::messageTextStyle,
		text,
		Ui::ItemTextDefaultOptions());
	if (_message.error) {
		_author.setText(
			st::semiboldTextStyle,
			tr::lng_exd_ai_error(tr::now));
	}
	if (_message.date && !_typing) {
		_time = QLocale().toString(
			base::unixtime::parse(_message.date).time(),
			QLocale::ShortFormat);
	}
}

QRect Bubble::bubbleRect() const {
	const auto &padding = st::msgPadding;
	const auto author = _author.isEmpty() ? 0 : st::msgNameFont->height;
	const auto timeWidth = _time.isEmpty()
		? 0
		: (st::msgDateFont->width(_time) + style::ConvertScale(10));
	const auto lines = _text.countLineWidths(_textWidth);
	const auto last = lines.empty() ? 0 : lines.back();
	const auto inner = _timeExtra
		? std::max(_textWidth, timeWidth)
		: std::max(_textWidth, last + timeWidth);
	const auto w = std::max(inner, _author.maxWidth())
		+ padding.left()
		+ padding.right();
	const auto h = _textHeight
		+ _timeExtra
		+ author
		+ padding.top()
		+ padding.bottom();
	const auto margin = st::msgMargin;
	const auto left = _message.user
		? (width() - margin.right() - w)
		: margin.left();
	return QRect(left, st::msgMargin.top() / 2, w, h);
}

int Bubble::resizeGetHeight(int newWidth) {
	const auto &padding = st::msgPadding;
	const auto margin = st::msgMargin;
	const auto available = std::min(
		int(newWidth * 0.78),
		st::msgMaxWidth) - padding.left() - padding.right()
		- margin.left();
	const auto timeWidth = _time.isEmpty()
		? 0
		: (st::msgDateFont->width(_time) + style::ConvertScale(10));
	_textWidth = std::max(std::min(_text.maxWidth(), available), 1);
	_textHeight = _text.countHeight(_textWidth);
	const auto lines = _text.countLineWidths(_textWidth);
	const auto last = lines.empty() ? 0 : lines.back();
	_timeExtra = (timeWidth && (last + timeWidth > available))
		? st::msgDateFont->height
		: 0;
	const auto author = _author.isEmpty() ? 0 : st::msgNameFont->height;
	return _textHeight
		+ _timeExtra
		+ author
		+ padding.top()
		+ padding.bottom()
		+ st::msgMargin.top();
}

void Bubble::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto rect = bubbleRect();
	const auto out = _message.user;
	p.setPen(Qt::NoPen);
	p.setBrush(out ? st::msgOutBg : st::msgInBg);
	p.drawRoundedRect(rect, st::bubbleRadiusLarge, st::bubbleRadiusLarge);
	const auto &padding = st::msgPadding;
	auto top = rect.y() + padding.top();
	if (!_author.isEmpty()) {
		p.setPen(st::attentionButtonFg);
		_author.draw(p, {
			.position = QPoint(rect.x() + padding.left(), top),
			.availableWidth = rect.width() - padding.left() - padding.right(),
		});
		top += st::msgNameFont->height;
	}
	p.setPen(_typing
		? (out ? st::msgOutDateFg : st::msgInDateFg)
		: (out ? st::historyTextOutFg : st::historyTextInFg));
	_text.draw(p, {
		.position = QPoint(rect.x() + padding.left(), top),
		.availableWidth = _textWidth,
		.palette = out ? &st::outTextPalette : &st::inTextPalette,
	});
	if (!_time.isEmpty()) {
		p.setFont(st::msgDateFont);
		p.setPen(out ? st::msgOutDateFg : st::msgInDateFg);
		const auto timeWidth = st::msgDateFont->width(_time);
		p.drawText(
			rect.x() + rect.width() - padding.right() - timeWidth,
			rect.y() + rect.height() - padding.bottom()
				+ st::msgDateFont->ascent
				- st::msgDateFont->height
				+ style::ConvertScale(2),
			_time);
	}
}

void Bubble::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::RightButton || _typing) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	const auto text = _message.text;
	_menu->addAction(tr::lng_context_copy_text(tr::now), [=] {
		QGuiApplication::clipboard()->setText(text);
	}, &st::menuIconCopy);
	_menu->popup(e->globalPos());
}

// Widget

class AiChatMemento;

class AiChatWidget final : public Window::SectionWidget {
public:
	AiChatWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	bool hasTopBarShadow() const override {
		return true;
	}
	bool floatPlayerHandleWheelEvent(QEvent *e) override {
		return false;
	}
	QRect floatPlayerAvailableRect() override {
		return mapToGlobal(_scroll->geometry());
	}
	QPixmap grabForShowAnimation(
		const Window::SectionSlideParams &params) override;
	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	std::shared_ptr<Window::SectionMemento> createMemento() override;

	void setInternalState(const QRect &geometry) {
		setGeometry(geometry);
		Ui::SendPendingMoveResizeEvents(this);
	}

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void showFinishedHook() override {
		_field->setFocusFast();
	}
	void doSetInnerFocus() override {
		_field->setFocusFast();
	}

private:
	void setupTopBar();
	void setupComposer();
	void showMenu();
	void refresh();
	void send();
	void updateControlsGeometry();
	void scrollToBottom();

	std::shared_ptr<Ui::ChatTheme> _theme;
	object_ptr<Ui::RpWidget> _topBar;
	object_ptr<Ui::PlainShadow> _topBarShadow;
	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<Ui::VerticalLayout> _list;
	object_ptr<Ui::RpWidget> _composer;
	object_ptr<Ui::PlainShadow> _composerShadow;
	Ui::InputField *_field = nullptr;
	Ui::SendButton *_send = nullptr;
	Ui::IconButton *_menuToggle = nullptr;
	base::unique_qptr<Ui::PopupMenu> _menu;
	Ui::Text::String _title;
	Ui::Text::String _beta;

};

class AiChatMemento final : public Window::SectionMemento {
public:
	object_ptr<Window::SectionWidget> createWidget(
			QWidget *parent,
			not_null<Window::SessionController*> controller,
			Window::Column column,
			const QRect &geometry) override {
		if (column == Window::Column::Third) {
			return nullptr;
		}
		auto result = object_ptr<AiChatWidget>(parent, controller);
		result->setInternalState(geometry);
		return result;
	}

};

AiChatWidget::AiChatWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Window::SectionWidget(parent, controller)
, _theme(controller->defaultChatTheme())
, _topBar(this)
, _topBarShadow(this)
, _scroll(this, st::defaultSolidScroll)
, _composer(this)
, _composerShadow(this) {
	controller->setChatStyleTheme(_theme);
	_title.setText(st::semiboldTextStyle, tr::lng_extera_ai_chat(tr::now));
	_list = _scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(this));
	setupTopBar();
	setupComposer();
	refresh();
	Extera::AiChatUpdates(
	) | rpl::on_next([=] {
		refresh();
		_topBar->update();
	}, lifetime());
}

void AiChatWidget::setupTopBar() {
	_topBar->paintRequest() | rpl::on_next([=](QRect clip) {
		auto p = Painter(_topBar.data());
		auto hq = PainterHighQualityEnabler(p);
		p.fillRect(clip, st::topBarBg);
		const auto height = _topBar->height();
		const auto size = height - style::ConvertScale(14);
		const auto left = st::historyTopBarBack.width;
		const auto circle = QRect(left, (height - size) / 2, size, size);
		auto gradient = QLinearGradient(circle.topLeft(), circle.bottomRight());
		gradient.setColorAt(0., QColor(0x7B, 0x61, 0xFF));
		gradient.setColorAt(1., QColor(0x2F, 0xB8, 0xFF));
		p.setPen(Qt::NoPen);
		p.setBrush(gradient);
		p.drawEllipse(circle);
		auto letters = st::semiboldFont->f;
		letters.setPixelSize(size * 2 / 5);
		p.setFont(letters);
		p.setPen(QColor(255, 255, 255));
		p.drawText(circle, Qt::AlignCenter, u"AI"_q);
		const auto textLeft = circle.x() + size + style::ConvertScale(12);
		const auto nameTop = height / 2 - st::semiboldFont->height;
		p.setPen(st::dialogsNameFg);
		_title.draw(p, {
			.position = QPoint(textLeft, nameTop),
			.availableWidth = _topBar->width() - textLeft,
		});
		const auto chipLeft = textLeft
			+ _title.maxWidth()
			+ style::ConvertScale(6);
		const auto chipPadding = style::ConvertScale(6);
		const auto &chipFont = st::settingsPremiumNewBadge.style.font;
		const auto chipText = tr::lng_exd_beta(tr::now);
		const auto chip = QRect(
			chipLeft,
			nameTop + (st::semiboldFont->height - chipFont->height) / 2 - 1,
			chipFont->width(chipText) + chipPadding * 2,
			chipFont->height + 2);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgActive);
		p.drawRoundedRect(chip, chip.height() / 2., chip.height() / 2.);
		p.setPen(st::windowFgActive);
		p.setFont(chipFont);
		p.drawText(chip, Qt::AlignCenter, chipText);
		const auto waiting = Extera::AiChatWaiting();
		p.setFont(st::normalFont);
		p.setPen(waiting ? st::historyStatusFgTyping : st::historyStatusFg);
		p.drawText(
			textLeft,
			height / 2 + st::normalFont->ascent,
			waiting
				? tr::lng_exd_ai_typing(tr::now)
				: (tr::lng_status_bot(tr::now)
					+ QString::fromUtf8(" \xC2\xB7 ")
					+ Extera::AiChatModel()));
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
	_topBar->show();
	_topBarShadow->show();
}

void AiChatWidget::setupComposer() {
	_composer->paintRequest() | rpl::on_next([=](QRect clip) {
		auto p = QPainter(_composer.data());
		p.fillRect(clip, st::historyComposeAreaBg);
	}, _composer->lifetime());
	_field = Ui::CreateChild<Ui::InputField>(
		_composer.data(),
		st::historyComposeField,
		Ui::InputField::Mode::MultiLine,
		tr::lng_extera_ai_placeholder());
	_field->setMaxHeight(st::historyComposeFieldMaxHeight);
	_send = Ui::CreateChild<Ui::SendButton>(
		_composer.data(),
		st::historySend);
	_send->setClickedCallback([=] { send(); });
	_field->submits(
	) | rpl::on_next([=] { send(); }, _field->lifetime());
	_field->heightChanges(
	) | rpl::on_next([=] { updateControlsGeometry(); }, _field->lifetime());
	_composer->show();
	_composerShadow->show();
}

void AiChatWidget::send() {
	const auto text = _field->getLastText();
	if (Extera::AiChatSend(controller(), text)) {
		_field->clear();
	}
}

void AiChatWidget::showMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	_menu->addAction(tr::lng_extera_ai_clear(tr::now), [] {
		Extera::AiChatClear();
	}, &st::menuIconDelete);
	_menu->addAction(tr::lng_extera_ai_key(tr::now), [=] {
		Extera::ShowKeyBox(controller());
	}, &st::menuIconPremium);
	_menu->addAction(tr::lng_menu_settings(tr::now), [=] {
		controller()->showSettings(Settings::ExteraAiChatId());
	}, &st::menuIconSettings);
	_menu->popup(QCursor::pos());
}

void AiChatWidget::refresh() {
	if (!_list) {
		return;
	}
	const auto atBottom = (_scroll->scrollTop() >= _scroll->scrollTopMax());
	_list->clear();
	const auto history = Extera::AiChatHistory();
	if (history.empty()) {
		const auto empty = _list->add(object_ptr<Ui::RpWidget>(_list));
		empty->resize(empty->width(), style::ConvertScale(48));
		empty->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(empty);
			auto hq = PainterHighQualityEnabler(p);
			const auto text = tr::lng_extera_ai_empty(tr::now);
			const auto &font = st::msgServiceFont;
			const auto w = font->width(text) + style::ConvertScale(24);
			const auto h = font->height + style::ConvertScale(10);
			const auto rect = QRect(
				(empty->width() - w) / 2,
				(empty->height() - h) / 2,
				w,
				h);
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgServiceBg);
			p.drawRoundedRect(rect, h / 2., h / 2.);
			p.setPen(st::msgServiceFg);
			p.setFont(font);
			p.drawText(rect, Qt::AlignCenter, text);
		}, empty->lifetime());
	}
	_list->add(object_ptr<Ui::FixedHeightWidget>(
		_list,
		st::msgMargin.top()));
	for (const auto &message : history) {
		_list->add(object_ptr<Bubble>(_list, message, false));
	}
	if (Extera::AiChatWaiting()) {
		const auto partial = Extera::AiChatPartial();
		_list->add(object_ptr<Bubble>(
			_list,
			Extera::AiChatMessage{
				.text = partial.isEmpty()
					? QString::fromUtf8("\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2")
					: partial,
			},
			partial.isEmpty()));
	}
	_list->resizeToWidth(_scroll->width());
	_list->showChildren();
	if (atBottom || Extera::AiChatWaiting()) {
		scrollToBottom();
	}
}

void AiChatWidget::scrollToBottom() {
	crl::on_main(this, [=] {
		_scroll->scrollToY(_scroll->scrollTopMax());
	});
}

QPixmap AiChatWidget::grabForShowAnimation(
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

bool AiChatWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	return (dynamic_cast<AiChatMemento*>(memento.get()) != nullptr);
}

std::shared_ptr<Window::SectionMemento> AiChatWidget::createMemento() {
	return std::make_shared<AiChatMemento>();
}

void AiChatWidget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}
	controller()->adaptive().setChatLayout(
		(width() < st::adaptiveChatWideWidth)
			? Window::Adaptive::ChatLayout::Normal
			: Window::Adaptive::ChatLayout::Wide);
	updateControlsGeometry();
}

void AiChatWidget::updateControlsGeometry() {
	const auto w = width();
	_topBar->setGeometry(0, 0, w, st::topBarHeight);
	_menuToggle->moveToRight(0, 0, w);
	_topBarShadow->setGeometry(0, st::topBarHeight, w, st::lineWidth);
	const auto sendWidth = _send->width();
	const auto padding = st::historySendPadding;
	const auto left = style::ConvertScale(16);
	const auto fieldWidth = w - sendWidth - left;
	_field->resizeToWidth(fieldWidth);
	const auto composerHeight = std::max(
		_field->height() + padding * 2,
		_send->height());
	_composer->setGeometry(0, height() - composerHeight, w, composerHeight);
	_field->moveToLeft(left, (composerHeight - _field->height()) / 2);
	_send->moveToRight(0, composerHeight - _send->height());
	_composerShadow->setGeometry(
		0,
		height() - composerHeight - st::lineWidth,
		w,
		st::lineWidth);
	const auto top = st::topBarHeight;
	const auto scrollHeight = height() - top - composerHeight;
	const auto atBottom = (_scroll->scrollTop() >= _scroll->scrollTopMax());
	_scroll->setGeometry(0, top, w, scrollHeight);
	if (_list) {
		_list->resizeToWidth(w);
	}
	_topBar->raise();
	_topBarShadow->raise();
	_composer->raise();
	_composerShadow->raise();
	if (atBottom) {
		scrollToBottom();
	}
}

void AiChatWidget::paintEvent(QPaintEvent *e) {
	if (animatingShow()) {
		SectionWidget::paintEvent(e);
		return;
	} else if (controller()->contentOverlapped(this, e)) {
		return;
	}
	SectionWidget::PaintBackground(controller(), _theme.get(), this, e->rect());
}

} // namespace

void ShowAiChatSection(not_null<Window::SessionController*> controller) {
	controller->showSection(std::make_shared<AiChatMemento>());
}

} // namespace Exd
