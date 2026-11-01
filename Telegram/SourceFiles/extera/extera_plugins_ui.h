#pragma once

class PeerData;
class HistoryItem;

namespace Ui {
class VerticalLayout;
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Extera::Plugins {

void FillPluginsList(
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller);

void ShowPluginSettings(
	not_null<Window::SessionController*> controller,
	const QString &id);

void PaintPuzzle(QPainter &p, QRectF rect, QColor color);
void ShowSourceBox(
	not_null<Window::SessionController*> controller,
	const QString &path);
void ShowUnknownSourcesWarning(
	not_null<Window::SessionController*> controller,
	Fn<void(bool)> done);
void ShowInstallBox(
	not_null<Window::SessionController*> controller,
	const QString &path,
	bool trusted);

void ChooseAndInstall(not_null<Window::SessionController*> controller);
void ShowPluginStore(not_null<Window::SessionController*> controller);

void AddChatMenuItems(
	const Fn<void(QString, Fn<void()>)> &add,
	not_null<PeerData*> peer);

void AddMessageMenuItems(
	not_null<Ui::PopupMenu*> menu,
	not_null<HistoryItem*> item);

} // namespace Extera::Plugins
