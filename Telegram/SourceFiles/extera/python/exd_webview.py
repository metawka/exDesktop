import sys
import itertools
import secrets
import json
import threading

import android_views
from exd_java import JavaObject

_counter = itertools.count(1)
_lock = threading.RLock()
_windows = {}


def _native(name):
    try:
        _extera = sys.modules["_extera"]
        return getattr(_extera, name, None)
    except Exception:
        return None


def _owner():
    try:
        import extera_host
        return extera_host.current_plugin_id() or extera_host.owner_now()
    except Exception:
        return None


def _scope(owner):
    import extera_host
    return extera_host.owner_scope(owner)


def _log(text):
    try:
        import extera_host
        extera_host.log(text)
    except Exception:
        pass


# Window

class WebApp:
    def __init__(self, title="", html=None, url=None, width=420, height=640, on_message=None, on_close=None,
                 on_load=None, on_navigate=None, base_url=None):
        self.id = secrets.randbits(52) or next(_counter)
        self.title = str(title or "")
        self.html = html
        self.url = url
        self.width = int(width)
        self.height = int(height)
        self.base_url = base_url
        self.on_message = on_message
        self.on_close = on_close
        self.on_load = on_load
        self.on_navigate = on_navigate
        self.owner = _owner()
        self.interfaces = {}
        self.shown = False
        self.closed = False
        self._pending = []

    def setCanDismissWithSwipe(self, value):
        return self

    def setAllowNestedScroll(self, value):
        return self

    def setOnDismissListener(self, listener):
        previous = self.on_close
        run = getattr(listener, "onDismiss", listener)
        self.on_close = (lambda: (previous() if previous else None, run(None))) if callable(run) else previous
        return self

    def dismiss(self):
        self.close()

    def add_interface(self, name, obj):
        self.interfaces[str(name)] = obj
        return self

    def show(self):
        if self.shown:
            return self
        open_ = _native("webview_open")
        if open_ is None:
            raise RuntimeError("web windows are not available")
        payload = {"id": self.id, "title": self.title, "width": self.width, "height": self.height,
                   "interfaces": list(self.interfaces)}
        if self.url:
            payload["url"] = str(self.url)
        else:
            payload["html"] = str(self.html or "")
        if self.base_url:
            payload["base_url"] = str(self.base_url)
        with _lock:
            _windows[self.id] = self
        open_(json.dumps(payload, ensure_ascii=False))
        self.shown = True
        return self

    def eval(self, script):
        if self.closed:
            return
        if not self.shown:
            self._pending.append(str(script))
            return
        native = _native("webview_eval")
        if native is not None:
            native(self.id, str(script))

    def send(self, data):
        self.eval("window.exd && window.exd._deliver(%s);" % json.dumps(data, ensure_ascii=False))

    def close(self):
        if self.closed:
            return
        native = _native("webview_close")
        if native is not None and self.shown:
            native(self.id)
        else:
            self._closed()

    def _closed(self):
        if self.closed:
            return
        self.closed = True
        with _lock:
            _windows.pop(self.id, None)
        if self.on_close is not None:
            with _scope(self.owner):
                self.on_close()

    def _handle(self, event):
        kind = event.get("event")
        if kind == "message":
            if self.on_message is not None:
                with _scope(self.owner):
                    self.on_message(event.get("data"))
        elif kind == "call":
            target = self.interfaces.get(event.get("iface"))
            method = getattr(target, str(event.get("method") or ""), None) if target is not None else None
            if callable(method) and not str(event.get("method")).startswith("_"):
                with _scope(self.owner):
                    method(*list(event.get("args") or ()))
        elif kind == "loaded":
            for script in self._pending:
                self.eval(script)
            self._pending = []
            if self.on_load is not None:
                with _scope(self.owner):
                    self.on_load(bool(event.get("success")))
        elif kind == "closed":
            self._closed()
        elif kind == "console":
            _log("[%s] web: %s" % (self.owner, event.get("text")))
        elif kind == "error":
            _log("[%s] web window failed: %s" % (self.owner, event.get("reason") or "webview unavailable"))
            self._closed()

    def _navigate(self, url, new_window):
        if self.on_navigate is not None:
            with _scope(self.owner):
                result = self.on_navigate(url)
            if result is True:
                return False
            if result is False:
                return False
        try:
            _extera = sys.modules["_extera"]
            with _scope(self.owner):
                _extera.open_url(str(url))
        except Exception:
            pass
        return False


def dispatch(args):
    window = _windows.get(int(args.get("id") or 0))
    if window is None:
        return False
    try:
        window._handle(args)
    except Exception:
        import traceback
        _log("[%s] web window handler failed:\n%s" % (window.owner, traceback.format_exc(limit=4)))
    return True


def navigate(args):
    window = _windows.get(int(args.get("id") or 0))
    if window is None:
        return False
    try:
        return bool(window._navigate(str(args.get("url") or ""), bool(args.get("new_window"))))
    except Exception:
        return False


def close_plugin(plugin_id):
    with _lock:
        targets = [w for w in _windows.values() if w.owner == plugin_id]
    for window in targets:
        try:
            window.close()
        except Exception:
            pass


# Android

class ValueCallback(JavaObject):
    _java_name = "android.webkit.ValueCallback"

    def onReceiveValue(self, value):
        pass


class WebSettings(JavaObject):
    _java_name = "android.webkit.WebSettings"
    LOAD_DEFAULT = -1
    LOAD_NO_CACHE = 2
    MIXED_CONTENT_ALWAYS_ALLOW = 0

    def __init__(self):
        self._values = {}

    def __getattr__(self, item):
        if item.startswith("set") and len(item) > 3:
            key = item[3:]
            return lambda value=None, *rest: self._values.__setitem__(key, value)
        if item.startswith(("get", "is")):
            key = item[3:] if item.startswith("get") else item[2:]
            return lambda *rest: self._values.get(key)
        return super().__getattr__(item)


class WebView(android_views.View):
    _java_name = "android.webkit.WebView"

    def __init__(self, context=None, *args):
        super().__init__(context)
        self._props["type"] = "webview"
        self._settings = WebSettings()
        self._client = None
        self._chrome = None
        self._html = None
        self._url = None
        self._base = None
        self._interfaces = {}
        self._app = None
        self._title = ""

    def getSettings(self):
        return self._settings

    def setWebViewClient(self, client):
        self._client = client

    def getWebViewClient(self):
        return self._client

    def setWebChromeClient(self, client):
        self._chrome = client

    def addJavascriptInterface(self, obj, name):
        self._interfaces[str(name)] = obj
        if self._app is not None:
            self._app.add_interface(str(name), obj)
            self._app.eval("window.exd._interface(%s);" % json.dumps(str(name)))

    def removeJavascriptInterface(self, name):
        self._interfaces.pop(str(name), None)

    def loadData(self, data, mime_type=None, encoding=None):
        self._html, self._url = str(data or ""), None
        self._refresh()

    def loadDataWithBaseURL(self, base_url, data, mime_type=None, encoding=None, history_url=None):
        self._base = base_url if base_url and str(base_url).startswith("https://") else None
        self.loadData(data, mime_type, encoding)

    def loadUrl(self, url, *args):
        url = str(url or "")
        if url.startswith("javascript:"):
            self.evaluateJavascript(url[len("javascript:"):], None)
            return
        self._url, self._html = url, None
        self._refresh()

    def evaluateJavascript(self, script, callback=None):
        if self._app is not None:
            self._app.eval(str(script))
        if callback is not None:
            run = getattr(callback, "onReceiveValue", callback)
            if callable(run):
                run(None)

    def reload(self):
        self._refresh(True)

    def getUrl(self):
        return self._url

    def destroy(self):
        if self._app is not None:
            self._app.close()
            self._app = None

    def _refresh(self, force=False):
        if self._app is None or self._app.closed:
            return
        if force or self._url or self._html is not None:
            self._app.close()
            self._app = None
            self.open(self._title)

    def open(self, title=""):
        self._title = str(title or "")
        client = self._client

        def on_load(success):
            hook = getattr(client, "onPageFinished", None) if client is not None else None
            if callable(hook):
                hook(self, self._url or "about:blank")

        def on_navigate(url):
            hook = getattr(client, "shouldOverrideUrlLoading", None) if client is not None else None
            if callable(hook):
                try:
                    handled = hook(self, url)
                except TypeError:
                    handled = hook(self, _Request(url))
                return True if handled else None
            return None

        app = WebApp(self._title, html=self._html, url=self._url, base_url=self._base,
                     on_load=on_load, on_navigate=on_navigate,
                     on_close=lambda: setattr(self, "_app", None))
        for name, obj in self._interfaces.items():
            app.add_interface(name, obj)
        self._app = app
        app.show()
        return app


class _Request(JavaObject):
    _java_name = "android.webkit.WebResourceRequest"

    def __init__(self, url):
        self._url = url

    def getUrl(self):
        import _compat
        return _compat._Uri(self._url)

    def isForMainFrame(self):
        return True


def find_webview(view):
    if isinstance(view, WebView):
        return view
    for entry in getattr(view, "_children", None) or ():
        found = find_webview(entry.get("view") if isinstance(entry, dict) else entry)
        if found is not None:
            return found
    return None


def install(shims):
    import exd_java
    entries = {"android.webkit": {"WebView": WebView, "WebSettings": WebSettings, "ValueCallback": ValueCallback,
                                  "WebResourceRequest": _Request}}
    for module, values in entries.items():
        shims.setdefault(module, {}).update(values)
        for value in values.values():
            exd_java._CLASSES[value._java_name] = value
