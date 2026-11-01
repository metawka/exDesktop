from dataclasses import dataclass
from typing import Any, Callable, List, Optional

_registered = {}


@dataclass
class Header:
    text: str


@dataclass
class Divider:
    text: Optional[str] = None


@dataclass
class Switch:
    key: str
    text: str
    default: bool
    subtext: Optional[str] = None
    icon: Optional[str] = None
    on_change: Optional[Callable] = None
    on_long_click: Optional[Callable] = None
    link_alias: Optional[str] = None


@dataclass
class Selector:
    key: str
    text: str
    default: int
    items: List[str]
    subtext: Optional[str] = None
    icon: Optional[str] = None
    on_change: Optional[Callable] = None
    on_long_click: Optional[Callable] = None
    link_alias: Optional[str] = None


@dataclass
class Input:
    key: str
    text: str
    default: Optional[str] = None
    subtext: Optional[str] = None
    icon: Optional[str] = None
    on_change: Optional[Callable] = None
    on_long_click: Optional[Callable] = None
    link_alias: Optional[str] = None


@dataclass
class Text:
    text: str
    subtext: Optional[str] = None
    icon: Optional[str] = None
    accent: bool = False
    red: bool = False
    on_click: Optional[Callable] = None
    on_long_click: Optional[Callable] = None
    create_sub_fragment: Optional[Callable[[], list]] = None
    link_alias: Optional[str] = None


@dataclass
class EditText:
    key: str
    hint: str
    default: str = ""
    multiline: bool = False
    max_length: Optional[int] = None
    mask: Optional[str] = None
    on_change: Optional[Callable] = None


@dataclass
class Slider:
    key: str
    text: str
    default: float = 0
    min: float = 0
    max: float = 100
    step: float = 1
    subtext: Optional[str] = None
    icon: Optional[str] = None
    on_change: Optional[Callable] = None
    on_long_click: Optional[Callable] = None
    link_alias: Optional[str] = None


@dataclass
class Custom:
    item: Optional[Any] = None
    view: Optional[Any] = None
    factory: Optional[Any] = None
    factory_args: Optional[tuple] = None
    on_click: Optional[Callable] = None
    on_long_click: Optional[Callable] = None
    create_sub_fragment: Optional[Callable[[], list]] = None
    link_alias: Optional[str] = None


class _FactoryPeer:
    def __init__(self, factory):
        self.java = factory
        self.instance = factory


class SimpleSettingFactory:
    def __init__(self, create_view=None, bind_view=None, is_clickable: bool = False,
                 is_shadow: bool = False, create_item=None, on_click=None,
                 on_long_click=None, attached_view=None, equals=None,
                 content_equals=None):
        self.create_view = create_view
        self.bind_view = bind_view
        self.is_clickable = is_clickable
        self.is_shadow = is_shadow
        self.create_item = create_item
        self.on_click = on_click
        self.on_long_click = on_long_click
        self.attached_view = attached_view
        self.equals = equals
        self.content_equals = content_equals

    @property
    def instance(self):
        return _FactoryPeer(self)

    @property
    def java(self):
        return _FactoryPeer(self)

    @classmethod
    def getInstance(cls):
        return _FactoryPeer(None)

    def __getattr__(self, item):
        return lambda *args, **kwargs: None

    def __call__(self, *factory_args, link_alias: Optional[str] = None) -> Custom:
        return Custom(factory=self,
                      factory_args=factory_args or None,
                      link_alias=link_alias)


PluginItemFactory = SimpleSettingFactory


def register_items(plugin_id, items):
    _registered[plugin_id] = list(items)


def find_item(plugin_id, index):
    items = _registered.get(plugin_id, [])
    if 0 <= index < len(items):
        return items[index]
    return None


def describe(item, index):
    kind = type(item).__name__.lower()
    if kind == "custom":
        return None
    data = {"index": index, "kind": kind}
    for key in ("text", "subtext", "key", "default", "items", "hint",
                "multiline", "max_length", "mask", "accent", "red", "icon",
                "min", "max", "step"):
        if not hasattr(item, key):
            continue
        value = getattr(item, key)
        if value is not None and not callable(value):
            data[key] = value
    data["clickable"] = bool(getattr(item, "on_click", None))
    data["long_clickable"] = bool(getattr(item, "on_long_click", None))
    return data
