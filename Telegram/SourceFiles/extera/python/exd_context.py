import zlib

import android_views
from exd_java import JavaObject, lookup


def _res_id(kind, name):
    return zlib.crc32(("%s/%s" % (kind, name)).encode()) & 0x7FFFFFFF


_RES_NAMES = {}


def _resolve_res(value):
    if isinstance(value, str):
        return value
    return _RES_NAMES.get(int(value or 0), (None, None))[1]


# Loader

class ClassNotFoundException(Exception):
    _java_name = "java.lang.ClassNotFoundException"


class ClassLoader(JavaObject):
    _java_name = "java.lang.ClassLoader"

    def loadClass(self, name, *args):
        found = lookup(str(name))
        if found is None:
            raise ClassNotFoundException(str(name))
        return found

    def findClass(self, name):
        return self.loadClass(name)

    def getParent(self):
        return None

    def getResource(self, name):
        return None

    @staticmethod
    def getSystemClassLoader():
        return _LOADER


_LOADER = ClassLoader()


# Resources

class DisplayMetrics(JavaObject):
    _java_name = "android.util.DisplayMetrics"

    @property
    def density(self):
        return android_views.density()

    @property
    def scaledDensity(self):
        return android_views.density()

    @property
    def densityDpi(self):
        return int(160 * android_views.density())

    @property
    def widthPixels(self):
        return int(android_views.displaySize.x)

    @property
    def heightPixels(self):
        return int(android_views.displaySize.y)

    @property
    def xdpi(self):
        return float(self.densityDpi)

    @property
    def ydpi(self):
        return float(self.densityDpi)


class Configuration(JavaObject):
    _java_name = "android.content.res.Configuration"
    ORIENTATION_PORTRAIT = 1
    ORIENTATION_LANDSCAPE = 2
    UI_MODE_NIGHT_MASK = 0x30
    UI_MODE_NIGHT_NO = 0x10
    UI_MODE_NIGHT_YES = 0x20

    @property
    def orientation(self):
        size = android_views.displaySize
        return self.ORIENTATION_LANDSCAPE if size.x > size.y else self.ORIENTATION_PORTRAIT

    @property
    def uiMode(self):
        try:
            color = android_views.theme_color("windowBg") or 0
            dark = (((color >> 16) & 0xFF) * 299 + ((color >> 8) & 0xFF) * 587 + (color & 0xFF) * 114) < 128000
        except Exception:
            dark = False
        return self.UI_MODE_NIGHT_YES if dark else self.UI_MODE_NIGHT_NO

    @property
    def locale(self):
        import exd_java
        return exd_java.Locale(exd_java.language())

    @property
    def screenWidthDp(self):
        return int(android_views.displaySize.x / max(android_views.density(), 0.1))

    @property
    def screenHeightDp(self):
        return int(android_views.displaySize.y / max(android_views.density(), 0.1))

    @property
    def fontScale(self):
        return 1.0

    def getLocales(self):
        import exd_java
        return exd_java.ArrayList([self.locale])


class Resources(JavaObject):
    _java_name = "android.content.res.Resources"

    def getIdentifier(self, name, kind=None, package=None):
        name = str(name or "")
        if ":" in name and "/" in name:
            package, rest = name.split(":", 1)
            kind, name = rest.split("/", 1)
        elif "/" in name:
            kind, name = name.split("/", 1)
        value = _res_id(kind or "id", name)
        _RES_NAMES[value] = (kind, name)
        return value

    def getResourceName(self, value):
        kind, name = _RES_NAMES.get(int(value or 0), (None, None))
        return "org.telegram.messenger:%s/%s" % (kind, name) if name else None

    def getResourceEntryName(self, value):
        return _RES_NAMES.get(int(value or 0), (None, None))[1]

    def getDisplayMetrics(self):
        return DisplayMetrics()

    def getConfiguration(self):
        return Configuration()

    def getString(self, value, *args):
        import exd_java
        text = _resolve_res(value) or ""
        return exd_java.LocaleController.getString(text) % args if args else exd_java.LocaleController.getString(text)

    def getText(self, value, *args):
        return self.getString(value)

    def getColor(self, value, *args):
        name = _resolve_res(value) or ""
        try:
            return android_views.theme_color(name) or 0
        except Exception:
            return 0

    def getDimension(self, value):
        return 0.0

    def getDimensionPixelSize(self, value):
        return 0

    def getDrawable(self, value, *args):
        try:
            import exd_java_more2
            return exd_java_more2.drawable_by_name(_resolve_res(value))
        except Exception:
            return android_views.ColorDrawable(0) if hasattr(android_views, "ColorDrawable") else None

    @staticmethod
    def getSystem():
        return Resources()


# Package manager

class PackageInfo(JavaObject):
    _java_name = "android.content.pm.PackageInfo"

    def __init__(self, package):
        import extera_host
        self.packageName = str(package)
        self.versionName = getattr(extera_host, "APP_VERSION", None) or "12.1.1"
        self.versionCode = 6000
        self.firstInstallTime = 0
        self.lastUpdateTime = 0

    def getLongVersionCode(self):
        return self.versionCode


class ApplicationInfo(JavaObject):
    _java_name = "android.content.pm.ApplicationInfo"

    def __init__(self, package):
        self.packageName = str(package)
        self.dataDir = ""
        self.targetSdkVersion = 34
        self.flags = 0


class PackageManager(JavaObject):
    _java_name = "android.content.pm.PackageManager"
    PERMISSION_GRANTED = 0
    PERMISSION_DENIED = -1
    GET_META_DATA = 128

    class NameNotFoundException(Exception):
        _java_name = "android.content.pm.PackageManager$NameNotFoundException"

    def getPackageInfo(self, package, flags=0):
        if str(package) not in ("com.exteragram.messenger", "org.telegram.messenger"):
            raise PackageManager.NameNotFoundException(str(package))
        return PackageInfo(package)

    def getApplicationInfo(self, package, flags=0):
        return ApplicationInfo(package)

    def checkPermission(self, permission, package):
        return self.PERMISSION_DENIED

    def hasSystemFeature(self, name, *args):
        return False

    def getInstalledPackages(self, flags=0):
        import exd_java
        return exd_java.ArrayList([PackageInfo("com.exteragram.messenger")])


# Window

class Point(JavaObject):
    _java_name = "android.graphics.Point"

    def __init__(self, x=0, y=0):
        if isinstance(x, Point):
            x, y = x.x, x.y
        self.x = int(x)
        self.y = int(y)

    def set(self, x, y):
        self.x, self.y = int(x), int(y)

    def equals(self, other, *args):
        if args:
            return self.x == int(other) and self.y == int(args[0])
        return isinstance(other, Point) and other.x == self.x and other.y == self.y

    def __eq__(self, other):
        return isinstance(other, Point) and other.x == self.x and other.y == self.y

    def __hash__(self):
        return hash((self.x, self.y))


class WindowManagerLayoutParams(JavaObject):
    _java_name = "android.view.WindowManager$LayoutParams"
    MATCH_PARENT = -1
    WRAP_CONTENT = -2
    FLAG_NOT_FOCUSABLE = 8
    FLAG_NOT_TOUCH_MODAL = 32
    FLAG_KEEP_SCREEN_ON = 128
    FLAG_SECURE = 8192
    FLAG_LAYOUT_NO_LIMITS = 512
    TYPE_APPLICATION = 2
    TYPE_APPLICATION_OVERLAY = 2038
    TYPE_APPLICATION_PANEL = 1000

    def __init__(self, width=-1, height=-1, *args):
        self.width = int(width)
        self.height = int(height)
        self.x = 0
        self.y = 0
        self.flags = 0
        self.gravity = 0
        self.type = 2
        self.format = -3
        self.dimAmount = 0.0
        self.softInputMode = 0


class Display(JavaObject):
    _java_name = "android.view.Display"

    def getSize(self, point):
        point.x = int(android_views.displaySize.x)
        point.y = int(android_views.displaySize.y)

    def getRealSize(self, point):
        self.getSize(point)

    def getMetrics(self, metrics):
        return None

    def getWidth(self):
        return int(android_views.displaySize.x)

    def getHeight(self):
        return int(android_views.displaySize.y)

    def getRotation(self):
        return 0

    def getRefreshRate(self):
        return 60.0


class WindowManager(JavaObject):
    _java_name = "android.view.WindowManager"
    LayoutParams = WindowManagerLayoutParams

    def __init__(self):
        self._views = {}

    def getDefaultDisplay(self):
        return Display()

    def addView(self, view, params=None):
        handle = android_views._show_sheet(view, "sheet")
        self._views[id(view)] = handle

    def updateViewLayout(self, view, params=None):
        android_views.ViewGroup.requestLayout(view) if hasattr(android_views.ViewGroup, "requestLayout") else None

    def removeView(self, view):
        handle = self._views.pop(id(view), None)
        dismiss = getattr(handle, "dismiss", None)
        if callable(dismiss):
            dismiss()

    removeViewImmediate = removeView


_WINDOW_MANAGER = WindowManager()


class Window(JavaObject):
    _java_name = "android.view.Window"
    FEATURE_NO_TITLE = 1

    def __init__(self):
        self._decor = android_views.FrameLayout(None)
        self._attributes = WindowManagerLayoutParams()

    def getDecorView(self):
        return self._decor

    def getAttributes(self):
        return self._attributes

    def setAttributes(self, params):
        self._attributes = params

    def addFlags(self, flags):
        self._attributes.flags |= int(flags)

    def clearFlags(self, flags):
        self._attributes.flags &= ~int(flags)

    def setFlags(self, flags, mask):
        self._attributes.flags = (self._attributes.flags & ~int(mask)) | (int(flags) & int(mask))

    def getWindowManager(self):
        return _WINDOW_MANAGER

    def setStatusBarColor(self, color):
        self._status = int(color)

    def getStatusBarColor(self):
        return getattr(self, "_status", 0)

    def setNavigationBarColor(self, color):
        self._navigation = int(color)

    def getNavigationBarColor(self):
        return getattr(self, "_navigation", 0)

    def setSoftInputMode(self, mode):
        self._attributes.softInputMode = int(mode)

    def setBackgroundDrawable(self, drawable):
        self._background = drawable

    def setDimAmount(self, amount):
        self._attributes.dimAmount = float(amount)

    def setLayout(self, width, height):
        self._attributes.width, self._attributes.height = int(width), int(height)

    def setGravity(self, gravity):
        self._attributes.gravity = int(gravity)

    def getContext(self):
        import _compat
        return _compat._LaunchActivity.instance


_WINDOW = Window()


# Install

def _patch_context(context_class):
    def getClassLoader(self, *args):
        return _LOADER

    def getResources(self, *args):
        return Resources()

    def getPackageManager(self, *args):
        return PackageManager()

    def getWindow(self, *args):
        return _WINDOW

    def getWindowManager(self, *args):
        return _WINDOW_MANAGER

    def getApplicationContext(self, *args):
        import _compat
        return _compat._ApplicationLoader.applicationContext

    def isFinishing(self, *args):
        return False

    def isDestroyed(self, *args):
        return False

    def getString(self, value, *args):
        return Resources().getString(value, *args)

    def getColor(self, value, *args):
        return Resources().getColor(value)

    def getMainLooper(self, *args):
        import exd_java
        return exd_java.Looper.getMainLooper()

    def checkSelfPermission(self, permission, *args):
        return PackageManager.PERMISSION_DENIED

    def runOnUiThread(self, runnable, *args):
        import android_utils
        android_utils.run_on_ui_thread(getattr(runnable, "run", runnable))

    for name, fn in list(locals().items()):
        if callable(fn) and name not in ("context_class",) and not name.startswith("_"):
            if name not in context_class.__dict__:
                setattr(context_class, name, fn)


def install(shims):
    import _compat
    import exd_java
    _patch_context(_compat._Context)
    android_r = type("R", (JavaObject,), {"_java_name": "android.R"})
    for kind in ("id", "attr", "drawable", "string", "color", "style", "layout", "dimen", "anim"):
        group = type(kind, (), {"__getattr__": (lambda k: lambda self, item: _res_id(
            "android:" + k, item) if not item.startswith("__") else (_ for _ in ()).throw(AttributeError(item)))(kind)})
        setattr(android_r, kind, group())
    entries = {
        "java.lang": {"ClassLoader": ClassLoader, "ClassNotFoundException": ClassNotFoundException},
        "android.util": {"DisplayMetrics": DisplayMetrics},
        "android.content.res": {"Resources": Resources, "Configuration": Configuration},
        "android.content.pm": {"PackageManager": PackageManager, "PackageInfo": PackageInfo,
                               "ApplicationInfo": ApplicationInfo},
        "android.graphics": {"Point": Point},
        "android.view": {"Window": Window, "WindowManager": WindowManager, "Display": Display},
        "android": {"R": android_r},
    }
    for module, values in entries.items():
        shims.setdefault(module, {}).update(values)
        for value in values.values():
            name = getattr(value, "_java_name", None)
            if name:
                exd_java._CLASSES[name] = value
    exd_java._CLASSES["android.view.WindowManager$LayoutParams"] = WindowManagerLayoutParams
    install_services(shims)


# Services

class NetworkInfo(JavaObject):
    _java_name = "android.net.NetworkInfo"

    def isConnected(self):
        return _online()

    def isConnectedOrConnecting(self):
        return _online()

    def isAvailable(self):
        return True

    def getType(self):
        return 1

    def getTypeName(self):
        return "WIFI"


def _online():
    import socket
    try:
        socket.getaddrinfo("telegram.org", 443)
        return True
    except Exception:
        return False


class ConnectivityManager(JavaObject):
    _java_name = "android.net.ConnectivityManager"
    TYPE_WIFI = 1
    TYPE_MOBILE = 0

    def getActiveNetworkInfo(self):
        return NetworkInfo()

    def getActiveNetwork(self):
        return NetworkInfo()

    def isActiveNetworkMetered(self):
        return False

    def getNetworkCapabilities(self, network):
        return _Capabilities()


class _Capabilities(JavaObject):
    _java_name = "android.net.NetworkCapabilities"
    TRANSPORT_WIFI = 1
    TRANSPORT_CELLULAR = 0
    NET_CAPABILITY_INTERNET = 12

    def hasTransport(self, transport):
        return int(transport) == 1

    def hasCapability(self, capability):
        return True


class AudioManager(JavaObject):
    _java_name = "android.media.AudioManager"
    STREAM_MUSIC = 3
    STREAM_RING = 2
    RINGER_MODE_NORMAL = 2
    RINGER_MODE_SILENT = 0
    RINGER_MODE_VIBRATE = 1

    def __init__(self):
        self._volume = {3: 10}

    def getStreamVolume(self, stream):
        return self._volume.get(int(stream), 10)

    def getStreamMaxVolume(self, stream):
        return 15

    def setStreamVolume(self, stream, value, flags=0):
        self._volume[int(stream)] = int(value)

    def getRingerMode(self):
        return self.RINGER_MODE_NORMAL

    def isMusicActive(self):
        return False

    def requestAudioFocus(self, *args):
        return 1

    def abandonAudioFocus(self, *args):
        return 1

    def isWiredHeadsetOn(self):
        return False

    def isBluetoothA2dpOn(self):
        return False


class NotificationManager(JavaObject):
    _java_name = "android.app.NotificationManager"
    IMPORTANCE_DEFAULT = 3
    IMPORTANCE_HIGH = 4
    IMPORTANCE_LOW = 2

    def notify(self, *args):
        notification = args[-1] if args else None
        title = getattr(notification, "_title", None) or getattr(notification, "title", None) or ""
        text = getattr(notification, "_text", None) or getattr(notification, "text", None) or ""
        message = ("%s: %s" % (title, text)) if title and text else (title or text)
        if message:
            try:
                import android_utils
                from ui.bulletin import BulletinHelper
                android_utils.run_on_ui_thread(lambda: BulletinHelper.show_info(str(message)))
            except Exception:
                pass

    def cancel(self, *args):
        return None

    def cancelAll(self):
        return None

    def createNotificationChannel(self, channel):
        return None

    def areNotificationsEnabled(self):
        return True


class _WakeLock(JavaObject):
    _java_name = "android.os.PowerManager$WakeLock"

    def __init__(self):
        self._held = False

    def acquire(self, *args):
        self._held = True

    def release(self, *args):
        self._held = False

    def isHeld(self):
        return self._held

    def setReferenceCounted(self, value):
        return None


class PowerManager(JavaObject):
    _java_name = "android.os.PowerManager"
    PARTIAL_WAKE_LOCK = 1
    SCREEN_BRIGHT_WAKE_LOCK = 10

    def isInteractive(self):
        return True

    def isScreenOn(self):
        return True

    def isPowerSaveMode(self):
        return False

    def newWakeLock(self, flags, tag):
        return _WakeLock()

    def isIgnoringBatteryOptimizations(self, package):
        return True


class InputMethodManager(JavaObject):
    _java_name = "android.view.inputmethod.InputMethodManager"
    SHOW_IMPLICIT = 1
    HIDE_NOT_ALWAYS = 2

    def hideSoftInputFromWindow(self, *args):
        return True

    def showSoftInput(self, view, *args):
        request = getattr(view, "requestFocus", None)
        if callable(request):
            request()
        return True

    def toggleSoftInput(self, *args):
        return None

    def isActive(self, *args):
        return False


class _MemoryInfo(JavaObject):
    _java_name = "android.app.ActivityManager$MemoryInfo"

    def __init__(self):
        self.availMem = 0
        self.totalMem = 0
        self.threshold = 0
        self.lowMemory = False


class ActivityManager(JavaObject):
    _java_name = "android.app.ActivityManager"
    MemoryInfo = _MemoryInfo

    def getMemoryInfo(self, info):
        try:
            import psutil
            memory = psutil.virtual_memory()
            info.availMem, info.totalMem = int(memory.available), int(memory.total)
        except Exception:
            info.availMem, info.totalMem = 4 << 30, 8 << 30
        info.threshold = 256 << 20
        info.lowMemory = info.availMem < info.threshold

    def getMemoryClass(self):
        return 512

    def isLowRamDevice(self):
        return False

    def getRunningAppProcesses(self):
        import exd_java
        return exd_java.ArrayList()


_SERVICES = {
    "window": lambda: _WINDOW_MANAGER,
    "connectivity": ConnectivityManager,
    "audio": AudioManager,
    "notification": NotificationManager,
    "power": PowerManager,
    "input_method": InputMethodManager,
    "activity": ActivityManager,
}


def service(name):
    factory = _SERVICES.get(str(name))
    return factory() if factory is not None else None


def install_services(shims):
    import exd_android
    import exd_java
    original = exd_android.get_system_service

    def get_system_service(name):
        found = service(name)
        return found if found is not None else original(name)

    exd_android.get_system_service = get_system_service
    entries = {
        "android.net": {"ConnectivityManager": ConnectivityManager, "NetworkInfo": NetworkInfo,
                        "NetworkCapabilities": _Capabilities},
        "android.media": {"AudioManager": AudioManager},
        "android.app": {"NotificationManager": NotificationManager, "ActivityManager": ActivityManager},
        "android.os": {"PowerManager": PowerManager},
        "android.view.inputmethod": {"InputMethodManager": InputMethodManager},
    }
    for module, values in entries.items():
        shims.setdefault(module, {}).update(values)
        for value in values.values():
            exd_java._CLASSES[value._java_name] = value
