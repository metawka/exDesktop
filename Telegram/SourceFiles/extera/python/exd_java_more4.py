import io
import struct
import zipfile

import android_views
import exd_java
import exd_java_more2
import exd_java_more3
from exd_java import JavaObject


def _to_bytes(value):
    return exd_java_more3._to_bytes(value)


# Animation

class ArgbEvaluator(JavaObject):
    _java_name = "android.animation.ArgbEvaluator"
    _instance = None

    @staticmethod
    def getInstance():
        if ArgbEvaluator._instance is None:
            ArgbEvaluator._instance = ArgbEvaluator()
        return ArgbEvaluator._instance

    def evaluate(self, fraction, start, end):
        return exd_java_more3.ColorUtils.blendARGB(int(start), int(end), float(fraction))


def _bezier(t, p1, p2):
    u = 1.0 - t
    return 3 * u * u * t * p1 + 3 * u * t * t * p2 + t ** 3


class CubicBezierInterpolator(JavaObject):
    _java_name = "org.telegram.ui.Components.CubicBezierInterpolator"

    def __init__(self, x1=0.25, y1=0.1, x2=0.25, y2=1.0):
        self._points = (float(x1), float(y1), float(x2), float(y2))

    def getInterpolation(self, value):
        x1, y1, x2, y2 = self._points
        x = min(1.0, max(0.0, float(value)))
        low, high = 0.0, 1.0
        t = x
        for _ in range(30):
            current = _bezier(t, x1, x2)
            if abs(current - x) < 1e-5:
                break
            if current < x:
                low = t
            else:
                high = t
            t = (low + high) / 2.0
        return _bezier(t, y1, y2)


for _name, _points in (("DEFAULT", (0.25, 0.1, 0.25, 1.0)), ("EASE_OUT", (0, 0, 0.58, 1)),
                       ("EASE_OUT_QUINT", (0.23, 1, 0.32, 1)), ("EASE_IN", (0.42, 0, 1, 1)),
                       ("EASE_BOTH", (0.42, 0, 0.58, 1)), ("EASE_OUT_BACK", (0.34, 1.56, 0.64, 1)),
                       ("EASE_IN_OUT_QUAD", (0.455, 0.03, 0.515, 0.955))):
    setattr(CubicBezierInterpolator, _name, CubicBezierInterpolator(*_points))


# Collections

class LongSparseArray(JavaObject):
    _java_name = "androidx.collection.LongSparseArray"

    def __init__(self, *args):
        self._data = {}

    def _keys(self):
        return sorted(self._data)

    def put(self, key, value):
        self._data[int(key)] = value

    def get(self, key, default=None):
        return self._data.get(int(key), default)

    def remove(self, key):
        self._data.pop(int(key), None)

    def delete(self, key):
        self._data.pop(int(key), None)

    def size(self):
        return len(self._data)

    def keyAt(self, index):
        return self._keys()[int(index)]

    def valueAt(self, index):
        return self._data[self._keys()[int(index)]]

    def indexOfKey(self, key):
        keys = self._keys()
        return keys.index(int(key)) if int(key) in self._data else -1

    def containsKey(self, key):
        return int(key) in self._data

    def clear(self):
        self._data.clear()

    def isEmpty(self):
        return not self._data


class AndroidLongSparseArray(LongSparseArray):
    _java_name = "android.util.LongSparseArray"


class SparseArray(LongSparseArray):
    _java_name = "android.util.SparseArray"


class Consumer(JavaObject):
    _java_name = "androidx.core.util.Consumer"

    def accept(self, *args):
        return None


class ExoConsumer(Consumer):
    _java_name = "com.google.android.exoplayer2.util.Consumer"


class JavaConsumer(Consumer):
    _java_name = "java.util.function.Consumer"


# Android

class InputMethodManager(JavaObject):
    _java_name = "android.view.inputmethod.InputMethodManager"
    SHOW_IMPLICIT = 1
    SHOW_FORCED = 2
    HIDE_IMPLICIT_ONLY = 1
    HIDE_NOT_ALWAYS = 2

    def showSoftInput(self, *args):
        return True

    def hideSoftInputFromWindow(self, *args):
        return True

    def toggleSoftInput(self, *args):
        return None

    def isActive(self, *args):
        return True


class PixelFormat(JavaObject):
    _java_name = "android.graphics.PixelFormat"
    UNKNOWN = 0
    TRANSLUCENT = -3
    TRANSPARENT = -2
    OPAQUE = -1
    RGBA_8888 = 1
    RGB_565 = 4


class _AudioAttributesBuilder(JavaObject):
    _java_name = "android.media.AudioAttributes$Builder"

    def __init__(self, *args):
        self._result = AudioAttributes()

    def __getattr__(self, item):
        if item.startswith("set"):
            return lambda *args, **kwargs: self
        return JavaObject.__getattr__(self, item)

    def build(self):
        return self._result


class AudioAttributes(JavaObject):
    _java_name = "android.media.AudioAttributes"
    Builder = _AudioAttributesBuilder
    CONTENT_TYPE_UNKNOWN = 0
    CONTENT_TYPE_SPEECH = 1
    CONTENT_TYPE_MUSIC = 2
    CONTENT_TYPE_MOVIE = 3
    CONTENT_TYPE_SONIFICATION = 4
    USAGE_UNKNOWN = 0
    USAGE_MEDIA = 1
    USAGE_VOICE_COMMUNICATION = 2
    USAGE_ALARM = 4
    USAGE_NOTIFICATION = 5
    USAGE_ASSISTANCE_SONIFICATION = 13
    USAGE_GAME = 14


class AndroidStyle(JavaObject):
    _java_name = "android.R$style"
    Theme_Black_NoTitleBar = 0x01030007
    Theme_Black_NoTitleBar_Fullscreen = 0x01030008
    Theme_DeviceDefault_Dialog_Alert = 0x010302d1
    Theme_Translucent_NoTitleBar = 0x0103000f
    Theme_Material_Dialog_Alert = 0x01030226
    Animation_Dialog = 0x01030002
    Animation_Toast = 0x01030004


class ColorMatrix(JavaObject):
    _java_name = "android.graphics.ColorMatrix"

    def __init__(self, values=None):
        self._values = [float(v) for v in values] if values else [
            1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0]

    def reset(self):
        ColorMatrix.__init__(self)

    def set(self, values):
        self._values = [float(v) for v in getattr(values, "_values", values)]

    def getArray(self):
        return list(self._values)

    def setSaturation(self, sat):
        sat = float(sat)
        inv = 1 - sat
        r, g, b = 0.213 * inv, 0.715 * inv, 0.072 * inv
        self._values = [r + sat, g, b, 0, 0, r, g + sat, b, 0, 0, r, g, b + sat, 0, 0, 0, 0, 0, 1, 0]

    def setScale(self, rs, gs, bs, a_s):
        self._values = [rs, 0, 0, 0, 0, 0, gs, 0, 0, 0, 0, 0, bs, 0, 0, 0, 0, 0, a_s, 0]

    def postConcat(self, other):
        a = self._values
        b = other._values
        out = [0.0] * 20
        for row in range(4):
            for col in range(5):
                value = sum(b[row * 5 + k] * a[k * 5 + col] for k in range(4))
                if col == 4:
                    value += b[row * 5 + 4]
                out[row * 5 + col] = value
        self._values = out

    def preConcat(self, other):
        copy = ColorMatrix(other._values)
        copy.postConcat(self)
        self._values = copy._values


class ColorMatrixColorFilter(JavaObject):
    _java_name = "android.graphics.ColorMatrixColorFilter"

    def __init__(self, matrix=None):
        self._values = getattr(matrix, "_values", matrix)


class BitmapShader(exd_java_more2.Shader):
    _java_name = "android.graphics.BitmapShader"


class StateListDrawable(JavaObject):
    _java_name = "android.graphics.drawable.StateListDrawable"

    def __init__(self, *args):
        self._states = []

    def addState(self, states, drawable):
        self._states.append((list(states or []), drawable))

    def _data(self):
        for states, drawable in self._states:
            if not states and hasattr(drawable, "_data"):
                return drawable._data()
        for _, drawable in self._states:
            if hasattr(drawable, "_data"):
                return drawable._data()
        return {"kind": "flat", "color": 0}


class TextPaint(android_views.Paint):
    _java_name = "android.text.TextPaint"


class BuildConfig(JavaObject):
    _java_name = "org.telegram.messenger.BuildConfig"
    APPLICATION_ID = "com.exteragram.messenger"
    BUILD_TYPE = "release"
    DEBUG = False
    VERSION_CODE = 0
    VERSION_NAME = "desktop"


class _ViewConfigurationInstance(JavaObject):
    _java_name = "android.view.ViewConfiguration"

    def getScaledTouchSlop(self):
        return android_views.dp(8)

    def getScaledDoubleTapSlop(self):
        return android_views.dp(100)

    def getScaledMinimumFlingVelocity(self):
        return android_views.dp(50)

    def getScaledMaximumFlingVelocity(self):
        return android_views.dp(8000)

    def getScaledPagingTouchSlop(self):
        return android_views.dp(16)


class ViewConfiguration(JavaObject):
    _java_name = "android.view.ViewConfiguration"
    _instance = _ViewConfigurationInstance()

    @staticmethod
    def get(*args):
        return ViewConfiguration._instance

    @staticmethod
    def getLongPressTimeout():
        return 500

    @staticmethod
    def getTapTimeout():
        return 100

    @staticmethod
    def getDoubleTapTimeout():
        return 300

    @staticmethod
    def getKeyRepeatTimeout():
        return 500


class PackageManager(JavaObject):
    _java_name = "android.content.pm.PackageManager"
    PERMISSION_GRANTED = 0
    PERMISSION_DENIED = -1
    GET_META_DATA = 128
    GET_ACTIVITIES = 1
    MATCH_DEFAULT_ONLY = 0x10000
    FEATURE_CAMERA = "android.hardware.camera"

    def checkPermission(self, *args):
        return 0

    def hasSystemFeature(self, *args):
        return False

    def getInstalledPackages(self, *args):
        return exd_java.ArrayList()

    def getInstalledApplications(self, *args):
        return exd_java.ArrayList()

    def queryIntentActivities(self, *args):
        return exd_java.ArrayList()

    def getLaunchIntentForPackage(self, *args):
        return None


class HttpsURLConnection(exd_java_more2._HttpConnection):
    _java_name = "javax.net.ssl.HttpsURLConnection"


class BufferedInputStream(JavaObject):
    _java_name = "java.io.BufferedInputStream"

    def __init__(self, source, *args):
        self._source = source

    def read(self, *args):
        return self._source.read(*args)

    def available(self):
        available = getattr(self._source, "available", None)
        return available() if callable(available) else 0

    def readAllBytes(self):
        reader = getattr(self._source, "readAllBytes", None)
        if callable(reader):
            return reader()
        data = self._source.read()
        return data if isinstance(data, (bytes, bytearray)) else b""

    def skip(self, count):
        skip = getattr(self._source, "skip", None)
        return skip(count) if callable(skip) else 0

    def close(self):
        close = getattr(self._source, "close", None)
        if callable(close):
            close()


class BufferedOutputStream(JavaObject):
    _java_name = "java.io.BufferedOutputStream"

    def __init__(self, target, *args):
        self._target = target

    def write(self, *args):
        return self._target.write(*args)

    def flush(self):
        flush = getattr(self._target, "flush", None)
        if callable(flush):
            flush()

    def close(self):
        self.flush()
        close = getattr(self._target, "close", None)
        if callable(close):
            close()


class ZipEntry(JavaObject):
    _java_name = "java.util.zip.ZipEntry"

    def __init__(self, name):
        self._name = str(getattr(name, "_name", name))
        self._size = -1
        self._time = 0

    def getName(self):
        return self._name

    def isDirectory(self):
        return self._name.endswith("/")

    def getSize(self):
        return self._size

    def setTime(self, value):
        self._time = int(value)

    def getTime(self):
        return self._time


class ZipOutputStream(JavaObject):
    _java_name = "java.util.zip.ZipOutputStream"
    DEFLATED = 8
    STORED = 0

    def __init__(self, target, *args):
        self._target = target
        self._buffer = io.BytesIO()
        self._zip = zipfile.ZipFile(self._buffer, "w", zipfile.ZIP_DEFLATED)
        self._entry = None
        self._data = b""

    def putNextEntry(self, entry):
        self.closeEntry()
        self._entry = entry
        self._data = b""

    def write(self, data, offset=0, length=None):
        if isinstance(data, int):
            self._data += bytes([data & 0xFF])
            return
        chunk = _to_bytes(data)
        end = len(chunk) if length is None else int(offset) + int(length)
        self._data += chunk[int(offset):end]

    def closeEntry(self):
        if self._entry is not None:
            self._zip.writestr(self._entry.getName(), self._data)
            self._entry = None
            self._data = b""

    def setLevel(self, *args):
        return None

    def setMethod(self, *args):
        return None

    def finish(self):
        self.closeEntry()

    def close(self):
        self.closeEntry()
        self._zip.close()
        self._target.write(self._buffer.getvalue())
        close = getattr(self._target, "close", None)
        if callable(close):
            close()


class ZipInputStream(JavaObject):
    _java_name = "java.util.zip.ZipInputStream"

    def __init__(self, source, *args):
        reader = getattr(source, "readAllBytes", None)
        data = reader() if callable(reader) else source.read()
        self._zip = zipfile.ZipFile(io.BytesIO(_to_bytes(data)))
        self._names = self._zip.namelist()
        self._index = -1
        self._current = io.BytesIO()

    def getNextEntry(self):
        self._index += 1
        if self._index >= len(self._names):
            return None
        name = self._names[self._index]
        self._current = io.BytesIO(self._zip.read(name))
        entry = ZipEntry(name)
        entry._size = len(self._current.getvalue())
        return entry

    def read(self, buffer=None, offset=0, length=None):
        if buffer is None:
            value = self._current.read(1)
            return value[0] if value else -1
        size = len(buffer) if length is None else int(length)
        chunk = self._current.read(size)
        if not chunk:
            return -1
        for i, value in enumerate(chunk):
            buffer[int(offset) + i] = value - 256 if value > 127 else value
        return len(chunk)

    def readAllBytes(self):
        return self._current.read()

    def closeEntry(self):
        return None

    def close(self):
        self._zip.close()


class ClassLoader(JavaObject):
    _java_name = "java.lang.ClassLoader"

    @staticmethod
    def getSystemClassLoader():
        return ClassLoader()

    def loadClass(self, name, *args):
        found = exd_java.lookup(str(name))
        if found is None:
            raise LookupError("ClassNotFoundException: " + str(name))
        return found


class SerializedData(JavaObject):
    _java_name = "org.telegram.tgnet.SerializedData"

    def __init__(self, data=None, *args):
        if isinstance(data, int) and not isinstance(data, bool):
            data = None
        self._buffer = io.BytesIO(_to_bytes(data) if data is not None else b"")
        self._out = data is None

    def writeInt32(self, value):
        self._buffer.write(struct.pack("<i", int(value)))

    def writeInt64(self, value):
        self._buffer.write(struct.pack("<q", int(value)))

    def writeBool(self, value):
        self.writeInt32(-1720552011 if value else -1132882121)

    def writeDouble(self, value):
        self._buffer.write(struct.pack("<d", float(value)))

    def writeByteArray(self, data):
        data = _to_bytes(data)
        if len(data) <= 253:
            header = bytes([len(data)])
        else:
            header = bytes([254]) + struct.pack("<I", len(data))[:3]
        padding = (4 - (len(header) + len(data)) % 4) % 4
        self._buffer.write(header + data + b"\x00" * padding)

    def writeString(self, value):
        self.writeByteArray(str(value or "").encode("utf-8"))

    def writeBytes(self, data):
        self._buffer.write(_to_bytes(data))

    def readInt32(self, *args):
        return struct.unpack("<i", self._buffer.read(4))[0]

    def readInt64(self, *args):
        return struct.unpack("<q", self._buffer.read(8))[0]

    def readBool(self, *args):
        return self.readInt32() == -1720552011

    def readDouble(self, *args):
        return struct.unpack("<d", self._buffer.read(8))[0]

    def readByteArray(self, *args):
        first = self._buffer.read(1)[0]
        if first <= 253:
            size = first
            header = 1
        else:
            size = struct.unpack("<I", self._buffer.read(3) + b"\x00")[0]
            header = 4
        data = self._buffer.read(size)
        self._buffer.read((4 - (header + size) % 4) % 4)
        return data

    def readString(self, *args):
        return self.readByteArray().decode("utf-8", errors="replace")

    def toByteArray(self):
        return self._buffer.getvalue()

    def length(self):
        return len(self._buffer.getvalue())

    def remaining(self):
        return len(self._buffer.getvalue()) - self._buffer.tell()

    def getPosition(self):
        return self._buffer.tell()

    def cleanup(self):
        return None


class ProfileStatus(JavaObject):
    _java_name = "com.exteragram.messenger.api.model.ProfileStatus"
    NONE = "NONE"
    DEVELOPER = "DEVELOPER"
    SUPPORTER = "SUPPORTER"
    MODERATOR = "MODERATOR"
    TRANSLATOR = "TRANSLATOR"


class ExifInterface(JavaObject):
    _java_name = "android.media.ExifInterface"
    _exd_stub = True
    ORIENTATION_NORMAL = 1
    ORIENTATION_ROTATE_90 = 6
    ORIENTATION_ROTATE_180 = 3
    ORIENTATION_ROTATE_270 = 8
    TAG_ORIENTATION = "Orientation"
    TAG_DATETIME = "DateTime"
    TAG_DATETIME_ORIGINAL = "DateTimeOriginal"
    TAG_DATETIME_DIGITIZED = "DateTimeDigitized"
    TAG_MAKE = "Make"
    TAG_MODEL = "Model"
    TAG_SOFTWARE = "Software"
    TAG_ARTIST = "Artist"
    TAG_COPYRIGHT = "Copyright"
    TAG_IMAGE_DESCRIPTION = "ImageDescription"
    TAG_USER_COMMENT = "UserComment"
    TAG_GPS_LATITUDE = "GPSLatitude"
    TAG_GPS_LATITUDE_REF = "GPSLatitudeRef"
    TAG_GPS_LONGITUDE = "GPSLongitude"
    TAG_GPS_LONGITUDE_REF = "GPSLongitudeRef"
    TAG_GPS_ALTITUDE = "GPSAltitude"
    TAG_IMAGE_WIDTH = "ImageWidth"
    TAG_IMAGE_LENGTH = "ImageLength"

    def __init__(self, path=None, *args):
        self._attributes = {}

    def getAttribute(self, tag):
        return self._attributes.get(str(tag))

    def getAttributeInt(self, tag, default=0):
        try:
            return int(self._attributes.get(str(tag), default))
        except Exception:
            return default

    def setAttribute(self, tag, value):
        self._attributes[str(tag)] = value

    def saveAttributes(self):
        return None

    def getLatLong(self, *args):
        return None


class _Element(JavaObject):
    _java_name = "android.renderscript.Element"
    _exd_stub = True
    U8_4 = staticmethod(lambda *args: _Element())


def _no_renderscript(*args, **kwargs):
    raise RuntimeError("RenderScript is not available on desktop")


class RenderScript(JavaObject):
    _java_name = "android.renderscript.RenderScript"
    _exd_stub = True
    create = staticmethod(_no_renderscript)


class Allocation(JavaObject):
    _java_name = "android.renderscript.Allocation"
    _exd_stub = True
    createTyped = staticmethod(_no_renderscript)
    createFromBitmap = staticmethod(_no_renderscript)


class ScriptIntrinsicBlur(JavaObject):
    _java_name = "android.renderscript.ScriptIntrinsicBlur"
    _exd_stub = True
    create = staticmethod(_no_renderscript)


class TableRow(android_views.LinearLayout):
    _java_name = "android.widget.TableRow"
    LayoutParams = android_views.LayoutParams


class TableLayout(android_views.LinearLayout):
    _java_name = "android.widget.TableLayout"
    LayoutParams = android_views.LayoutParams


# Stubs

_STUB_NAMES = (
    "org.telegram.ui.Components.EditTextCaption",
    "org.telegram.ui.Components.CheckBox2",
    "org.telegram.ui.Cells.ChatActionCell",
    "org.telegram.ui.Components.FilterTabsView",
    "com.exteragram.messenger.pillstack.ui.PillStackPreferencesActivity",
    "android.view.Surface",
    "org.telegram.ui.Stories.DarkThemeResourceProvider",
    "org.telegram.ui.Components.ChatAttachAlertPhotoLayoutPreview",
    "com.exteragram.messenger.utils.chats.ChatUtils",
    "com.exteragram.messenger.plugins.models.CustomSetting",
    "com.exteragram.messenger.plugins.models.CustomSetting$Factory",
    "org.telegram.ui.ContentPreviewViewer$1",
    "org.telegram.ui.Components.Reactions.ReactionsLayoutInBubble",
    "org.telegram.ui.Components.Reactions.ReactionsLayoutInBubble$VisibleReaction",
    "org.telegram.ui.Components.UniversalRecyclerView",
    "org.telegram.messenger.DownloadController",
    "org.telegram.ui.ArticleViewer$WindowView",
    "org.telegram.ui.Components.ChatAvatarContainer",
    "org.telegram.ui.Cells.HeaderCell",
    "android.app.ProgressDialog",
    "org.telegram.ui.Components.ColorPicker",
    "org.telegram.ui.Components.ColorPicker$ColorPickerDelegate",
)


def _stub(name):
    short = name.rsplit(".", 1)[-1].split("$")[-1]
    cls = type(short, (JavaObject,), {"_java_name": name, "_exd_stub": True})
    holder = {}

    def get_instance(*args):
        if "value" not in holder:
            holder["value"] = cls()
        return holder["value"]

    cls.getInstance = staticmethod(get_instance)
    return cls


_stubs = {name: _stub(name) for name in _STUB_NAMES}
_stubs["com.exteragram.messenger.plugins.models.CustomSetting"].Factory = _stubs[
    "com.exteragram.messenger.plugins.models.CustomSetting$Factory"]
_stubs["org.telegram.ui.Components.Reactions.ReactionsLayoutInBubble"].VisibleReaction = _stubs[
    "org.telegram.ui.Components.Reactions.ReactionsLayoutInBubble$VisibleReaction"]
_stubs["org.telegram.ui.Components.ColorPicker"].ColorPickerDelegate = _stubs[
    "org.telegram.ui.Components.ColorPicker$ColorPickerDelegate"]


exd_java._register(
    ArgbEvaluator, CubicBezierInterpolator, LongSparseArray, AndroidLongSparseArray, SparseArray,
    Consumer, ExoConsumer, JavaConsumer, InputMethodManager, PixelFormat, _AudioAttributesBuilder,
    AudioAttributes, AndroidStyle, ColorMatrix, ColorMatrixColorFilter, BitmapShader,
    StateListDrawable, TextPaint, BuildConfig, ViewConfiguration, PackageManager,
    HttpsURLConnection, BufferedInputStream, BufferedOutputStream, ZipEntry, ZipOutputStream,
    ZipInputStream, ClassLoader, SerializedData, ProfileStatus, ExifInterface, _Element,
    RenderScript, Allocation, ScriptIntrinsicBlur, TableRow, TableLayout, *_stubs.values())
