import inspect
import json
import threading
from dataclasses import dataclass, field
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


def _path_regex(template):
    import re
    if not template:
        return None
    parts = re.split(r"(\{[A-Za-z_][A-Za-z0-9_]*\})", str(template))
    out = []
    for part in parts:
        if part.startswith("{") and part.endswith("}"):
            out.append("(?P<%s>[^/]+)" % part[1:-1])
        else:
            out.append(re.escape(part))
    return "^" + "".join(out) + "/?$"


def _path_args(regex, path):
    import re
    if not regex:
        return {}
    match = re.match(regex, str(path or ""))
    return None if match is None else match.groupdict()


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
    if path:
        if "{" in str(path):
            found = _path_args(_path_regex(path), context.get("path"))
            if found is None:
                return False
            context.setdefault("path_args", {}).update(found)
        elif str(context.get("path") or "").rstrip("/") != str(path).rstrip("/"):
            return False
    required = filters.get("query_args")
    if required:
        args = dict(_context_to_dict(context.get("query_args")) or {})
        args.update(_context_to_dict(context.get("path_args")) or {})
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
        present = [str(c) for c in (context.get("categories") or [])]
        for category in categories:
            if str(category) not in present:
                return False
    value = context.get("flags")
    for flag in filters.get("whitelist_flags") or ():
        try:
            if not int(value or 0) & int(flag):
                return False
        except Exception:
            return False
    for flag in filters.get("blacklist_flags") or ():
        try:
            if int(value or 0) & int(flag):
                return False
        except Exception:
            pass
    return True


class IntentsManager:
    class HandlerNotRegistered(Exception):
        def __init__(self, handler_id: str = ""):
            super().__init__(f"no intent handler registered as {handler_id!r}")
            self.handler_id = handler_id

    @dataclass
    class HandlerInfo:
        callback: Callable
        scheme: Optional[str] = None
        host: Optional[str] = None
        path: Optional[str] = None
        required_path_args_names: Optional[List[str]] = None
        priority: Optional[int] = 0
        action: Optional[str] = None
        whitelist_flags: Optional[List[int]] = None
        blacklist_flags: Optional[List[int]] = None
        type: Optional[str] = None
        categories: Optional[List[str]] = None

    @dataclass
    class _HandlerInfo:
        handler_id: str
        callback: Callable = field(compare=False, repr=False)
        hook_type: str = "before"
        priority: int = 0
        scheme: Optional[str] = None
        host: Optional[str] = None
        path: Optional[str] = None
        path_regex: Optional[str] = None
        required_path_args_names: tuple = field(default_factory=tuple)
        action: Optional[str] = None
        whitelist_flags: tuple = field(default_factory=tuple)
        blacklist_flags: tuple = field(default_factory=tuple)
        type: Optional[str] = None
        categories: tuple = field(default_factory=tuple)

    @dataclass
    class HandlerHandle:
        handler_id: str
        plugin_id: Optional[str] = None

        def unhandle(self):
            IntentsManager.unhandle(self.handler_id)

    _lock = threading.RLock()
    _handlers: Dict[str, Dict[str, Any]] = {}
    _by_plugin: Dict[str, set] = {}

    @classmethod
    def new_global_before_handler(cls, info=None, *, priority: int = 0, **filters):
        return cls._register(True, info, priority, filters)

    @classmethod
    def new_global_after_handler(cls, info=None, *, priority: int = 0, **filters):
        return cls._register(False, info, priority, filters)

    @classmethod
    def _register(cls, before: bool, info, priority: int, filters: Dict[str, Any]):
        if info is None:
            return lambda real_fn: cls._register(before, real_fn, priority, filters)
        if isinstance(info, IntentsManager.HandlerInfo):
            fn = info.callback
            filters = {key: getattr(info, key) for key in (
                "scheme", "host", "path", "required_path_args_names", "action",
                "whitelist_flags", "blacklist_flags", "type", "categories")}
            priority = int(info.priority or 0)
        else:
            fn = info
        if not callable(fn):
            raise TypeError("handler must be callable")
        plugin_id = _current_plugin_id()
        if not plugin_id:
            raise RuntimeError(
                "IntentsManager handlers must be registered from a plugin "
                "context (on_plugin_load / a hook callback)")
        built = _build_filters(filters)
        with cls._lock:
            _handler_counter[0] += 1
            handler_id = f"intent{_handler_counter[0]}"
            cls._handlers[handler_id] = {
                "plugin_id": plugin_id, "before": bool(before),
                "priority": int(priority), "filters": built, "fn": fn,
                "info": IntentsManager._HandlerInfo(
                    handler_id=handler_id, callback=fn, hook_type="before" if before else "after",
                    priority=int(priority), scheme=filters.get("scheme"), host=filters.get("host"),
                    path=filters.get("path"), path_regex=_path_regex(filters.get("path")),
                    required_path_args_names=tuple(filters.get("required_path_args_names") or ()),
                    action=filters.get("action"), whitelist_flags=tuple(filters.get("whitelist_flags") or ()),
                    blacklist_flags=tuple(filters.get("blacklist_flags") or ()), type=filters.get("type"),
                    categories=tuple(filters.get("categories") or ())),
            }
            cls._by_plugin.setdefault(plugin_id, set()).add(handler_id)
        return IntentsManager.HandlerHandle(handler_id=handler_id, plugin_id=plugin_id)

    @classmethod
    def unhandle(cls, handler_id):
        handler_id = str(getattr(handler_id, "handler_id", handler_id))
        with cls._lock:
            record = cls._handlers.pop(handler_id, None)
            if record is None:
                raise IntentsManager.HandlerNotRegistered(handler_id)
            bucket = cls._by_plugin.get(record["plugin_id"])
            if bucket is not None:
                bucket.discard(handler_id)

    @staticmethod
    def dispatch(before: bool, context: Dict[str, Any]) -> bool:
        with IntentsManager._lock:
            records = sorted(
                (record for record in IntentsManager._handlers.values()
                 if record["before"] == before),
                key=lambda record: -record["priority"])
        for record in records:
            local = _context_to_dict(context)
            if not _matches(record["filters"], local):
                continue
            result = None
            try:
                import extera_host
                with extera_host.owner_scope(record.get("plugin_id")):
                    result = _invoke(record["fn"], _flatten_context(local))
            except Exception:
                import traceback
                from android_utils import log
                log("intent handler failed:\n" + traceback.format_exc(limit=4))
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
