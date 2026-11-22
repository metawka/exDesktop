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
#include "ui/style/style_core.h"
#include "ui/toast/toast.h"
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

[[nodiscard]] QColor Mix(QColor a, QColor b, float64 k) {
	return QColor(
		anim::interpolate(a.red(), b.red(), k),
		anim::interpolate(a.green(), b.green(), k),
		anim::interpolate(a.blue(), b.blue(), k));
}

[[nodiscard]] QColor CardColor() {
	return Mix(st::windowBg->c, st::windowFg->c, 0.055);
}

[[nodiscard]] QColor TonalColor(float64 amount = 0.22) {
	return Mix(st::windowBg->c, st::windowBgActive->c, amount);
}

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
	[[nodiscard]] QRect copyRect() const;
	[[nodiscard]] QMargins bubblePadding() const;
	[[nodiscard]] QMargins bubbleMargin(int width) const;
	[[nodiscard]] bool hasCopy() const;
	[[nodiscard]] int copyExtra() const;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	const Extera::AiChatMessage _message;
	bool _copyOver = false;
	const bool _typing = false;
	Ui::Text::String _text;
	Ui::Text::String _author;
	QString _time;
	int _textWidth = 0;
	int _textHeight = 0;
	int _lastLineWidth = 0;
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
	setMouseTracking(true);
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
	if (_message.date && !_typing && !style::Md3Enabled()) {
		_time = QLocale().toString(
			base::unixtime::parse(_message.date).time(),
			QLocale::ShortFormat);
	}
}

QRect Bubble::bubbleRect() const {
	const auto padding = bubblePadding();
	const auto author = _author.isEmpty() ? 0 : st::msgNameFont->height;
	const auto timeWidth = _time.isEmpty()
		? 0
		: (st::msgDateFont->width(_time) + style::ConvertScale(10));
	const auto last = _lastLineWidth;
	const auto inner = _timeExtra
		? std::max(_textWidth, timeWidth)
		: std::max(_textWidth, last + timeWidth);
	const auto copyWidth = hasCopy() ? copyRect().width() : 0;
	const auto w = std::max({ inner, _author.maxWidth(), copyWidth })
		+ padding.left()
		+ padding.right();
	const auto h = _textHeight
		+ _timeExtra
		+ author
		+ copyExtra()
		+ padding.top()
		+ padding.bottom();
	const auto margin = bubbleMargin(width());
	const auto left = _message.user
		? (width() - margin.right() - w)
		: margin.left();
	return QRect(left, margin.top() / 2, w, h);
}

QMargins Bubble::bubblePadding() const {
	if (!style::Md3Enabled()) {
		return st::msgPadding;
	}
	return _message.user
		? st::exdAiUserPadding
		: st::exdAiAnswerPadding;
}

QMargins Bubble::bubbleMargin(int width) const {
	if (!style::Md3Enabled()) {
		return st::msgMargin;
	}
	const auto side = std::max(
		st::exdAiColumnMinSide,
		(width - st::exdAiColumnMax) / 2);
	return QMargins(side, st::exdAiBubbleSkip, side, 0);
}

bool Bubble::hasCopy() const {
	return style::Md3Enabled()
		&& !_message.user
		&& !_typing
		&& !_message.error
		&& !_message.text.isEmpty();
}

int Bubble::copyExtra() const {
	return hasCopy()
		? (st::exdAiCopyHeight + st::exdAiCopySkip)
		: 0;
}

QRect Bubble::copyRect() const {
	const auto &font = st::normalFont;
	const auto &icon = st::menuIconCopy;
	const auto padding = st::exdAiCopyPadding;
	const auto copyWidth = padding
		+ icon.width()
		+ st::exdAiCopyIconSkip
		+ font->width(tr::lng_context_copy_text(tr::now))
		+ padding;
	const auto author = _author.isEmpty() ? 0 : st::msgNameFont->height;
	const auto msg = bubblePadding();
	const auto margin = bubbleMargin(width());
	return QRect(
		margin.left() + msg.left(),
		margin.top() / 2
			+ msg.top()
			+ author
			+ _textHeight
			+ _timeExtra
			+ st::exdAiCopySkip,
		copyWidth,
		st::exdAiCopyHeight);
}

void Bubble::mouseMoveEvent(QMouseEvent *e) {
	const auto over = hasCopy() && copyRect().contains(e->pos());
	if (_copyOver != over) {
		_copyOver = over;
		setCursor(over ? style::cur_pointer : style::cur_default);
		update();
	}
}

void Bubble::leaveEventHook(QEvent *e) {
	if (_copyOver) {
		_copyOver = false;
		setCursor(style::cur_default);
		update();
	}
}

int Bubble::resizeGetHeight(int newWidth) {
	const auto padding = bubblePadding();
	const auto margin = bubbleMargin(newWidth);
	const auto column = newWidth - margin.left() - margin.right();
	const auto available = style::Md3Enabled()
		? (int(column * (_message.user ? 0.7 : 0.8))
			- padding.left()
			- padding.right())
		: (std::min(
			int(newWidth * 0.78),
			st::msgMaxWidth) - padding.left() - padding.right()
			- margin.left());
	const auto timeWidth = _time.isEmpty()
		? 0
		: (st::msgDateFont->width(_time) + style::ConvertScale(10));
	_textWidth = std::max(std::min(_text.maxWidth(), available), 1);
	const auto dimensions = _text.countDimensions(
		Ui::Text::SimpleGeometry(_textWidth, 0, 0, false),
		{ .lineWidths = true });
	_textHeight = dimensions.height;
	const auto last = dimensions.lineWidths.empty()
		? 0
		: dimensions.lineWidths.back();
	_lastLineWidth = last;
	_timeExtra = (timeWidth && (last + timeWidth > available))
		? st::msgDateFont->height
		: 0;
	const auto author = _author.isEmpty() ? 0 : st::msgNameFont->height;
	return _textHeight
		+ _timeExtra
		+ author
		+ copyExtra()
		+ padding.top()
		+ padding.bottom()
		+ margin.top();
}

void Bubble::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto rect = bubbleRect();
	const auto out = _message.user;
	const auto md3 = style::Md3Enabled();
	p.setPen(Qt::NoPen);
	if (md3) {
		p.setBrush(out ? TonalColor() : CardColor());
		p.drawRoundedRect(rect, st::exdAiBubbleRadius, st::exdAiBubbleRadius);
	} else {
		p.setBrush(out ? st::msgOutBg : st::msgInBg);
		p.drawRoundedRect(rect, st::bubbleRadiusLarge, st::bubbleRadiusLarge);
	}
	const auto padding = bubblePadding();
	auto top = rect.y() + padding.top();
	if (!_author.isEmpty()) {
		p.setPen(st::attentionButtonFg);
		_author.draw(p, {
			.position = QPoint(rect.x() + padding.left(), top),
			.availableWidth = rect.width() - padding.left() - padding.right(),
		});
		top += st::msgNameFont->height;
	}
	if (md3) {
		p.setPen(_typing ? st::windowSubTextFg : st::windowFg);
	} else {
		p.setPen(_typing
			? (out ? st::msgOutDateFg : st::msgInDateFg)
			: (out ? st::historyTextOutFg : st::historyTextInFg));
	}
	_text.draw(p, {
		.position = QPoint(rect.x() + padding.left(), top),
		.availableWidth = _textWidth,
		.palette = (out && !md3) ? &st::outTextPalette : &st::inTextPalette,
	});
	if (hasCopy()) {
		const auto copy = copyRect();
		p.setPen(Qt::NoPen);
		p.setBrush(TonalColor(_copyOver ? 0.3 : 0.22));
		p.drawRoundedRect(copy, copy.height() / 2., copy.height() / 2.);
		const auto &icon = st::menuIconCopy;
		icon.paint(
			p,
			copy.x() + st::exdAiCopyPadding,
			copy.y() + (copy.height() - icon.height()) / 2,
			width(),
			st::windowFg->c);
		p.setPen(st::windowFg);
		p.setFont(st::normalFont);
		p.drawText(
			copy.x() + st::exdAiCopyPadding + icon.width() + st::exdAiCopyIconSkip,
			copy.y() + (copy.height() - st::normalFont->height) / 2
				+ st::normalFont->ascent,
			tr::lng_context_copy_text(tr::now));
	}
	if (!_time.isEmpty()) {
		p.setFont(st::msgDateFont);
		if (md3) {
			p.setPen(st::windowSubTextFg);
		} else {
			p.setPen(out ? st::msgOutDateFg : st::msgInDateFg);
		}
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
	if (e->button() == Qt::LeftButton
		&& hasCopy()
		&& copyRect().contains(e->pos())) {
		QGuiApplication::clipboard()->setText(_message.text);
		Ui::Toast::Show(window(), tr::lng_text_copied(tr::now));
		return;
	}
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
	Ui::IconButton *_exdClear = nullptr;
	Ui::IconButton *_exdBack = nullptr;
	Ui::IconButton *_exdSettings = nullptr;
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
	crl::on_main(this, [=] {
		controller->closeThirdSection();
	});
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
		const auto md3 = style::Md3Enabled();
		if (md3) {
			const auto plaque = QRectF(_topBar->rect().marginsRemoved(
				st::exdTopBarPlaqueMargins));
			const auto radius = float64(st::exdTopBarPlaqueRadius);
			auto path = QPainterPath();
			path.addRoundedRect(plaque, radius, radius);
			p.fillPath(path, st::topBarBg);
			const auto textLeft = (_exdBack && !_exdBack->isHidden())
				? (_exdBack->x() + _exdBack->width())
				: (int(plaque.x()) + st::exdAiTitleLeft);
			const auto &font = st::exdAiTitleFont;
			const auto nameTop = int(plaque.y())
				+ (int(plaque.height()) - font->height) / 2;
			p.setPen(st::windowBoldFg);
			p.setFont(font);
			const auto title = tr::lng_extera_ai_chat(tr::now);
			p.drawText(textLeft, nameTop + font->ascent, title);
			const auto &chipFont = st::exdAiChipFont;
			const auto chipPadding = style::ConvertScale(6);
			const auto chipText = tr::lng_exd_beta(tr::now);
			const auto chip = QRect(
				textLeft + font->width(title) + style::ConvertScale(8),
				nameTop + (font->height - chipFont->height - 4) / 2,
				chipFont->width(chipText) + chipPadding * 2,
				chipFont->height + 4);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgActive);
			const auto r = style::ConvertScale(6);
			p.drawRoundedRect(chip, r, r);
			p.setPen(st::windowBg);
			p.setFont(chipFont);
			p.drawText(chip, Qt::AlignCenter, chipText);
			return;
		} else {
			p.fillRect(clip, st::topBarBg);
		}
		const auto height = _topBar->height();
		const auto size = height - style::ConvertScale(md3 ? 20 : 14);
		const auto left = st::historyTopBarBack.width
			+ (md3 ? st::exdTopBarPlaqueMargins.left() : 0);
		const auto circle = QRect(left, (height - size) / 2, size, size);
		auto gradient = QLinearGradient(circle.topLeft(), circle.bottomRight());
		gradient.setColorAt(0., QColor(0x7B, 0x61, 0xFF));
		gradient.setColorAt(1., QColor(0x2F, 0xB8, 0xFF));
		p.setPen(Qt::NoPen);
		if (md3) {
			p.setBrush(TonalColor());
		} else {
			p.setBrush(gradient);
		}
		p.drawEllipse(circle);
		auto letters = st::semiboldFont->f;
		letters.setPixelSize(size * 2 / 5);
		p.setFont(letters);
		p.setPen(md3 ? st::windowBgActive->c : QColor(255, 255, 255));
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
		if (md3) {
			const auto r = style::ConvertScale(6);
			p.drawRoundedRect(chip, r, r);
			p.setPen(st::windowBg);
		} else {
			p.drawRoundedRect(chip, chip.height() / 2., chip.height() / 2.);
			p.setPen(st::windowFgActive);
		}
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
	back->moveToLeft(
		style::Md3Enabled() ? st::exdTopBarPlaqueMargins.left() : 0,
		0);
	_exdBack = back;
	if (style::Md3Enabled()) {
		controller()->adaptive().value(
		) | rpl::on_next([=] {
			back->setVisible(controller()->adaptive().isOneColumn());
			_topBar->update();
		}, back->lifetime());
	}
	_menuToggle = Ui::CreateChild<Ui::IconButton>(
		_topBar.data(),
		st::topBarMenuToggle);
	_menuToggle->setClickedCallback([=] { showMenu(); });
	if (style::Md3Enabled()) {
		_menuToggle->hide();
		_exdClear = Ui::CreateChild<Ui::IconButton>(
			_topBar.data(),
			st::exdAiClear);
		_exdClear->setAccessibleName(tr::lng_extera_ai_clear(tr::now));
		_exdClear->setClickedCallback([] { Extera::AiChatClear(); });
		_exdSettings = Ui::CreateChild<Ui::IconButton>(
			_topBar.data(),
			st::exdFeedSettings);
		_exdSettings->setAccessibleName(tr::lng_menu_settings(tr::now));
		_exdSettings->setClickedCallback([=] {
			controller()->showSettings(Settings::ExteraAiChatId());
		});
	}
	_topBar->show();
	_topBarShadow->show();
}

void AiChatWidget::setupComposer() {
	_composer->paintRequest() | rpl::on_next([=](QRect clip) {
		auto p = QPainter(_composer.data());
		if (style::Md3Enabled()) {
			auto hq = PainterHighQualityEnabler(p);
			const auto side = std::max(
				st::exdAiColumnMinSide,
				(_composer->width() - st::exdAiColumnMax) / 2);
			const auto pill = QRect(
				side,
				0,
				_send->x() - st::exdAiComposerSkip - side,
				_composer->height() - st::exdAiComposerBottom);
			const auto radius = pill.height() / 2.;
			p.setPen(Qt::NoPen);
			p.setBrush(CardColor());
			p.drawRoundedRect(pill, radius, radius);
			return;
		}
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
	if (style::Md3Enabled()) {
		_composerShadow->hide();
	}
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
	if (Extera::AiChatWaiting()
		&& Extera::AiChatPartial().isEmpty()
		&& style::Md3Enabled()) {
		const auto typing = _list->add(object_ptr<Ui::RpWidget>(_list));
		typing->resize(
			typing->width(),
			st::normalFont->height + st::exdAiBubbleSkip * 2);
		typing->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(typing);
			const auto side = std::max(
				st::exdAiColumnMinSide,
				(typing->width() - st::exdAiColumnMax) / 2);
			p.setPen(st::windowSubTextFg);
			p.setFont(st::normalFont);
			p.drawText(
				side,
				st::exdAiBubbleSkip + st::normalFont->ascent,
				tr::lng_exd_ai_typing(tr::now));
		}, typing->lifetime());
	} else if (Extera::AiChatWaiting()) {
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
	const auto md3 = style::Md3Enabled();
	_topBar->setGeometry(0, 0, w, st::topBarHeight);
	_menuToggle->moveToRight(0, 0, w);
	if (_exdSettings) {
		_exdSettings->moveToRight(st::exdTopBarPlaqueMargins.right(), 0, w);
		_exdClear->moveToRight(
			st::exdTopBarPlaqueMargins.right() + _exdSettings->width(),
			0,
			w);
	}
	_topBarShadow->setGeometry(
		0,
		st::topBarHeight,
		w,
		md3 ? 0 : st::lineWidth);
	const auto sendWidth = _send->width();
	const auto padding = st::historySendPadding;
	const auto side = md3
		? std::max(st::exdAiColumnMinSide, (w - st::exdAiColumnMax) / 2)
		: 0;
	const auto left = md3
		? (side + st::exdAiComposerInner)
		: style::ConvertScale(16);
	const auto sendRight = md3 ? side : 0;
	const auto fieldWidth = w - sendWidth - left - sendRight
		- (md3 ? (st::exdAiComposerSkip + st::exdAiComposerInner) : 0);
	_field->resizeToWidth(fieldWidth);
	const auto composerHeight = md3
		? (std::max(
			_field->height() + padding * 2,
			st::exdAiComposerHeight) + st::exdAiComposerBottom)
		: std::max(_field->height() + padding * 2, _send->height());
	_composer->setGeometry(0, height() - composerHeight, w, composerHeight);
	const auto fieldArea = md3
		? (composerHeight - st::exdAiComposerBottom)
		: composerHeight;
	const auto fieldTop = 0;
	const auto lineHeight = st::historyComposeField.style.font->height;
	const auto singleLine = (_field->height() <= st::historyComposeField.heightMin);
	const auto fieldY = (md3 && singleLine)
		? (fieldTop + (fieldArea - lineHeight) / 2)
		: (fieldTop + (fieldArea - _field->height()) / 2);
	_field->moveToLeft(left, fieldY);
	_send->moveToRight(
		sendRight,
		md3
			? (fieldArea - (st::exdAiComposerHeight + _send->height()) / 2)
			: (fieldTop + fieldArea - _send->height()),
		w);
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
	if (md3) {
		_composerShadow->hide();
	}
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
	if (style::Md3Enabled()) {
		auto p = QPainter(this);
		p.fillRect(e->rect(), st::windowBg);
		return;
	}
	SectionWidget::PaintBackground(controller(), _theme.get(), this, e->rect());
}

} // namespace

void ShowAiChatSection(not_null<Window::SessionController*> controller) {
	controller->showSection(std::make_shared<AiChatMemento>());
}

} // namespace Exd
