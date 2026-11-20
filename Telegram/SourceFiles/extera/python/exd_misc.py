import threading
import weakref

import android_views
from exd_java import HashMap, JavaObject


class WeakHashMap(HashMap):
    _java_name = "java.util.WeakHashMap"


class _ThreadHandlers:
    handler = None
    installed = False


def _excepthook(args):
    handler = _ThreadHandlers.handler
    method = getattr(handler, "uncaughtException", handler)
    if callable(method):
        try:
            method(args.thread, args.exc_value)
            return
        except Exception:
            pass
    _ThreadHandlers.previous(args)


def set_default_handler(handler):
    _ThreadHandlers.handler = handler
    if not _ThreadHandlers.installed:
        _ThreadHandlers.previous = threading.excepthook
        threading.excepthook = _excepthook
        _ThreadHandlers.installed = True


class UncaughtExceptionHandler(JavaObject):
    _java_name = "java.lang.Thread$UncaughtExceptionHandler"

    def uncaughtException(self, thread, error):
        pass


class LiteMode(JavaObject):
    _java_name = "org.telegram.messenger.LiteMode"
    FLAG_ANIMATED_STICKERS_KEYBOARD = 1
    FLAG_ANIMATED_STICKERS_CHAT = 2
    FLAG_ANIMATED_EMOJI_KEYBOARD = 4
    FLAG_ANIMATED_EMOJI_REACTIONS = 8
    FLAG_ANIMATED_EMOJI_CHAT = 16
    FLAG_AUTOPLAY_VIDEOS = 32
    FLAG_AUTOPLAY_GIFS = 64
    FLAG_CHAT_BACKGROUND = 128
    FLAG_CHAT_FORUM_TWOCOLUMN = 256
    FLAG_CALLS_ANIMATIONS = 512
    FLAG_CHAT_SPOILER = 1024
    FLAG_CHAT_BLUR = 2048
    FLAG_CHAT_SCALE = 4096
    FLAG_PARTICLES = 8192
    FLAG_LIQUID_GLASS = 16384
    PRESET_LOW = 0
    PRESET_MEDIUM = 0x1FFF
    PRESET_HIGH = 0x3FFF
    _flags = 0x3FFF

    @staticmethod
    def isEnabled(flag):
        return bool(LiteMode._flags & int(flag))

    @staticmethod
    def isEnabledSetting(flag):
        return LiteMode.isEnabled(flag)

    @staticmethod
    def getValue(*args):
        return LiteMode._flags

    @staticmethod
    def toggleFlag(flag, *args):
        LiteMode._flags ^= int(flag)

    @staticmethod
    def isPowerSaverApplied():
        return False


class ViewOutlineProviderImpl(JavaObject):
    _java_name = "org.telegram.messenger.utils.ViewOutlineProviderImpl"

    def __init__(self, radius=0.0, oval=False):
        self.radius = float(radius)
        self.oval = bool(oval)

    @staticmethod
    def boundsWithRoundRect(radius):
        return ViewOutlineProviderImpl(radius)

    @staticmethod
    def boundsWithRoundRectAndAlpha(radius, *args):
        return ViewOutlineProviderImpl(radius)

    @staticmethod
    def boundsWithOval():
        return ViewOutlineProviderImpl(0, True)

    def getOutline(self, view, outline):
        return None


class TextCheckCell2(android_views.Switch):
    _java_name = "org.telegram.ui.Cells.TextCheckCell2"

    def __init__(self, context=None, *args):
        android_views.Switch.__init__(self, context)
        self._props["collapsed"] = None
        self._value = ""

    def setTextAndCheck(self, text, checked, divider=False, *args):
        self.setText(text)
        self.setChecked(checked)

    def setTextAndValueAndCheck(self, text, value, checked, *args):
        self._value = str(value)
        self.setText("%s  %s" % (text, value) if value else text)
        self.setChecked(checked)

    def setCollapsed(self, collapsed):
        self._props["collapsed"] = None if collapsed is None else bool(collapsed)

    def isCollapsed(self):
        return bool(self._props.get("collapsed"))

    def setDivider(self, divider):
        return None

    def getCheckBox(self):
        return self


class WebChromeClient(JavaObject):
    _java_name = "android.webkit.WebChromeClient"

    def onProgressChanged(self, view, progress):
        pass

    def onReceivedTitle(self, view, title):
        pass

    def onConsoleMessage(self, *args):
        return False


class OnCompletionListener(JavaObject):
    _java_name = "android.media.MediaPlayer$OnCompletionListener"

    def onCompletion(self, player):
        pass


def install(shims):
    import exd_java
    import exd_android
    import _compat
    exd_java.Thread.setDefaultUncaughtExceptionHandler = staticmethod(set_default_handler)
    exd_java.Thread.getDefaultUncaughtExceptionHandler = staticmethod(lambda: _ThreadHandlers.handler)
    exd_java.Thread.UncaughtExceptionHandler = UncaughtExceptionHandler
    controller = exd_android.PluginsController
    hooks = {}

    def addXposedHook(self, plugin_id, unhook):
        hooks.setdefault(str(plugin_id), []).append(unhook)
        try:
            import extera_host
            record = extera_host._records.get(str(plugin_id))
            instance = getattr(record, "instance", None)
            if instance is not None:
                instance.register_cleanup(lambda u=unhook: getattr(u, "unhook", lambda: None)())
        except Exception:
            pass

    def removeXposedHook(self, plugin_id, unhook):
        bucket = hooks.get(str(plugin_id), [])
        if unhook in bucket:
            bucket.remove(unhook)
        method = getattr(unhook, "unhook", None)
        if callable(method):
            method()

    controller.addXposedHook = addXposedHook
    controller.removeXposedHook = removeXposedHook
    controller.isPluginActive = lambda self, plugin_id: exd_android.PythonPluginsEngine.isPluginActive(plugin_id)
    exd_android.Plugin.getEngine = lambda self: "python"
    exd_android.Plugin.getEngineType = lambda self: "python"
    folder = _compat._plugin_files_dir
    def get_dir(self, name, mode=0):
        import os
        root = folder()
        root = root.getAbsolutePath() if hasattr(root, "getAbsolutePath") else str(root)
        path = os.path.join(root, "app_" + str(name))
        os.makedirs(path, exist_ok=True)
        return exd_java.File(path)

    _compat._Context.getDir = get_dir
    _compat._Context.setRequestedOrientation = lambda self, value: setattr(_ThreadHandlers, "orientation", int(value))
    _compat._Context.getRequestedOrientation = lambda self: getattr(_ThreadHandlers, "orientation", -1)
    android_views.View.setOutlineProvider = lambda self, provider: self.__dict__.__setitem__("outlineProvider", provider)
    android_views.View.getOutlineProvider = lambda self: self.__dict__.get("outlineProvider")
    android_views.View.setClipToOutline = lambda self, value: self.__dict__.__setitem__("clipToOutline", bool(value))
    android_views.View.setWillNotDraw = lambda self, value: self.__dict__.__setitem__("willNotDraw", bool(value))
    entries = {
        "java.util": {"WeakHashMap": WeakHashMap},
        "org.telegram.messenger": {"LiteMode": LiteMode},
        "org.telegram.messenger.utils": {"ViewOutlineProviderImpl": ViewOutlineProviderImpl},
        "org.telegram.ui.Cells": {"TextCheckCell2": TextCheckCell2},
        "android.webkit": {"WebChromeClient": WebChromeClient},
    }
    java = shims.setdefault("java", {})
    entries["java.chaquopy"] = {key: java[key] for key in ("dynamic_proxy", "cast", "jarray", "jclass", "jint",
                                                          "jlong", "jboolean", "jfloat", "jdouble", "jvoid")
                                if key in java}
    for module, values in entries.items():
        shims.setdefault(module, {}).update(values)
        for value in values.values():
            name = getattr(value, "_java_name", None)
            if name:
                exd_java._CLASSES[name] = value
    exd_java._CLASSES["android.media.MediaPlayer$OnCompletionListener"] = OnCompletionListener
