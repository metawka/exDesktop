from typing import Any, Callable, List, Optional

_registered = {}


# Items

class _Item:
    kind = "item"

    def __init__(self, **kwargs):
        for key, value in kwargs.items():
            setattr(self, key, value)

    def __getattr__(self, item):
        if item.startswith("__"):
            raise AttributeError(item)
        return None


class Header(_Item):
    kind = "header"

    def __init__(self, text: str = "", **kwargs):
        super().__init__(text=text, **kwargs)


class Divider(_Item):
    kind = "divider"

    def __init__(self, text: Optional[str] = None, **kwargs):
        super().__init__(text=text, **kwargs)


class Switch(_Item):
    kind = "switch"

    def __init__(self, key: str = "", text: str = "", default: bool = False,
                 subtext: Optional[str] = None, icon: Optional[str] = None,
                 on_change: Optional[Callable[[bool], None]] = None, **kwargs):
        super().__init__(key=key, text=text, default=default, subtext=subtext,
                         icon=icon, on_change=on_change, **kwargs)


class Selector(_Item):
    kind = "selector"

    def __init__(self, key: str = "", text: str = "", default: int = 0,
                 items: Optional[List[str]] = None, icon: Optional[str] = None,
                 on_change: Optional[Callable[[int], None]] = None, **kwargs):
        super().__init__(key=key, text=text, default=default,
                         items=list(items or []), icon=icon,
                         on_change=on_change, **kwargs)


class Input(_Item):
    kind = "input"

    def __init__(self, key: str = "", text: str = "", default: str = "",
                 subtext: Optional[str] = None, icon: Optional[str] = None,
                 on_change: Optional[Callable[[str], None]] = None, **kwargs):
        super().__init__(key=key, text=text, default=default, subtext=subtext,
                         icon=icon, on_change=on_change, **kwargs)


class EditText(_Item):
    kind = "edit_text"

    def __init__(self, key: str = "", hint: str = "", default: str = "",
                 multiline: bool = False, max_length: int = 0,
                 on_change: Optional[Callable[[str], None]] = None, **kwargs):
        super().__init__(key=key, hint=hint, default=default,
                         multiline=multiline, max_length=max_length,
                         on_change=on_change, **kwargs)


class Text(_Item):
    kind = "text"

    def __init__(self, text: str = "", icon: Optional[str] = None,
                 accent: bool = False, red: bool = False,
                 on_click: Optional[Callable[[Any], None]] = None,
                 subtext: Optional[str] = None,
                 create_sub_fragment: Optional[Callable[[], List[Any]]] = None,
                 **kwargs):
        super().__init__(text=text, icon=icon, accent=accent, red=red,
                         on_click=on_click, subtext=subtext,
                         create_sub_fragment=create_sub_fragment, **kwargs)


class Custom(_Item):
    kind = "custom"


class SimpleSettingFactory:
    def __init__(self, *args, **kwargs):
        pass

    def __getattr__(self, item):
        return lambda *args, **kwargs: None


# Bridge

def register_items(plugin_id, items):
    _registered[plugin_id] = list(items)


def unregister_items(plugin_id):
    _registered.pop(plugin_id, None)


def find_item(plugin_id, index):
    items = _registered.get(plugin_id, [])
    if 0 <= index < len(items):
        return items[index]
    return None


def describe(item, index):
    kind = getattr(item, "kind", None)
    if kind is None or kind == "custom":
        return None
    data = {"index": index, "kind": kind}
    for key in ("text", "subtext", "key", "default", "items", "hint",
                "multiline", "accent", "red", "icon"):
        value = getattr(item, key, None)
        if value is not None:
            data[key] = value if not callable(value) else None
    data["clickable"] = bool(getattr(item, "on_click", None))
    return data
