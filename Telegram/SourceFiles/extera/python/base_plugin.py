import json
import sys
from abc import ABC
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Callable, Dict, List, Optional

import _compat
import extera_host as _host


# Hooks

class HookStrategy(str, Enum):
    DEFAULT = "DEFAULT"
    CANCEL = "CANCEL"
    MODIFY = "MODIFY"
    MODIFY_FINAL = "MODIFY_FINAL"
    NONE = "DEFAULT"

    def __str__(self):
        return self.value


class IntentHookType(Enum):
    BEFORE = 0
    AFTER = 1


class HookResult:
    __slots__ = ("strategy", "request", "response", "update", "updates", "error", "params", "result")

    def __init__(self, strategy=HookStrategy.DEFAULT, request=None, response=None,
                 update=None, updates=None, error=None, params=None, result=None):
        self.strategy = strategy
        self.request = request
        self.response = response
        self.update = update
        self.updates = updates
        self.error = error
        self.params = params
        self.result = result

    def __eq__(self, other):
        return isinstance(other, HookResult) and all(
            getattr(self, k) == getattr(other, k) for k in self.__slots__)

    def __repr__(self):
        return f"HookResult(strategy={self.strategy!r})"


class PluginError(Exception):
    def __init__(self, message: str, plugin_id: Optional[str] = None):
        super().__init__(message)
        self.plugin_id = plugin_id

    def __str__(self):
        text = super().__str__()
        return f"[{self.plugin_id}] {text}" if self.plugin_id else text


@dataclass
class PluginMetadata:
    id: str
    name: str
    description: str = ""
    author: str = ""
    version: str = ""
    icon: str = ""
    min_version: str = ""
    requirements: List[str] = field(default_factory=list)


class AppEvent(str, Enum):
    START = "START"
    STOP = "STOP"
    PAUSE = "PAUSE"
    RESUME = "RESUME"

    def __str__(self):
        return self.value

    @staticmethod
    def from_java(event: str):
        return _APP_EVENT_FROM_JAVA.get(event)


_APP_EVENT_FROM_JAVA = {
    "app_start": AppEvent.START,
    "app_stop": AppEvent.STOP,
    "app_pause": AppEvent.PAUSE,
    "app_resume": AppEvent.RESUME,
}


# Menu

class MenuItemType(str, Enum):
    MESSAGE_CONTEXT_MENU = "MESSAGE_CONTEXT_MENU"
    DRAWER_MENU = "DRAWER_MENU"
    MAIN_MENU = "MAIN_MENU"
    CHAT_ACTION_MENU = "CHAT_ACTION_MENU"
    PROFILE_ACTION_MENU = "PROFILE_ACTION_MENU"
    CHAT_CONTEXT = "MESSAGE_CONTEXT_MENU"

    def __str__(self):
        return self.value


@dataclass
class MenuItemData:
    menu_type: Any
    text: str
    on_click: Callable[[Dict[str, Any]], None]
    item_id: Optional[str] = None
    icon: Optional[str] = None
    subtext: Optional[str] = None
    condition: Optional[str] = None
    priority: int = 0


# Method hooking

class XposedHook(ABC):
    pass


class MethodHook(XposedHook):
    def before_hooked_method(self, param):
        pass

    def after_hooked_method(self, param):
        pass


class MethodReplacement(XposedHook):
    def replace_hooked_method(self, param):
        return None


class BaseHook(MethodHook):
    def __init__(self, plugin=None, *, before=None, after=None, before_filters=None, after_filters=None):
        if plugin is not None and not isinstance(plugin, BasePlugin) and callable(plugin) and before is None:
            before, plugin = plugin, None
        self.plugin = plugin
        self.before = before
        self.after = after
        self.before_filters = list(before_filters or ())
        self.after_filters = list(after_filters or ())

    def before_hooked_method(self, param):
        if self.before is not None:
            self.before(param)

    def after_hooked_method(self, param):
        if self.after is not None:
            self.after(param)


import exd_xposed as _xposed


class MethodHookParam(_xposed.MethodHookParam):
    pass


class VirtualParam(MethodHookParam):
    def __init__(self, args, this=None, method=None):
        super().__init__(method, this, args)


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

    class HookFilterTypes:
        ARGUMENT_EQUAL = "argument_equal"
        ARGUMENT_IS_FALSE = "argument_is_false"
        ARGUMENT_IS_INSTANCE_OF = "argument_is_instance_of"
        ARGUMENT_IS_NULL = "argument_is_null"
        ARGUMENT_IS_TRUE = "argument_is_true"
        ARGUMENT_NOT_EQUAL = "argument_not_equal"
        ARGUMENT_NOT_NULL = "argument_not_null"
        CONDITION = "condition"
        OR = "or"
        RESULT_EQUAL = "result_equal"
        RESULT_IS_FALSE = "result_is_false"
        RESULT_IS_INSTANCE_OF = "result_is_instance_of"
        RESULT_IS_NULL = "result_is_null"
        RESULT_IS_TRUE = "result_is_true"
        RESULT_NOT_EQUAL = "result_not_equal"
        RESULT_NOT_NULL = "result_not_null"


class _FunctionalHook:
    pass

_VIRTUAL_HOOKS = {
    ("SendMessagesHelper", "sendMessage"): "send",
    ("MessagesController", "processNewMessages"): "incoming",
    ("LaunchActivity", "onActivityResult"): "activity_result",
    ("ChatActivity", "onResume"): "chat_open",
    ("ChatActivity", "createView"): "chat_open",
    ("ChatActivity", "onFragmentCreate"): "chat_open",
    ("ChatActivity", "onBecomeFullyVisible"): "chat_open",
    ("ChatActivity", "onPause"): "chat_close",
    ("ChatActivity", "onFragmentDestroy"): "chat_close",
    ("ProfileActivity", "onResume"): "profile_open",
    ("ProfileActivity", "createView"): "profile_open",
    ("ProfileActivity", "onFragmentCreate"): "profile_open",
    ("DialogsActivity", "onResume"): "dialogs_open",
    ("DialogsActivity", "createView"): "dialogs_open",
    ("Activity", "onActivityResult"): "activity_result",
    ("BaseFragment", "onActivityResultFragment"): "activity_result",
    ("ChatActivity", "onActivityResultFragment"): "activity_result",
    ("ProfileActivity", "onActivityResultFragment"): "activity_result",
    ("NotificationsController", "processNewMessages"): "incoming",
    ("LaunchActivity", "onCreate"): "app_create",
    ("LaunchActivity", "onResume"): "app_resume",
    ("LaunchActivity", "onStart"): "app_resume",
    ("LaunchActivity", "onPause"): "app_pause",
    ("LaunchActivity", "onStop"): "app_pause",
    ("ProfileActivity", "onPause"): "profile_close",
    ("ProfileActivity", "onFragmentDestroy"): "profile_close",
    ("DialogsActivity", "onPause"): "dialogs_close",
    ("DialogsActivity", "onFragmentDestroy"): "dialogs_close",
    ("DialogsActivity", "onBecomeFullyVisible"): "dialogs_open",
    ("DialogsActivity", "onFragmentCreate"): "dialogs_open",
    ("ProfileActivity", "onBecomeFullyVisible"): "profile_open",
    ("MediaController", "playMessage"): "play_message",
    ("AudioPlayerAlert", "updateTitle"): "player_title",
    ("ChatActivityEnterView", "setReplyingMessageObject"): "reply",
    ("VoIPHelper", "startCall"): "start_call",
    ("LaunchActivity", "handleIntent"): "intent",
    ("ChatMessageCell", "setMessageObject"): "message_text",
    ("ChatMessageCell", "setMessageContent"): "message_text",
    ("MessageObject", "generateLayout"): "message_text",
    ("ChatAvatarContainer", "setSubtitle"): "chat_subtitle_set",
    ("ChatAvatarContainer", "updateSubtitle"): "chat_subtitle",
    ("PluginSettingsActivity", "fillItems"): "settings_fill",
    ("PluginSettingsActivity", "createView"): "settings_fill",
    ("TranscribeButton", "onTap"): "transcribe",
    ("ChatActivity", "createMenu"): "message_menu",
    ("ChatActivity", "fillMessageMenu"): "message_menu",
    ("SendMessagesHelper", "sendReaction"): "send_reaction",
    ("NotificationsController", "showOrUpdateNotification"): "notification",
    ("ChatAvatarContainer", "onAvatarClick"): "avatar_click",
    ("ChatActivity", "openProfile"): "avatar_click",
    ("MediaController", "playNextMessage"): "player_next",
    ("MediaController", "playNextMessageWithoutOrder"): "player_next",
    ("MediaController", "playPreviousMessage"): "player_prev",
    ("MediaController", "cleanupPlayer"): "player_stop",
    ("MediaController", "setPlaylist"): "player_playlist",
    ("LaunchActivity", "onNewIntent"): "intent",
    ("MainTabsActivity", "onResume"): "dialogs_open",
    ("MainTabsActivity", "createView"): "dialogs_open",
    ("MainTabsActivity", "onFragmentCreate"): "dialogs_open",
    ("ChatActivity", "onTransitionAnimationEnd"): "chat_open",
    ("ChatMessageCell", "setMessageObjectInternal"): "message_text",
    ("ProfileActivity", "updateProfileData"): "profile_open",
    ("ProfileActivity", "updateRowsIds"): "profile_rows",
    ("ListAdapter", "onBindViewHolder"): "profile_bind",
    ("AndroidUtilities", "openForView"): "file_open",
    ("ChatActivity", "getMessageContent"): "copy_text",
    ("ChatAvatarContainer", "setTitle"): "chat_title",
    ("DialogCell", "update"): "dialog_update",
    ("SettingsActivity", "fillItems"): "settings_main",
    ("ChatActivity", "addToSelectedMessages"): "select_message",
    ("ChatActivity", "sendMedia"): "send_media",
    ("SendMessagesHelper", "prepareSendingMedia"): "send_media",
    ("SendMessagesHelper", "prepareSendingDocuments"): "send_media",
    ("SettingsActivity", "onClick"): "settings_main_click",
    ("DialogCell", "buildLayout"): "dialog_name",
    ("DialogCell", "setDialog"): "dialog_name",
    ("ChatActivity", "updateTitle"): "chat_title",
    ("ChatActivity", "processNewMessages"): "incoming",
    ("ChatActivity", "updateSubtitle"): "chat_subtitle",
    ("ChatActivity", "onStart"): "chat_open",
    ("TopicsFragment", "onResume"): "chat_open",
    ("TopicsFragment", "createView"): "chat_open",
    ("TopicsFragment", "onPause"): "chat_close",
    ("BaseFragment", "onResume"): "chat_open",
    ("BaseFragment", "onBecomeFullyVisible"): "chat_open",
}

_UI_KINDS = {"message_text": "message_text", "chat_subtitle": "chat_subtitle", "chat_subtitle_set": "chat_subtitle",
             "copy_text": "copy_text", "chat_title": "chat_title", "dialog_name": "dialog_name",
             "dialog_update": "dialog_name"}
_EVENT_KINDS = {
    "play_message": "play_message", "player_title": "play_message", "player_playlist": "play_message",
    "reply": "reply", "start_call": "start_call", "transcribe": "transcribe", "message_menu": "message_menu",
    "send_reaction": "send_reaction", "notification": "notification", "avatar_click": "avatar_click",
    "player_next": "player_next", "player_prev": "player_prev", "player_stop": "player_stop",
    "select_message": "select_message", "send_media": "send_media",
}
_HOST_KINDS = dict(_UI_KINDS, **_EVENT_KINDS)


def _exd_shim_target(target, method_name=None):
    import exd_java
    if isinstance(target, exd_java.ShimMethod):
        return target._cls, target._method
    cls = target
    if not isinstance(cls, type):
        if isinstance(target, str):
            if "." in target and not method_name:
                target_cls, _, m_name = target.rpartition(".")
                cls = exd_java.lookup(target_cls)
                method_name = m_name
            else:
                cls = exd_java.lookup(target)
        elif callable(target) and hasattr(target, "__qualname__") and not method_name:
            qual = target.__qualname__
            if "." in qual:
                cls_name, _, m_name = qual.rpartition(".")
                cls = exd_java.lookup(cls_name) or getattr(target, "__self__", None)
                if not isinstance(cls, type) and cls is not None:
                    cls = type(cls)
                method_name = m_name
        else:
            inner = target._class if isinstance(target, exd_java.JavaClassInfo) else getattr(target, "_class", None)
            cls = inner if isinstance(inner, type) else None
    if cls is None and isinstance(target, str):
        cls = exd_java.lookup(target)
    if cls is None or not method_name:
        return None, None
    if exd_java.shim_method(cls, method_name) is None and not exd_java.has_shim_hooks(cls, method_name):
        return None, None
    return cls, method_name


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


@dataclass
class HookFilterData:
    filter_type: str
    arg_index: Optional[int] = None
    or_filters: Any = None
    mvel_expression: Optional[str] = None
    instance_of: Any = None
    object: Any = None

    def to_java_filter(self):
        java = _xposed.HookFilter(self.filter_type)
        if self.arg_index is not None:
            java.setArgIndex(int(self.arg_index))
        if self.or_filters is not None:
            java.setOrFilters(_xposed.to_java_filters(self.or_filters))
        if self.mvel_expression is not None:
            java.setMvelExpression(str(self.mvel_expression))
        if self.instance_of is not None:
            java.setInstanceOf(self.instance_of)
        java.setObject(self.object)
        return java

    def to_dict(self) -> Dict[str, Any]:
        out = {"type": self.filter_type}
        if self.arg_index is not None:
            out["index"] = self.arg_index
        if self.mvel_expression is not None:
            out["expression"] = self.mvel_expression
        if self.object is not None:
            out["object"] = self.object
        return out

    def __hash__(self):
        return hash((self.filter_type, self.arg_index, self.mvel_expression))


class HookFilter(Enum):
    RESULT_IS_NULL = HookFilterData("result_is_null")
    RESULT_IS_TRUE = HookFilterData("result_is_true")
    RESULT_IS_FALSE = HookFilterData("result_is_false")
    RESULT_NOT_NULL = HookFilterData("result_not_null")

    @property
    def filter_data(self):
        return self.value

    def to_java_filter(self):
        return self.value.to_java_filter()

    @staticmethod
    def ResultIsInstanceOf(clazz):
        return HookFilterData("result_is_instance_of", instance_of=clazz)

    @staticmethod
    def ResultEqual(value):
        return HookFilterData("result_equal", object=value)

    @staticmethod
    def ResultNotEqual(value):
        return HookFilterData("result_not_equal", object=value)

    @staticmethod
    def ArgumentIsNull(index: int):
        return HookFilterData("argument_is_null", arg_index=int(index))

    @staticmethod
    def ArgumentIsTrue(index: int):
        return HookFilterData("argument_is_true", arg_index=int(index))

    @staticmethod
    def ArgumentIsFalse(index: int):
        return HookFilterData("argument_is_false", arg_index=int(index))

    @staticmethod
    def ArgumentNotNull(index: int):
        return HookFilterData("argument_not_null", arg_index=int(index))

    @staticmethod
    def ArgumentIsInstanceOf(index: int, clazz):
        return HookFilterData("argument_is_instance_of", arg_index=int(index), instance_of=clazz)

    @staticmethod
    def ArgumentEqual(index: int, value):
        return HookFilterData("argument_equal", arg_index=int(index), object=value)

    @staticmethod
    def ArgumentNotEqual(index: int, value):
        return HookFilterData("argument_not_equal", arg_index=int(index), object=value)

    @staticmethod
    def Condition(condition: str, object: Any = None):
        return HookFilterData("condition", mvel_expression=str(condition), object=object)

    @staticmethod
    def Or(*filters):
        return HookFilterData("or", or_filters=list(filters))


for _old, _new in (("argument_is_null", "ArgumentIsNull"), ("argument_not_null", "ArgumentNotNull"),
                   ("argument_is_true", "ArgumentIsTrue"), ("argument_is_false", "ArgumentIsFalse"),
                   ("argument_equal", "ArgumentEqual"), ("argument_not_equal", "ArgumentNotEqual"),
                   ("argument_is_instance_of", "ArgumentIsInstanceOf"), ("result_equal", "ResultEqual"),
                   ("result_not_equal", "ResultNotEqual"), ("result_is_instance_of", "ResultIsInstanceOf"),
                   ("condition", "Condition"), ("or_", "Or")):
    type.__setattr__(HookFilter, _old, staticmethod(getattr(HookFilter, _new)))
for _old, _member in (("result_is_null", "RESULT_IS_NULL"), ("result_not_null", "RESULT_NOT_NULL"),
                      ("result_is_true", "RESULT_IS_TRUE"), ("result_is_false", "RESULT_IS_FALSE")):
    type.__setattr__(HookFilter, _old, staticmethod((lambda m: lambda: HookFilter[m])(_member)))


def hook_filters(*filters, before=None, after=None):
    def decorator(fn):
        values = [*(before or ()), *filters, *(after or ())]
        try:
            setattr(fn, PluginsConstants.Xposed.HOOK_FILTERS, values)
        except AttributeError:
            target = getattr(fn, "__func__", None)
            if target is not None:
                setattr(target, PluginsConstants.Xposed.HOOK_FILTERS, values)
        return fn
    return decorator


def fn_hook_filters(field_name: str):
    def getter(obj):
        method = getattr(obj, field_name, None)
        return list(getattr(method, PluginsConstants.Xposed.HOOK_FILTERS, None) or ())
    return getter

_NO_CLIENT_OVERRIDE = object()


# Plugin

class BasePlugin:
    _extera_plugin_id: Optional[str] = None
    _last_instance = None
    MenuType = MenuItemType
    _extera_send_message_registered = False

    id: Optional[str] = None
    enabled: bool = True
    error_message: Optional[str] = None

    def __init__(self):
        BasePlugin._last_instance = self

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

    @property
    def plugin_id(self) -> Optional[str]:
        return self._extera_plugin_id

    def _exd_meta(self, key, default=None):
        try:
            record = _host._records.get(self._extera_plugin_id or "")
            value = (record.meta if record is not None else {}).get(key)
        except Exception:
            value = None
        return default if value is None else value

    @property
    def name(self) -> str:
        return self.__dict__.get("name") or self._exd_meta("name", self._extera_plugin_id or "")

    @name.setter
    def name(self, value):
        self.__dict__["name"] = value

    @property
    def description(self) -> str:
        return self.__dict__.get("description") or self._exd_meta("description", "")

    @description.setter
    def description(self, value):
        self.__dict__["description"] = value

    @property
    def author(self) -> str:
        return self.__dict__.get("author") or self._exd_meta("author", "")

    @author.setter
    def author(self, value):
        self.__dict__["author"] = value

    @property
    def version(self) -> str:
        return self.__dict__.get("version") or self._exd_meta("version", "")

    @version.setter
    def version(self, value):
        self.__dict__["version"] = value

    @property
    def min_version(self) -> str:
        return self.__dict__.get("min_version") or self._exd_meta("min_version", "")

    @min_version.setter
    def min_version(self, value):
        self.__dict__["min_version"] = value

    @property
    def icon(self):
        return self.__dict__.get("icon") or self._exd_meta("icon")

    @icon.setter
    def icon(self, value):
        self.__dict__["icon"] = value

    @property
    def requirements(self) -> list:
        value = self.__dict__.get("requirements")
        return list(value if value is not None else self._exd_meta("requirements", []) or [])

    @requirements.setter
    def requirements(self, value):
        self.__dict__["requirements"] = list(value or [])

    @property
    def initialized(self) -> bool:
        return bool(self.__dict__.get("initialized"))

    @initialized.setter
    def initialized(self, value):
        self.__dict__["initialized"] = bool(value)

    @property
    def metadata(self) -> PluginMetadata:
        return PluginMetadata(self._extera_plugin_id or "", self.name, self.description, self.author,
                              self.version, self.icon or "", self.min_version, self.requirements)

    @property
    def stop_event(self):
        import threading
        event = self.__dict__.get("_exd_stop_event")
        if event is None:
            event = threading.Event()
            self.__dict__["_exd_stop_event"] = event
        return event

    def register_cleanup(self, cleanup):
        if not callable(cleanup):
            raise TypeError("cleanup must be callable")
        self._extera_state("_exd_cleanups", list).append(cleanup)
        return cleanup

    def add_file_hook(self, file_info):
        import file_utils
        secret = file_utils.FilesController.register(file_info)
        self._extera_state("_exd_file_hooks", dict)[file_info.ext] = secret
        return secret

    def remove_file_hook(self, ext, secret=None) -> None:
        import file_utils
        ext = str(ext or "").lstrip(".").lower()
        secret = secret or self._extera_state("_exd_file_hooks", dict).get(ext)
        file_utils.FilesController.unregister(ext, secret)
        self._extera_state("_exd_file_hooks", dict).pop(ext, None)

    def add_intent_hook(self, info, type=IntentHookType.BEFORE):
        import intents
        kind = type.value if isinstance(type, IntentHookType) else type
        if kind in (IntentHookType.AFTER.value, "after", "AFTER"):
            handle = intents.IntentsManager.new_global_after_handler(info)
        else:
            handle = intents.IntentsManager.new_global_before_handler(info)
        self._extera_state("_exd_intent_hooks", list).append(handle.handler_id)
        return handle

    def remove_intent_hook(self, handler_id):
        import intents
        handler_id = str(getattr(handler_id, "handler_id", handler_id))
        intents.IntentsManager.unhandle(handler_id)
        hooks = self._extera_state("_exd_intent_hooks", list)
        if handler_id in hooks:
            hooks.remove(handler_id)


    def log(self, msg):
        _host.log(f"[{self._extera_plugin_id or '?'}] {msg}")

    @property
    def logger(self):
        if "_extera_logger" not in self.__dict__:
            import log_utils
            self.__dict__["_extera_logger"] = log_utils.Logger(self._extera_plugin_id or "plugin")
        return self.__dict__["_extera_logger"]

    @logger.setter
    def logger(self, value):
        self.__dict__["_extera_logger"] = value

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

    def _extera_build_method_handler(self, handler, before=None, after=None,
                                     before_filters=None, after_filters=None):
        if isinstance(handler, (MethodHook, MethodReplacement, _xposed.XC_MethodHook)) or (
                handler is not None and any(hasattr(handler, attr) for attr in (
                    "before_hooked_method", "after_hooked_method", "replace_hooked_method",
                    "beforeHookedMethod", "afterHookedMethod", "replaceHookedMethod"))):
            handler_obj = handler
        else:
            if handler is not None:
                if not callable(handler):
                    raise TypeError(
                        "handler must be a MethodHook/MethodReplacement instance or a callable")
                if before is not None:
                    raise TypeError("pass either a handler callable or before=, not both")
                before, handler = handler, None
            if before is None and after is None:
                raise TypeError("hook_method requires a handler or before=/after= callbacks")
            handler_obj = _FunctionalHook()
            if before is not None:
                handler_obj.before_hooked_method = before
            if after is not None:
                handler_obj.after_hooked_method = after
        return _xposed.wrap(handler_obj, self._extera_plugin_id, before_filters, after_filters)

    def _exd_target(self, target, method_name=None):
        import exd_java
        if isinstance(target, exd_java.ShimMember):
            name = target._java_name
            if isinstance(target, exd_java.ShimConstructor):
                name = name[:-len(".<init>")]
        else:
            name = None
        for attr in ("getName", "_java_name", "_name", "__name__"):
            if name:
                break
            try:
                value = getattr(target, attr, None)
                value = value() if callable(value) and attr == "getName" else value
            except Exception:
                value = None
            if isinstance(value, str) and value:
                name = value
        if not name:
            try:
                name = str(target)
            except Exception:
                name = "?"
        name = name.replace("<class ", "").strip("<>' ")
        parts = [p for p in name.replace("$", ".").split(".") if p]
        short = ""
        for part in parts:
            if part[:1].isupper() and part != "TLRPC":
                short = part
        short = short or (parts[-1] if parts else "?")
        if not method_name and parts and parts[-1] != short and parts[-1][:1].islower():
            method_name = parts[-1]
        return short, method_name

    def _exd_note_hook(self, target, method_name=None):
        short, method_name = self._exd_target(target, method_name)
        if method_name:
            short = "%s.%s" % (short, method_name)
        notes = self.__dict__.setdefault("_exd_unsupported", [])
        if short not in notes and len(notes) < 32:
            notes.append(short)

    def _exd_index(self, hook_id, member, handler):
        self.__dict__.setdefault("_exd_hook_index", {})[str(hook_id)] = (member, handler)
        return _xposed.Unhook(str(hook_id), member, getattr(handler, "handler", handler), self)

    def _exd_virtual_hook(self, target, method_name, handler_obj, priority):
        key = self._exd_target(target, method_name)
        kind = _VIRTUAL_HOOKS.get(key)
        if kind is None:
            return None
        handlers = self.__dict__.setdefault("_exd_virtual_" + kind, [])
        handlers.append(handler_obj)
        if kind == "send":
            _host.add_send_hook(self.id, int(priority or 0))
        counter = self.__dict__.get("_exd_virtual_counter", 0) + 1
        self.__dict__["_exd_virtual_counter"] = counter
        hook_id = "exd_virtual_%s_%d" % (kind, counter)
        self.__dict__.setdefault("_exd_virtual_ids", {})[hook_id] = (kind, handler_obj)
        if kind in _HOST_KINDS:
            _host.sync_ui_hooks()
        return self._exd_index(hook_id, target, handler_obj)

    def _exd_shim_hook(self, target, method_name, handler_obj, priority):
        try:
            import exd_java
            if isinstance(target, exd_java.ShimConstructor):
                cls, name = target._cls, "<init>"
            else:
                cls, name = _exd_shim_target(target, method_name)
            if cls is None:
                return None
            hook_id = exd_java.hook_shim(cls, name, self.id, handler_obj, priority)
        except Exception:
            return None
        return self._exd_index(hook_id, target, handler_obj) if hook_id else None

    @staticmethod
    def _exd_priority(handler, priority):
        if isinstance(priority, int) and not isinstance(priority, bool):
            return priority
        value = getattr(handler, "priority", None)
        return value if isinstance(value, int) else 50

    def _extera_hook_all(self, kind: str, clazz, method_name, handler_obj, priority, filters_json=None):
        import exd_java
        if kind == "constructors":
            cls = clazz if isinstance(clazz, type) else (
                clazz._class if isinstance(clazz, exd_java.JavaClassInfo) else None)
            if isinstance(cls, type):
                hook_id = exd_java.hook_shim(cls, "<init>", self.id, handler_obj, priority)
                if hook_id:
                    return UnhookIdList((self._exd_index(hook_id, exd_java.ShimConstructor(cls), handler_obj),), self)
        virtual = self._exd_virtual_hook(clazz, method_name, handler_obj, priority)
        if virtual is not None:
            return UnhookIdList((virtual,), self)
        shim = self._exd_shim_hook(clazz, method_name, handler_obj, priority)
        if shim is not None:
            return UnhookIdList((shim,), self)
        self._exd_note_hook(clazz, method_name)
        self.log(f"{kind} {self._exd_target(clazz, method_name)} is not available on desktop")
        return UnhookIdList((_xposed.Unhook("exd_unsupported", clazz, handler_obj, self),), self)

    def hook_method(self, method_or_constructor=None, xposed_hook=None, priority=None, filters=None, *,
                    before=None, after=None, before_filters=None, after_filters=None, method=None, handler=None):
        method_or_constructor = method if method_or_constructor is None else method_or_constructor
        xposed_hook = handler if xposed_hook is None else xposed_hook
        if isinstance(method_or_constructor, (str, type)) and isinstance(xposed_hook, str):
            real = priority if not isinstance(priority, int) else filters
            res = self.hook_all_methods(method_or_constructor, xposed_hook, real, before=before, after=after,
                                         before_filters=before_filters, after_filters=after_filters)
            return res[0] if res else _xposed.Unhook("exd_unsupported", method_or_constructor, xposed_hook, self)
        handler_obj = self._extera_build_method_handler(xposed_hook, before, after, before_filters, after_filters)
        priority = self._exd_priority(xposed_hook, priority)
        virtual = self._exd_virtual_hook(method_or_constructor, None, handler_obj, priority)
        if virtual is not None:
            return virtual
        shim = self._exd_shim_hook(method_or_constructor, None, handler_obj, priority)
        if shim is not None:
            return shim
        self._exd_note_hook(method_or_constructor)
        self.log(f"hook_method {self._exd_target(method_or_constructor)} is not available on desktop")
        return _xposed.Unhook("exd_unsupported", method_or_constructor, xposed_hook, self)

    def hook_all_methods(self, hook_class=None, method_name=None, xposed_hook=None, priority=None, filters=None, *,
                         before=None, after=None, before_filters=None, after_filters=None,
                         clazz=None, handler=None) -> list:
        hook_class = clazz if hook_class is None else hook_class
        xposed_hook = handler if xposed_hook is None else xposed_hook
        handler_obj = self._extera_build_method_handler(xposed_hook, before, after, before_filters, after_filters)
        return self._extera_hook_all("methods", hook_class, method_name, handler_obj,
                                     self._exd_priority(xposed_hook, priority))

    def hook_all_constructors(self, hook_class=None, xposed_hook=None, priority=None, filters=None, *,
                              before=None, after=None, before_filters=None, after_filters=None,
                              clazz=None, handler=None) -> list:
        hook_class = clazz if hook_class is None else hook_class
        xposed_hook = handler if xposed_hook is None else xposed_hook
        handler_obj = self._extera_build_method_handler(xposed_hook, before, after, before_filters, after_filters)
        return self._extera_hook_all("constructors", hook_class, None, handler_obj,
                                     self._exd_priority(xposed_hook, priority))

    def unhook_method(self, unhook_obj):
        if unhook_obj is None:
            return
        ids = unhook_obj if isinstance(unhook_obj, (list, tuple, set)) else [unhook_obj]
        index = self.__dict__.setdefault("_exd_hook_index", {})
        for value in ids:
            value = str(value)
            index.pop(value, None)
            if value.startswith("exd_shim_"):
                try:
                    import exd_java
                    exd_java.unhook_shim(hook_id=value)
                except Exception:
                    pass
                continue
            entry = self.__dict__.setdefault("_exd_virtual_ids", {}).pop(value, None)
            if entry is None:
                continue
            kind, handler_obj = entry
            handlers = self.__dict__.get("_exd_virtual_" + kind) or []
            self.__dict__["_exd_virtual_" + kind] = [h for h in handlers if h is not handler_obj]
            if kind in _HOST_KINDS:
                _host.sync_ui_hooks()
            if kind == "send" and not self.__dict__["_exd_virtual_send"] \
                    and not self.__dict__.get("_extera_send_message_registered"):
                _host.remove_send_hook(self.id)

    def _extera_cleanup_resources(self):
        self.stop_event.set()
        for cleanup in reversed(self._extera_state("_exd_cleanups", list)):
            try:
                cleanup()
            except Exception as e:
                self.log(f"cleanup failed: {e!r}")
        self.__dict__["_exd_cleanups"] = []
        try:
            import exd_java
            exd_java.unhook_shim(plugin_id=self._extera_plugin_id)
        except Exception:
            pass
        for kind in set(_VIRTUAL_HOOKS.values()):
            self.__dict__.pop("_exd_virtual_" + kind, None)
        self.__dict__.pop("_exd_virtual_ids", None)
        self.__dict__.pop("_exd_hook_index", None)
        _host.sync_ui_hooks(exclude=self._extera_plugin_id)
        for module_name, cleanup in (("file_utils", "_unregister_all_for_plugin"),
                                     ("intents", "_unhandle_all_for_plugin"),
                                     ("exd_webview", "close_plugin"),
                                     ("extera_utils.classes", "release_plugin")):
            try:
                import importlib
                module = importlib.import_module(module_name)
                getattr(module, cleanup)(self._extera_plugin_id)
            except Exception:
                pass
