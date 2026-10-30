import html
import importlib.abc
import importlib.machinery
import json
import re
import sys
from html.parser import HTMLParser

import _extera


# Stub

class StubMeta(type):
    def __getattr__(cls, item):
        if item.startswith("__") and item.endswith("__"):
            raise AttributeError(item)
        return Stub(cls.__name__ + "." + item)

    def __instancecheck__(cls, instance):
        return True

    def __subclasscheck__(cls, subclass):
        return True


class Stub(metaclass=StubMeta):
    def __init__(self, name="stub"):
        object.__setattr__(self, "_name", name)

    def __getattr__(self, item):
        if item.startswith("__") and item.endswith("__"):
            raise AttributeError(item)
        return Stub(self._name + "." + item)

    def __setattr__(self, key, value):
        object.__setattr__(self, key, value)

    def __call__(self, *args, **kwargs):
        return Stub(self._name + "()")

    def __bool__(self):
        return True

    def __iter__(self):
        return iter(())

    def __len__(self):
        return 0

    def __int__(self):
        return 0

    def __float__(self):
        return 0.0

    def __index__(self):
        return 0

    def _zero(self, *args):
        return 0

    __add__ = __radd__ = __sub__ = __rsub__ = _zero
    __mul__ = __rmul__ = __truediv__ = __rtruediv__ = _zero
    __floordiv__ = __rfloordiv__ = __mod__ = __rmod__ = _zero
    __lt__ = __le__ = __gt__ = __ge__ = lambda self, other: False

    def __str__(self):
        return ""

    def __repr__(self):
        return f"<stub {self._name}>"


class ArrayList(list):
    def add(self, *args):
        if len(args) == 1:
            self.append(args[0])
        elif len(args) == 2:
            self.insert(args[0], args[1])
        return True

    def size(self):
        return len(self)

    def get(self, index):
        try:
            return self[index]
        except IndexError:
            return None

    def remove(self, val):
        if isinstance(val, int) and val < len(self):
            return self.pop(val)
        try:
            super().remove(val)
            return True
        except ValueError:
            return False

    def clear(self):
        super().clear()

    __hash__ = None


# Shims

def _open_url(url):
    _extera.open_url(str(url))
    return True


class _Toast:
    LENGTH_SHORT = 0
    LENGTH_LONG = 1

    def __init__(self, text):
        self._text = str(text)

    @staticmethod
    def makeText(context, text, duration=0):
        return _Toast(text)

    def show(self):
        _extera.toast(self._text)


class _Uri:
    def __init__(self, value):
        self._value = str(value)

    @staticmethod
    def parse(value):
        return _Uri(value)

    def toString(self):
        return self._value

    def __str__(self):
        return self._value


class _Intent(Stub):
    ACTION_VIEW = "android.intent.action.VIEW"
    ACTION_SEND = "android.intent.action.SEND"
    FLAG_ACTIVITY_NEW_TASK = 0x10000000

    def __init__(self, action=None, uri=None, *args):
        Stub.__init__(self, "Intent")
        object.__setattr__(self, "action", action)
        object.__setattr__(self, "uri", uri)

    def addFlags(self, flags):
        return self

    def setData(self, uri):
        object.__setattr__(self, "uri", uri)
        return self


class _Context(Stub):
    def startActivity(self, intent, *args):
        uri = getattr(intent, "uri", None)
        if uri is not None:
            _open_url(uri)

    def getParentActivity(self):
        return self


class _Browser:
    @staticmethod
    def openUrl(context, url, *args):
        return _open_url(url)


class _ApplicationLoader:
    applicationContext = _Context("applicationContext")


class _LaunchActivity:
    instance = _Context("LaunchActivity")


class _AndroidUtilities(Stub):
    @staticmethod
    def addToClipboard(text):
        _extera.copy(str(text))
        return True

    @staticmethod
    def runOnUIThread(runnable, delay=0):
        run = getattr(runnable, "run", runnable)
        _extera.run_on_main(run, int(delay or 0))

    @staticmethod
    def dp(value):
        return int(value)


class _LocaleController(Stub):
    @staticmethod
    def getString(*args):
        for value in args:
            if isinstance(value, str):
                return value
        return ""

    @staticmethod
    def formatString(*args):
        return ""


def _dynamic_proxy(*interfaces):
    return object


SHIMS = {
    "android.widget": {"Toast": _Toast},
    "android.net": {"Uri": _Uri},
    "android.content": {"Intent": _Intent},
    "org.telegram.messenger.browser": {"Browser": _Browser},
    "org.telegram.messenger": {
        "ApplicationLoader": _ApplicationLoader,
        "AndroidUtilities": _AndroidUtilities("AndroidUtilities"),
        "LocaleController": _LocaleController("LocaleController"),
    },
    "org.telegram.ui": {"LaunchActivity": _LaunchActivity},
    "java": {
        "jclass": lambda name: Stub(str(name)),
        "dynamic_proxy": _dynamic_proxy,
        "jarray": lambda kind: (lambda values: list(values)),
        "cast": lambda kind, value: value,
        "jint": int,
        "jlong": int,
        "jboolean": bool,
        "jfloat": float,
        "jdouble": float,
    },
    "java.util": {
        "ArrayList": ArrayList,
        "List": list,
        "Map": dict,
        "HashMap": dict,
        "Set": set,
        "HashSet": set,
    },
    "java.lang": {
        "Boolean": bool,
        "Integer": int,
        "Long": int,
        "String": str,
        "Float": float,
        "Double": float,
        "Exception": Exception,
        "Object": object,
    },
}

ROOTS = ("java", "android", "androidx", "org", "com", "kotlin", "dalvik",
         "de", "me", "io")


class _StubModule(type(sys)):
    def __getattr__(self, item):
        if item.startswith("__") and item.endswith("__"):
            raise AttributeError(item)
        class DynamicStub(Stub):
            pass
        DynamicStub.__name__ = item
        setattr(self, item, DynamicStub)
        return DynamicStub


class _Finder(importlib.abc.MetaPathFinder, importlib.abc.Loader):
    def find_spec(self, fullname, path, target=None):
        root = fullname.split(".")[0]
        if root not in ROOTS:
            return None
        if root in ("com", "io", "me", "de") and not fullname.startswith(
                ("com.exteragram", "com.google.android", "com.chaquo",
                 "de.robv", "me.", "io.")):
            return None
        return importlib.machinery.ModuleSpec(fullname, self, is_package=True)

    def create_module(self, spec):
        module = _StubModule(spec.name)
        module.__path__ = []
        for key, value in SHIMS.get(spec.name, {}).items():
            setattr(module, key, value)
        return module

    def exec_module(self, module):
        pass


def install():
    if not any(isinstance(f, _Finder) for f in sys.meta_path):
        sys.meta_path.append(_Finder())


# Send params

class SendMessageParams:
    def __init__(self, peer=None, message="", reply_to=0):
        self.peer = peer
        self.message = message
        self.caption = None
        self.entities = []
        self.replyToMsg = _ReplyStub(reply_to) if reply_to else None
        self.replyToTopMsg = None
        self.replyMarkup = None
        self.params = None
        self.notify = True
        self.scheduleDate = 0
        self.searchLinks = True
        self.photo = None
        self.document = None
        self.path = None


class _ReplyStub(Stub):
    def __init__(self, msg_id):
        Stub.__init__(self, "replyToMsg")
        object.__setattr__(self, "id", msg_id)
        object.__setattr__(self, "messageText", "")


# Formatting

def _utf16_len(text):
    return len(text.encode("utf-16-le")) // 2


class _HtmlToEntities(HTMLParser):
    TAGS = {
        "b": "bold", "strong": "bold", "i": "italic", "em": "italic",
        "u": "underline", "ins": "underline", "s": "strike",
        "strike": "strike", "del": "strike", "code": "code", "pre": "pre",
        "blockquote": "blockquote", "tg-spoiler": "spoiler", "a": "url",
        "span": "span",
    }

    def __init__(self):
        HTMLParser.__init__(self, convert_charrefs=True)
        self.text = ""
        self.stack = []
        self.entities = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "br":
            self.text += "\n"
            return
        kind = self.TAGS.get(tag)
        if kind == "span" and "tg-spoiler" not in (attrs.get("class") or ""):
            kind = None
        if kind == "span":
            kind = "spoiler"
        if kind == "blockquote" and "expandable" in attrs:
            kind = "blockquote_collapsed"
        self.stack.append((tag, kind, _utf16_len(self.text), attrs))

    def handle_endtag(self, tag):
        for index in range(len(self.stack) - 1, -1, -1):
            if self.stack[index][0] == tag:
                _, kind, start, attrs = self.stack.pop(index)
                length = _utf16_len(self.text) - start
                if kind and length > 0:
                    data = attrs.get("href", "") if kind == "url" else ""
                    if kind == "pre":
                        data = attrs.get("language", "")
                    self.entities.append([kind, start, length, data])
                return

    def handle_data(self, data):
        self.text += data


def parse_html(text):
    parser = _HtmlToEntities()
    parser.feed(text)
    parser.close()
    return parser.text, parser.entities


_MD = [
    (re.compile(r"\*\*(.+?)\*\*", re.S), "bold"),
    (re.compile(r"__(.+?)__", re.S), "italic"),
    (re.compile(r"~~(.+?)~~", re.S), "strike"),
    (re.compile(r"\|\|(.+?)\|\|", re.S), "spoiler"),
    (re.compile(r"`([^`]+?)`", re.S), "code"),
]


def parse_markdown(text):
    entities = []
    changed = True
    while changed:
        changed = False
        best = None
        for pattern, kind in _MD:
            match = pattern.search(text)
            if match and (best is None or match.start() < best[0].start()):
                best = (match, kind)
        if best:
            match, kind = best
            inner = match.group(1)
            start = _utf16_len(text[:match.start()])
            shift = _utf16_len(match.group(0)) - _utf16_len(inner)
            for entity in entities:
                if entity[1] > start:
                    entity[1] -= shift
            entities.append([kind, start, _utf16_len(inner), ""])
            text = text[:match.start()] + inner + text[match.end():]
            changed = True
    link = re.compile(r"\[([^\]]+)\]\(([^)]+)\)")
    while True:
        match = link.search(text)
        if not match:
            break
        start = _utf16_len(text[:match.start()])
        full_len = _utf16_len(match.group(0))
        inner_len = _utf16_len(match.group(1))
        shift = full_len - inner_len
        prefix_len = 1
        for entity in entities:
            if entity[1] >= start + full_len:
                entity[1] -= shift
            elif entity[1] >= start + prefix_len:
                entity[1] -= prefix_len
        entities.append(["url", start, inner_len, match.group(2)])
        text = text[:match.start()] + match.group(1) + text[match.end():]
    return text, entities


def format_text(text, parse_mode):
    mode = (parse_mode or "").lower()
    if mode == "html":
        return parse_html(text)
    if mode == "markdown":
        return parse_markdown(text)
    return text, []


def to_dialog_id(peer):
    if peer is None or peer == 0 or isinstance(peer, Stub):
        return int(_extera.current_peer() or 0)
    try:
        dialog_id = int(peer)
        return dialog_id if dialog_id != 0 else int(_extera.current_peer() or 0)
    except Exception:
        value = getattr(peer, "user_id", None) or getattr(peer, "channel_id", None) \
            or getattr(peer, "chat_id", None) or getattr(peer, "dialog_id", None) \
            or getattr(peer, "id", None)
        return int(value or _extera.current_peer() or 0)


def send(peer, text, parse_mode=None, reply_to=0, entities=None):
    plain, parsed_entities = format_text(str(text or ""), parse_mode)
    if entities:
        for e in entities:
            offset = getattr(e, "offset", 0)
            length = getattr(e, "length", 0)
            url = getattr(e, "url", "")
            cls_name = type(e).__name__
            kind = "custom"
            if "Blockquote" in cls_name:
                kind = "blockquote"
            elif "TextUrl" in cls_name or "Url" in cls_name:
                kind = "url"
            elif "Bold" in cls_name:
                kind = "bold"
            elif "Italic" in cls_name:
                kind = "italic"
            elif "Code" in cls_name:
                kind = "code"
            elif "Pre" in cls_name:
                kind = "pre"
            elif "Spoiler" in cls_name:
                kind = "spoiler"
            elif "Strike" in cls_name:
                kind = "strike"
            if length > 0:
                parsed_entities.append([kind, offset, length, str(url or "")])
    _extera.send_message(
        to_dialog_id(peer), plain, json.dumps(parsed_entities), int(reply_to or 0))
