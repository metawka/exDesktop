import os
import time

import exd_java
from exd_java import JavaObject, File

_START = time.monotonic()


# Process

class Process(JavaObject):
    _java_name = "android.os.Process"
    THREAD_PRIORITY_DEFAULT = 0
    THREAD_PRIORITY_LOWEST = 19
    THREAD_PRIORITY_BACKGROUND = 10
    THREAD_PRIORITY_FOREGROUND = -2
    THREAD_PRIORITY_DISPLAY = -4
    THREAD_PRIORITY_URGENT_DISPLAY = -8
    THREAD_PRIORITY_AUDIO = -16
    SYSTEM_UID = 1000

    @staticmethod
    def myPid():
        return os.getpid()

    @staticmethod
    def myTid():
        import threading
        return threading.get_native_id()

    @staticmethod
    def myUid():
        return 10000

    @staticmethod
    def killProcess(pid):
        return None

    @staticmethod
    def sendSignal(pid, signal):
        return None

    @staticmethod
    def setThreadPriority(*args):
        return None

    @staticmethod
    def getThreadPriority(tid=0):
        return 0

    @staticmethod
    def getStartElapsedRealtime():
        return 0

    @staticmethod
    def getElapsedCpuTime():
        return int(time.process_time() * 1000)

    @staticmethod
    def is64Bit():
        return True


# Environment

def _known_folder(name):
    home = os.path.expanduser("~")
    candidates = {
        "Download": "Downloads",
        "Movies": "Videos",
        "Pictures": "Pictures",
        "Music": "Music",
        "Documents": "Documents",
        "DCIM": "Pictures",
    }
    folder = os.path.join(home, candidates.get(name, name))
    return folder


class Environment(JavaObject):
    _java_name = "android.os.Environment"
    DIRECTORY_DOWNLOADS = "Download"
    DIRECTORY_MOVIES = "Movies"
    DIRECTORY_PICTURES = "Pictures"
    DIRECTORY_MUSIC = "Music"
    DIRECTORY_DOCUMENTS = "Documents"
    DIRECTORY_DCIM = "DCIM"
    DIRECTORY_PODCASTS = "Podcasts"
    DIRECTORY_RINGTONES = "Ringtones"
    DIRECTORY_ALARMS = "Alarms"
    DIRECTORY_NOTIFICATIONS = "Notifications"
    DIRECTORY_SCREENSHOTS = "Screenshots"
    MEDIA_MOUNTED = "mounted"

    @staticmethod
    def getExternalStoragePublicDirectory(kind):
        return File(_known_folder(str(kind)))

    @staticmethod
    def getExternalStorageDirectory():
        return File(os.path.expanduser("~"))

    @staticmethod
    def getDataDirectory():
        base = os.environ.get("APPDATA") or os.path.expanduser("~")
        return File(base)

    @staticmethod
    def getDownloadCacheDirectory():
        import tempfile
        return File(tempfile.gettempdir())

    @staticmethod
    def getRootDirectory():
        return File(os.path.abspath(os.sep))

    @staticmethod
    def getExternalStorageState(*args):
        return "mounted"

    @staticmethod
    def isExternalStorageManager(*args):
        return True


# Colors

class ColorStateList(JavaObject):
    _java_name = "android.content.res.ColorStateList"

    def __init__(self, states=None, colors=None):
        values = list(colors or [])
        self._color = values[-1] if values else 0

    @staticmethod
    def valueOf(color):
        result = ColorStateList()
        result._color = int(color)
        return result

    def getDefaultColor(self):
        return self._color

    def getColorForState(self, state, default=0):
        return self._color

    def isStateful(self):
        return False

    def withAlpha(self, alpha):
        return ColorStateList.valueOf((int(alpha) << 24) | (self._color & 0xFFFFFF))


# Text

class Spanned(JavaObject):
    _java_name = "android.text.Spanned"
    SPAN_INCLUSIVE_EXCLUSIVE = 17
    SPAN_INCLUSIVE_INCLUSIVE = 18
    SPAN_EXCLUSIVE_EXCLUSIVE = 33
    SPAN_EXCLUSIVE_INCLUSIVE = 34
    SPAN_PARAGRAPH = 51
    SPAN_POINT_MARK = 17
    SPAN_MARK_POINT = 34


class SpannableString(Spanned):
    _java_name = "android.text.SpannableString"

    def __init__(self, text=""):
        self._text = str(text) if text is not None else ""
        self._spans = []

    @staticmethod
    def valueOf(text):
        return SpannableString(text)

    def setSpan(self, span, start, end, flags=0):
        self._spans.append((span, int(start), int(end), int(flags)))

    def removeSpan(self, span):
        self._spans = [s for s in self._spans if s[0] is not span]

    def getSpans(self, start=0, end=None, kind=None):
        return [s[0] for s in self._spans]

    def getSpanStart(self, span):
        return next((s[1] for s in self._spans if s[0] is span), -1)

    def getSpanEnd(self, span):
        return next((s[2] for s in self._spans if s[0] is span), -1)

    def length(self):
        return len(self._text)

    def charAt(self, index):
        return self._text[int(index)]

    def subSequence(self, start, end):
        return SpannableString(self._text[int(start):int(end)])

    def toString(self):
        return self._text

    def __str__(self):
        return self._text

    def __len__(self):
        return len(self._text)


class SpannableStringBuilder(SpannableString):
    _java_name = "android.text.SpannableStringBuilder"

    @staticmethod
    def valueOf(text):
        return SpannableStringBuilder(text)

    def append(self, text, *args):
        start = len(self._text)
        self._text += str(text) if text is not None else ""
        if len(args) >= 2:
            self.setSpan(args[0], start, len(self._text), args[1])
        return self

    def insert(self, where, text):
        where = int(where)
        self._text = self._text[:where] + str(text) + self._text[where:]
        return self

    def replace(self, start, end, text, *args):
        self._text = self._text[:int(start)] + str(text) + self._text[int(end):]
        return self

    def delete(self, start, end):
        self._text = self._text[:int(start)] + self._text[int(end):]
        return self

    def clear(self):
        self._text = ""
        self._spans = []

    def clearSpans(self):
        self._spans = []


class _Span(JavaObject):
    def __init__(self, *args):
        self._args = args

    def updateDrawState(self, *args):
        return None


class ForegroundColorSpan(_Span):
    _java_name = "android.text.style.ForegroundColorSpan"

    def getForegroundColor(self):
        return int(self._args[0]) if self._args else 0


class BackgroundColorSpan(_Span):
    _java_name = "android.text.style.BackgroundColorSpan"

    def getBackgroundColor(self):
        return int(self._args[0]) if self._args else 0


class StyleSpan(_Span):
    _java_name = "android.text.style.StyleSpan"

    def getStyle(self):
        return int(self._args[0]) if self._args else 0


class RelativeSizeSpan(_Span):
    _java_name = "android.text.style.RelativeSizeSpan"


class AbsoluteSizeSpan(_Span):
    _java_name = "android.text.style.AbsoluteSizeSpan"


class UnderlineSpan(_Span):
    _java_name = "android.text.style.UnderlineSpan"


class StrikethroughSpan(_Span):
    _java_name = "android.text.style.StrikethroughSpan"


class ClickableSpan(_Span):
    _java_name = "android.text.style.ClickableSpan"

    def onClick(self, view):
        return None


class URLSpan(_Span):
    _java_name = "android.text.style.URLSpan"

    def getURL(self):
        return str(self._args[0]) if self._args else ""


class TypefaceSpan(_Span):
    _java_name = "android.text.style.TypefaceSpan"


class ImageSpan(_Span):
    _java_name = "android.text.style.ImageSpan"


class ReplacementSpan(_Span):
    _java_name = "android.text.style.ReplacementSpan"


# Dialogs

class DialogObject(JavaObject):
    _java_name = "org.telegram.messenger.DialogObject"

    @staticmethod
    def isUserDialog(dialog_id):
        return int(dialog_id) > 0

    @staticmethod
    def isChatDialog(dialog_id):
        return int(dialog_id) < 0

    @staticmethod
    def isEncryptedDialog(dialog_id):
        return False

    @staticmethod
    def isFolderDialogId(dialog_id):
        return 0 < int(dialog_id) < 8

    @staticmethod
    def getEncryptedChatId(dialog_id):
        return 0

    @staticmethod
    def makeEncryptedDialogId(chat_id):
        return 0

    @staticmethod
    def getFolderId(dialog_id):
        return 0

    @staticmethod
    def isChannel(dialog):
        peer = getattr(dialog, "peer", None)
        return bool(getattr(peer, "channel_id", 0))

    @staticmethod
    def getDialogId(dialog):
        return int(getattr(dialog, "id", 0) or 0)

    @staticmethod
    def getPeerDialogId(peer):
        if peer is None:
            return 0
        user_id = getattr(peer, "user_id", 0) or 0
        if user_id:
            return int(user_id)
        chat_id = getattr(peer, "chat_id", 0) or 0
        if chat_id:
            return -int(chat_id)
        channel_id = getattr(peer, "channel_id", 0) or 0
        return -int(channel_id)

    @staticmethod
    def getLastMessageOrDraftDate(dialog, draft=None):
        draft_date = int(getattr(draft, "date", 0) or 0)
        return max(int(getattr(dialog, "last_message_date", 0) or 0), draft_date)

    @staticmethod
    def getEmojiStatusDocumentId(status):
        return int(getattr(status, "document_id", 0) or 0)

    @staticmethod
    def getEmojiStatusUntil(status):
        return int(getattr(status, "until", 0) or 0)


exd_java._register(
    Process, Environment, ColorStateList, Spanned, SpannableString,
    SpannableStringBuilder, ForegroundColorSpan, BackgroundColorSpan,
    StyleSpan, RelativeSizeSpan, AbsoluteSizeSpan, UnderlineSpan,
    StrikethroughSpan, ClickableSpan, URLSpan, TypefaceSpan, ImageSpan,
    ReplacementSpan, DialogObject)
