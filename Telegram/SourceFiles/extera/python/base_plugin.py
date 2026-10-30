from enum import Enum
from typing import Any, Callable, List, Optional

import extera_host as _host


# Hooks

class HookStrategy(Enum):
    DEFAULT = 0
    CANCEL = 1
    MODIFY = 2
    MODIFY_FINAL = 3


class AppEvent(Enum):
    START = "start"
    STOP = "stop"
    RESUME = "resume"
    PAUSE = "pause"


class HookResult:
    def __init__(self, strategy: HookStrategy = HookStrategy.DEFAULT,
                 request: Any = None, response: Any = None,
                 update: Any = None, updates: Any = None, params: Any = None):
        self.strategy = strategy
        self.request = request
        self.response = response
        self.update = update
        self.updates = updates
        self.params = params


class MethodHook:
    def before_hooked_method(self, param):
        pass

    def after_hooked_method(self, param):
        pass


class MethodReplacement(MethodHook):
    def replace_hooked_method(self, param):
        return None


# Menu

class MenuItemType(Enum):
    MESSAGE_CONTEXT_MENU = "message_context_menu"
    DRAWER_MENU = "drawer_menu"
    CHAT_ACTION_MENU = "chat_action_menu"
    PROFILE_ACTION_MENU = "profile_action_menu"


class MenuItemData:
    def __init__(self, menu_type: MenuItemType, text: str,
                 on_click: Optional[Callable[[dict], None]] = None,
                 item_id: Optional[str] = None, icon: Optional[str] = None,
                 subtext: Optional[str] = None,
                 condition: Optional[str] = None, priority: int = 0,
                 **kwargs):
        self.menu_type = menu_type
        self.text = text
        self.on_click = on_click
        self.item_id = item_id
        self.icon = icon
        self.subtext = subtext
        self.condition = condition
        self.priority = priority


# Plugin

class BasePlugin:
    def __init__(self):
        self.id: str = ""
        self.name: str = ""
        self.error_message: Optional[str] = None
        import logging
        self.logger = logging.getLogger("extera." + (self.__class__.__name__))

    def log(self, message: Any):
        _host.log(f"[{self.id or self.name}] {message}")

    def on_plugin_load(self):
        pass

    def on_plugin_unload(self):
        pass

    def create_settings(self) -> List[Any]:
        return []

    def get_setting(self, key: str, default: Any = None) -> Any:
        return _host.get_setting(self.id, key, default)

    def set_setting(self, key: str, value: Any, reload_settings: bool = False):
        _host.set_setting(self.id, key, value, reload_settings)

    def export_settings(self) -> dict:
        return _host.export_settings(self.id)

    def import_settings(self, data: dict, reload_settings: bool = True):
        _host.import_settings(self.id, data, reload_settings)

    def add_on_send_message_hook(self, priority: int = 0):
        _host.add_send_hook(self.id, priority)

    def remove_on_send_message_hook(self):
        _host.remove_send_hook(self.id)

    def add_hook(self, name: str, match_substring: bool = False,
                 priority: int = 0):
        _host.add_request_hook(self.id, name, match_substring, priority)

    def remove_hook(self, name: str):
        _host.remove_request_hook(self.id, name)

    def on_send_message_hook(self, account: int, params: Any) -> HookResult:
        return HookResult()

    def pre_request_hook(self, request_name: str, account: int,
                         request: Any) -> HookResult:
        return HookResult()

    def post_request_hook(self, request_name: str, account: int,
                          response: Any, error: Any) -> HookResult:
        return HookResult()

    def on_update_hook(self, update_name: str, account: int,
                       update: Any) -> HookResult:
        return HookResult()

    def on_updates_hook(self, container_name: str, account: int,
                        updates: Any) -> HookResult:
        return HookResult()

    def add_menu_item(self, menu_item_data: MenuItemData) -> str:
        return _host.add_menu_item(self.id, menu_item_data)

    def remove_menu_item(self, menu_item_id: str) -> bool:
        return _host.remove_menu_item(self.id, menu_item_id)

    def hook_method(self, method: Any, hook: MethodHook,
                    priority: int = 10) -> Any:
        _host.log(f"[{self.id}] hook_method is not available on desktop")
        return None

    def hook_all_methods(self, clazz: Any, method_name: str,
                         hook: MethodHook, priority: int = 10) -> List[Any]:
        _host.log(f"[{self.id}] hook_all_methods is not available on desktop")
        return []

    def hook_all_constructors(self, clazz: Any, hook: MethodHook,
                              priority: int = 10) -> List[Any]:
        return []

    def unhook_method(self, unhook: Any):
        pass
