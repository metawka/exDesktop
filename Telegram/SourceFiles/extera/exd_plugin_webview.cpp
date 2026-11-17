#include "extera/exd_plugin_webview.h"

#include "extera/extera_plugins.h"
#include "core/application.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "ui/widgets/separate_panel.h"
#include "ui/rp_widget.h"
#include "webview/webview_embed.h"
#include "webview/webview_interface.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>

namespace Extera::PluginWeb {
namespace {

struct Entry {
	std::unique_ptr<Ui::SeparatePanel> panel;
	std::unique_ptr<Webview::Window> webview;
	bool closing = false;
};

base::flat_map<int64, std::unique_ptr<Entry>> Entries;

[[nodiscard]] QByteArray BridgeScript() {
	return R"(
(function () {
	if (window.exd) return;
	var handlers = [];
	var post = function (body) {
		try { window.external.invoke(JSON.stringify(body)); } catch (e) {}
	};
	window.exd = {
		send: function (data) { post({ event: 'message', data: data }); },
		close: function () { post({ event: 'close' }); },
		onMessage: function (fn) { handlers.push(fn); },
		_deliver: function (data) {
			for (var i = 0; i < handlers.length; ++i) {
				try { handlers[i](data); } catch (e) {}
			}
		},
		_interface: function (name) {
			window[name] = new Proxy({}, {
				get: function (target, method) {
					return function () {
						post({
							event: 'call',
							iface: name,
							method: String(method),
							args: Array.prototype.slice.call(arguments)
						});
					};
				}
			});
		}
	};
	window.addEventListener('error', function (e) {
		post({ event: 'console', level: 'error', text: String(e.message) });
	});
	var log = console.log;
	console.log = function () {
		post({ event: 'console', level: 'log', text: Array.prototype.join.call(arguments, ' ') });
		return log.apply(console, arguments);
	};
})();
)";
}

void Notify(int64 id, QJsonObject event) {
	event.insert(u"type"_q, u"webview"_q);
	event.insert(u"id"_q, double(id));
	Extera::Plugins::Call(u"event"_q, event);
}

[[nodiscard]] Window::SessionController *ActiveSession() {
	if (const auto window = Core::App().activeWindow()) {
		return window->sessionController();
	}
	return nullptr;
}

} // namespace

void Open(const QString &payload) {
	const auto data = QJsonDocument::fromJson(payload.toUtf8()).object();
	const auto id = int64(data.value(u"id"_q).toDouble());
	const auto title = data.value(u"title"_q).toString();
	const auto html = data.value(u"html"_q).toString();
	const auto url = data.value(u"url"_q).toString();
	const auto baseUrl = data.value(u"base_url"_q).toString(u"https://plugin.exdesktop.local/"_q);
	const auto width = std::clamp(data.value(u"width"_q).toInt(420), 240, 1600);
	const auto height = std::clamp(data.value(u"height"_q).toInt(640), 200, 1200);
	const auto interfaces = data.value(u"interfaces"_q).toArray();
	const auto netOff = data.value(u"net_off"_q).toBool(true);
	if (!url.isEmpty() && (netOff || !url.startsWith(u"https://"_q))) {
		Notify(id, QJsonObject{ { u"event"_q, u"error"_q }, { u"reason"_q, u"network"_q } });
		return;
	}
	const auto controller = ActiveSession();
	if (!controller || Entries.contains(id)) {
		return;
	}
	auto entry = std::make_unique<Entry>();
	const auto raw = entry.get();
	raw->panel = std::make_unique<Ui::SeparatePanel>();
	const auto panel = raw->panel.get();
	panel->setWindowFlag(Qt::WindowStaysOnTopHint, false);
	panel->setTitle(rpl::single(title));
	panel->setInnerSize(QSize(width, height), true);
	panel->showInner(base::make_unique_q<Ui::RpWidget>(panel));
	const auto body = panel->inner();
	panel->closeRequests() | rpl::on_next([=] {
		Close(id);
	}, panel->lifetime());

	raw->webview = std::make_unique<Webview::Window>(
		body,
		Webview::WindowConfig{
			.opaqueBg = st::windowBg->c,
			.storageId = controller->session().local().resolveStorageIdOther(),
		});
	const auto webview = raw->webview.get();
	if (!webview->widget()) {
		Notify(id, QJsonObject{ { u"event"_q, u"error"_q } });
		return;
	}
	webview->widget()->show();
	body->sizeValue() | rpl::on_next([=](QSize size) {
		if (const auto widget = webview->widget()) {
			widget->setGeometry(QRect(QPoint(), size));
		}
	}, body->lifetime());
	webview->setMessageHandler([=](const QJsonDocument &message) {
		const auto object = message.object();
		crl::on_main(panel, [=] {
			if (object.value(u"event"_q).toString() == u"close"_q) {
				Close(id);
				return;
			}
			Notify(id, object);
		});
	});
	webview->setNavigationPolicyHandler([=](const QString &uri, bool newWindow) {
		if (uri.startsWith(u"about:"_q)
			|| uri.startsWith(u"data:"_q)
			|| uri.startsWith(baseUrl)) {
			return true;
		}
		const auto allowed = Extera::Plugins::Call(u"event"_q, QJsonObject{
			{ u"type"_q, u"webview_nav"_q },
			{ u"id"_q, double(id) },
			{ u"url"_q, uri },
			{ u"new_window"_q, newWindow },
		}).toBool();
		return allowed;
	});
	webview->setNavigationDoneHandler([=](bool success) {
		Notify(id, QJsonObject{
			{ u"event"_q, u"loaded"_q },
			{ u"success"_q, success },
		});
	});
	auto init = BridgeScript();
	for (const auto &value : interfaces) {
		init += "\nwindow.exd._interface("
			+ QJsonDocument(QJsonArray{ value.toString() }).toJson(
				QJsonDocument::Compact).mid(1).chopped(1)
			+ ");";
	}
	webview->init(init);
	if (!url.isEmpty()) {
		webview->navigate(url);
	} else {
		const auto connect = netOff ? u"'none'"_q : u"https: wss:"_q;
		const auto remote = netOff ? QString() : u" https:"_q;
		const auto policy = u"default-src 'self' data: blob: 'unsafe-inline' 'unsafe-eval'; "_q
			+ u"img-src data: blob:"_q + remote + u"; media-src data: blob:"_q + remote
			+ u"; font-src data:"_q + remote + u"; style-src 'unsafe-inline' data:"_q + remote
			+ u"; script-src 'unsafe-inline' 'unsafe-eval' data: blob:"_q + remote
			+ u"; connect-src "_q + connect + u"; frame-src data: blob:"_q + remote + u";"_q;
		webview->loadHtml(
			u"<meta http-equiv=\"Content-Security-Policy\" content=\""_q
				+ policy
				+ u"\">"_q
				+ html,
			baseUrl);
	}
	const auto parent = controller->widget()->geometry();
	panel->move(
		parent.x() + (parent.width() - panel->width()) / 2,
		parent.y() + (parent.height() - panel->height()) / 2);
	panel->showAndActivate();
	Entries.emplace(id, std::move(entry));
}

void Eval(int64 id, const QString &script) {
	const auto it = Entries.find(id);
	if (it == Entries.end() || !it->second->webview) {
		return;
	}
	it->second->webview->eval(script.toUtf8());
}

void Close(int64 id) {
	const auto it = Entries.find(id);
	if (it == Entries.end() || it->second->closing) {
		return;
	}
	it->second->closing = true;
	auto entry = std::move(it->second);
	Entries.erase(it);
	entry->webview = nullptr;
	if (entry->panel) {
		entry->panel->hide();
	}
	Notify(id, QJsonObject{ { u"event"_q, u"closed"_q } });
	crl::on_main([entry = std::shared_ptr<Entry>(std::move(entry))] {});
}

void CloseAll() {
	auto ids = std::vector<int64>();
	for (const auto &[id, entry] : Entries) {
		ids.push_back(id);
	}
	for (const auto id : ids) {
		Close(id);
	}
}

} // namespace Extera::PluginWeb
