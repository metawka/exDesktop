import io
import os
import random
import string
import zlib

import _extera
from exd_java import JavaObject, _LenientJavaMeta


# Helpers

def _host():
    import extera_host
    return extera_host


def _log(text):
    try:
        _host().log(str(text))
    except Exception:
        pass


def _plugin_dir(*parts):
    import _compat
    return os.path.join(os.fspath(_compat._plugin_files_dir()), *parts)


def _path_of(value):
    if value is None:
        return ""
    getter = getattr(value, "getAbsolutePath", None) or getattr(value, "getPath", None)
    if callable(getter):
        return str(getter())
    return os.fspath(value) if isinstance(value, os.PathLike) else str(value)


def _to_bytes(data):
    if data is None:
        return b""
    if isinstance(data, (bytes, bytearray, memoryview)):
        return bytes(data)
    if isinstance(data, str):
        return data.encode("utf-8")
    return bytes(int(x) & 0xFF for x in data)


# Context

class ClipData(JavaObject):
    _java_name = "android.content.ClipData"

    def __init__(self, label="", text=""):
        self.label = str(label or "")
        self.text = "" if text is None else str(text)

    @staticmethod
    def newPlainText(label, text):
        return ClipData(label, text)

    @staticmethod
    def newHtmlText(label, text, html=None):
        return ClipData(label, text)

    def getItemCount(self):
        return 1

    def getItemAt(self, index=0):
        return _ClipItem(self.text)

    def getDescription(self):
        return self


class _ClipItem(JavaObject):
    _java_name = "android.content.ClipData$Item"

    def __init__(self, text):
        self._text = text

    def getText(self):
        return self._text

    def coerceToText(self, *args):
        return self._text


class ClipboardManager(JavaObject):
    _java_name = "android.content.ClipboardManager"
    _last = [None]

    def setPrimaryClip(self, clip):
        text = getattr(clip, "text", None)
        if text is None:
            text = str(clip)
        ClipboardManager._last[0] = ClipData("", text)
        _extera.copy(str(text))

    def setText(self, text):
        self.setPrimaryClip(ClipData("", text))

    def getPrimaryClip(self):
        return ClipboardManager._last[0]

    def hasPrimaryClip(self):
        return ClipboardManager._last[0] is not None

    def getText(self):
        clip = ClipboardManager._last[0]
        return clip.text if clip else None

    def addPrimaryClipChangedListener(self, *args):
        return None

    def removePrimaryClipChangedListener(self, *args):
        return None


class _Vibrator(JavaObject):
    _java_name = "android.os.Vibrator"

    def vibrate(self, *args):
        return None

    def hasVibrator(self):
        return False

    def cancel(self):
        return None


class _ServiceStub:
    def __getattr__(self, item):
        if item.startswith("__"):
            raise AttributeError(item)
        return lambda *args, **kwargs: None


_CONTEXT_CONSTANTS = {
    "CLIPBOARD_SERVICE": "clipboard",
    "WINDOW_SERVICE": "window",
    "ACTIVITY_SERVICE": "activity",
    "INPUT_METHOD_SERVICE": "input_method",
    "WIFI_SERVICE": "wifi",
    "TELEPHONY_SERVICE": "phone",
    "VIBRATOR_SERVICE": "vibrator",
    "NOTIFICATION_SERVICE": "notification",
    "AUDIO_SERVICE": "audio",
    "CONNECTIVITY_SERVICE": "connectivity",
    "POWER_SERVICE": "power",
    "LAYOUT_INFLATER_SERVICE": "layout_inflater",
    "DOWNLOAD_SERVICE": "download",
    "MODE_PRIVATE": 0,
    "MODE_APPEND": 32768,
}


def get_system_service(name):
    name = str(name)
    if name == "clipboard":
        return ClipboardManager()
    if name == "vibrator":
        return _Vibrator()
    import _compat
    return _compat.Stub("android.app.Service." + name)


def _context_class():
    import _compat

    class Context(_compat._Context):
        _java_name = "android.content.Context"

    for key, value in _CONTEXT_CONSTANTS.items():
        setattr(Context, key, value)
        setattr(_compat._Context, key, value)
    _compat._Context.getSystemService = lambda self, name, *args: get_system_service(name)
    _compat._Context.getApplicationContext = lambda self, *args: self
    return Context


# Resources

class _ResId(str):
    def __int__(self):
        return zlib.crc32(self.encode()) & 0x7FFFFFFF

    def __index__(self):
        return int(self)


class _ResGroup:
    def __init__(self, kind):
        self._kind = kind

    def __getattr__(self, item):
        if item.startswith("__"):
            raise AttributeError(item)
        return _ResId(item)


class R(JavaObject):
    _java_name = "org.telegram.messenger.R"
    drawable = _ResGroup("drawable")
    raw = _ResGroup("raw")
    string = _ResGroup("string")
    id = _ResGroup("id")
    style = _ResGroup("style")
    attr = _ResGroup("attr")
    color = _ResGroup("color")
    dimen = _ResGroup("dimen")
    layout = _ResGroup("layout")
    plurals = _ResGroup("plurals")
    array = _ResGroup("array")
    bool = _ResGroup("bool")
    integer = _ResGroup("integer")
    anim = _ResGroup("anim")
    font = _ResGroup("font")


# Notifications

_NOTIFICATION_NAMES = (
    "didReceiveNewMessages", "messagesDidLoad", "fileLoaded", "fileLoadFailed", "fileLoadProgressChanged",
    "updateInterfaces", "emojiLoaded", "messagesDeleted", "dialogsNeedReload", "mainUserInfoChanged",
    "didUpdateConnectionState", "messagesRead", "messageReceivedByServer", "messageSendError",
    "chatInfoDidLoad", "userInfoDidLoad", "closeChats", "notificationsSettingsUpdated", "reloadInterface",
    "didReplacedPhotoInMemCache", "stickersDidLoad", "recentDocumentsDidLoad", "updateMessageMedia",
    "messagePlayingDidStart", "messagePlayingDidReset", "messagePlayingPlayStateChanged",
    "messagePlayingProgressDidChanged", "chatDidCreated", "contactsDidLoad", "privacyRulesUpdated",
    "appDidLogout", "botKeyboardDidLoad", "replaceMessagesObjects", "historyCleared", "dialogDeleted",
    "updateTranscriptionLock", "themeListUpdated", "didSetNewTheme", "needSetDayNightTheme",
    "currentUserPremiumStatusChanged", "pluginsLoaded", "pluginSettingsChanged",
)


class _NotificationMeta(_LenientJavaMeta):
    def __getattr__(cls, item):
        if item.startswith("__"):
            raise AttributeError(item)
        return 1000 + (zlib.crc32(item.encode()) & 0xFFFF)


class NotificationCenterDelegate(JavaObject):
    _java_name = "org.telegram.messenger.NotificationCenter$NotificationCenterDelegate"

    def didReceivedNotification(self, notification_id, account, *args):
        pass


class _Center(JavaObject):
    _java_name = "org.telegram.messenger.NotificationCenter"

    def __init__(self, account):
        self.account = account

    def addObserver(self, delegate, notification_id):
        host = _host()
        plugin_id = host.current_plugin_id()
        if plugin_id and getattr(delegate, "_extera_plugin_id", None) is None:
            try:
                delegate._extera_plugin_id = plugin_id
            except Exception:
                pass
        if delegate not in host._observers.get(int(notification_id), []):
            host.add_observer(delegate, int(notification_id))

    def removeObserver(self, delegate, notification_id):
        _host().remove_observer(delegate, int(notification_id))

    def hasObservers(self, notification_id):
        return bool(_host()._observers.get(int(notification_id)))

    def postNotificationName(self, notification_id, *args):
        post(int(notification_id), self.account, *args)

    def postNotificationNameOnUIThread(self, notification_id, *args):
        notification_id = int(notification_id)
        account = self.account
        _extera.run_on_main(lambda: post(notification_id, account, *args), 0)

    def isAnimationInProgress(self, *args):
        return False

    def setAnimationInProgress(self, *args):
        return 0

    def onAnimationFinish(self, *args):
        return None


class NotificationCenter(JavaObject, metaclass=_NotificationMeta):
    _java_name = "org.telegram.messenger.NotificationCenter"
    NotificationCenterDelegate = NotificationCenterDelegate
    _centers = {}

    @staticmethod
    def getInstance(account=0):
        try:
            account = int(account or 0)
        except Exception:
            account = 0
        center = NotificationCenter._centers.get(account)
        if center is None:
            center = NotificationCenter._centers[account] = _Center(account)
        return center

    @staticmethod
    def getGlobalInstance():
        return NotificationCenter.getInstance(-1)


for _index, _name in enumerate(_NOTIFICATION_NAMES, start=1):
    setattr(NotificationCenter, _name, _index)


def post(notification_id, account, *args):
    host = _host()
    for delegate in list(host._observers.get(int(notification_id), [])):
        plugin_id = getattr(delegate, "_extera_plugin_id", None)
        try:
            if plugin_id:
                with host._plugin_context(plugin_id):
                    delegate.didReceivedNotification(int(notification_id), account, *args)
            else:
                delegate.didReceivedNotification(int(notification_id), account, *args)
        except TypeError:
            try:
                delegate.didReceivedNotification(int(notification_id), account, list(args))
            except Exception as e:
                _log("notification %d delegate failed: %r" % (notification_id, e))
        except Exception as e:
            _log("notification %d delegate failed: %r" % (notification_id, e))


def post_new_message(account, update, message):
    if not _host()._observers.get(NotificationCenter.didReceiveNewMessages):
        return
    import exd_java
    dialog_id = int((update or {}).get("dialog_id") or 0)
    post(NotificationCenter.didReceiveNewMessages, account, dialog_id, exd_java.ArrayList([message]), False)


# Files

class FileLoader(JavaObject):
    _java_name = "org.telegram.messenger.FileLoader"
    PRIORITY_LOW = 0
    PRIORITY_NORMAL_UP = 1
    PRIORITY_NORMAL = 2
    PRIORITY_HIGH = 3
    PRIORITY_STREAM = 4
    MEDIA_DIR_IMAGE = 0
    MEDIA_DIR_AUDIO = 1
    MEDIA_DIR_VIDEO = 2
    MEDIA_DIR_DOCUMENT = 3
    MEDIA_DIR_CACHE = 4
    MEDIA_DIR_FILES = 5
    MEDIA_DIR_STORIES = 6
    IMAGE_TYPE_LOTTIE = 1
    IMAGE_TYPE_ANIMATION = 2
    IMAGE_TYPE_SVG = 3
    _instances = {}

    def __init__(self, account=0):
        self.account = account

    @staticmethod
    def getInstance(account=0):
        try:
            account = int(account or 0)
        except Exception:
            account = 0
        loader = FileLoader._instances.get(account)
        if loader is None:
            loader = FileLoader._instances[account] = FileLoader(account)
        return loader

    @staticmethod
    def getDirectory(kind=4):
        import exd_java
        path = _plugin_dir("cache")
        os.makedirs(path, exist_ok=True)
        return exd_java.File(path)

    @staticmethod
    def getAttachFileName(obj, ext=None, *args):
        name = FileLoader.getDocumentFileName(obj)
        if name:
            return name
        ident = getattr(obj, "id", None) or getattr(getattr(obj, "location", None), "local_id", None) or 0
        dc = getattr(obj, "dc_id", 0) or 0
        suffix = ext if isinstance(ext, str) else ".jpg"
        return "%s_%s%s" % (dc, ident, suffix)

    @staticmethod
    def getDocumentFileName(document):
        if document is None:
            return ""
        name = getattr(document, "file_name", None)
        if isinstance(name, str) and name:
            return name
        for attribute in list(getattr(document, "attributes", None) or []):
            value = getattr(attribute, "file_name", None)
            if isinstance(value, str) and value:
                return value
        return ""

    @staticmethod
    def getClosestPhotoSizeWithSize(sizes, side, by_min_side=False, *args):
        best = None
        best_value = 0
        for size in list(sizes or []):
            width = int(getattr(size, "w", 0) or 0)
            height = int(getattr(size, "h", 0) or 0)
            value = min(width, height) if by_min_side else max(width, height)
            if best is None or (best_value < side and value > best_value) or (value >= side and value < best_value):
                best, best_value = size, value
        return best

    @staticmethod
    def getFileExtension(file):
        name = _path_of(file)
        return name.rsplit(".", 1)[-1] if "." in name else ""

    @staticmethod
    def fixFileName(name):
        return "".join(c for c in str(name or "") if c not in '<>:"/\\|?*')

    def getPathToAttach(self, obj=None, *args):
        import exd_java
        return exd_java.File(FileLoader.getDirectory(), FileLoader.getAttachFileName(obj))

    def getPathToMessage(self, message=None, *args):
        media = getattr(message, "media", None)
        document = getattr(media, "document", None) or getattr(media, "photo", None)
        return self.getPathToAttach(document or message)

    def loadFile(self, *args):
        return None

    def cancelLoadFile(self, *args):
        return None

    def isLoadingFile(self, *args):
        return False

    def getBufferedProgressFromPosition(self, *args):
        return 0.0


class FileOutputStream(JavaObject):
    _java_name = "java.io.FileOutputStream"

    def __init__(self, target, append=False):
        self._path = _path_of(target)
        folder = os.path.dirname(self._path)
        if folder:
            os.makedirs(folder, exist_ok=True)
        self._file = open(self._path, "ab" if append else "wb")

    def write(self, data, offset=0, length=None):
        if isinstance(data, int) and length is None and offset == 0:
            self._file.write(bytes([data & 0xFF]))
            return
        chunk = _to_bytes(data)
        if length is not None:
            chunk = chunk[int(offset):int(offset) + int(length)]
        self._file.write(chunk)

    def flush(self):
        self._file.flush()

    def close(self):
        self._file.close()

    def getFD(self):
        return self

    def sync(self):
        self._file.flush()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


class FileInputStream(JavaObject):
    _java_name = "java.io.FileInputStream"

    def __init__(self, target):
        self._file = open(_path_of(target), "rb")

    def read(self, buffer=None, offset=0, length=None):
        if buffer is None:
            value = self._file.read(1)
            return value[0] if value else -1
        size = len(buffer) - int(offset) if length is None else int(length)
        chunk = self._file.read(size)
        if not chunk:
            return -1
        for index, value in enumerate(chunk):
            buffer[int(offset) + index] = value
        return len(chunk)

    def readAllBytes(self):
        return self._file.read()

    def readBytes(self):
        return self._file.read()

    def available(self):
        position = self._file.tell()
        end = self._file.seek(0, 2)
        self._file.seek(position)
        return end - position

    def skip(self, count):
        self._file.seek(int(count), 1)
        return int(count)

    def close(self):
        self._file.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


class ByteArrayOutputStream(JavaObject):
    _java_name = "java.io.ByteArrayOutputStream"

    def __init__(self, size=32):
        self._buffer = io.BytesIO()

    def write(self, data, offset=0, length=None):
        if isinstance(data, int) and length is None and offset == 0:
            self._buffer.write(bytes([data & 0xFF]))
            return
        chunk = _to_bytes(data)
        if length is not None:
            chunk = chunk[int(offset):int(offset) + int(length)]
        self._buffer.write(chunk)

    def toByteArray(self):
        return self._buffer.getvalue()

    def size(self):
        return len(self._buffer.getvalue())

    def reset(self):
        self._buffer = io.BytesIO()

    def toString(self, *args):
        return self._buffer.getvalue().decode("utf-8", "replace")

    def flush(self):
        return None

    def close(self):
        return None


class ByteArrayInputStream(FileInputStream):
    _java_name = "java.io.ByteArrayInputStream"

    def __init__(self, data, offset=0, length=None):
        chunk = _to_bytes(data)
        if length is not None:
            chunk = chunk[int(offset):int(offset) + int(length)]
        self._file = io.BytesIO(chunk)


# Bitmaps

def _image_bitmap_class():
    import android_views

    class ImageBitmap(android_views.Bitmap):
        def __init__(self, image):
            android_views.Bitmap.__init__(self, image.width, image.height)
            self.image = image
            self._recycled = False

        def getWidth(self):
            return self.width

        def getHeight(self):
            return self.height

        def getByteCount(self):
            return self.width * self.height * 4

        def compress(self, fmt, quality, stream):
            name = str(fmt or "PNG").upper()
            kind = "JPEG" if "JP" in name else "WEBP" if "WEBP" in name else "PNG"
            buffer = io.BytesIO()
            image = self.image.convert("RGB") if kind == "JPEG" else self.image
            image.save(buffer, kind, quality=int(quality or 90))
            stream.write(buffer.getvalue())
            return True

        def recycle(self):
            self._recycled = True

        def isRecycled(self):
            return self._recycled

        def copy(self, *args):
            return ImageBitmap(self.image.copy())

        def getPixel(self, x, y):
            r, g, b, a = self.image.convert("RGBA").getpixel((int(x), int(y)))
            return (a << 24) | (r << 16) | (g << 8) | b

    return ImageBitmap


_IMAGE_BITMAP = []


def image_bitmap(image):
    if not _IMAGE_BITMAP:
        _IMAGE_BITMAP.append(_image_bitmap_class())
    return _IMAGE_BITMAP[0](image)


class BitmapFactory(JavaObject):
    _java_name = "android.graphics.BitmapFactory"

    class Options:
        def __init__(self):
            self.inJustDecodeBounds = False
            self.inSampleSize = 1
            self.inPreferredConfig = None
            self.inMutable = False
            self.outWidth = -1
            self.outHeight = -1
            self.outMimeType = None

    @staticmethod
    def _decode(source, options=None):
        try:
            from PIL import Image
            image = Image.open(source)
            image.load()
        except Exception:
            return None
        if options is not None:
            options.outWidth, options.outHeight = image.size
            options.outMimeType = "image/" + str(image.format or "png").lower()
            if getattr(options, "inJustDecodeBounds", False):
                return None
            sample = int(getattr(options, "inSampleSize", 1) or 1)
            if sample > 1:
                image = image.resize((max(1, image.width // sample), max(1, image.height // sample)))
        return image_bitmap(image)

    @staticmethod
    def decodeFile(path, options=None):
        return BitmapFactory._decode(_path_of(path), options)

    @staticmethod
    def decodeByteArray(data, offset=0, length=None, options=None):
        chunk = _to_bytes(data)
        if length is not None:
            chunk = chunk[int(offset):int(offset) + int(length)]
        return BitmapFactory._decode(io.BytesIO(chunk), options)

    @staticmethod
    def decodeStream(stream, *args):
        options = args[-1] if args and isinstance(args[-1], BitmapFactory.Options) else None
        reader = getattr(stream, "readAllBytes", None) or getattr(stream, "read", None)
        data = reader() if callable(reader) else stream
        return BitmapFactory._decode(io.BytesIO(_to_bytes(data)), options)

    @staticmethod
    def decodeResource(*args):
        return None


def _patch_bitmap():
    import android_views
    bitmap = android_views.Bitmap

    class CompressFormat:
        PNG = "PNG"
        JPEG = "JPEG"
        WEBP = "WEBP"
        WEBP_LOSSY = "WEBP"
        WEBP_LOSSLESS = "WEBP"

    bitmap.CompressFormat = CompressFormat
    if not hasattr(bitmap, "getWidth"):
        bitmap.getWidth = lambda self: self.width
        bitmap.getHeight = lambda self: self.height

    def create_scaled(source, width, height, *args):
        image = getattr(source, "image", None)
        if image is not None:
            return image_bitmap(image.resize((max(1, int(width)), max(1, int(height)))))
        return bitmap(width, height)

    bitmap.createScaledBitmap = staticmethod(create_scaled)


# Constants

class InputType(JavaObject):
    _java_name = "android.text.InputType"
    TYPE_NULL = 0
    TYPE_CLASS_TEXT = 1
    TYPE_CLASS_NUMBER = 2
    TYPE_CLASS_PHONE = 3
    TYPE_CLASS_DATETIME = 4
    TYPE_TEXT_FLAG_CAP_CHARACTERS = 4096
    TYPE_TEXT_FLAG_CAP_WORDS = 8192
    TYPE_TEXT_FLAG_CAP_SENTENCES = 16384
    TYPE_TEXT_FLAG_AUTO_CORRECT = 32768
    TYPE_TEXT_FLAG_AUTO_COMPLETE = 65536
    TYPE_TEXT_FLAG_MULTI_LINE = 131072
    TYPE_TEXT_FLAG_IME_MULTI_LINE = 262144
    TYPE_TEXT_FLAG_NO_SUGGESTIONS = 524288
    TYPE_TEXT_VARIATION_NORMAL = 0
    TYPE_TEXT_VARIATION_URI = 16
    TYPE_TEXT_VARIATION_EMAIL_ADDRESS = 32
    TYPE_TEXT_VARIATION_PERSON_NAME = 96
    TYPE_TEXT_VARIATION_PASSWORD = 128
    TYPE_TEXT_VARIATION_VISIBLE_PASSWORD = 144
    TYPE_TEXT_VARIATION_WEB_EDIT_TEXT = 160
    TYPE_NUMBER_FLAG_SIGNED = 4096
    TYPE_NUMBER_FLAG_DECIMAL = 8192
    TYPE_NUMBER_VARIATION_PASSWORD = 16


class DialogInterface(JavaObject):
    _java_name = "android.content.DialogInterface"
    BUTTON_POSITIVE = -1
    BUTTON_NEGATIVE = -2
    BUTTON_NEUTRAL = -3

    class OnClickListener:
        def onClick(self, dialog, which):
            pass

    class OnDismissListener:
        def onDismiss(self, dialog):
            pass

    class OnCancelListener:
        def onCancel(self, dialog):
            pass

    class OnShowListener:
        def onShow(self, dialog):
            pass

    class OnKeyListener:
        def onKey(self, dialog, code, event):
            return False

    class OnMultiChoiceClickListener:
        def onClick(self, dialog, which, checked):
            pass


class PorterDuff(JavaObject):
    _java_name = "android.graphics.PorterDuff"

    class Mode:
        CLEAR = "CLEAR"
        SRC = "SRC"
        DST = "DST"
        SRC_OVER = "SRC_OVER"
        DST_OVER = "DST_OVER"
        SRC_IN = "SRC_IN"
        DST_IN = "DST_IN"
        SRC_OUT = "SRC_OUT"
        DST_OUT = "DST_OUT"
        SRC_ATOP = "SRC_ATOP"
        DST_ATOP = "DST_ATOP"
        XOR = "XOR"
        DARKEN = "DARKEN"
        LIGHTEN = "LIGHTEN"
        MULTIPLY = "MULTIPLY"
        SCREEN = "SCREEN"
        ADD = "ADD"
        OVERLAY = "OVERLAY"


class PorterDuffColorFilter(JavaObject):
    _java_name = "android.graphics.PorterDuffColorFilter"

    def __init__(self, color=0, mode=None):
        self.color = int(color or 0)
        self.mode = mode

    def getColor(self):
        return self.color

    def getMode(self):
        return self.mode


class PorterDuffXfermode(PorterDuffColorFilter):
    _java_name = "android.graphics.PorterDuffXfermode"

    def __init__(self, mode=None):
        PorterDuffColorFilter.__init__(self, 0, mode)


class _LenientConfigMeta(_LenientJavaMeta):
    def __getattr__(cls, item):
        if item.startswith("__"):
            raise AttributeError(item)
        if item.startswith(("get", "load")):
            return lambda *args, **kwargs: args[-1] if len(args) > 1 else 0
        if item.startswith(("is", "has", "should", "can")):
            return lambda *args, **kwargs: False
        if item.startswith(("put", "set", "toggle", "save", "remove")):
            return lambda *args, **kwargs: None
        return False


class ExteraConfig(JavaObject, metaclass=_LenientConfigMeta):
    _java_name = "com.exteragram.messenger.ExteraConfig"
    pluginsSafeMode = False
    pluginsEngine = True
    pluginsDevMode = False
    inAppVibration = True
    avatarCorners = 0

    @staticmethod
    def getStringValue(key=None, default=""):
        return default

    @staticmethod
    def getAvatarCorners(*args):
        return 0.0

    @staticmethod
    def getExtendedFramesPerSecond(*args):
        return 60


# Utilities

class Callback(JavaObject):
    _java_name = "org.telegram.messenger.Utilities$Callback"

    def __init__(self, function=None):
        self._function = function

    def run(self, *args):
        if self._function is not None:
            return self._function(*args)
        return None

    def __call__(self, *args):
        return self.run(*args)


class _DispatchQueue(JavaObject):
    _java_name = "org.telegram.messenger.DispatchQueue"

    def __init__(self, name):
        self.name = name

    def postRunnable(self, runnable, delay=0):
        import client_utils
        run = getattr(runnable, "run", None)
        if not callable(run):
            run = runnable if callable(runnable) else None
        if run is None:
            return False
        if delay:
            import threading
            threading.Timer(float(delay) / 1000.0, run).start()
        else:
            client_utils.run_on_queue(run)
        return True

    def cancelRunnable(self, *args):
        return None

    def isAlive(self):
        return True


def _patch_utilities():
    import exd_java
    utilities = exd_java.Utilities
    utilities.Callback = Callback
    for suffix in ("0", "2", "3", "4", "5", "Return", "2Return", "3Return"):
        setattr(utilities, "Callback" + suffix, Callback)
    for name in ("stageQueue", "globalQueue", "searchQueue", "pluginsQueue", "phoneBookQueue",
                 "themeQueue", "externalNetworkQueue", "videoPlayerQueue"):
        setattr(utilities, name, _DispatchQueue(name))
    utilities.dp = staticmethod(lambda value: __import__("android_views").dp(value))
    utilities.generateRandomString = staticmethod(
        lambda length=16: "".join(random.choice(string.ascii_letters + string.digits) for _ in range(int(length))))
    utilities.stackBlurBitmap = staticmethod(lambda *args: None)
    utilities.clamp = staticmethod(lambda value, top, bottom: max(bottom, min(top, value)))
    utilities.isNullOrEmpty = staticmethod(lambda value: not value)


# Sending

class SendMessageParams(JavaObject):
    _java_name = "org.telegram.messenger.SendMessagesHelper$SendMessageParams"

    def __init__(self):
        self.message = None
        self.peer = 0
        self.replyToMsg = None
        self.replyToTopMsg = None
        self.entities = None
        self.document = None
        self.photo = None
        self.path = None
        self.caption = None
        self.notify = True
        self.scheduleDate = 0

    @staticmethod
    def of(*args, **kwargs):
        params = SendMessageParams()
        values = list(args)
        if values and isinstance(values[0], str):
            params.message = values[0]
            params.peer = values[1] if len(values) > 1 else 0
            params.replyToMsg = values[2] if len(values) > 2 else None
            params.replyToTopMsg = values[3] if len(values) > 3 else None
            entities = values[6] if len(values) > 6 else None
            params.entities = entities
        elif values:
            first = values[0]
            if getattr(first, "sizes", None) is not None:
                params.photo = first
            else:
                params.document = first
            for value in values[1:]:
                if isinstance(value, str) and params.path is None and os.path.sep in value:
                    params.path = value
                elif isinstance(value, int) and not isinstance(value, bool) and not params.peer:
                    params.peer = value
        for key, value in kwargs.items():
            setattr(params, key, value)
        return params


def _peer_id(value):
    if hasattr(value, "getDialogId"):
        return int(value.getDialogId())
    try:
        return int(value or 0)
    except Exception:
        return 0


def _reply(value):
    if value is None:
        return None
    getter = getattr(value, "getId", None)
    return value if getter is None else value


class _SendHelper(JavaObject):
    _java_name = "org.telegram.messenger.SendMessagesHelper"

    def __init__(self, account=0):
        self.account = account

    def sendMessage(self, params, *args):
        import client_utils
        if isinstance(params, str):
            return client_utils.send_text(_peer_id(args[0] if args else 0), params, account=self.account)
        peer = _peer_id(getattr(params, "peer", 0))
        if not peer:
            return None
        if getattr(params, "message", None):
            return client_utils.send_text(peer, params.message, _reply(getattr(params, "replyToMsg", None)),
                                          account=self.account)
        path = getattr(params, "path", None)
        if path and os.path.exists(path):
            return client_utils.send_document(peer, path, getattr(params, "caption", None), account=self.account)
        return None

    def sendSticker(self, *args):
        return None

    def sendReaction(self, *args):
        return None


class SendMessagesHelper(JavaObject):
    _java_name = "org.telegram.messenger.SendMessagesHelper"
    SendMessageParams = SendMessageParams
    _instances = {}

    @staticmethod
    def getInstance(account=0):
        try:
            account = int(account or 0)
        except Exception:
            account = 0
        helper = SendMessagesHelper._instances.get(account)
        if helper is None:
            helper = SendMessagesHelper._instances[account] = _SendHelper(account)
        return helper

    @staticmethod
    def sendMessage(params, *args):
        return SendMessagesHelper.getInstance(0).sendMessage(params, *args)

    @staticmethod
    def prepareSendingDocument(*args):
        import client_utils
        path = next((a for a in args if isinstance(a, str) and os.path.exists(a)), None)
        strings = [a for a in args if isinstance(a, str) and a != path]
        caption = strings[1] if len(strings) > 1 else None
        dialog = next((a for a in args if isinstance(a, int) and not isinstance(a, bool) and abs(a) > 1000), 0)
        if path and dialog:
            return client_utils.send_document(dialog, path, caption)
        return None

    @staticmethod
    def prepareSendingPhoto(*args):
        import client_utils
        path = next((a for a in args if isinstance(a, str) and os.path.exists(a)), None)
        dialog = next((a for a in args if isinstance(a, int) and not isinstance(a, bool) and abs(a) > 1000), 0)
        if path and dialog:
            return client_utils.send_photo(dialog, path)
        return None

    @staticmethod
    def prepareSendingMedia(*args):
        return None


# Plugins

class Plugin(JavaObject):
    _java_name = "com.exteragram.messenger.plugins.Plugin"

    def __init__(self, record):
        self._record = record
        info = record.info()
        self.id = info.get("id")
        self.name = info.get("name")
        self.version = info.get("version")
        self.author = info.get("author")
        self.description = info.get("description")
        self.icon = info.get("icon")
        self.enabled = bool(info.get("enabled"))
        self.error = info.get("error")

    def getId(self):
        return self.id

    def getName(self):
        return self.name

    def getVersion(self):
        return self.version

    def getAuthor(self):
        return self.author

    def getDescription(self):
        return self.description

    def getIcon(self):
        return self.icon

    def isEnabled(self):
        return self.enabled

    def hasError(self):
        return bool(self.error)

    def getError(self):
        return self.error

    def setEnabled(self, value):
        PluginsController.getInstance().setPluginEnabled(self.id, bool(value))


class _Controller(JavaObject):
    _java_name = "com.exteragram.messenger.plugins.PluginsController"

    @property
    def plugins(self):
        import exd_java
        return exd_java.HashMap({key: Plugin(record) for key, record in _host()._records.items()})

    @property
    def pluginsDir(self):
        import exd_java
        return exd_java.File(_host().PLUGINS_DIR)

    def getPluginsDir(self):
        return self.pluginsDir

    def openPluginSettings(self, plugin_id, *args):
        try:
            _extera.open_plugin_settings(str(plugin_id))
        except Exception:
            pass

    def getPlugin(self, plugin_id):
        record = _host()._records.get(str(plugin_id))
        return Plugin(record) if record else None

    def getPlugins(self):
        return self.plugins

    def isPluginEnabled(self, plugin_id):
        return bool(_host().is_enabled(str(plugin_id)))

    def isPluginPinned(self, *args):
        return False

    def setPluginEnabled(self, plugin_id, enabled=True, *args):
        result = _host()._set_enabled({"id": str(plugin_id), "enabled": bool(enabled)})
        try:
            _extera.plugins_changed()
        except Exception:
            pass
        callback = args[-1] if args else None
        run = getattr(callback, "run", callback)
        if callable(run):
            run(bool(result))
        return result

    def loadPluginSettings(self, plugin_id, *args):
        return None

    def getPluginSetting(self, plugin_id, key, default=None):
        import plugin_settings
        try:
            return plugin_settings.get(str(plugin_id), key, default)
        except Exception:
            return default

    def getPluginEngine(self):
        return PythonPluginsEngine

    def getEngine(self):
        return PythonPluginsEngine


class PluginsController(JavaObject):
    _java_name = "com.exteragram.messenger.plugins.PluginsController"
    _instance = None

    @staticmethod
    def getInstance():
        if PluginsController._instance is None:
            PluginsController._instance = _Controller()
        return PluginsController._instance


class PythonPluginsEngine(JavaObject):
    _java_name = "com.exteragram.messenger.plugins.PythonPluginsEngine"

    @staticmethod
    def getInstance():
        return PythonPluginsEngine

    @staticmethod
    def getPlugin(plugin_id):
        return PluginsController.getInstance().getPlugin(plugin_id)

    @staticmethod
    def isInitialized():
        return True

    @staticmethod
    def unloadPlugin(*args):
        return None

    @staticmethod
    def loadPlugin(*args):
        return None


# Xposed

class _XposedAdapter:
    def __init__(self, callback):
        self._callback = callback
        for java, python in (("beforeHookedMethod", "before_hooked_method"),
                             ("afterHookedMethod", "after_hooked_method"),
                             ("replaceHookedMethod", "replace_hooked_method")):
            method = getattr(callback, python, None) or getattr(callback, java, None)
            if callable(method):
                setattr(self, python, method)


def _current_instance():
    host = _host()
    record = host._records.get(host.current_plugin_id() or "")
    return getattr(record, "instance", None)


class XposedBridge(JavaObject):
    _java_name = "de.robv.android.xposed.XposedBridge"

    @staticmethod
    def log(message):
        _log("[xposed] %s" % message)

    @staticmethod
    def hookMethod(method, callback):
        instance = _current_instance()
        if instance is None:
            return None
        return instance.hook_method(method, _XposedAdapter(callback))

    @staticmethod
    def hookAllMethods(clazz, name, callback):
        instance = _current_instance()
        if instance is None:
            return []
        return instance.hook_all_methods(clazz, name, _XposedAdapter(callback))

    @staticmethod
    def hookAllConstructors(clazz, callback):
        instance = _current_instance()
        if instance is None:
            return []
        return instance.hook_all_constructors(clazz, _XposedAdapter(callback))

    @staticmethod
    def invokeOriginalMethod(method, target, args):
        invoke = getattr(method, "invoke", None)
        return invoke(target, *list(args or [])) if callable(invoke) else None


class XC_MethodHook(JavaObject):
    _java_name = "de.robv.android.xposed.XC_MethodHook"

    def beforeHookedMethod(self, param):
        pass

    def afterHookedMethod(self, param):
        pass


class XC_MethodReplacement(XC_MethodHook):
    _java_name = "de.robv.android.xposed.XC_MethodReplacement"
    DO_NOTHING = None

    def replaceHookedMethod(self, param):
        return None


# Install

def install(shims):
    import android_views
    import base_plugin
    import client_utils
    XC_MethodHook.MethodHookParam = base_plugin.VirtualParam
    _patch_bitmap()
    _patch_utilities()
    context = _context_class()
    group = android_views.ViewGroup
    group.LayoutParams = android_views.LayoutParams
    group.MarginLayoutParams = android_views.LayoutParams
    entries = {
        "android.content": {"Context": context, "ClipData": ClipData, "ClipboardManager": ClipboardManager,
                            "DialogInterface": DialogInterface},
        "android.text": {"InputType": InputType},
        "android.view": {"ViewGroup": group},
        "android.graphics": {"BitmapFactory": BitmapFactory, "PorterDuff": PorterDuff,
                             "PorterDuffColorFilter": PorterDuffColorFilter,
                             "PorterDuffXfermode": PorterDuffXfermode},
        "java.io": {"FileOutputStream": FileOutputStream, "FileInputStream": FileInputStream,
                    "ByteArrayOutputStream": ByteArrayOutputStream, "ByteArrayInputStream": ByteArrayInputStream},
        "org.telegram.messenger": {"R": R, "NotificationCenter": NotificationCenter, "FileLoader": FileLoader,
                                   "SendMessagesHelper": SendMessagesHelper},
        "com.exteragram.messenger": {"ExteraConfig": ExteraConfig},
        "com.exteragram.messenger.plugins": {"PluginsController": PluginsController, "Plugin": Plugin,
                                             "PythonPluginsEngine": PythonPluginsEngine},
        "de.robv.android.xposed": {"XposedBridge": XposedBridge, "XC_MethodHook": XC_MethodHook,
                                   "XC_MethodReplacement": XC_MethodReplacement},
    }
    for module, values in entries.items():
        shims.setdefault(module, {}).update(values)
    java = shims.setdefault("java", {})
    java.setdefault("jbyte", int)
    java.setdefault("jshort", int)
    java.setdefault("jchar", str)
    client_utils.get_notification_center = lambda account=None: NotificationCenter.getInstance(account or 0)
    client_utils.get_send_messages_helper = lambda account=None: SendMessagesHelper.getInstance(account or 0)
    client_utils.get_file_loader = lambda account=None: FileLoader.getInstance(account or 0)
    try:
        import exd_java
        for values in entries.values():
            for value in values.values():
                name = getattr(value, "_java_name", None)
                if name and name not in exd_java._CLASSES:
                    exd_java._CLASSES[name] = value
    except Exception:
        pass
