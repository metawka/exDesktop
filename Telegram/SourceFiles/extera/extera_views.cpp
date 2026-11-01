#include "extera/extera_views.h"

#include "core/application.h"
#include "extera/extera_plugins.h"
#include "ui/layers/generic_box.h"
#include "ui/rp_widget.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/palette.h"
#include "styles/style_layers.h"
#include "styles/style_window.h"

#include <QtWidgets/QScrollBar>

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTimer>
#include <QtGui/QFontMetrics>
#include <QtGui/QGuiApplication>
#include <QtGui/QPainter>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QVBoxLayout>

#include <QGraphicsOpacityEffect>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>

namespace Extera::Views {
namespace {

class SheetHolder : public Ui::RpWidget {
public:
	using Ui::RpWidget::RpWidget;

	void setContent(not_null<QWidget*> widget) {
		content = widget;
		widget->setParent(this);
		widget->show();
	}

	void remeasure() {
		resizeToWidth(widthNoMargins());
	}

protected:
	int resizeGetHeight(int newWidth) override {
		if (!content) {
			return heightNoMargins();
		}
		const auto height = content->hasHeightForWidth()
			? int(content->heightForWidth(newWidth))
			: content->sizeHint().height();
		content->resize(newWidth, std::max(height, 0));
		LOG(("PluginViews: sheet size %1x%2").arg(newWidth).arg(content->height()));
		return content->height();
	}

private:
	QPointer<QWidget> content;

};

struct SheetEntry {
	QPointer<QWidget> root;
	QPointer<SheetHolder> holder;
	Fn<void()> close;
};

std::map<int64, SheetEntry> Sheets;
std::map<int64, QPointer<QWidget>> Widgets;
QMutex CanvasMutex;
std::map<int64, QImage> Canvases;
QMutex MetricsMutex;
double DpScale = 0.;
int WidthPx = 0;
int HeightPx = 0;

[[nodiscard]] Window::SessionController *ActiveController() {
	if (const auto window = Core::App().activePrimaryWindow()) {
		return window->sessionController();
	}
	return nullptr;
}

class ClickLabel : public QLabel {
public:
	using QLabel::QLabel;

	Fn<void()> handler;

protected:
	void mouseReleaseEvent(QMouseEvent *event) override {
		if (rect().contains(event->pos()) && handler) {
			handler();
		}
		QLabel::mouseReleaseEvent(event);
	}

};

[[nodiscard]] QColor FromArgb(int64 value) {
	const auto argb = uint32(int32(value));
	return QColor(
		int((argb >> 16) & 0xFF),
		int((argb >> 8) & 0xFF),
		int(argb & 0xFF),
		int((argb >> 24) & 0xFF));
}

[[nodiscard]] int SheetWidth() {
	return std::clamp(
		WidthPx - int(st::boxPadding.left() + st::boxPadding.right()),
		360,
		840);
}

[[nodiscard]] Qt::Alignment AlignmentFromGravity(int gravity) {
	auto result = Qt::Alignment();
	if (gravity & 0x1) result |= Qt::AlignHCenter;
	if (gravity & 0x2) result |= Qt::AlignLeft;
	if (gravity & 0x4) result |= Qt::AlignRight;
	if (gravity & 0x10) result |= Qt::AlignVCenter;
	if (gravity & 0x20) result |= Qt::AlignTop;
	if (gravity & 0x40) result |= Qt::AlignBottom;
	if (gravity & 0x200) result |= Qt::AlignRight;
	if (!result) result = Qt::AlignLeft | Qt::AlignVCenter;
	return result;
}

void ApplySize(QWidget *widget, const QJsonObject &params) {
	const auto w = params.contains(u"w"_q) ? params.value(u"w"_q).toInt() : -2;
	const auto h = params.contains(u"h"_q) ? params.value(u"h"_q).toInt() : -2;
	if (w > 0) {
		widget->setFixedWidth(std::min(w, SheetWidth()));
	} else if (w == -1) {
		widget->setSizePolicy(QSizePolicy::Expanding, widget->sizePolicy().verticalPolicy());
	}
	if (h > 0) {
		widget->setFixedHeight(h);
	} else if (h == -1) {
		widget->setSizePolicy(widget->sizePolicy().horizontalPolicy(), QSizePolicy::Expanding);
	}
}

void ApplyBackground(QWidget *widget, const QJsonObject &data) {
	if (data.isEmpty()) {
		return;
	}
	const auto kind = data.value(u"kind"_q).toString();
	if (kind == u"bitmap"_q) {
		return;
	}
	auto parts = QStringList();
	if (data.contains(u"color"_q)) {
		parts << u"background-color: %1"_q
			.arg(FromArgb(data.value(u"color"_q).toInteger()).name(QColor::HexArgb));
	}
	if (data.contains(u"radius"_q) && data.value(u"radius"_q).toDouble() > 0.) {
		parts << u"border-radius: %1px"_q.arg(
			int(data.value(u"radius"_q).toDouble()));
	}
	const auto stroke = data.value(u"stroke"_q).toArray();
	if (stroke.size() == 2) {
		parts << u"border: %1px solid %2"_q
			.arg(int(stroke[0].toDouble()))
			.arg(FromArgb(stroke[1].toInteger()).name(QColor::HexArgb));
	}
	widget->setStyleSheet(parts.join(u"; "_q));
}

[[nodiscard]] QWidget *BuildNode(
	const QJsonObject &node,
	QWidget *parent);

void BuildChildren(
		const QJsonObject &node,
		not_null<QBoxLayout*> layout,
		not_null<QWidget*> parent,
		bool frame) {
	const auto children = node.value(u"children"_q).toArray();
	for (const auto &value : children) {
		const auto entry = value.toObject();
		const auto params = entry.value(u"params"_q).toObject();
		auto child = BuildNode(entry.value(u"view"_q).toObject(), parent);
		if (!child) {
			continue;
		}
		ApplySize(child, params);
		const auto gravity = params.value(u"gravity"_q);
		const auto margins = params.value(u"margins"_q).toArray();
		auto wrapped = child;
		if (margins.size() == 4
			&& (margins[0].toInt() || margins[1].toInt()
				|| margins[2].toInt() || margins[3].toInt())) {
			auto holder = new QWidget(parent);
			holder->setAttribute(Qt::WA_TransparentForMouseEvents);
			auto inner = new QVBoxLayout(holder);
			inner->setContentsMargins(
				margins[0].toInt(), margins[1].toInt(),
				margins[2].toInt(), margins[3].toInt());
			inner->setSpacing(0);
			inner->addWidget(child);
			child->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
			ApplySize(holder, params);
			wrapped = holder;
		}
		if (frame && gravity.isDouble()) {
			layout->addWidget(wrapped, 0, AlignmentFromGravity(gravity.toInt()));
		} else if (!frame && node.contains(u"child_gravity"_q)) {
			layout->addWidget(wrapped, 0, AlignmentFromGravity(
				node.value(u"child_gravity"_q).toInt()));
		} else {
			layout->addWidget(wrapped);
		}
	}
}

[[nodiscard]] QBoxLayout *MakeLayout(const QJsonObject &node) {
	const auto vertical = node.value(u"orientation"_q).toInt(0) == 0;
	auto layout = vertical
		? static_cast<QBoxLayout*>(new QVBoxLayout())
		: static_cast<QBoxLayout*>(new QHBoxLayout());
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	return layout;
}

void ApplyCommon(QWidget *widget, const QJsonObject &node) {
	Widgets[int64(node.value(u"id"_q).toDouble())] = widget;
	const auto padding = node.value(u"padding"_q).toArray();
	if (padding.size() == 4 && widget->layout()) {
		widget->layout()->setContentsMargins(
			padding[0].toInt(), padding[1].toInt(),
			padding[2].toInt(), padding[3].toInt());
	}
	ApplyBackground(widget, node.value(u"bg"_q).toObject());
	const auto visibility = node.value(u"visibility"_q).toInt(0);
	if (widget->parentWidget()) {
		widget->setVisible(visibility == 0);
	} else if (visibility != 0) {
		widget->hide();
	}
}

[[nodiscard]] QPixmap CanvasPixmap(int64 id) {
	{
		QMutexLocker lock(&CanvasMutex);
		const auto it = Canvases.find(id);
		if (it != Canvases.end()) {
			return QPixmap::fromImage(it->second);
		}
	}
	return QPixmap();
}

[[nodiscard]] QWidget *BuildNode(
		const QJsonObject &node,
		QWidget *parent) {
	const auto id = int64(node.value(u"id"_q).toDouble());
	const auto type = node.value(u"type"_q).toString();
	LOG(("PluginViews: build type=%1 id=%2 children=%3").arg(
		type).arg(id).arg(node.value(u"children"_q).toArray().size()));
	if (type == u"scrollview"_q) {
		auto area = new Ui::ScrollArea(parent);
		const auto inner = BuildNode(
			node.value(u"children"_q).toArray().at(0)
				.toObject().value(u"view"_q).toObject(),
			nullptr);
		if (inner) {
			area->setWidget(inner);
			area->setWidgetResizable(true);
		}
		area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		ApplyCommon(area, node);
		Widgets[id] = area;
		return area;
	}
	if (type == u"textview"_q || type == u"edittext"_q) {
		if (type == u"edittext"_q) {
			auto edit = new QLineEdit(parent);
			edit->setText(node.value(u"text"_q).toString());
			edit->setPlaceholderText(node.value(u"hint"_q).toString());
		QObject::connect(edit, &QLineEdit::textChanged, [=](const QString &text) {
			Extera::Plugins::Call(u"text_changed"_q, QJsonObject{
				{ u"id"_q, double(id) },
				{ u"text"_q, text },
			});
		});
			ApplySize(edit, QJsonObject());
			ApplyBackground(edit, node.value(u"bg"_q).toObject());
			Widgets[id] = edit;
			return edit;
		}
		auto label = new ClickLabel(parent);
		label->setTextFormat(Qt::PlainText);
		label->setText(node.value(u"text"_q).toString());
		const auto single = node.value(u"single_line"_q).toBool(false);
		label->setWordWrap(!single);
		auto font = label->font();
		const auto size = node.value(u"size"_q);
		if (size.isDouble()) {
			font.setPixelSize(size.toInt());
		}
		font.setBold(node.value(u"bold"_q).toBool(false));
		label->setFont(font);
		const auto color = node.value(u"color"_q);
		if (color.isDouble()) {
			label->setStyleSheet(u"color: %1"_q
				.arg(FromArgb(color.toInteger()).name(QColor::HexArgb)));
		}
		const auto gravity = node.value(u"gravity"_q);
		if (gravity.isDouble()) {
			label->setAlignment(AlignmentFromGravity(gravity.toInt()));
		}
		label->handler = [=] {
			Extera::Plugins::Call(u"view_click"_q, QJsonObject{
				{ u"id"_q, double(id) },
			});
		};
		ApplySize(label, QJsonObject());
		Widgets[id] = label;
		return label;
	}
	if (type == u"imageview"_q) {
		auto label = new QLabel(parent);
		const auto bitmap = node.value(u"bitmap"_q);
		if (bitmap.isDouble()) {
			label->setPixmap(CanvasPixmap(int64(bitmap.toDouble())));
		}
		ApplySize(label, QJsonObject());
		Widgets[id] = label;
		return label;
	}
	auto widget = new QWidget(parent);
	widget->setAttribute(Qt::WA_TransparentForMouseEvents, false);
	auto layout = MakeLayout(node);
	const auto frame = type == u"framelayout"_q;
	BuildChildren(node, layout, widget, frame);
	widget->setLayout(layout);
	ApplyCommon(widget, node);
	Widgets[id] = widget;
	return widget;
}

void RebuildChildren(QWidget *widget, const QJsonObject &payload) {
	const auto children = payload.value(u"children"_q).toArray();
	for (const auto &value : children) {
		const auto view = value.toObject().value(u"view"_q).toObject();
		const auto id = int64(view.value(u"id"_q).toDouble());
		const auto it = Widgets.find(id);
		if (it != Widgets.end() && it->second) {
			it->second->deleteLater();
			Widgets.erase(it);
		}
	}
	delete widget->layout();
	const auto leftovers = widget->findChildren<QWidget*>(
		QString(),
		Qt::FindDirectChildrenOnly);
	for (auto child : leftovers) {
		child->deleteLater();
	}
	auto layout = new QVBoxLayout(widget);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	for (const auto &value : children) {
		const auto entry = value.toObject();
		auto child = BuildNode(entry.value(u"view"_q).toObject(), widget);
		if (child) {
			ApplySize(child, entry.value(u"params"_q).toObject());
			layout->addWidget(child);
		}
	}
}

void RunAnimation(QWidget *widget, const QJsonObject &op) {
	auto group = new QParallelAnimationGroup(widget);
	auto effect = widget->graphicsEffect();
	if (op.contains(u"alpha"_q)) {
		if (!effect || !qobject_cast<QGraphicsOpacityEffect*>(effect)) {
			effect = new QGraphicsOpacityEffect(widget);
			widget->setGraphicsEffect(effect);
		}
		auto fade = new QPropertyAnimation(effect, "opacity", group);
		fade->setDuration(int(op.value(u"duration"_q).toInt()));
		fade->setStartValue(qobject_cast<QGraphicsOpacityEffect*>(effect)->opacity());
		fade->setEndValue(op.value(u"alpha"_q).toDouble());
		fade->setEasingCurve(QEasingCurve(QEasingCurve::OutQuad));
		group->addAnimation(fade);
	}
	if (op.contains(u"tx"_q)) {
		auto shift = new QPropertyAnimation(widget, "pos", group);
		shift->setDuration(int(op.value(u"duration"_q).toInt()));
		shift->setStartValue(widget->pos());
		shift->setEndValue(widget->pos()
			+ QPoint(int(op.value(u"tx"_q).toDouble()), 0));
		shift->setEasingCurve(QEasingCurve(QEasingCurve::OutQuad));
		group->addAnimation(shift);
	}
	if (op.contains(u"ty"_q)) {
		auto shift = new QPropertyAnimation(widget, "pos", group);
		shift->setDuration(int(op.value(u"duration"_q).toInt()));
		shift->setStartValue(widget->pos());
		shift->setEndValue(widget->pos()
			+ QPoint(0, int(op.value(u"ty"_q).toDouble())));
		shift->setEasingCurve(QEasingCurve(QEasingCurve::OutQuad));
		group->addAnimation(shift);
	}
	group->start(QAbstractAnimation::DeleteWhenStopped);
}

void ApplyUpdate(const QJsonObject &payload) {
	const auto id = int64(payload.value(u"id"_q).toDouble());
	LOG(("PluginViews: op=%1 id=%2").arg(
		payload.value(u"op"_q).toString()).arg(id));
	const auto op = payload.value(u"op"_q).toString();
	if (op == u"add_view"_q) {
		const auto parentIt = Widgets.find(
			int64(payload.value(u"parent"_q).toDouble()));
		if (parentIt == Widgets.end() || !parentIt->second) {
			return;
		}
		const auto parentWidget = parentIt->second;
		if (auto layout = qobject_cast<QBoxLayout*>(parentWidget->layout())) {
			auto child = BuildNode(payload.value(u"view"_q).toObject(), parentWidget);
			if (child) {
				const auto index = payload.value(u"index"_q).toInt(layout->count());
				layout->insertWidget(std::clamp(index, 0, layout->count()), child);
			}
		}
		return;
	}
	auto it = Widgets.find(id);
	if (it == Widgets.end() || !it->second) {
		return;
	}
	auto widget = it->second;
	if (op == u"visibility"_q) {
		widget->setVisible(payload.value(u"value"_q).toInt() == 0);
	} else if (op == u"alpha"_q) {
		auto existing = qobject_cast<QGraphicsOpacityEffect*>(
			widget->graphicsEffect());
		if (!existing) {
			existing = new QGraphicsOpacityEffect(widget);
			widget->setGraphicsEffect(existing);
		}
		existing->setOpacity(payload.value(u"value"_q).toDouble());
	} else if (op == u"translation_x"_q) {
		widget->move(widget->x() + int(payload.value(u"value"_q).toDouble()), widget->y());
	} else if (op == u"translation_y"_q) {
		widget->move(widget->x(), widget->y() + int(payload.value(u"value"_q).toDouble()));
	} else if (op == u"child_gravity"_q) {
		if (auto layout = widget->layout()) {
			const auto alignment = AlignmentFromGravity(
				payload.value(u"value"_q).toInt());
			for (auto i = 0; i != layout->count(); ++i) {
				layout->itemAt(i)->setAlignment(alignment);
			}
		}
	} else if (op == u"params"_q) {
		ApplySize(widget, payload);
		if (auto layout = widget->layout()) {
			const auto gravity = payload.value(u"gravity"_q);
			if (gravity.isDouble() && layout->count()) {
				layout->itemAt(0)->setAlignment(
					AlignmentFromGravity(gravity.toInt()));
			}
		}
	} else if (op == u"text"_q) {
		if (auto label = qobject_cast<QLabel*>(widget)) {
			label->setText(payload.value(u"value"_q).toString());
		} else if (auto edit = qobject_cast<QLineEdit*>(widget)) {
			edit->setText(payload.value(u"value"_q).toString());
		}
	} else if (op == u"color"_q) {
		widget->setStyleSheet(u"color: %1"_q
			.arg(FromArgb(payload.value(u"value"_q).toInteger())
				.name(QColor::HexArgb)));
	} else if (op == u"size"_q) {
		auto font = widget->font();
		font.setPixelSize(payload.value(u"value"_q).toInt());
		widget->setFont(font);
	} else if (op == u"bold"_q) {
		auto font = widget->font();
		font.setBold(payload.value(u"value"_q).toBool());
		widget->setFont(font);
	} else if (op == u"gravity"_q) {
		if (auto label = qobject_cast<QLabel*>(widget)) {
			label->setAlignment(AlignmentFromGravity(payload.value(u"value"_q).toInt()));
		}
	} else if (op == u"single_line"_q) {
		if (auto label = qobject_cast<QLabel*>(widget)) {
			label->setWordWrap(!payload.value(u"value"_q).toBool());
		}
	} else if (op == u"hint"_q) {
		if (auto edit = qobject_cast<QLineEdit*>(widget)) {
			edit->setPlaceholderText(payload.value(u"value"_q).toString());
		}
	} else if (op == u"bitmap"_q) {
		if (auto label = qobject_cast<QLabel*>(widget)) {
			label->setPixmap(CanvasPixmap(int64(payload.value(u"value"_q).toDouble())));
		}
	} else if (op == u"bg"_q) {
		widget->setStyleSheet(QString());
		ApplyBackground(widget, payload.value(u"value"_q).toObject());
	} else if (op == u"padding"_q) {
		const auto padding = payload.value(u"value"_q).toArray();
		if (padding.size() == 4 && widget->layout()) {
			widget->layout()->setContentsMargins(
				padding[0].toInt(), padding[1].toInt(),
				padding[2].toInt(), padding[3].toInt());
		}
	} else if (op == u"orientation"_q) {
		const auto vertical = payload.value(u"value"_q).toInt(0) == 0;
		const auto old = widget->layout();
		auto items = std::vector<QPair<QWidget*, QMargins>>();
		if (old) {
			for (auto i = 0; i != old->count(); ++i) {
				const auto item = old->takeAt(0);
				if (item->widget()) {
					items.emplace_back(item->widget(), item->widget()->contentsMargins());
				}
			}
			delete old;
		}
		auto layout = vertical
			? static_cast<QBoxLayout*>(new QVBoxLayout(widget))
			: static_cast<QBoxLayout*>(new QHBoxLayout(widget));
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(0);
		for (auto &[child, margins] : items) {
			layout->addWidget(child);
		}
		widget->setLayout(layout);
	} else if (op == u"children"_q) {
		RebuildChildren(widget, payload);
	} else if (op == u"animate"_q) {
		RunAnimation(widget, payload);
	} else if (op == u"animate_cancel"_q) {
	} else if (op == u"bring_to_front"_q) {
		widget->raise();
	} else if (op == u"scroll_to"_q) {
		if (auto area = qobject_cast<QScrollArea*>(widget)) {
			area->verticalScrollBar()->setValue(payload.value(u"y"_q).toInt());
		}
	}
}

void EnsureMetrics() {
	QMutexLocker lock(&MetricsMutex);
	if (DpScale > 0.) {
		return;
	}
	const auto window = Core::App().activePrimaryWindow();
	const auto screen = QGuiApplication::primaryScreen();
	const auto size = screen ? screen->availableSize() : QSize(1080, 1920);
	const auto usable = window ? window->widget()->size() : size;
	DpScale = style::ConvertScale(100) / 100.;
	WidthPx = std::min(usable.width(), size.width());
	HeightPx = std::min(usable.height(), size.height());
}

void PaintOps(QPainter &p, const QJsonArray &ops) {
	for (const auto &value : ops) {
		const auto op = value.toObject();
		const auto paint = op.value(u"paint"_q).toObject();
		const auto color = FromArgb(paint.value(u"color"_q).toInteger(0xFF000000));
		const auto style = paint.value(u"style"_q).toInt(0);
		QPen pen(color);
		if (style == 1) {
			pen.setWidthF(paint.value(u"stroke_width"_q).toDouble(1.));
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
		} else {
			p.setPen(Qt::NoPen);
			p.setBrush(color);
		}
		const auto name = op.value(u"op"_q).toString();
		if (name == u"rrect"_q) {
			const auto rect = op.value(u"rect"_q).toArray();
			p.drawRoundedRect(
				QRectF(
					rect[0].toDouble(), rect[1].toDouble(),
					rect[2].toDouble() - rect[0].toDouble(),
					rect[3].toDouble() - rect[1].toDouble()),
				op.value(u"rx"_q).toDouble(),
				op.value(u"ry"_q).toDouble());
		} else if (name == u"rect"_q) {
			const auto rect = op.value(u"rect"_q).toArray();
			p.drawRect(QRectF(
				rect[0].toDouble(), rect[1].toDouble(),
				rect[2].toDouble() - rect[0].toDouble(),
				rect[3].toDouble() - rect[1].toDouble()));
		} else if (name == u"circle"_q) {
			p.drawEllipse(
				QPointF(op.value(u"cx"_q).toDouble(), op.value(u"cy"_q).toDouble()),
				op.value(u"radius"_q).toDouble(),
				op.value(u"radius"_q).toDouble());
		} else if (name == u"line"_q) {
			const auto from = op.value(u"from"_q).toArray();
			const auto to = op.value(u"to"_q).toArray();
			p.drawLine(
				QPointF(from[0].toDouble(), from[1].toDouble()),
				QPointF(to[0].toDouble(), to[1].toDouble()));
		} else if (name == u"arc"_q) {
			const auto rect = op.value(u"rect"_q).toArray();
			p.drawArc(
				QRectF(
					rect[0].toDouble(), rect[1].toDouble(),
					rect[2].toDouble() - rect[0].toDouble(),
					rect[3].toDouble() - rect[1].toDouble()),
				int(op.value(u"start"_q).toDouble() * 16.),
				int(op.value(u"sweep"_q).toDouble() * 16.));
		} else if (name == u"text"_q) {
			auto font = QGuiApplication::font();
			font.setPixelSize(int(paint.value(u"text_size"_q).toDouble(16.)));
			font.setBold(paint.value(u"bold"_q).toBool(false));
			p.setFont(font);
			p.setPen(color);
			const auto text = op.value(u"text"_q).toString();
			const auto x = op.value(u"x"_q).toDouble();
			const auto y = op.value(u"y"_q).toDouble();
			const auto align = paint.value(u"text_align"_q).toInt(0);
			const auto width = QFontMetricsF(font).horizontalAdvance(text);
			auto point = QPointF(x, y);
			if (align == 1) {
				point.setX(x - width / 2.);
			} else if (align == 2) {
				point.setX(x - width);
			}
			p.drawText(point, text);
		} else if (name == u"bitmap"_q) {
			QMutexLocker lock(&CanvasMutex);
			const auto it = Canvases.find(op.value(u"bitmap"_q).toInteger());
			if (it != Canvases.end()) {
				p.drawImage(
					QPointF(op.value(u"x"_q).toDouble(), op.value(u"y"_q).toDouble()),
					it->second);
			}
		}
	}
}

[[nodiscard]] int64 ColorByName(const QString &name) {
	const auto mapped = (name == u"windowFg"_q)
		? st::windowFg
		: (name == u"windowSubFg"_q)
		? st::windowSubTextFg
		: (name == u"windowActiveTextFg"_q)
		? st::windowActiveTextFg
		: (name == u"windowBg"_q)
		? st::windowBg
		: st::windowFg;
	const auto c = mapped->c;
	return int64((uint32(c.alpha()) << 24)
		| (uint32(c.red()) << 16)
		| (uint32(c.green()) << 8)
		| uint32(c.blue()));
}

} // namespace

void ShowSheet(const QString &payload) {
	const auto data = QJsonDocument::fromJson(payload.toUtf8()).object();
	const auto id = int64(data.value(u"id"_q).toDouble());
	const auto rootJson = data.value(u"root"_q).toObject();
	crl::on_main([=] {
		const auto controller = ActiveController();
		if (!controller) {
			return;
		}
		EnsureMetrics();
		auto root = BuildNode(rootJson, nullptr);
		if (!root) {
			return;
		}
		Sheets[id].root = root;
		const auto width = SheetWidth();
		controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->setNoContentMargin(true);
			box->setWidth(width);
			auto holder = object_ptr<SheetHolder>(new SheetHolder(box));
			const auto holderRaw = holder.data();
			holderRaw->setContent(root);
			box->addRow(std::move(holder), QMargins());
			Sheets[id].holder = holderRaw;
			Sheets[id].close = crl::guard(holderRaw, [=] {
				box->closeBox();
			});
			QObject::connect(holderRaw, &QObject::destroyed, [=] {
				LOG(("PluginViews: sheet holder destroyed id=%1").arg(id));
				Extera::Plugins::Call(u"event"_q, QJsonObject{
					{ u"type"_q, u"sheet_closed"_q },
					{ u"id"_q, double(id) },
				});
				Sheets.erase(id);
			});
			for (const auto delay : { 100, 1000, 4000 }) {
				QTimer::singleShot(delay, crl::guard(holderRaw, [=] {
					LOG(("PluginViews: sheet check id=%1 delay=%2"
						" holder=%3x%4 pos=%5,%6 visible=%7 root=%8x%9"
						" window=%10x%11").arg(
							id
						).arg(delay
						).arg(holderRaw->width()
						).arg(holderRaw->height()
						).arg(holderRaw->x()
						).arg(holderRaw->y()
						).arg(holderRaw->isVisible()
						).arg(root->width()
						).arg(root->height()
						).arg(holderRaw->window()->width()
						).arg(holderRaw->window()->height()));
				}));
			}
		}));
	});
}

void Update(const QString &payload) {
	const auto data = QJsonDocument::fromJson(payload.toUtf8()).object();
	crl::on_main([=] {
		ApplyUpdate(data);
		for (auto &pair : Sheets) {
			if (pair.second.holder && pair.second.root) {
				pair.second.holder->remeasure();
			}
		}
	});
}

void DismissSheet(int64 id) {
	crl::on_main([=] {
		const auto it = Sheets.find(id);
		if (it != Sheets.end() && it->second.close) {
			it->second.close();
		}
		Sheets.erase(id);
	});
}

void RenderCanvas(int64 id, int width, int height, const QString &ops) {
	if (width <= 0 || height <= 0) {
		return;
	}
	auto image = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	{
		QPainter p(&image);
		p.setRenderHint(QPainter::Antialiasing);
		PaintOps(p, QJsonDocument::fromJson(ops.toUtf8()).array());
	}
	QMutexLocker lock(&CanvasMutex);
	Canvases[id] = std::move(image);
	if (Canvases.size() > 128) {
		Canvases.erase(Canvases.begin());
	}
}

double MeasureText(const QString &text, int sizePx, bool bold) {
	auto font = QGuiApplication::font();
	font.setPixelSize(std::max(1, sizePx));
	font.setBold(bold);
	return QFontMetricsF(font).horizontalAdvance(text);
}

QString UiMetrics() {
	EnsureMetrics();
	QJsonObject data;
	{
		QMutexLocker lock(&MetricsMutex);
		data = QJsonObject{
			{ u"dp"_q, DpScale },
			{ u"width"_q, SheetWidth() },
			{ u"height"_q, HeightPx },
		};
	}
	return QJsonDocument(data).toJson(QJsonDocument::Compact);
}

int64 ThemeColor(const QString &name) {
	return ColorByName(name);
}

} // namespace Extera::Views
