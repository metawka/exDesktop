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


_sbx_push = getattr(_extera, "sbx_push", None)
_sbx_pop = getattr(_extera, "sbx_pop", None)
_sbx_owner = getattr(_extera, "sbx_owner", None)


@contextlib.contextmanager
def _plugin_context(plugin_id):
    _plugin_stack.append(plugin_id)
    token = None
    if _sbx_push is not None and plugin_id:
        token = _sbx_push(str(plugin_id))
    try:
        yield
    finally:
        if token is not None and _sbx_pop is not None:
            _sbx_pop(token)
        _plugin_stack.pop()


def owner_now():
    owner = _sbx_owner
    if owner is None:
        return current_plugin_id() or ""
    try:
        return owner()
    except Exception:
        return "<unknown>"


def owner_scope(owner):
    return _plugin_context(owner if owner else "<unknown>")


# Metadata

def read_metadata(path):
    import exd_elyx
    if exd_elyx.is_elyx(path):
        return exd_elyx.read_meta(path)
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
                         if _delegate_owner(d) == plugin_id]:
            bucket.remove(delegate)


def _delegate_owner(delegate):
    try:
        return vars(delegate).get("_extera_plugin_id")
    except TypeError:
        return None


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
        self.installing = False
        self.requirements_done = False
        self.elyx = meta.get("format") == "elyx"

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
            "installing": self.installing,
            "has_settings": self._has_settings(),
            "path": self.path,
            "permissions": self.meta.get("permissions") or [],
            "permissions_declared": self.meta.get("permissions_declared", False),
            "requirements": self.meta.get("requirements") or [],
            "app_version": self.meta.get("app_version"),
            "capabilities": self.capabilities or {},
            "format": "elyx" if self.elyx else "plugin",
            "desktop": bool(self.meta.get("desktop")),
            "ported_from": self.meta.get("ported_from") or "",
            "unsupported": list(getattr(self.instance, "__dict__", {}).get("_exd_unsupported", []))
            if self.instance is not None else [],
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


_state_cache = [None]
_digests = {}


def _load_state():
    if _state_cache[0] is None:
        try:
            with open(_state_path(), encoding="utf-8") as f:
                _state_cache[0] = dict(json.load(f))
        except Exception:
            return {}
    return dict(_state_cache[0])


def _save_state(state):
    _state_cache[0] = dict(state)
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
        if record is None or getattr(record, "elyx", False):
            return None
        import exd_runtime
        loader = exd_runtime.PluginLoader(
            importlib.machinery.SourceFileLoader(fullname, record.path), record.id)
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
        stat = os.stat(path)
        key = (stat.st_mtime_ns, stat.st_size)
        cached = _digests.get(path)
        if cached is None or cached[0] != key:
            cached = (key, file_sha256(path))
            _digests[path] = cached
        return cached[1] in _trusted_hashes()
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
    if record.elyx:
        import exd_elyx
        module = exd_elyx.load(record, sys.modules[__name__])
    else:
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
    for key in ("version", "author", "description", "icon"):
        if key not in instance.__dict__ and not hasattr(type(instance), key):
            setattr(instance, key, record.meta.get(key) or "")
    record.instance = instance


_HOST_CALL = {}


def _engine_hooked(name):
    try:
        import exd_android
        import exd_java
        return exd_java.has_shim_hooks(exd_android.PythonPluginsEngine, name)
    except Exception:
        return False


def _activate(record):
    if record.instance is None and not record.library and _engine_hooked("loadPlugin"):
        import exd_android
        exd_android.PythonPluginsEngine.loadPlugin(record.id, record.path)
        return
    _activate_core(record)


def _activate_core(record):
    if record.instance is not None and record.instance.__dict__.get("initialized"):
        return
    record.error = None
    blocked = _blocked_reason(record)
    if blocked:
        record.error = blocked
        log(f"plugin {record.id} blocked: {blocked}")
        return
    try:
        if record.instance is None and not record.library:
            import requirements_installer
            requirements = record.meta.get("requirements") or []
            if not record.requirements_done and requirements_installer.missing(requirements):
                _install_requirements(record, requirements)
                return
            _instantiate(record)
        if record.library:
            log(f"plugin library {record.id} loaded")
            return
        if record.instance is None:
            raise RuntimeError("No BasePlugin subclass found")
        with _plugin_context(record.id):
            record.instance.on_plugin_load()
        record.instance.__dict__["initialized"] = True
        log(f"plugin {record.id} loaded")
        if record.instance.__dict__.get("_exd_virtual_app_create"):
            _fire_app_create(record)
    except Exception as e:
        record.error = traceback.format_exc(limit=6)
        library = getattr(e, "name", None) if isinstance(e, ModuleNotFoundError) else None
        if library and library.split(".")[0] in _LIBRARIES:
            record.error += "\n" + _library_text(library.split(".")[0])
        record.instance = None
        _clear_plugin(record.id)
        log(f"plugin {record.id} failed:\n{record.error}")


_LIBRARIES = {"zwylib": "ZwyLib", "cactuslib": "CactusLib", "plugins_manager": "Plugins Manager"}


def _library_text(name):
    try:
        import exd_java
        russian = exd_java.language().startswith("ru")
    except Exception:
        russian = False
    title = _LIBRARIES.get(name, name)
    return ("Требуется плагин-библиотека %s: установите её из канала плагинов." % title) if russian \
        else ("Requires the library plugin %s: install it from the plugins channel." % title)


def _install_requirements(record, requirements):
    if record.installing:
        return
    record.installing = True
    log(f"plugin {record.id} installing requirements: {', '.join(map(str, requirements))}")

    def work():
        import requirements_installer
        try:
            problems = requirements_installer.ensure(requirements)
        except Exception as e:
            problems = [repr(e)]

        def finish():
            record.installing = False
            record.requirements_done = True
            for problem in problems:
                log(f"plugin {record.id} requirement failed: {problem}")
            if _records.get(record.id) is record and is_enabled(record.id) and not _safe_mode:
                _activate(record)
            try:
                _extera.plugins_changed()
            except Exception:
                pass

        _extera.run_on_main(finish, 0)

    threading.Thread(target=work, name="exd-requirements", daemon=True).start()


def _deactivate(record):
    if record.instance is not None and _engine_hooked("unloadPlugin"):
        import exd_android
        exd_android.PythonPluginsEngine.unloadPlugin(record.id)
        if record.instance is not None:
            return
    _deactivate_core(record)


def _deactivate_core(record):
    if record.instance is not None:
        instance = record.instance
        try:
            with _plugin_context(record.id):
                instance.on_plugin_unload()
        except Exception:
            log(f"plugin {record.id} unload failed:\n{traceback.format_exc(limit=4)}")
        try:
            with _plugin_context(record.id):
                instance._extera_cleanup_resources()
        except Exception:
            log(f"plugin {record.id} cleanup failed:\n{traceback.format_exc(limit=4)}")
        instance.__dict__["initialized"] = False
    _clear_plugin(record.id)
    record.instance = None
    record.module = None
    if record.elyx:
        import exd_elyx
        exd_elyx.unload(record.id)
    sys.modules.pop(record.id, None)
    sys.modules.pop("plugin_" + record.id, None)


def load_all():
    import exd_elyx
    for name in sorted(os.listdir(PLUGINS_DIR)) if os.path.isdir(PLUGINS_DIR) else []:
        if not name.lower().endswith(".plugin") and not exd_elyx.is_elyx(name):
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
        virtual = list(record.instance.__dict__.get("_exd_virtual_send") or ())
        for handler in virtual:
            before_text = _params_text(params)
            before_entities = list(getattr(params, "entities", None) or [])
            param = base.VirtualParam([params], _compat.Stub("SendMessagesHelper"),
                                      _compat.Missing("SendMessagesHelper.sendMessage"))
            try:
                with client_utils.hook_scope(args.get("account", 0)):
                    with _plugin_context(plugin_id):
                        replace = getattr(handler, "replace_hooked_method", None)
                        before = getattr(handler, "before_hooked_method", None)
                        if callable(replace):
                            replace(param)
                            param.returnEarly = True
                        elif callable(before):
                            before(param)
            except Exception:
                log(f"[{plugin_id}] send hook failed:\n{traceback.format_exc(limit=4)}")
                continue
            if param.returnEarly:
                return {"strategy": "cancel"}
            if param.args and param.args[0] is not params and param.args[0] is not None:
                params = param.args[0]
                modified = True
            if _params_text(params) != before_text or list(getattr(params, "entities", None) or []) != before_entities:
                modified = True
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


def _settings_fill_items(record):
    if not any(r.instance and r.instance.__dict__.get("_exd_virtual_settings_fill") for r in _records.values()):
        return []
    import exd_android
    import exd_java
    import exd_java_more2
    import ui.settings as settings
    values = exd_java.ArrayList()
    activity = exd_java_more2.PluginSettingsActivity(exd_android.Plugin(record))
    _run_virtual("settings_fill", activity, "PluginSettingsActivity.fillItems", [values, None])
    out = []
    for value in list(values):
        if isinstance(value, settings.Custom):
            out.append(value)
        elif hasattr(value, "viewType"):
            listener = value.__dict__.get("onClickListener") or value.__dict__.get("clickCallback")
            out.append(settings.Custom(item=value, on_click=getattr(listener, "onClick", listener)))
    return out


def _setting_view(item):
    import exd_misc
    uitem = getattr(item, "item", None)
    view = exd_misc.TextCheckCell2(None)
    if uitem is not None:
        import ui.settings as settings
        view.setText(settings.uitem_text(uitem))
        view.setChecked(bool(getattr(uitem, "checked", False)))
        if "collapsed" in uitem.__dict__:
            view.setCollapsed(uitem.__dict__["collapsed"])
    return view


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
    items = list(items) + _settings_fill_items(record)
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


_settings_uitems = {}


def _settings_activity_items():
    import exd_java
    import exd_java_more2
    uitem = exd_java_more2.UItem
    seed = [uitem("button", -1, 0, "exteraGram")]
    items = exd_java.ArrayList(list(seed))
    cls = exd_java._CLASSES.get("org.telegram.ui.SettingsActivity")
    activity = cls() if cls is not None else None
    _run_virtual("settings_main", activity, "SettingsActivity.fillItems", [items, None])
    _settings_uitems.clear()
    result = []
    for index, item in enumerate(list(items)):
        if any(item is entry for entry in seed):
            continue
        text = getattr(item, "text", None)
        if not text:
            continue
        key = "settings_item:%d:%d" % (index, int(getattr(item, "id", 0) or 0))
        _settings_uitems[key] = (activity, item, index)
        result.append({"id": key, "text": str(text), "subtext": "", "priority": 0})
    return result


def _settings_item_click(key):
    import android_views
    entry = _settings_uitems.get(key)
    if entry is None:
        return False
    activity, item, index = entry
    _run_virtual("settings_main_click", activity, "SettingsActivity.onClick",
                 [item, android_views.View(None), index, 0.0, 0.0])
    return True


def _menu_list(args):
    wanted = args.get("type")
    wanted = _MENU_TYPE_ALIASES.get(str(wanted).lower(), str(wanted).upper())
    result = []
    if wanted == "DRAWER_MENU":
        try:
            result.extend(_settings_activity_items())
        except Exception:
            log(f"settings items failed:\n{traceback.format_exc(limit=4)}")
    for item_id, (plugin_id, data) in _menu_items.items():
        record = _records.get(plugin_id)
        if not record or not record.instance:
            continue
        menu_type = str(getattr(data.menu_type, "value", data.menu_type))
        if menu_type.upper() != wanted:
            continue
        if callable(data.text):
            with _plugin_context(plugin_id):
                text = data.text()
        else:
            text = data.text
        result.append({"id": item_id, "text": str(text),
                       "subtext": str(data.subtext or ""),
                       "priority": data.priority})
    result.sort(key=lambda x: -x["priority"])
    return result


def _menu_click(args):
    if str(args.get("id") or "").startswith("settings_item:"):
        return _settings_item_click(str(args.get("id")))
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
        import _compat
        import exd_java
        import tgnet_bridge
        context.setdefault("context", _compat._LaunchActivity.instance)
        dialog = int(context.get("dialog_id") or 0)
        if dialog > 0:
            user = tgnet_bridge.get_user(dialog)
            if user is not None:
                context.setdefault("user", user)
                context.setdefault("userId", int(user.id))
        elif dialog < 0:
            chat = tgnet_bridge.get_chat(dialog)
            if chat is not None:
                context.setdefault("chat", chat)
                context.setdefault("chatId", int(chat.id))
        if context.get("message_id"):
            context.setdefault("message", exd_java.message_object({
                "id": int(context.get("message_id")), "dialog_id": dialog,
                "text": context.get("message_text") or ""}, int(context.get("account") or 0)))
    except Exception:
        log(f"menu context failed:\n{traceback.format_exc(limit=3)}")
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


_screen_state = {"chat": 0}


def _run_virtual(kind, this, name, args=(), original=None, account=0, records=None):
    import client_utils
    import exd_xposed
    import _compat
    base = _base()
    entries = []
    for record in records if records is not None else list(_records.values()):
        if not record.instance:
            continue
        for handler in list(record.instance.__dict__.get("_exd_virtual_" + kind) or ()):
            entries.append({"plugin": record.id, "handler": handler,
                            "priority": getattr(handler, "priority", 50) if isinstance(
                                getattr(handler, "priority", 50), int) else 50})
    param = base.VirtualParam(list(args), this, _compat.Missing(name))
    if not entries:
        return param, False
    with client_utils.hook_scope(account):
        try:
            exd_xposed.run(entries, param, original or (lambda p: None))
        except Exception:
            log(f"{name} hook failed: {traceback.format_exc(limit=4)}")
    return param, bool(param.returnEarly)


def _virtual_screen(kind, fragment, name, args=()):
    _run_virtual(kind, fragment, name, args)


def _fire_app_create(record):
    import _compat
    _run_virtual("app_create", _compat._LaunchActivity.instance, "LaunchActivity.onCreate", [None],
                 records=[record])


def sync_ui_hooks(exclude=None):
    base = _base()
    kinds = set()
    for record in list(_records.values()):
        if not record.instance or record.id == exclude:
            continue
        for kind, target in base._HOST_KINDS.items():
            if record.instance.__dict__.get("_exd_virtual_" + kind):
                kinds.add(target)
    try:
        _extera.set_ui_hooks(json.dumps(sorted(kinds)))
    except Exception:
        pass


class _SubtitleView:
    def __init__(self, owner, field):
        self._owner = owner
        self._field = field

    def setText(self, text, *args):
        setattr(self._owner, self._field, str(text))

    def getText(self):
        return getattr(self._owner, self._field)


class _ChatAvatarContainer:
    _java_name = "org.telegram.ui.Components.ChatAvatarContainer"

    def __init__(self, dialog_id, subtitle):
        import tgnet_bridge
        self.dialog_id = dialog_id
        self.subtitle = subtitle
        self.title = None
        self.fragment = tgnet_bridge.ChatActivity(dialog_id)

    def setSubtitle(self, text, *args):
        self.subtitle = str(text)

    def setTitle(self, text, *args):
        self.title = str(text)

    def getSubtitleTextView(self):
        return _SubtitleView(self, "subtitle")

    def getTitleTextView(self):
        return _SubtitleView(self, "title")

    def getLastSubtitle(self):
        return self.subtitle

    def getChatActivity(self):
        return self.fragment

    def getDialogId(self):
        return self.dialog_id


class _ChatMessageCell:
    _java_name = "org.telegram.ui.Cells.ChatMessageCell"

    def __init__(self, message):
        self.message = message

    def getMessageObject(self):
        return self.message

    def getPrimaryMessageObject(self):
        return self.message

    def setMessageObject(self, message, *args):
        self.message = message


def _ui_hook(args):
    import exd_java
    kind = str(args.get("kind") or "")
    text = str(args.get("text") or "")
    if kind == "message_text":
        data = dict(args.get("message") or {})
        data.setdefault("text", text)
        message = exd_java.message_object(data, 0)
        cell = _ChatMessageCell(message)
        _run_virtual("message_text", cell, "ChatMessageCell.setMessageObject", [message, None, False, False])
        shown = str(message.messageText) if message.messageText is not None else text
        if shown == text:
            shown = str(getattr(message.messageOwner, "message", text) or "")
        return shown if shown != text else None
    if kind == "chat_subtitle":
        container = _ChatAvatarContainer(int(args.get("dialog_id") or 0), text)
        param, _ = _run_virtual("chat_subtitle_set", container, "ChatAvatarContainer.setSubtitle", [text])
        if param.args and str(param.args[0]) != text and container.subtitle == text:
            container.subtitle = str(param.args[0])
        _run_virtual("chat_subtitle", container, "ChatAvatarContainer.updateSubtitle", [False])
        return container.subtitle if container.subtitle != text else None
    if kind == "dialog_name":
        return _dialog_name(int(args.get("dialog_id") or 0), text)
    if kind == "chat_title":
        container = _ChatAvatarContainer(int(args.get("dialog_id") or 0), None)
        container.title = text
        param, _ = _run_virtual("chat_title", container, "ChatAvatarContainer.setTitle", [text])
        if param.args and str(param.args[0]) != text and container.title == text:
            container.title = str(param.args[0])
        return container.title if container.title and container.title != text else None
    if kind == "copy_text":
        import tgnet_bridge
        data = dict(args.get("message") or {})
        message = exd_java.message_object(data, 0)
        chat = tgnet_bridge.ChatActivity(int(data.get("dialog_id") or 0))
        param, _ = _run_virtual("copy_text", chat, "ChatActivity.getMessageContent", [message, 0, True],
                                original=lambda p: text)
        result = param.getResult() if hasattr(param, "getResult") else None
        if result is None:
            return None
        result = str(result)
        return result if result != text else None
    return None


def _dialog_name(dialog_id, text):
    import exd_player
    import tgnet_bridge
    cell = exd_player.DialogCell(None)
    cell.currentDialogId = dialog_id
    cell.nameString = text
    pinned = {}
    if dialog_id > 0:
        user = tgnet_bridge.get_user(dialog_id)
        if user is None:
            return None
        cell.user = user
        pinned[("user", dialog_id)] = user
    elif dialog_id < 0:
        chat = tgnet_bridge.get_chat(-dialog_id)
        if chat is None:
            return None
        cell.chat = chat
        pinned[("chat", -dialog_id)] = chat

    def shown():
        if cell.nameString != text:
            return str(cell.nameString)
        if cell.user is not None:
            first = str(getattr(cell.user, "first_name", "") or "")
            last = str(getattr(cell.user, "last_name", "") or "")
            return (first + " " + last).strip() or text
        if cell.chat is not None:
            return str(getattr(cell.chat, "title", "") or text)
        return text

    with tgnet_bridge.pin_peers(pinned):
        _run_virtual("dialog_update", cell, "DialogCell.update", [0])
        result = {"value": text}

        def build(param):
            result["value"] = shown()
            return None
        _run_virtual("dialog_name", cell, "DialogCell.buildLayout", [], original=build)
    value = result["value"]
    return value if value and value != text else None


def _client_event(args):
    import exd_java
    import tgnet_bridge
    import _compat
    kind = str(args.get("kind") or "")
    data = args.get("message") or None
    message = exd_java.message_object(data, int(args.get("account") or 0)) if data else None
    dialog_id = int(args.get("dialog_id") or (data or {}).get("dialog_id") or 0)
    player = _media_controller(int(args.get("account") or 0))
    if kind == "play_message":
        _, cancelled = _run_virtual("player_playlist", player, "MediaController.setPlaylist",
                                    [exd_java.ArrayList([message] if message is not None else []), message, 0])
        if cancelled:
            return True
        _, cancelled = _run_virtual("play_message", player, "MediaController.playMessage", [message])
        if not cancelled:
            import exd_player
            _run_virtual("player_title", exd_player.current() or exd_player.AudioPlayerAlert(),
                         "AudioPlayerAlert.updateTitle",
                         [False])
        return cancelled
    if kind == "reply":
        enter = tgnet_bridge.ChatActivity(dialog_id) if dialog_id else None
        _, cancelled = _run_virtual("reply", enter, "ChatActivityEnterView.setReplyingMessageObject",
                                    [message, None])
        return cancelled
    if kind == "start_call":
        user = int(args.get("user_id") or dialog_id or 0)
        _, cancelled = _run_virtual("start_call", _compat.Stub("VoIPHelper"), "VoIPHelper.startCall",
                                    [tgnet_bridge.get_user(user) if hasattr(tgnet_bridge, "get_user") else user,
                                     bool(args.get("video")), True, _compat._LaunchActivity.instance, None, None])
        return cancelled
    if kind == "player_next":
        return _run_virtual("player_next", player, "MediaController.playNextMessage", [])[1]
    if kind == "player_prev":
        return _run_virtual("player_prev", player, "MediaController.playPreviousMessage", [])[1]
    if kind == "player_stop":
        return _run_virtual("player_stop", player, "MediaController.cleanupPlayer", [True, True])[1]
    if kind == "transcribe":
        return _run_virtual("transcribe", __import__("exd_player").TranscribeButton(None, message), "TranscribeButton.onTap", [])[1]
    if kind == "message_menu":
        chat = tgnet_bridge.ChatActivity(dialog_id) if dialog_id else None
        return _run_virtual("message_menu", chat, "ChatActivity.createMenu",
                            [_ChatMessageCell(message), True, False, 0.0, 0.0, False, False])[1]
    if kind == "send_reaction":
        reaction = _VisibleReaction(str(args.get("emoji") or ""), int(args.get("document_id") or 0))
        helper = _controller("SendMessagesHelper", int(args.get("account") or 0))
        return _run_virtual("send_reaction", helper, "SendMessagesHelper.sendReaction",
                            [message, exd_java.ArrayList([] if args.get("remove") else [reaction]), reaction,
                             False, True, tgnet_bridge.ChatActivity(dialog_id) if dialog_id else None, None])[1]
    if kind == "notification":
        controller = _controller("NotificationsController", int(args.get("account") or 0))
        try:
            controller.pushMessages = exd_java.ArrayList([message] if message is not None else [])
        except Exception:
            pass
        return _run_virtual("notification", controller, "NotificationsController.showOrUpdateNotification",
                            [False])[1]
    if kind == "send_media":
        import exd_java_more2
        entries = []
        for index, path in enumerate(args.get("paths") or ()):
            entry = exd_java_more2.MediaController.PhotoEntry(0, index, 0, str(path), 0, False)
            entry.caption = str(args.get("caption") or "") or None
            entries.append(entry)
        chat = tgnet_bridge.ChatActivity(dialog_id) if dialog_id else None
        return _run_virtual("send_media", chat, "ChatActivity.sendMedia",
                            [entries[0] if entries else None, None, True, 0, False, exd_java.ArrayList(entries)])[1]
    if kind == "select_message":
        chat = tgnet_bridge.ChatActivity(dialog_id) if dialog_id else None
        return _run_virtual("select_message", chat, "ChatActivity.addToSelectedMessages",
                            [message, False, True])[1]
    if kind == "avatar_click":
        return _run_virtual("avatar_click", _ChatAvatarContainer(dialog_id, None),
                            "ChatAvatarContainer.onAvatarClick", [False])[1]
    return False


def _dev_server(args):
    import dev_server
    try:
        if args.get("enabled"):
            dev_server.DevServer.start_server()
        else:
            dev_server.DevServer.stop_server()
            dev_server.DevServer.stop_remote_debugging(dev_server.DebuggerPlatform.VSCode)
        return True
    except Exception:
        log(f"dev server failed:\n{traceback.format_exc(limit=4)}")
        return False


def _profile_rows(args):
    import exd_profile
    try:
        return exd_profile.rows(int(args.get("dialog_id") or 0), args)
    except Exception:
        log(f"profile rows failed:\n{traceback.format_exc(limit=4)}")
    return []


def _player_menu(args):
    import exd_player
    action = str(args.get("action") or "")
    try:
        if action == "open":
            alert = exd_player.opened()
            _run_virtual("player_title", alert, "AudioPlayerAlert.updateTitle", [False])
            return True
        if action == "close":
            exd_player.closed()
            return True
        if action == "items":
            return exd_player.menu_items()
        if action == "click":
            return exd_player.menu_click(int(args.get("id") or 0))
    except Exception:
        log(f"player menu failed:\n{traceback.format_exc(limit=4)}")
    return [] if action == "items" else False


_CONTROLLER_GETTERS = {
    "MediaController": "get_media_controller",
    "SendMessagesHelper": "get_send_messages_helper",
    "NotificationsController": "get_notifications_controller",
}


def _controller(name, account=0):
    import client_utils
    import _compat
    getter = getattr(client_utils, _CONTROLLER_GETTERS.get(name, ""), None)
    return getter(account) if callable(getter) else _compat.Stub(name)


def _media_controller(account=0):
    return _controller("MediaController", account)


class _VisibleReaction:
    _java_name = "org.telegram.ui.Components.Reactions.ReactionsLayoutInBubble$VisibleReaction"

    def __init__(self, emoji, document_id):
        self.emojicon = emoji or None
        self.documentId = int(document_id or 0)
        self.isStar = False
        self.isEffect = False

    def equals(self, other):
        return isinstance(other, _VisibleReaction) and other.emojicon == self.emojicon             and other.documentId == self.documentId

    def __eq__(self, other):
        return self.equals(other)

    def __hash__(self):
        return hash((self.emojicon, self.documentId))

    def __repr__(self):
        return "VisibleReaction(%s)" % (self.emojicon or self.documentId)


def _file_open_hook(args):
    import file_utils
    import tgnet_bridge
    import _compat
    import exd_java
    data = args.get("message") or None
    message = exd_java.message_object(data, 0) if data else None
    dialog_id = int((data or {}).get("dialog_id") or 0)
    fragment = tgnet_bridge.ChatActivity(dialog_id) if dialog_id else None
    _, cancelled = _run_virtual("file_open", _compat.Stub("AndroidUtilities"), "AndroidUtilities.openForView",
                                [message, _compat._LaunchActivity.instance, None, False])
    if cancelled:
        return True
    try:
        return bool(file_utils.FilesController.dispatch(
            str(args.get("path") or ""), str(args.get("place") or "ChatActivity"),
            message, fragment, _compat._LaunchActivity.instance))
    except Exception:
        log(f"file handler failed:\n{traceback.format_exc(limit=4)}")
        return False


def _open_url_hook(args):
    import _compat
    import exd_java
    cls = _compat._Browser
    if not exd_java.has_shim_hooks(cls, "openUrl"):
        return False
    url = str(args.get("url") or "")
    _compat._BROWSER_EXTERNAL[0] = False
    try:
        cls.openUrl(_compat._LaunchActivity.instance, url)
    finally:
        handled = not _compat._BROWSER_EXTERNAL[0]
        _compat._BROWSER_EXTERNAL[0] = True
    return handled


def _screen_event(args):
    import tgnet_bridge
    screen = str(args.get("screen") or "")
    dialog_id = int(args.get("dialog_id") or 0)
    if screen == "chat":
        previous = _screen_state.get("chat", 0)
        if previous and previous != dialog_id:
            _virtual_screen("chat_close", tgnet_bridge.ChatActivity(previous), "ChatActivity.onPause")
        _screen_state["chat"] = dialog_id
        if dialog_id and dialog_id != previous:
            _virtual_screen("chat_open", tgnet_bridge.ChatActivity(dialog_id), "ChatActivity.onResume")
        elif not dialog_id:
            _virtual_screen("dialogs_open", tgnet_bridge.DialogsActivity(0), "DialogsActivity.onResume")
    elif screen == "profile" and dialog_id:
        _screen_state["profile"] = dialog_id
        _virtual_screen("profile_open", __import__("exd_profile").activity(dialog_id), "ProfileActivity.onResume")
        return True
    if screen == "chat":
        profile = _screen_state.pop("profile", 0)
        if profile:
            _virtual_screen("profile_close", tgnet_bridge.ProfileActivity(profile), "ProfileActivity.onPause")
        if dialog_id and not previous:
            _virtual_screen("dialogs_close", tgnet_bridge.DialogsActivity(0), "DialogsActivity.onPause")
    return True


def _virtual_incoming(account, update):
    import exd_java
    import _compat
    if not any(r.instance and r.instance.__dict__.get("_exd_virtual_incoming") for r in _records.values()):
        return
    message = exd_java.message_object(update, account)
    values = exd_java.ArrayList([message])
    _run_virtual("incoming", _compat.Stub("MessagesController"), "MessagesController.processNewMessages",
                 [values, int(update.get("dialog_id") or 0), False, False], account=account)


def _event(args):
    import client_utils
    kind = args.get("type")
    account = args.get("account", 0)
    base = _base()
    if kind == "update":
        name = args.get("name", "")
        update = args.get("update") or {}
        if name == "updateNewMessage" and isinstance(update, dict):
            _virtual_incoming(account, update)
            try:
                import exd_android
                import exd_java
                if _observers.get(exd_android.NotificationCenter.didReceiveNewMessages):
                    exd_android.post_new_message(account, update, exd_java.message_object(update, account))
            except Exception:
                log(f"didReceiveNewMessages failed: {traceback.format_exc(limit=3)}")
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
    if kind == "screen":
        return _screen_event(args)
    if kind == "app":
        name = str(args.get("name", ""))
        if name in ("app_resume", "app_pause"):
            import _compat
            _virtual_screen(name, _compat._LaunchActivity.instance,
                            "LaunchActivity.onResume" if name == "app_resume" else "LaunchActivity.onPause")
        event = base.AppEvent.from_java(name)
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
                with owner_scope(_delegate_owner(delegate)):
                    delegate.didReceivedNotification(notification_id, account, payload)
            except Exception:
                log(f"notification delegate failed:\n{traceback.format_exc(limit=4)}")
        return True
    if kind == "intent":
        from intents import IntentsManager
        context = dict(args.get("context") or {})
        if args.get("before"):
            import _compat
            intent = _compat._Intent(_compat._Intent.ACTION_VIEW, _compat._Uri.parse(context.get("url") or ""))
            _, cancelled = _run_virtual("intent", _compat._LaunchActivity.instance, "LaunchActivity.handleIntent",
                                        [intent, False, False, False])
            if cancelled:
                return True
        return IntentsManager.dispatch(bool(args.get("before")), context)
    if kind == "webview":
        import exd_webview
        return exd_webview.dispatch(args)
    if kind == "peers_picked":
        import exd_pickers
        return exd_pickers.dispatch(args)
    if kind == "webview_nav":
        import exd_webview
        return exd_webview.navigate(args)
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


def _view_checked(args):
    import android_views
    return android_views.dispatch_checked(args.get("id"), bool(args.get("checked")))


def _view_progress(args):
    import android_views
    return android_views.dispatch_progress(args.get("id"), int(args.get("progress") or 0),
                                           str(args.get("phase") or "change"))


def _install(args):
    import shutil
    import capability_scan
    path = args["path"]
    meta = read_metadata(path)
    os.makedirs(PLUGINS_DIR, exist_ok=True)
    import exd_elyx
    extension = exd_elyx.canonical_extension(path) if exd_elyx.is_elyx(path) else ".plugin"
    target = os.path.join(PLUGINS_DIR, meta["id"] + extension)
    for other in (".plugin", ".elyx", ".eaf"):
        stale = os.path.join(PLUGINS_DIR, meta["id"] + other)
        if other != extension and os.path.exists(stale):
            try:
                os.remove(stale)
            except OSError:
                pass
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
    if record is not None:
        try:
            import exd_android
            import exd_java
            controller = exd_android.PluginsController
            if exd_java.has_shim_hooks(controller, "setPluginEnabled"):
                _HOST_CALL["setPluginEnabled"] = True
                try:
                    controller.getInstance().setPluginEnabled(record.id, bool(args["enabled"]))
                finally:
                    _HOST_CALL.pop("setPluginEnabled", None)
                return record.info()
        except Exception:
            log(f"setPluginEnabled hook failed:\n{traceback.format_exc(limit=4)}")
    return _set_enabled_core(args)


def _set_enabled_core(args):
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
    if type(item).__name__ == "Custom":
        uitem = getattr(item, "item", None)
        if uitem is not None:
            uitem.checked = bool(value)
        return _setting_click({"id": plugin_id, "index": args["index"]})
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
                callback(_setting_view(item))
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


def _guard_preview(args):
    import plugin_guard
    path = args.get("path")
    if not path and args.get("id") in _records:
        path = _records[args["id"]].path
    try:
        return plugin_guard.preview(path)
    except Exception:
        return _source({"path": path})


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
    import exd_java
    exd_java.set_language(args.get("lang"))
    import exd_runtime
    exd_runtime.install_os_mapping()
    import exd_elyx
    exd_elyx.install_api(sys.modules[__name__])
    if not args.get("sandbox_off"):
        import exd_sandbox
        exd_sandbox.install(
            sys.modules[__name__],
            PLUGINS_DIR,
            SETTINGS_DIR,
            os.path.dirname(PLUGINS_DIR),
            os.path.join(os.path.dirname(PLUGINS_DIR), "plugin_deps"),
            interactive=bool(args.get("sandbox_prompts", True)),
            level=int(args.get("sandbox_level", 0) or 0))
        sys.modules.pop("exd_sandbox", None)
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
    "guard_preview": _guard_preview,
    "capability_lines": _capability_lines,
    "list": lambda args: [r.info() for r in _records.values()],
    "send_hook": _send_hook,
    "settings": lambda args: _settings_items(args["id"]),
    "setting_changed": _setting_changed,
    "setting_click": _setting_click,
    "alert_click": _alert_click,
    "open_url_hook": _open_url_hook,
    "file_open_hook": _file_open_hook,
    "client_event": _client_event,
    "player_menu": _player_menu,
    "profile_rows": _profile_rows,
    "dev_server": _dev_server,
    "ui_hook": _ui_hook,
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
    "view_checked": _view_checked,
    "view_progress": _view_progress,
}


def _file_picked(args):
    import exd_picker
    return exd_picker.picked(args.get("token"), args.get("paths") or [])


def _port_prepare(args):
    import exd_porter
    path = str(args.get("path") or "")
    if not path and args.get("id") in _records:
        path = _records[args["id"]].path
    meta = read_metadata(path)
    try:
        import capability_scan
        capabilities = capability_scan.scan(path)
    except Exception:
        capabilities = {}
    result = exd_porter.prepare(path, meta, capabilities)
    result["name"] = str(meta.get("name") or meta.get("id") or "")
    return result


def _port_check(args):
    import exd_porter
    prepared = args.get("prepared") or {}
    need = exd_porter.needed(args.get("answer"), prepared)
    if need:
        return {
            "code": "",
            "report": "",
            "need": need,
            "errors": ["NEED: " + ", ".join(need)],
            "fix": exd_porter.need_prompt(need),
        }
    code, report = exd_porter.extract(args.get("answer"))
    code = exd_porter.autofix(code)
    errors = exd_porter.check(code, args.get("prepared") or {})
    if not errors and not exd_porter.has_function(code):
        return {"code": "", "report": report, "parsed": exd_porter.parse_report(report), "nothing": True,
                "errors": [], "fix": ""}
    return {
        "code": code,
        "report": report,
        "parsed": exd_porter.parse_report(report),
        "errors": errors,
        "fix": exd_porter.fix_prompt(errors) if errors else "",
    }


def _port_save(args):
    import exd_porter
    out_dir = os.path.join(os.path.dirname(PLUGINS_DIR), "ai_ports")
    return exd_porter.save(str(args.get("code") or ""), args.get("prepared") or {}, out_dir)


_METHODS.update({
    "file_picked": _file_picked,
    "port_prepare": _port_prepare,
    "port_check": _port_check,
    "port_save": _port_save,
})


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
