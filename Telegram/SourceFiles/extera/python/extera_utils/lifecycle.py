import contextlib
import threading

_scopes = {}
_lock = threading.RLock()


def _current_plugin_id():
    try:
        import extera_host
        return extera_host.current_plugin_id()
    except Exception:
        return None


class PluginScope:
    def __init__(self, plugin_id):
        self.plugin_id = plugin_id
        self.stop_event = threading.Event()
        self._cleanups = []

    def close(self):
        self.stop_event.set()
        cleanups, self._cleanups = self._cleanups, []
        for cleanup in reversed(cleanups):
            try:
                cleanup()
            except Exception:
                pass

    def register_cleanup(self, cleanup):
        self._cleanups.append(cleanup)
        return cleanup


class ManagedCallback:
    def __init__(self, fn, scope=None, once=False, allow_stopped=False,
                 cleanup_task=False):
        self._fn = fn
        self.scope = scope
        self._once = once
        self._allow_stopped = allow_stopped
        self._cancelled = False
        self._cancel = None

    @property
    def cancelled(self):
        return self._cancelled

    def set_cancel(self, cancel):
        self._cancel = cancel

    def cancel(self):
        self._cancelled = True
        if self._cancel:
            try:
                self._cancel()
            except Exception:
                pass

    def __call__(self, *args, **kwargs):
        if self._cancelled:
            return None
        if (self.scope is not None
                and self.scope.stop_event.is_set()
                and not self._allow_stopped):
            return None
        if self._once:
            self._cancelled = True
        return self._fn(*args, **kwargs)


def _scope(plugin_id=None, create=True):
    plugin_id = plugin_id or _current_plugin_id()
    if not plugin_id:
        return None
    with _lock:
        scope = _scopes.get(plugin_id)
        if scope is None and create:
            scope = _scopes[plugin_id] = PluginScope(plugin_id)
        return scope


def begin(plugin_id):
    with _lock:
        old = _scopes.pop(plugin_id, None)
    if old is not None:
        old.close()
    _scope(plugin_id)


def stop(plugin_id):
    with _lock:
        scope = _scopes.pop(plugin_id, None)
    if scope is not None:
        scope.close()


def stop_all():
    with _lock:
        ids = list(_scopes)
    for plugin_id in ids:
        stop(plugin_id)


def capture_scope(fn=None):
    scope = _scope()
    if fn is None:
        return scope
    return ManagedCallback(fn, scope=scope)


def bind(fn, *, once=False):
    return ManagedCallback(fn, scope=_scope(), once=once)


@contextlib.contextmanager
def cleanup_context(scope):
    yield


def bind_task(fn, *, once=False, scope=None):
    return ManagedCallback(fn, scope=scope or _scope(), once=once)


def bind_request(fn):
    return ManagedCallback(fn, scope=_scope(), once=True)
