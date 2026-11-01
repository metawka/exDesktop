import html
import importlib.abc
import importlib.machinery
import json
import re
import os
import sys
from html.parser import HTMLParser

import _extera

try:
    from tl_runtime import ArrayList
except ImportError:
    class ArrayList(list):
        pass


# Stub

class Stub:
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
    __or__ = __ror__ = __and__ = __rand__ = __xor__ = __rxor__ = _zero
    __lshift__ = __rlshift__ = __rshift__ = __rrshift__ = _zero
    __neg__ = __pos__ = __invert__ = __abs__ = _zero

    def __getitem__(self, item):
        return Stub(self._name + "[]")

    def __setitem__(self, item, value):
        return None

    def __contains__(self, item):
        return False

    def __fspath__(self):
        import tempfile
        path = os.path.join(tempfile.gettempdir(), "exdesktop_plugins")
        os.makedirs(path, exist_ok=True)
        return path
    __lt__ = __le__ = __gt__ = __ge__ = lambda self, other: False

    def __str__(self):
        return ""

    def __repr__(self):
        return f"<stub {self._name}>"

    def __eq__(self, other):
        return isinstance(other, Stub)

    def __hash__(self):
        return hash(self._name)

    def __mro_entries__(self, bases):
        return (object,)


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


class LenientMeta(type):
    def __getattr__(cls, item):
        if item.startswith("__") and item.endswith("__"):
            raise AttributeError(item)
        return Stub(cls.__name__ + "." + item)


class Missing(Stub):
    def __bool__(self):
        return False


class _ApplicationLoader(metaclass=LenientMeta):
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
        import android_views
        return android_views.dp(value)

    @staticmethod
    def displaySize():
        import android_views
        return android_views.displaySize


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


def _widget_shims():
    import android_views
    return {
        "android.widget": {
            "LinearLayout": android_views.LinearLayout,
            "FrameLayout": android_views.FrameLayout,
            "ScrollView": android_views.ScrollView,
            "TextView": android_views.TextView,
            "EditText": android_views.EditText,
            "ImageView": android_views.ImageView,
            "Toast": _Toast,
        },
        "android.view": {
            "View": android_views.View,
            "Gravity": android_views.Gravity,
        },
        "android.graphics": {
            "Bitmap": android_views.Bitmap,
            "Canvas": android_views.Canvas,
            "Paint": android_views.Paint,
            "RectF": android_views.RectF,
            "Typeface": android_views.Typeface,
        },
        "android.graphics.drawable": {
            "GradientDrawable": android_views.GradientDrawable,
            "ColorDrawable": android_views.ColorDrawable,
            "BitmapDrawable": android_views.BitmapDrawable,
            "Drawable": android_views.Drawable,
            "LayerDrawable": android_views.LayerDrawable,
        },
        "android.text": {
            "TextWatcher": android_views.TextWatcher,
        },
        "android.view.animation": {
            "DecelerateInterpolator": android_views.DecelerateInterpolator,
            "OvershootInterpolator": android_views.OvershootInterpolator,
        },
        "org.telegram.ui.ActionBar": {
            "BottomSheet": android_views.BottomSheet,
            "AlertDialog": android_views.AlertDialog,
            "Theme": android_views.Theme,
        },
    }


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
    "org.telegram.tgnet": {},
    "org.telegram.tgnet.tl": {},
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
}

ROOTS = ("java", "android", "androidx", "org", "com", "kotlin", "dalvik",
         "de", "me", "io")


class _StubModule(type(sys)):
    def __getattr__(self, item):
        if item.startswith("__") and item.endswith("__"):
            raise AttributeError(item)
        value = Stub(self.__name__ + "." + item)
        setattr(self, item, value)
        return value


class _Finder(importlib.abc.MetaPathFinder, importlib.abc.Loader):
    def find_spec(self, fullname, path, target=None):
        root = fullname.split(".")[0]
        if root not in ROOTS:
            return None
        if fullname != root and root in ("com", "io", "me", "de") \
                and not fullname.startswith(
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
        import android_views
        for module, shims in _widget_shims().items():
            SHIMS.setdefault(module, {}).update(shims)
        import tgnet_bridge
        tgnet_bridge.install(SHIMS)
        android = SHIMS.setdefault("org.telegram.messenger", {})["AndroidUtilities"]
        android.displaySize = android_views.displaySize
        android.density = android_views.density()
        sys.meta_path.append(_Finder())
    if sys.platform == "win32" and "fcntl" not in sys.modules:
        sys.modules["fcntl"] = _fcntl_module()


def _fcntl_module():
    module = type(sys)("fcntl")
    for index, name in enumerate(
            ("LOCK_SH", "LOCK_EX", "LOCK_NB", "LOCK_UN", "F_GETFL", "F_SETFL",
             "F_GETFD", "F_SETFD", "FD_CLOEXEC")):
        setattr(module, name, 1 << index)
    module.flock = lambda *args, **kwargs: None
    module.lockf = lambda *args, **kwargs: None
    module.fcntl = lambda *args, **kwargs: 0
    module.ioctl = lambda *args, **kwargs: 0
    return module


# Send params

class SendMessageParams:
    def __init__(self, peer=None, message="", reply_to=0):
        self.peer = peer
        self.message = message
        self.caption = None
        self.entities = []
        self.replyToMsg = _ReplyStub(reply_to) if reply_to else None
        self.replyToTopMsg = None
        self.replyQuote = None
        self.replyQuoteOffset = None
        self.replyQuoteLength = None
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
        object.__setattr__(self, "messageOwner", None)


# Formatting

try:
    from markdown_utils import parse_markdown, parse_html, MarkdownEntity, ParsedText
except ImportError:
    pass


def format_text(text, parse_mode):
    from text_formatting import parse_text
    mode = (parse_mode or "").lower()
    if mode not in ("html", "markdown"):
        return text, []
    key = "message"
    parsed = parse_text(str(text or ""), mode, is_caption=False)
    return parsed.get(key, str(text)), parsed.get("entities") or []


def to_dialog_id(peer):
    if isinstance(peer, Stub):
        return 0
    try:
        return int(peer)
    except Exception:
        value = getattr(peer, "user_id", None) or getattr(peer, "channel_id", None) \
            or getattr(peer, "chat_id", None)
        return int(value or 0)


def send(peer, text, parse_mode=None, reply_to=0):
    plain, entities = format_text(str(text or ""), parse_mode)
    _extera.send_message(
        to_dialog_id(peer), plain, json.dumps(entities), int(reply_to or 0))
