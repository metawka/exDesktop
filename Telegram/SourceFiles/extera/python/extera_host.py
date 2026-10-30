import ast
import json
import os
import sys
import threading
import traceback
import types
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
_settings_cache = {}
_safe_mode = False

META_KEYS = (
    "__id__", "__name__", "__description__", "__author__", "__version__",
    "__icon__", "__min_version__", "__app_version__", "__sdk_version__",
    "__requirements__",
)


# Log

def log(message):
    try:
        _extera.log(str(message))
    except Exception:
        pass


# Metadata

def read_metadata(path):
    with open(path, encoding="utf-8") as f:
        source = f.read()
    tree = ast.parse(source, filename=path)
    meta = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1:
            target = node.targets[0]
            if isinstance(target, ast.Name) and target.id in META_KEYS:
                try:
                    meta[target.id.strip("_")] = ast.literal_eval(node.value)
                except Exception:
                    pass
    if not meta.get("id"):
        raise ValueError("Plugin has no __id__")
    return meta, source


# Settings storage

def _settings_path(plugin_id):
    return os.path.join(SETTINGS_DIR, plugin_id + ".json")


def _load_settings(plugin_id):
    if plugin_id in _settings_cache:
        return _settings_cache[plugin_id]
    data = {}
    try:
        with open(_settings_path(plugin_id), encoding="utf-8") as f:
            data = json.load(f)
    except Exception:
        data = {}
    _settings_cache[plugin_id] = data
    return data


def _save_settings(plugin_id):
    try:
        os.makedirs(SETTINGS_DIR, exist_ok=True)
        target = _settings_path(plugin_id)
        temp_target = target + ".tmp"
        with open(temp_target, "w", encoding="utf-8") as f:
            json.dump(_settings_cache.get(plugin_id, {}), f, ensure_ascii=False)
        os.replace(temp_target, target)
    except Exception as e:
        log(f"settings save failed for {plugin_id}: {e}")


def get_setting(plugin_id, key, default=None):
    with _lock:
        return _load_settings(plugin_id).get(key, default)


def set_setting(plugin_id, key, value, reload_settings=False):
    with _lock:
        _load_settings(plugin_id)[key] = value
        _save_settings(plugin_id)
    if reload_settings:
        _extera.settings_changed(plugin_id)


def export_settings(plugin_id):
    with _lock:
        return dict(_load_settings(plugin_id))


def import_settings(plugin_id, data, reload_settings=True):
    with _lock:
        _settings_cache[plugin_id] = dict(data or {})
        _save_settings(plugin_id)
    if reload_settings:
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


def _clear_plugin(plugin_id):
    remove_send_hook(plugin_id)
    _request_hooks.pop(plugin_id, None)
    for item_id in [k for k, v in _menu_items.items() if v[0] == plugin_id]:
        del _menu_items[item_id]
    try:
        from ui import settings
        settings.unregister_items(plugin_id)
    except Exception:
        pass


# Loading

class Record:
    def __init__(self, path, meta):
        self.path = path
        self.meta = meta
        self.id = meta["id"]
        self.module = None
        self.instance = None
        self.error = None

    def info(self):
        enabled = is_enabled(self.id)
        return {
            "id": self.id,
            "name": self.meta.get("name", self.id),
            "description": self.meta.get("description", ""),
            "author": self.meta.get("author", ""),
            "version": self.meta.get("version", ""),
            "icon": self.meta.get("icon", ""),
            "enabled": enabled,
            "loaded": self.instance is not None,
            "error": self.error,
            "has_settings": self._has_settings(),
            "path": self.path,
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


def _instantiate(record):
    base = _base()
    module = types.ModuleType("plugin_" + record.id)
    module.__file__ = record.path
    sys.modules[module.__name__] = module
    with open(record.path, encoding="utf-8") as f:
        source = f.read()
    exec(compile(source, record.path, "exec"), module.__dict__)
    plugin_class = None
    for value in list(module.__dict__.values()):
        if (isinstance(value, type)
                and issubclass(value, base.BasePlugin)
                and value is not base.BasePlugin
                and value.__module__ == module.__name__):
            plugin_class = value
    if plugin_class is None:
        raise RuntimeError("No BasePlugin subclass found")
    instance = plugin_class()
    instance.id = record.id
    instance.name = record.meta.get("name", record.id)
    record.module = module
    record.instance = instance


def _activate(record):
    record.error = None
    try:
        if record.instance is None:
            _instantiate(record)
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
            record.instance.on_plugin_unload()
        except Exception:
            log(f"plugin {record.id} unload failed:\n{traceback.format_exc(limit=4)}")
    _clear_plugin(record.id)
    record.instance = None
    record.module = None
    sys.modules.pop("plugin_" + record.id, None)


def load_all():
    for name in sorted(os.listdir(PLUGINS_DIR)) if os.path.isdir(PLUGINS_DIR) else []:
        if not name.endswith(".plugin"):
            continue
        path = os.path.join(PLUGINS_DIR, name)
        try:
            meta, _ = read_metadata(path)
        except Exception as e:
            log(f"bad plugin file {name}: {e}")
            continue
        record = Record(path, meta)
        _records[record.id] = record
        if is_enabled(record.id) and not _safe_mode:
            _activate(record)


# Dispatch

def _send_hook(args):
    base = _base()
    params = _compat.SendMessageParams(
        args.get("peer"), args.get("text", ""), args.get("reply_to", 0))
    ordered = sorted(_send_hooks.items(), key=lambda x: -x[1])
    modified = False
    for plugin_id, _ in ordered:
        record = _records.get(plugin_id)
        if not record or not record.instance:
            continue
        try:
            result = record.instance.on_send_message_hook(0, params)
        except Exception:
            log(f"[{plugin_id}] send hook failed:\n{traceback.format_exc(limit=4)}")
            continue
        if result is None:
            continue
        strategy = getattr(result, "strategy", base.HookStrategy.DEFAULT)
        if result.params is not None:
            params = result.params
        if strategy == base.HookStrategy.CANCEL:
            return {"strategy": "cancel"}
        if strategy in (base.HookStrategy.MODIFY, base.HookStrategy.MODIFY_FINAL):
            modified = True
            if strategy == base.HookStrategy.MODIFY_FINAL:
                break
    if not modified:
        return {"strategy": "default"}
    return {"strategy": "modify", "text": str(getattr(params, "message", "") or "")}


def _settings_items(plugin_id):
    record = _records.get(plugin_id)
    if not record or not record.instance:
        return []
    import ui.settings as settings
    try:
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


def _menu_list(args):
    wanted = args.get("type")
    result = []
    for item_id, (plugin_id, data) in _menu_items.items():
        record = _records.get(plugin_id)
        if not record or not record.instance:
            continue
        menu_type = getattr(data.menu_type, "value", data.menu_type)
        if menu_type != wanted:
            continue
        text = data.text() if callable(data.text) else data.text
        result.append({"id": item_id, "text": str(text),
                       "subtext": str(data.subtext or ""),
                       "priority": data.priority})
    result.sort(key=lambda x: -x["priority"])
    return result


def _menu_click(args):
    entry = _menu_items.get(args.get("id"))
    if not entry or not entry[1].on_click:
        return False
    context = dict(args.get("context") or {})
    context.setdefault("account", 0)
    context.setdefault("dialog_id", context.get("peer"))
    try:
        entry[1].on_click(context)
    except Exception:
        log(f"[{entry[0]}] menu click failed:\n{traceback.format_exc(limit=4)}")
    return True


def _install(args):
    import shutil
    path = args["path"]
    meta, _ = read_metadata(path)
    os.makedirs(PLUGINS_DIR, exist_ok=True)
    target = os.path.join(PLUGINS_DIR, meta["id"] + ".plugin")
    old = _records.get(meta["id"])
    if old:
        _deactivate(old)
    if os.path.abspath(path) != os.path.abspath(target):
        shutil.copyfile(path, target)
    record = Record(target, meta)
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
            callback(value)
        except Exception:
            log(f"[{plugin_id}] on_change failed:\n{traceback.format_exc(limit=4)}")
    return True


def _setting_click(args):
    import ui.settings as settings
    plugin_id = args["id"]
    item = settings.find_item(plugin_id, args["index"])
    callback = getattr(item, "on_click", None) if item is not None else None
    if not callback:
        return False
    try:
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


def _metadata(args):
    meta, _ = read_metadata(args["path"])
    return meta


def _init(args):
    global PLUGINS_DIR, SETTINGS_DIR, _safe_mode
    PLUGINS_DIR = args["plugins"]
    SETTINGS_DIR = args["settings"]
    _safe_mode = bool(args.get("safe_mode"))
    _compat.install()
    os.makedirs(PLUGINS_DIR, exist_ok=True)
    os.makedirs(SETTINGS_DIR, exist_ok=True)
    load_all()
    return [r.info() for r in _records.values()]


def _shutdown(args):
    for record in list(_records.values()):
        _deactivate(record)
    return True


_METHODS = {
    "init": _init,
    "shutdown": _shutdown,
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
