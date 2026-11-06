import contextlib
import importlib.abc
import importlib.machinery
import importlib.util
import json
import os
import sys
import threading
import traceback
import uuid

import _extera
import _compat

PLUGINS_DIR = ""
SETTINGS_DIR = ""
_lock = threading.RLock()
_records = {}
_send_hooks = {}
_request_hooks = {}
_menu_items = {}
_safe_mode = False
_unknown_allowed = False
_trusted_cache = None
_observers = {}
_plugin_stack = []


# Log

def log(message):
    try:
        _extera.log(str(message))
    except Exception:
        pass


def current_plugin_id():
    return _plugin_stack[-1] if _plugin_stack else None


@contextlib.contextmanager
def _plugin_context(plugin_id):
    _plugin_stack.append(plugin_id)
    try:
        yield
    finally:
        _plugin_stack.pop()


# Metadata

def read_metadata(path):
    import metadata_parser
    return metadata_parser.read_metadata(path)


# Settings

def get_setting(plugin_id, key, default=None):
    import plugin_settings
    return plugin_settings.get_setting(plugin_id, key, default)


def set_setting(plugin_id, key, value, reload_settings=False):
    import plugin_settings
    plugin_settings.set_setting(plugin_id, key, value, reload_settings)


def export_settings(plugin_id):
    import plugin_settings
    return plugin_settings.get_settings(plugin_id)


def import_settings(plugin_id, data, reload_settings=True):
    base = _base()
    record = _records.get(plugin_id)
    instance = record.instance if record else None
    if instance is not None:
        _plugin_context(plugin_id).__enter__()
        try:
            instance.import_settings(data or {}, reload_settings)
        except Exception:
            log(f"[{plugin_id}] import_settings failed:\n{traceback.format_exc(limit=4)}")
        finally:
            _plugin_stack.pop()
    else:
        import plugin_settings
        plugin_settings.invalidate(plugin_id)
        for key, value in dict(data or {}).items():
            plugin_settings.set_setting(plugin_id, key, value)
        if reload_settings:
            _extera.settings_changed(plugin_id)


def reload_settings(plugin_id):
    _extera.settings_changed(plugin_id)


# Registrations

def add_send_hook(plugin_id, priority=0):
    _send_hooks[plugin_id] = priority


def remove_send_hook(plugin_id):
    _send_hooks.pop(plugin_id, None)


def add_request_hook(plugin_id, name, match_substring, priority):
    _request_hooks.setdefault(plugin_id, {})[name] = (match_substring, priority)


def remove_request_hook(plugin_id, name):
    _request_hooks.get(plugin_id, {}).pop(name, None)


def add_menu_item(plugin_id, data):
    item_id = data.item_id or (plugin_id + ":" + uuid.uuid4().hex[:8])
    _menu_items[item_id] = (plugin_id, data)
    return item_id


def remove_menu_item(plugin_id, item_id):
    entry = _menu_items.get(item_id)
    if entry and entry[0] == plugin_id:
        del _menu_items[item_id]
        return True
    return False


def add_observer(delegate, notification_id):
    _observers.setdefault(int(notification_id), []).append(delegate)


def remove_observer(delegate, notification_id):
    bucket = _observers.get(int(notification_id), [])
    if delegate in bucket:
        bucket.remove(delegate)


def notify_file_handler_registered(plugin_id, ext):
    pass


def _clear_plugin(plugin_id):
    remove_send_hook(plugin_id)
    _request_hooks.pop(plugin_id, None)
    for item_id in [k for k, v in _menu_items.items() if v[0] == plugin_id]:
        del _menu_items[item_id]
    for bucket in _observers.values():
        for delegate in [d for d in bucket
                         if getattr(d, "_extera_plugin_id", None) == plugin_id]:
            bucket.remove(delegate)


# Loading

class Record:
    def __init__(self, path, meta):
        self.path = path
        self.meta = meta
        self.id = meta["id"]
        self.module = None
        self.instance = None
        self.error = None
        self.capabilities = None
        self.library = False

    def info(self):
        enabled = is_enabled(self.id)
        return {
            "id": self.id,
            "name": self.meta.get("name", self.id),
            "description": self.meta.get("description", ""),
            "author": self.meta.get("author", ""),
            "version": self.meta.get("version", ""),
            "icon": self.meta.get("icon") or "",
            "enabled": enabled,
            "safe_mode": _safe_mode,
            "trusted": is_trusted_file(self.path),
            "obfuscated": bool((self.capabilities or {}).get("obfuscation")),
            "loaded": self.instance is not None or (self.library and self.module is not None),
            "library": self.library,
            "error": self.error,
            "has_settings": self._has_settings(),
            "path": self.path,
            "permissions": self.meta.get("permissions") or [],
            "permissions_declared": self.meta.get("permissions_declared", False),
            "requirements": self.meta.get("requirements") or [],
            "app_version": self.meta.get("app_version"),
            "capabilities": self.capabilities or {},
        }

    def _has_settings(self):
        if not self.instance:
            return False
        method = type(self.instance).create_settings
        return method is not _base().BasePlugin.create_settings


def _base():
    import base_plugin
    return base_plugin


def _state_path():
    return os.path.join(SETTINGS_DIR, "_state.json")


def _load_state():
    try:
        with open(_state_path(), encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return {}


def _save_state(state):
    try:
        os.makedirs(SETTINGS_DIR, exist_ok=True)
        with open(_state_path(), "w", encoding="utf-8") as f:
            json.dump(state, f)
    except Exception as e:
        log(f"state save failed: {e}")


def is_enabled(plugin_id):
    return bool(_load_state().get(plugin_id, True))


def _set_enabled_state(plugin_id, enabled):
    state = _load_state()
    state[plugin_id] = bool(enabled)
    _save_state(state)


class _PluginModuleFinder(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if path is not None or fullname in sys.modules:
            return None
        record = _records.get(fullname)
        if record is None:
            return None
        loader = importlib.machinery.SourceFileLoader(fullname, record.path)
        return importlib.util.spec_from_file_location(
            fullname, record.path, loader=loader)


def _install_plugin_importer():
    if not any(isinstance(f, _PluginModuleFinder) for f in sys.meta_path):
        sys.meta_path.insert(0, _PluginModuleFinder())
    if PLUGINS_DIR and PLUGINS_DIR not in sys.path:
        sys.path.append(PLUGINS_DIR)


# Trust

def file_sha256(path):
    import hashlib
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _trusted_path():
    return os.path.join(SETTINGS_DIR, "_trusted.json")


def _trusted_hashes():
    global _trusted_cache
    if _trusted_cache is None:
        try:
            with open(_trusted_path(), encoding="utf-8") as f:
                _trusted_cache = set(json.load(f))
        except Exception:
            _trusted_cache = set()
    return _trusted_cache


def _add_trusted(digest):
    hashes = _trusted_hashes()
    if digest in hashes:
        return
    hashes.add(digest)
    try:
        os.makedirs(SETTINGS_DIR, exist_ok=True)
        with open(_trusted_path(), "w", encoding="utf-8") as f:
            json.dump(sorted(hashes), f)
    except Exception as e:
        log(f"trusted save failed: {e}")


def is_trusted_file(path):
    try:
        return file_sha256(path) in _trusted_hashes()
    except Exception:
        return False


def _is_obfuscated(record):
    caps = record.capabilities
    if caps is None:
        try:
            import capability_scan
            caps = record.capabilities = capability_scan.scan(record.path)
        except Exception:
            caps = record.capabilities = {}
    return bool(caps.get("obfuscation"))


def _blocked_reason(record):
    if _is_obfuscated(record):
        return "OBFUSCATED"
    if not _unknown_allowed and not is_trusted_file(record.path):
        return "UNKNOWN_SOURCE"
    return None


def _instantiate(record):
    base = _base()
    module = importlib.import_module(record.id)
    record.module = module
    plugin_class = None
    for value in list(module.__dict__.values()):
        if (isinstance(value, type)
                and issubclass(value, base.BasePlugin)
                and value is not base.BasePlugin
                and value.__module__ == module.__name__):
            plugin_class = value
    if plugin_class is None:
        record.library = True
        return
    instance = plugin_class()
    instance._extera_attach(record.id)
    instance.name = record.meta.get("name", record.id)
    record.instance = instance


def _activate(record):
    record.error = None
    blocked = _blocked_reason(record)
    if blocked:
        record.error = blocked
        log(f"plugin {record.id} blocked: {blocked}")
        return
    try:
        if record.instance is None and not record.library:
            import requirements_installer
            for problem in requirements_installer.ensure(
                    record.meta.get("requirements") or []):
                log(f"plugin {record.id} requirement failed: {problem}")
            _instantiate(record)
        if record.library:
            log(f"plugin library {record.id} loaded")
            return
        if record.instance is None:
            raise RuntimeError("No BasePlugin subclass found")
        with _plugin_context(record.id):
            record.instance.on_plugin_load()
        log(f"plugin {record.id} loaded")
    except Exception:
        record.error = traceback.format_exc(limit=6)
        record.instance = None
        _clear_plugin(record.id)
        log(f"plugin {record.id} failed:\n{record.error}")


def _deactivate(record):
    if record.instance is not None:
        try:
            with _plugin_context(record.id):
                record.instance._extera_cleanup_resources()
                record.instance.on_plugin_unload()
        except Exception:
            log(f"plugin {record.id} unload failed:\n{traceback.format_exc(limit=4)}")
    _clear_plugin(record.id)
    record.instance = None
    record.module = None
    sys.modules.pop(record.id, None)
    sys.modules.pop("plugin_" + record.id, None)


def load_all():
    for name in sorted(os.listdir(PLUGINS_DIR)) if os.path.isdir(PLUGINS_DIR) else []:
        if not name.endswith(".plugin"):
            continue
        path = os.path.join(PLUGINS_DIR, name)
        try:
            meta = read_metadata(path)
        except Exception as e:
            log(f"bad plugin file {name}: {e}")
            continue
        record = Record(path, meta)
        _records[record.id] = record
    _install_plugin_importer()
    for record in list(_records.values()):
        if is_enabled(record.id) and not _safe_mode:
            _activate(record)


# Dispatch

def _entity_to_wire(entity):
    if isinstance(entity, dict):
        return entity
    if isinstance(entity, (list, tuple)) and len(entity) >= 3:
        kind, offset, length = entity[0], int(entity[1]), int(entity[2])
        data = entity[3] if len(entity) > 3 else ""
        wire = {"type": kind, "offset": offset, "length": length}
        if kind in ("url", "text_link"):
            wire["url"] = data
        elif kind == "pre":
            wire["language"] = data
        elif kind in ("custom_emoji",):
            try:
                wire["document_id"] = int(data)
            except Exception:
                pass
        return wire
    to_tlrpc = getattr(entity, "to_tlrpc_object", None)
    if callable(to_tlrpc):
        try:
            return to_tlrpc()
        except Exception:
            return None
    return None


def _params_text(params):
    message = getattr(params, "message", None)
    if message is None:
        caption = getattr(params, "caption", None)
        if caption:
            return str(caption)
        return ""
    return str(message)


def _send_hook(args):
    base = _base()
    import client_utils
    params = _compat.SendMessageParams(
        args.get("peer"), args.get("text", ""), args.get("reply_to", 0))
    ordered = sorted(_send_hooks.items(), key=lambda x: -x[1])
    modified = False
    for plugin_id, _ in ordered:
        record = _records.get(plugin_id)
        if not record or not record.instance:
            continue
        try:
            with client_utils.hook_scope(args.get("account", 0)):
                with _plugin_context(plugin_id):
                    result = record.instance.on_send_message_hook(
                        args.get("account", 0), params)
        except Exception:
            log(f"[{plugin_id}] send hook failed:\n{traceback.format_exc(limit=4)}")
            continue
        if result is None:
            continue
        strategy = getattr(result, "strategy", base.HookStrategy.DEFAULT)
        if getattr(result, "params", None) is not None:
            params = result.params
        if strategy == base.HookStrategy.CANCEL:
            return {"strategy": "cancel"}
        if strategy in (base.HookStrategy.MODIFY, base.HookStrategy.MODIFY_FINAL):
            modified = True
            if strategy == base.HookStrategy.MODIFY_FINAL:
                break
    if not modified:
        return {"strategy": "default"}
    entities = getattr(params, "entities", None) or []
    wire = [e for e in (_entity_to_wire(entity) for entity in entities) if e]
    result = {"strategy": "modify", "text": _params_text(params), "entities": wire}
    if getattr(params, "replyQuote", None):
        result["quote"] = True
    return result


def _settings_items(plugin_id):
    record = _records.get(plugin_id)
    if not record or not record.instance:
        return []
    import ui.settings as settings
    try:
        with _plugin_context(plugin_id):
            items = record.instance.create_settings() or []
    except Exception:
        log(f"[{plugin_id}] create_settings failed:\n{traceback.format_exc(limit=4)}")
        return []
    settings.register_items(plugin_id, items)
    result = []
    for index, item in enumerate(items):
        data = settings.describe(item, index)
        if data is None:
            continue
        if "key" in data and data["key"]:
            data["value"] = get_setting(plugin_id, data["key"], data.get("default"))
        result.append(data)
    return result


_MENU_TYPE_ALIASES = {
    "message_context_menu": "MESSAGE_CONTEXT_MENU",
    "drawer_menu": "DRAWER_MENU",
    "main_menu": "MAIN_MENU",
    "chat_action_menu": "CHAT_ACTION_MENU",
    "profile_action_menu": "PROFILE_ACTION_MENU",
}


def _menu_list(args):
    wanted = args.get("type")
    wanted = _MENU_TYPE_ALIASES.get(str(wanted).lower(), str(wanted).upper())
    result = []
    for item_id, (plugin_id, data) in _menu_items.items():
        record = _records.get(plugin_id)
        if not record or not record.instance:
            continue
        menu_type = str(getattr(data.menu_type, "value", data.menu_type))
        if menu_type.upper() != wanted:
            continue
        text = data.text() if callable(data.text) else data.text
        result.append({"id": item_id, "text": str(text),
                       "subtext": str(data.subtext or ""),
                       "priority": data.priority})
    result.sort(key=lambda x: -x["priority"])
    return result


def _menu_click(args):
    entry = _menu_items.get(args.get("id"))
    if not entry:
        return False
    plugin_id, data = entry
    record = _records.get(plugin_id)
    if not record or not record.instance:
        return False
    context = dict(args.get("context") or {})
    context.setdefault("account", 0)
    context.setdefault("dialog_id", context.get("peer"))
    try:
        with _plugin_context(plugin_id):
            record.instance._extera_dispatch_menu_click(args.get("id"), context)
    except Exception:
        log(f"[{plugin_id}] menu click failed:\n{traceback.format_exc(limit=4)}")
    return True


def _matching_request_plugins(name):
    matches = []
    for plugin_id, hooks in _request_hooks.items():
        best = None
        for hook_name, (match_substring, priority) in hooks.items():
            if hook_name == name or (match_substring and hook_name in name):
                if best is None or priority > best[1]:
                    best = (hook_name, priority, match_substring)
        if best is not None:
            matches.append((best[1], plugin_id))
    matches.sort(key=lambda x: -x[0])
    return [plugin_id for _, plugin_id in matches]


def _event(args):
    import client_utils
    kind = args.get("type")
    account = args.get("account", 0)
    base = _base()
    if kind == "update":
        name = args.get("name", "")
        update = args.get("update") or {}
        for record in _records.values():
            if not record.instance:
                continue
            method = type(record.instance).on_update_hook
            if method is base.BasePlugin.on_update_hook:
                continue
            try:
                with client_utils.hook_scope(account):
                    with _plugin_context(record.id):
                        record.instance.on_update_hook(name, account, update)
            except Exception:
                log(f"[{record.id}] update hook failed:\n{traceback.format_exc(limit=4)}")
        return True
    if kind == "updates":
        name = args.get("name", "")
        updates = args.get("updates") or {}
        for record in _records.values():
            if not record.instance:
                continue
            method = type(record.instance).on_updates_hook
            if method is base.BasePlugin.on_updates_hook:
                continue
            try:
                with client_utils.hook_scope(account):
                    with _plugin_context(record.id):
                        record.instance.on_updates_hook(name, account, updates)
            except Exception:
                log(f"[{record.id}] updates hook failed:\n{traceback.format_exc(limit=4)}")
        return True
    if kind == "app":
        event = base.AppEvent.from_java(args.get("name", ""))
        if event is None:
            return False
        for record in _records.values():
            if not record.instance:
                continue
            try:
                with _plugin_context(record.id):
                    record.instance.on_app_event(event)
            except Exception:
                log(f"[{record.id}] app event failed:\n{traceback.format_exc(limit=4)}")
        return True
    if kind == "notification":
        notification_id = int(args.get("id", 0))
        payload = args.get("args") or {}
        for delegate in list(_observers.get(notification_id, [])):
            try:
                delegate.didReceivedNotification(notification_id, account, payload)
            except Exception:
                log(f"notification delegate failed:\n{traceback.format_exc(limit=4)}")
        return True
    if kind == "intent":
        from intents import IntentsManager
        context = dict(args.get("context") or {})
        return IntentsManager.dispatch(bool(args.get("before")), context)
    if kind == "sheet_closed":
        import android_views
        return android_views.handle_sheet_closed(args.get("id"))
    return False


def _request_hook_dispatch(args):
    import client_utils
    base = _base()
    stage = args.get("stage")
    name = args.get("name", "")
    account = args.get("account", 0)
    payload = args.get("payload") or {}
    ordered = _matching_request_plugins(name)
    if stage == "pre":
        for plugin_id in ordered:
            record = _records.get(plugin_id)
            if not record or not record.instance:
                continue
            method = type(record.instance).pre_request_hook
            if method is base.BasePlugin.pre_request_hook:
                continue
            try:
                with client_utils.hook_scope(account):
                    with _plugin_context(plugin_id):
                        result = record.instance.pre_request_hook(name, account, payload)
            except Exception:
                log(f"[{plugin_id}] pre_request_hook failed:\n{traceback.format_exc(limit=4)}")
                continue
            if result is None:
                continue
            strategy = getattr(result, "strategy", base.HookStrategy.DEFAULT)
            if strategy == base.HookStrategy.CANCEL:
                return {"strategy": "cancel"}
            if strategy in (base.HookStrategy.MODIFY, base.HookStrategy.MODIFY_FINAL) \
                    and getattr(result, "request", None) is not None:
                return {"strategy": "modify", "request": result.request}
        return {"strategy": "continue"}
    if stage == "post":
        for plugin_id in ordered:
            record = _records.get(plugin_id)
            if not record or not record.instance:
                continue
            method = type(record.instance).post_request_hook
            if method is base.BasePlugin.post_request_hook:
                continue
            try:
                with client_utils.hook_scope(account):
                    with _plugin_context(plugin_id):
                        record.instance.post_request_hook(
                            name, account, args.get("response"), args.get("error"))
            except Exception:
                log(f"[{plugin_id}] post_request_hook failed:\n{traceback.format_exc(limit=4)}")
        return {"strategy": "continue"}
    return {"strategy": "continue"}


def _request_callback(args):
    import client_utils
    return client_utils.extera_host_dispatch_callback(
        args.get("id"), args.get("response"), args.get("error"),
        raw=args.get("raw"), code=args.get("code", 0))


def _view_click(args):
    import android_views
    return android_views.dispatch_click(args.get("id"))


def _text_changed(args):
    import android_views
    return android_views.dispatch_text_changed(args.get("id"), args.get("text"))


def _install(args):
    import shutil
    import capability_scan
    path = args["path"]
    meta = read_metadata(path)
    os.makedirs(PLUGINS_DIR, exist_ok=True)
    target = os.path.join(PLUGINS_DIR, meta["id"] + ".plugin")
    old = _records.get(meta["id"])
    if old:
        _deactivate(old)
    if os.path.abspath(path) != os.path.abspath(target):
        shutil.copyfile(path, target)
    record = Record(target, meta)
    try:
        record.capabilities = capability_scan.scan(target)
    except Exception:
        record.capabilities = {}
    _records[record.id] = record
    _set_enabled_state(record.id, True)
    if not _safe_mode:
        _activate(record)
    return record.info()


def _delete(args):
    record = _records.pop(args["id"], None)
    if not record:
        return False
    _deactivate(record)
    try:
        os.remove(record.path)
    except Exception:
        pass
    return True


def _set_enabled(args):
    record = _records.get(args["id"])
    if not record:
        return None
    if _safe_mode and args["enabled"]:
        info = record.info()
        info["blocked"] = True
        return info
    _set_enabled_state(record.id, args["enabled"])
    if args["enabled"] and not _safe_mode:
        _activate(record)
    else:
        _deactivate(record)
    return record.info()


def _setting_changed(args):
    import ui.settings as settings
    plugin_id = args["id"]
    item = settings.find_item(plugin_id, args["index"])
    if item is None:
        return False
    key = getattr(item, "key", None)
    value = args.get("value")
    if key:
        set_setting(plugin_id, key, value)
    callback = getattr(item, "on_change", None)
    if callback:
        try:
            with _plugin_context(plugin_id):
                callback(value)
        except Exception:
            log(f"[{plugin_id}] on_change failed:\n{traceback.format_exc(limit=4)}")
    return True


def _setting_click(args):
    import ui.settings as settings
    plugin_id = args["id"]
    item = settings.find_item(plugin_id, args["index"])
    if item is None:
        return False
    if args.get("long"):
        callback = getattr(item, "on_long_click", None)
    else:
        callback = getattr(item, "on_click", None) or getattr(item, "on_long_click", None)
    if not callback:
        return False
    try:
        with _plugin_context(plugin_id):
            try:
                callback(_compat.Stub("view"))
            except TypeError:
                callback()
    except Exception:
        log(f"[{plugin_id}] on_click failed:\n{traceback.format_exc(limit=4)}")
    return True


def _alert_click(args):
    import ui.alert as alert
    alert.dispatch(args["id"], args["which"])
    return True


def _trust(args):
    path = args.get("path")
    digest = args.get("hash") or (file_sha256(path) if path else None)
    if digest:
        _add_trusted(digest)
    return True


def _is_trusted(args):
    return is_trusted_file(args["path"])


def _set_unknown_sources(args):
    global _unknown_allowed
    _unknown_allowed = bool(args.get("enabled"))
    for record in list(_records.values()):
        if not is_enabled(record.id) or _safe_mode:
            continue
        if record.instance is None and not record.library:
            _activate(record)
        elif _blocked_reason(record):
            _deactivate(record)
            record.error = _blocked_reason(record)
    return [r.info() for r in _records.values()]


def _capability_lines(args):
    import capability_scan
    return capability_scan.locations(args["path"])


def _source(args):
    path = args.get("path")
    if not path and args.get("id") in _records:
        path = _records[args["id"]].path
    with open(path, "rb") as f:
        data = f.read(4 * 1024 * 1024)
    return data.decode("utf-8", "replace")


def _guard_scan(args):
    import plugin_guard
    path = args.get("path")
    if not path and args.get("id") in _records:
        path = _records[args["id"]].path
    return plugin_guard.scan(path)


def _metadata(args):
    meta = read_metadata(args["path"])
    result = dict(meta)
    try:
        import capability_scan
        result["capabilities"] = capability_scan.scan(args["path"])
    except Exception:
        result["capabilities"] = {}
    return result


def _init(args):
    global PLUGINS_DIR, SETTINGS_DIR, _safe_mode
    PLUGINS_DIR = args["plugins"]
    SETTINGS_DIR = args["settings"]
    _safe_mode = bool(args.get("safe_mode"))
    global _unknown_allowed
    _unknown_allowed = bool(args.get("unknown_sources"))
    _compat.install()
    import plugin_settings
    plugin_settings.configure(SETTINGS_DIR)
    plugin_settings.set_notifier(lambda plugin_id: _extera.settings_changed(plugin_id))
    os.makedirs(PLUGINS_DIR, exist_ok=True)
    os.makedirs(SETTINGS_DIR, exist_ok=True)
    import requirements_installer
    requirements_installer.configure(
        os.path.join(os.path.dirname(PLUGINS_DIR), "plugin_deps"))
    load_all()
    return [r.info() for r in _records.values()]


def _set_safe_mode(args):
    global _safe_mode
    enabled = bool(args.get("enabled"))
    if enabled == _safe_mode:
        return [r.info() for r in _records.values()]
    _safe_mode = enabled
    for record in list(_records.values()):
        if enabled:
            _deactivate(record)
        elif is_enabled(record.id):
            _activate(record)
    return [r.info() for r in _records.values()]


def _reload(args):
    record = _records.get(args["id"])
    if not record:
        return False
    _deactivate(record)
    if is_enabled(record.id) and not _safe_mode:
        _activate(record)
    return record.info()


def _shutdown(args):
    for record in list(_records.values()):
        _deactivate(record)
    return True


_METHODS = {
    "init": _init,
    "shutdown": _shutdown,
    "reload": _reload,
    "safe_mode": _set_safe_mode,
    "trust": _trust,
    "is_trusted": _is_trusted,
    "unknown_sources": _set_unknown_sources,
    "source": _source,
    "capability_lines": _capability_lines,
    "list": lambda args: [r.info() for r in _records.values()],
    "send_hook": _send_hook,
    "settings": lambda args: _settings_items(args["id"]),
    "setting_changed": _setting_changed,
    "setting_click": _setting_click,
    "alert_click": _alert_click,
    "menu_list": _menu_list,
    "menu_click": _menu_click,
    "install": _install,
    "delete": _delete,
    "set_enabled": _set_enabled,
    "metadata": _metadata,
    "guard_scan": _guard_scan,
    "event": _event,
    "request_hook": _request_hook_dispatch,
    "request_callback": _request_callback,
    "view_click": _view_click,
    "text_changed": _text_changed,
}


def dispatch(method, payload):
    try:
        args = json.loads(payload) if payload else {}
        with _lock:
            result = _METHODS[method](args)
        return json.dumps({"ok": True, "result": result}, ensure_ascii=False)
    except Exception:
        error = traceback.format_exc(limit=6)
        log(f"dispatch {method} failed:\n{error}")
        return json.dumps({"ok": False, "error": error}, ensure_ascii=False)
