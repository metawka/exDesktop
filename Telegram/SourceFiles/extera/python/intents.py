import inspect
import json
import threading
from dataclasses import dataclass
from typing import Any, Callable, Dict, List, Optional

_STANDARD_ORDER = ("intent", "scheme", "host", "path", "query_args",
                   "action", "flags", "type", "categories")

_FILTER_KEYS = {
    "scheme": "schemes",
    "schemes": "schemes",
    "host": "hosts",
    "hosts": "hosts",
    "path": "path_template",
    "path_template": "path_template",
    "required_path_args_names": "query_args",
    "query_args": "query_args",
    "action": "action",
    "type": "type",
    "categories": "categories",
    "whitelist_flags": "whitelist_flags",
    "blacklist_flags": "blacklist_flags",
    "flags": "whitelist_flags",
}

_LIST_VALUED = {"schemes", "hosts", "query_args", "categories"}

_handler_counter = [0]


def _build_filters(filters: Dict[str, Any]) -> Dict[str, Any]:
    out: Dict[str, Any] = {}
    for key, value in filters.items():
        target = _FILTER_KEYS.get(key)
        if target is None or value is None:
            continue
        if target in _LIST_VALUED and not isinstance(value, (list, tuple)):
            value = [value]
        elif isinstance(value, (list, tuple)):
            value = list(value)
        out[target] = value
    return out


def _context_to_dict(context) -> Dict[str, Any]:
    if context is None:
        return {}
    if isinstance(context, dict):
        return dict(context)
    try:
        return dict(context)
    except Exception:
        return {}


def _flatten_context(context: Dict[str, Any]) -> Dict[str, Any]:
    flat = dict(context)
    for key in ("query_args", "path_args"):
        nested = _context_to_dict(context.get(key)) if context.get(key) is not None else {}
        for name, value in nested.items():
            flat.setdefault(str(name), value)
    return flat


def _invoke(fn: Callable, context: Dict[str, Any]):
    try:
        signature = inspect.signature(fn)
    except (TypeError, ValueError):
        return fn(context)
    params = signature.parameters
    if any(p.kind == p.VAR_KEYWORD for p in params.values()):
        return fn(**context)
    if any(p.kind == p.VAR_POSITIONAL for p in params.values()):
        return fn(*[context.get(name) for name in _STANDARD_ORDER])
    kwargs = {}
    for name, param in params.items():
        if name in context:
            kwargs[name] = context[name]
        elif param.default is inspect.Parameter.empty:
            kwargs[name] = None
    return fn(**kwargs)


def _current_plugin_id() -> Optional[str]:
    try:
        import extera_host
        return extera_host.current_plugin_id()
    except Exception:
        return None


def _matches(filters: Dict[str, Any], context: Dict[str, Any]) -> bool:
    schemes = filters.get("schemes")
    if schemes and str(context.get("scheme") or "").lower() not in [
            str(s).lower() for s in schemes]:
        return False
    hosts = filters.get("hosts")
    if hosts and str(context.get("host") or "").lower() not in [
            str(h).lower() for h in hosts]:
        return False
    path = filters.get("path_template")
    if path and str(context.get("path") or "") != str(path):
        return False
    required = filters.get("query_args")
    if required:
        args = _context_to_dict(context.get("query_args")) or {}
        for name in required:
            if str(name) not in args:
                return False
    action = filters.get("action")
    if action and str(context.get("action") or "") != str(action):
        return False
    the_type = filters.get("type")
    if the_type and str(context.get("type") or "") != str(the_type):
        return False
    categories = filters.get("categories")
    if categories:
        present = context.get("categories") or []
        for category in categories:
            if str(category) not in [str(c) for c in present]:
                return False
    flags = filters.get("whitelist_flags")
    if flags:
        value = context.get("flags")
        for flag in flags:
            try:
                if not int(value) & int(flag):
                    return False
            except Exception:
                return False
    return True


class IntentsManager:
    class HandlerNotRegistered(Exception):
        pass

    @dataclass
    class HandlerHandle:
        handler_id: str
        plugin_id: Optional[str] = None

        def unhandle(self):
            IntentsManager.unhandle(self.handler_id)

    _lock = threading.RLock()
    _handlers: Dict[str, Dict[str, Any]] = {}
    _by_plugin: Dict[str, set] = {}

    @staticmethod
    def new_global_before_handler(fn: Optional[Callable] = None, *,
                                  priority: int = 0, **filters):
        return IntentsManager._register(True, fn, priority, filters)

    @staticmethod
    def new_global_after_handler(fn: Optional[Callable] = None, *,
                                 priority: int = 0, **filters):
        return IntentsManager._register(False, fn, priority, filters)

    @staticmethod
    def _register(before: bool, fn: Optional[Callable], priority: int,
                  filters: Dict[str, Any]):
        if fn is None:
            return lambda real_fn: IntentsManager._register(before, real_fn,
                                                            priority, filters)
        if not callable(fn):
            raise TypeError("handler must be callable")
        plugin_id = _current_plugin_id()
        if not plugin_id:
            raise RuntimeError(
                "IntentsManager handlers must be registered from a plugin "
                "context (on_plugin_load / a hook callback)")
        with IntentsManager._lock:
            _handler_counter[0] += 1
            handler_id = f"intent{_handler_counter[0]}"
            IntentsManager._handlers[handler_id] = {
                "plugin_id": plugin_id, "before": bool(before),
                "priority": int(priority), "filters": _build_filters(filters),
                "fn": fn,
            }
            IntentsManager._by_plugin.setdefault(plugin_id, set()).add(handler_id)
        return IntentsManager.HandlerHandle(handler_id=handler_id,
                                            plugin_id=plugin_id)

    @staticmethod
    def unhandle(handler_id: str):
        handler_id = str(handler_id)
        with IntentsManager._lock:
            record = IntentsManager._handlers.pop(handler_id, None)
            if record is None:
                raise IntentsManager.HandlerNotRegistered(
                    f"no intent handler registered as {handler_id!r}")
            bucket = IntentsManager._by_plugin.get(record["plugin_id"])
            if bucket is not None:
                bucket.discard(handler_id)

    @staticmethod
    def dispatch(before: bool, context: Dict[str, Any]) -> bool:
        with IntentsManager._lock:
            records = sorted(
                (record for record in IntentsManager._handlers.values()
                 if record["before"] == before),
                key=lambda record: record["priority"])
        for record in records:
            if not _matches(record["filters"], context):
                continue
            try:
                result = _invoke(record["fn"], _flatten_context(_context_to_dict(context)))
            except Exception:
                from android_utils import log
                log("intent handler failed")
            if before and result:
                return True
        return False

    @staticmethod
    def parse(url: str) -> Dict[str, Any]:
        from urllib.parse import parse_qsl, urlsplit
        parts = urlsplit(str(url))
        return {
            "scheme": parts.scheme or None,
            "host": parts.netloc or None,
            "path": parts.path or None,
            "query_args": dict(parse_qsl(parts.query)),
        }


def _unhandle_all_for_plugin(plugin_id: str):
    if not plugin_id:
        return
    with IntentsManager._lock:
        ids = list(IntentsManager._by_plugin.pop(plugin_id, ()))
    for handler_id in ids:
        IntentsManager._handlers.pop(handler_id, None)
