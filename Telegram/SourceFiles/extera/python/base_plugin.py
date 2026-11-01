import json
import sys
from dataclasses import dataclass
from typing import Any, Callable, Dict, List, Optional

import _compat
import extera_host as _host


# Hooks

class HookStrategy:
    DEFAULT = "DEFAULT"
    CANCEL = "CANCEL"
    MODIFY = "MODIFY"
    MODIFY_FINAL = "MODIFY_FINAL"
    NONE = DEFAULT


class HookResult:
    __slots__ = ("strategy", "request", "response", "update", "updates", "params", "result")

    def __init__(self, strategy: str = HookStrategy.DEFAULT, request=None, response=None,
                 update=None, updates=None, params=None, result=None):
        self.strategy = strategy
        self.request = request
        self.response = response
        self.update = update
        self.updates = updates
        self.params = params
        self.result = result

    def __repr__(self):
        return f"HookResult(strategy={self.strategy!r})"


class AppEvent:
    START = "START"
    STOP = "STOP"
    PAUSE = "PAUSE"
    RESUME = "RESUME"

    @staticmethod
    def from_java(event: str) -> Optional[str]:
        return _APP_EVENT_FROM_JAVA.get(event)


_APP_EVENT_FROM_JAVA = {
    "app_start": AppEvent.START,
    "app_stop": AppEvent.STOP,
    "app_pause": AppEvent.PAUSE,
    "app_resume": AppEvent.RESUME,
}


# Menu

class MenuItemType:
    MESSAGE_CONTEXT_MENU = "MESSAGE_CONTEXT_MENU"
    DRAWER_MENU = "DRAWER_MENU"
    MAIN_MENU = "MAIN_MENU"
    CHAT_ACTION_MENU = "CHAT_ACTION_MENU"
    PROFILE_ACTION_MENU = "PROFILE_ACTION_MENU"
    CHAT_CONTEXT = MESSAGE_CONTEXT_MENU


@dataclass
class MenuItemData:
    menu_type: str
    text: str
    on_click: Callable[[Dict[str, Any]], None]
    item_id: Optional[str] = None
    icon: Optional[str] = None
    subtext: Optional[str] = None
    condition: Optional[str] = None
    priority: int = 0


# Method hooking

class BaseHook:
    def __init__(self, xposed_hook=None, before=None, after=None):
        if before is not None:
            self.before_hooked_method = before
        if after is not None:
            self.after_hooked_method = after


class MethodHook(BaseHook):
    def before_hooked_method(self, param):
        pass

    def after_hooked_method(self, param):
        pass


class MethodReplacement(BaseHook):
    def replace_hooked_method(self, param):
        return None


XposedHook = MethodHook


PluginsController = _compat.Stub("PluginsController")


class PluginsConstants(metaclass=_compat.LenientMeta):
    PYTHON = "python"

    class MenuItemTypes:
        MESSAGE_CONTEXT_MENU = MenuItemType.MESSAGE_CONTEXT_MENU
        DRAWER_MENU = MenuItemType.DRAWER_MENU
        MAIN_MENU = MenuItemType.MAIN_MENU
        CHAT_ACTION_MENU = MenuItemType.CHAT_ACTION_MENU
        PROFILE_ACTION_MENU = MenuItemType.PROFILE_ACTION_MENU

    class Strategy:
        MODIFY = HookStrategy.MODIFY
        CANCEL = HookStrategy.CANCEL
        DEFAULT = HookStrategy.DEFAULT
        MODIFY_FINAL = HookStrategy.MODIFY_FINAL

    class Xposed:
        REPLACE_HOOKED_METHOD = "replace_hooked_method"
        BEFORE_HOOKED_METHOD = "before_hooked_method"
        AFTER_HOOKED_METHOD = "after_hooked_method"
        HOOK_FILTERS = "__hook_filters__"


class _FunctionalHook:
    pass


class UnhookId(str):
    __slots__ = ("_owner",)

    def __new__(cls, value, owner=None):
        obj = super().__new__(cls, value)
        obj._owner = owner
        return obj

    def unhook(self):
        owner = self._owner
        if owner is not None:
            owner.unhook_method(str(self))


class UnhookIdList(list):
    def __init__(self, ids=(), owner=None):
        super().__init__(ids)
        self._owner = owner

    def unhook(self):
        owner = self._owner
        if owner is not None:
            owner.unhook_method(list(self))


class HookParam:
    __slots__ = ("_param",)

    def __init__(self, param):
        object.__setattr__(self, "_param", param)

    @property
    def java(self):
        return object.__getattribute__(self, "_param")

    def __getattr__(self, name):
        param = object.__getattribute__(self, "_param")
        if name == "result":
            return param.get("result")
        if name == "throwable":
            return param.get("throwable")
        return param.get(name)

    def __setattr__(self, name, value):
        param = object.__getattribute__(self, "_param")
        param[name] = value

    def __repr__(self):
        return f"HookParam({object.__getattribute__(self, '_param')!r})"


def _filter_value(value):
    if isinstance(value, (str, int, float, bool)) or value is None:
        return value
    return str(value)


def _filter_class_name(cls):
    if isinstance(cls, str):
        return cls
    return str(cls)


class HookFilter:
    __slots__ = ("kind", "data")

    def __init__(self, kind: str, **data):
        self.kind = kind
        self.data = data

    def to_dict(self) -> Dict[str, Any]:
        out = {"type": self.kind}
        out.update(self.data)
        return out

    def __eq__(self, other):
        return isinstance(other, HookFilter) and self.to_dict() == other.to_dict()

    def __hash__(self):
        return hash((self.kind, repr(sorted(self.data.items()))))

    def __repr__(self):
        args = ", ".join(f"{k}={v!r}" for k, v in self.data.items())
        return f"HookFilter({self.kind}{', ' if args else ''}{args})"

    @staticmethod
    def argument_is_null(index: int) -> "HookFilter":
        return HookFilter("argument_is_null", index=int(index))

    @staticmethod
    def argument_not_null(index: int) -> "HookFilter":
        return HookFilter("argument_not_null", index=int(index))

    @staticmethod
    def argument_is_true(index: int) -> "HookFilter":
        return HookFilter("argument_is_true", index=int(index))

    @staticmethod
    def argument_is_false(index: int) -> "HookFilter":
        return HookFilter("argument_is_false", index=int(index))

    @staticmethod
    def argument_equal(index: int, value) -> "HookFilter":
        return HookFilter("argument_equal", index=int(index), value=_filter_value(value))

    @staticmethod
    def argument_not_equal(index: int, value) -> "HookFilter":
        return HookFilter("argument_not_equal", index=int(index), value=_filter_value(value))

    @staticmethod
    def argument_is_instance_of(index: int, cls) -> "HookFilter":
        return HookFilter("argument_is_instance_of", index=int(index),
                          **{"class": _filter_class_name(cls)})

    @staticmethod
    def result_is_null() -> "HookFilter":
        return HookFilter("result_is_null")

    @staticmethod
    def result_not_null() -> "HookFilter":
        return HookFilter("result_not_null")

    @staticmethod
    def result_is_true() -> "HookFilter":
        return HookFilter("result_is_true")

    @staticmethod
    def result_is_false() -> "HookFilter":
        return HookFilter("result_is_false")

    @staticmethod
    def result_equal(value) -> "HookFilter":
        return HookFilter("result_equal", value=_filter_value(value))

    @staticmethod
    def result_not_equal(value) -> "HookFilter":
        return HookFilter("result_not_equal", value=_filter_value(value))

    @staticmethod
    def result_is_instance_of(cls) -> "HookFilter":
        return HookFilter("result_is_instance_of", **{"class": _filter_class_name(cls)})

    @staticmethod
    def condition(expression: str, object=None) -> "HookFilter":
        data: Dict[str, Any] = {"expression": str(expression)}
        if object is not None:
            data["object"] = _filter_value(object)
        return HookFilter("condition", **data)

    @staticmethod
    def or_(*filters: "HookFilter") -> "HookFilter":
        return HookFilter("or", filters=[
            f.to_dict() if hasattr(f, "to_dict") else f for f in filters
        ])


HookFilter.ArgumentIsNull = HookFilter.argument_is_null
HookFilter.ArgumentNotNull = HookFilter.argument_not_null
HookFilter.ArgumentIsTrue = HookFilter.argument_is_true
HookFilter.ArgumentIsFalse = HookFilter.argument_is_false
HookFilter.ArgumentEqual = HookFilter.argument_equal
HookFilter.ArgumentNotEqual = HookFilter.argument_not_equal
HookFilter.ArgumentIsInstanceOf = HookFilter.argument_is_instance_of
HookFilter.ResultIsNull = HookFilter.result_is_null
HookFilter.ResultNotNull = HookFilter.result_not_null
HookFilter.ResultIsTrue = HookFilter.result_is_true
HookFilter.ResultIsFalse = HookFilter.result_is_false
HookFilter.ResultEqual = HookFilter.result_equal
HookFilter.ResultNotEqual = HookFilter.result_not_equal
HookFilter.ResultIsInstanceOf = HookFilter.result_is_instance_of
HookFilter.Condition = HookFilter.condition
HookFilter.Or = HookFilter.or_

HookFilter.RESULT_IS_NULL = HookFilter.result_is_null()
HookFilter.RESULT_NOT_NULL = HookFilter.result_not_null()
HookFilter.RESULT_IS_TRUE = HookFilter.result_is_true()
HookFilter.RESULT_IS_FALSE = HookFilter.result_is_false()


def hook_filters(*filters, before=None, after=None):
    def decorator(fn):
        fn.__hook_filters__ = {
            "before": [*(before or ()), *filters],
            "after": list(after or ()),
        }
        return fn
    return decorator


_NO_CLIENT_OVERRIDE = object()


class _PluginLogger:
    def __init__(self, plugin):
        self._plugin = plugin

    def _write(self, level, message, args):
        try:
            text = str(message) % args if args else str(message)
        except Exception:
            text = str(message)
        self._plugin.log(f"[{level}] {text}")

    def debug(self, message, *args):
        self._write("debug", message, args)

    def info(self, message, *args):
        self._write("info", message, args)

    def warning(self, message, *args):
        self._write("warning", message, args)

    def error(self, message, *args):
        self._write("error", message, args)

    def exception(self, message, *args):
        import traceback
        self._write("error", message, args)
        self._plugin.log(traceback.format_exc(limit=6))

    def dump(self, obj):
        try:
            text = repr(obj)
        except Exception:
            text = object.__repr__(obj)
        self._plugin.log(text)


# Plugin

class BasePlugin:
    _extera_plugin_id: Optional[str] = None
    MenuType = MenuItemType
    _extera_send_message_registered = False

    id: Optional[str] = None
    enabled: bool = True
    error_message: Optional[str] = None

    def on_plugin_load(self):
        pass

    def on_plugin_unload(self):
        pass

    def on_app_event(self, event_type: str):
        pass

    def create_settings(self) -> Optional[list]:
        return None

    def pre_request_hook(self, request_name, account, request):
        return None

    def post_request_hook(self, request_name, account, response, error):
        return None

    def on_send_message_hook(self, account, params):
        return None

    def on_update_hook(self, update_name, account, update):
        return None

    def on_updates_hook(self, container_name, account, updates):
        return None

    def _extera_state(self, name, factory):
        value = self.__dict__.get(name)
        if not isinstance(value, factory):
            value = factory()
            self.__dict__[name] = value
        return value

    def _extera_attach(self, plugin_id: str):
        self._extera_plugin_id = plugin_id
        self.id = plugin_id
        self._extera_state("_extera_request_hooks", list)
        self._extera_state("_extera_menu_callbacks", dict)
        if "_extera_send_message_registered" not in self.__dict__:
            self.__dict__["_extera_send_message_registered"] = False
        if type(self).on_send_message_hook is not BasePlugin.on_send_message_hook:
            self.add_on_send_message_hook(0)

    @property
    def plugin_id(self) -> Optional[str]:
        return self._extera_plugin_id

    def log(self, msg):
        _host.log(f"[{self._extera_plugin_id or '?'}] {msg}")

    @property
    def logger(self):
        if "_extera_logger" not in self.__dict__:
            self.__dict__["_extera_logger"] = _PluginLogger(self)
        return self.__dict__["_extera_logger"]

    def get_setting(self, key: str, default=None):
        import plugin_settings
        return plugin_settings.get_setting(self.id, key, default)

    def set_setting(self, key: str, value, reload_settings: bool = False):
        import plugin_settings
        plugin_settings.set_setting(self.id, key, value, reload_settings)

    def export_settings(self) -> dict:
        import plugin_settings
        return plugin_settings.get_settings(self.id)

    def import_settings(self, settings: dict, reload_settings: bool = True):
        import plugin_settings
        plugin_settings.invalidate(self.id)
        for key, value in dict(settings or {}).items():
            plugin_settings.set_setting(self.id, key, value)
        if reload_settings:
            _host.reload_settings(self.id)

    def add_hook(self, name: str, match_substring: bool = False, priority: int = 0):
        entry = (str(name), bool(match_substring), int(priority))
        hooks = self._extera_state("_extera_request_hooks", list)
        if entry not in hooks:
            hooks.append(entry)
        _host.add_request_hook(self.id, entry[0], entry[1], entry[2])

    def remove_hook(self, name: str):
        key = str(name)
        if key == "on_send_message_hook":
            self.__dict__["_extera_send_message_registered"] = False
            _host.remove_send_hook(self.id)
            return
        hooks = self._extera_state("_extera_request_hooks", list)
        hooks[:] = [entry for entry in hooks if entry[0] != key]
        _host.remove_request_hook(self.id, key)

    def add_on_send_message_hook(self, priority: int = 0):
        self.__dict__["_extera_send_message_registered"] = True
        _host.add_send_hook(self.id, int(priority))

    def remove_on_send_message_hook(self):
        self.__dict__["_extera_send_message_registered"] = False
        _host.remove_send_hook(self.id)

    def add_menu_item(self, menu_item_data: MenuItemData, text=None, *, on_click=None,
                      condition=None, priority=0, icon=None, subtext=None, item_id=None) -> Optional[str]:
        if isinstance(menu_item_data, str) and text is not None:
            menu_item_data = MenuItemData(menu_item_data, text, on_click, item_id, icon,
                                          subtext, condition, priority)
        elif isinstance(menu_item_data, MenuItemData) and (text is not None or on_click is not None
                or condition is not None or priority != 0 or icon is not None
                or subtext is not None or item_id is not None):
            raise TypeError("Use either MenuItemData or individual menu fields")
        if not isinstance(menu_item_data, MenuItemData):
            raise TypeError("add_menu_item() expects a MenuItemData instance")
        item_id = _host.add_menu_item(self.id, menu_item_data)
        if menu_item_data.on_click is not None and item_id:
            self._extera_state("_extera_menu_callbacks", dict)[item_id] = menu_item_data.on_click
        return item_id

    def remove_menu_item(self, item_id: str):
        self._extera_state("_extera_menu_callbacks", dict).pop(item_id, None)
        _host.remove_menu_item(self.id, item_id)

    def _extera_dispatch_menu_click(self, item_id: str, context):
        callback = self._extera_state("_extera_menu_callbacks", dict).get(item_id)
        if callback is None:
            self.log(f"no menu callback registered for item {item_id!r}")
            return
        callback(context)

    @property
    def client(self):
        override = self.__dict__.get("_extera_client_override", _NO_CLIENT_OVERRIDE)
        if override is not _NO_CLIENT_OVERRIDE:
            return override
        import client_utils
        return client_utils.AccountClient(None)

    @client.setter
    def client(self, value):
        self.__dict__["_extera_client_override"] = value

    @client.deleter
    def client(self):
        self.__dict__.pop("_extera_client_override", None)

    def _extera_build_method_handler(self, handler, before=None, after=None):
        if isinstance(handler, (MethodHook, MethodReplacement)):
            return handler
        if handler is not None and any(
                hasattr(handler, attr) for attr in
                ("before_hooked_method", "after_hooked_method", "replace_hooked_method")):
            return handler
        if handler is not None:
            if not callable(handler):
                raise TypeError(
                    "handler must be a MethodHook/MethodReplacement instance or a callable")
            if before is not None:
                raise TypeError("pass either a handler callable or before=, not both")
            before, handler = handler, None
        if before is None and after is None:
            raise TypeError("hook_method requires a handler or before=/after= callbacks")
        wrapper = _FunctionalHook()
        if before is not None:
            wrapper.before_hooked_method = before
        if after is not None:
            wrapper.after_hooked_method = after
        return wrapper

    def _extera_hook_all(self, kind: str, clazz, method_name, handler_obj, priority, filters_json):
        self.log(f"{kind} requires the Android runtime, not available on desktop")
        return UnhookIdList((), None)

    def hook_method(self, method, handler=None, priority: int = 50, filters=None,
                    before=None, after=None, before_filters=None, after_filters=None):
        handler_obj = self._extera_build_method_handler(handler, before=before, after=after)
        self.log("hook_method is not available on desktop")
        return None

    def hook_all_methods(self, clazz, method_name, handler=None, priority: int = 50,
                         filters=None, before=None, after=None,
                         before_filters=None, after_filters=None) -> list:
        handler_obj = self._extera_build_method_handler(handler, before=before, after=after)
        return self._extera_hook_all("methods", clazz, method_name, handler_obj,
                                     priority, None)

    def hook_all_constructors(self, clazz, handler=None, priority: int = 50,
                              filters=None, before=None, after=None,
                              before_filters=None, after_filters=None) -> list:
        handler_obj = self._extera_build_method_handler(handler, before=before, after=after)
        return self._extera_hook_all("constructors", clazz, None, handler_obj,
                                     priority, None)

    def unhook_method(self, unhook_obj):
        if unhook_obj is None:
            return

    def _extera_cleanup_resources(self):
        try:
            _host.remove_send_hook(self.id)
        except Exception:
            pass
        for module_name, cleanup in (("file_utils", "_unregister_all_for_plugin"),
                                     ("intents", "_unhandle_all_for_plugin")):
            try:
                module = __import__(module_name)
                getattr(module, cleanup)(self._extera_plugin_id)
            except Exception:
                pass
