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

    def release(self):
        for name in ("create_view", "bind_view", "create_item", "on_click", "on_long_click",
                     "attached_view", "equals", "content_equals"):
            setattr(self, name, None)
        self._released = True

    def __call__(self, *factory_args, create_sub_fragment=None, link_alias: Optional[str] = None) -> Custom:
        if getattr(self, "_released", False):
            raise RuntimeError("SimpleSettingFactory has been released")
        item = Custom(factory=self, factory_args=factory_args or None, link_alias=link_alias)
        if create_sub_fragment is not None:
            item.create_sub_fragment = create_sub_fragment
        return item


PluginItemFactory = SimpleSettingFactory

pyobject_type = object
object_array_type = list


def typehelper(value):
    return value


def pyobject_call_method(obj, name, *args):
    return getattr(obj, name)(*args)


def register_items(plugin_id, items):
    _registered[plugin_id] = list(items)


def find_item(plugin_id, index):
    items = _registered.get(plugin_id, [])
    if 0 <= index < len(items):
        return items[index]
    return None


_UITEM_KINDS = {
    "header": "header", "shadow": "divider", "largeshadow": "divider", "space": "divider",
    "switch": "switch", "check": "switch", "buttoncheck": "switch", "roundcheckbox": "switch",
    "exteraexpandableswitch": "switch", "radio": "switch", "radiobutton": "switch",
}


def uitem_text(uitem):
    for key in ("text", "title"):
        value = getattr(uitem, "__dict__", {}).get(key)
        if isinstance(value, str) and value:
            return value
    args = [a for a in getattr(uitem, "args", ()) if isinstance(a, str)]
    return args[0] if args else ""


def uitem_subtext(uitem):
    value = getattr(uitem, "__dict__", {}).get("subtext")
    if isinstance(value, str):
        return value
    args = [a for a in getattr(uitem, "args", ()) if isinstance(a, str)]
    return args[1] if len(args) > 1 else None


def _describe_custom(item, index):
    uitem = getattr(item, "item", None)
    if uitem is None or not hasattr(uitem, "viewType"):
        return None
    kind = _UITEM_KINDS.get(str(uitem.viewType).lower(), "text")
    data = {"index": index, "kind": kind}
    text = uitem_text(uitem)
    if kind == "divider":
        if text:
            data["text"] = text
        return data
    data["text"] = text
    subtext = uitem_subtext(uitem)
    if subtext and kind != "header":
        data["subtext"] = subtext
    if kind == "switch":
        data["default"] = bool(getattr(uitem, "checked", False))
        data["value"] = data["default"]
    if getattr(uitem, "__dict__", {}).get("red"):
        data["red"] = True
    return data


def describe(item, index):
    kind = type(item).__name__.lower()
    if kind == "custom":
        return _describe_custom(item, index)
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


def _exd_lenient(cls):
    import dataclasses
    if not dataclasses.is_dataclass(cls):
        return
    original = cls.__init__
    names = {f.name for f in dataclasses.fields(cls)}

    fields = dataclasses.fields(cls)

    def init(self, *args, **kwargs):
        extra = {k: kwargs.pop(k) for k in list(kwargs) if k not in names}
        for field in fields[len(args):]:
            if field.name in kwargs:
                continue
            if field.default is not dataclasses.MISSING or field.default_factory is not dataclasses.MISSING:
                continue
            kind = str(field.type)
            kwargs[field.name] = "" if "str" in kind else False if "bool" in kind else 0 if (
                "int" in kind or "float" in kind) else None
        original(self, *args, **kwargs)
        if extra:
            self.__dict__.setdefault("_extra", {}).update(extra)

    cls.__init__ = init


for _cls in list(globals().values()):
    if isinstance(_cls, type):
        _exd_lenient(_cls)
