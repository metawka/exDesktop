import json
import os
import re
import shutil
import sys
import threading

import _compat
import android_views
import exd_java
import tgnet_bridge
from exd_java import JavaObject


# Theme

_THEME_KEYS = {
    "key_dialogBackground": "boxBg",
    "key_dialogBackgroundGray": "windowBgOver",
    "key_dialogTextBlack": "boxTextFg",
    "key_dialogTextGray": "windowSubFg",
    "key_dialogTextGray2": "windowSubFg",
    "key_dialogTextGray3": "windowSubFg",
    "key_dialogTextGray4": "windowSubFg",
    "key_dialogTextHint": "windowSubFg",
    "key_dialogTextBlue": "windowActiveTextFg",
    "key_dialogTextBlue2": "windowActiveTextFg",
    "key_dialogTextLink": "windowActiveTextFg",
    "key_dialogTextRed": "attentionButtonFg",
    "key_dialogButton": "windowActiveTextFg",
    "key_dialogIcon": "menuIconFg",
    "key_dialogInputField": "inputBorderFg",
    "key_dialogInputFieldActivated": "activeLineFg",
    "key_dialogLineProgress": "activeLineFg",
    "key_listSelector": "windowBgOver",
    "key_divider": "shadowFg",
    "key_text_RedRegular": "attentionButtonFg",
    "key_text_RedBold": "attentionButtonFg",
    "key_featuredStickers_buttonText": "activeButtonFg",
    "key_featuredStickers_addButton": "activeButtonBg",
    "key_featuredStickers_addButtonPressed": "activeButtonBgOver",
    "key_windowBackgroundGray": "windowBgOver",
    "key_windowBackgroundWhite": "windowBg",
    "key_windowBackgroundWhiteBlackText": "windowFg",
    "key_windowBackgroundWhiteGrayText": "windowSubFg",
    "key_windowBackgroundWhiteGrayText2": "windowSubFg",
    "key_windowBackgroundWhiteGrayText3": "windowSubFg",
    "key_windowBackgroundWhiteGrayText4": "windowSubFg",
    "key_windowBackgroundWhiteHintText": "windowSubFg",
    "key_windowBackgroundWhiteGrayIcon": "menuIconFg",
    "key_windowBackgroundWhiteBlueHeader": "windowActiveTextFg",
    "key_windowBackgroundWhiteBlueText": "windowActiveTextFg",
    "key_windowBackgroundWhiteBlueText4": "windowActiveTextFg",
    "key_windowBackgroundWhiteLinkText": "windowActiveTextFg",
    "key_windowBackgroundWhiteValueText": "windowActiveTextFg",
    "key_windowBackgroundWhiteRedText": "attentionButtonFg",
    "key_windowBackgroundWhiteGreenText": "boxTextFgGood",
    "key_switchTrack": "windowSubFg",
    "key_switchTrackChecked": "activeButtonBg",
    "key_checkbox": "activeButtonBg",
    "key_checkboxCheck": "activeButtonFg",
    "key_radioBackgroundChecked": "activeButtonBg",
    "key_actionBarDefault": "windowBg",
    "key_actionBarDefaultIcon": "menuIconFg",
    "key_actionBarDefaultTitle": "windowFg",
    "key_actionBarDefaultSubmenuBackground": "menuBg",
    "key_actionBarDefaultSubmenuItem": "windowFg",
    "key_actionBarDefaultSubmenuItemIcon": "menuIconFg",
    "key_chats_actionBackground": "activeButtonBg",
    "key_chats_actionIcon": "activeButtonFg",
    "key_chats_menuItemText": "windowFg",
    "key_chats_name": "dialogsNameFg",
    "key_chats_message": "dialogsTextFg",
    "key_chat_inLoader": "msgFileInBg",
    "key_chat_outLoader": "msgFileOutBg",
    "key_chat_messagePanelBackground": "historyComposeAreaBg",
    "key_chat_messagePanelText": "historyComposeAreaFg",
    "key_chat_messagePanelHint": "historyComposeAreaFgService",
    "key_chat_inBubble": "msgInBg",
    "key_chat_outBubble": "msgOutBg",
    "key_chat_messageTextIn": "historyTextInFg",
    "key_chat_messageTextOut": "historyTextOutFg",
    "key_chat_serviceBackground": "msgServiceBg",
    "key_chat_serviceText": "msgServiceFg",
    "key_profile_title": "windowFg",
    "key_avatar_text": "activeButtonFg",
    "key_undo_background": "toastBg",
    "key_undo_infoColor": "toastFg",
    "key_undo_cancelColor": "windowActiveTextFg",
    "key_player_progress": "activeLineFg",
}


def _theme_meta_getattr(cls, item):
    if item.startswith("key_"):
        return _THEME_KEYS.get(item, "windowFg")
    if item.startswith("__"):
        raise AttributeError(item)
    return _compat.Stub("Theme." + item)


def _theme_getattr(self, item):
    return _theme_meta_getattr(type(self), item)


def _luminance(color):
    color = int(color)
    r = (color >> 16) & 0xFF
    g = (color >> 8) & 0xFF
    b = color & 0xFF
    return (0.299 * r + 0.587 * g + 0.114 * b) / 255.0


def _rounded(radius, color):
    drawable = android_views.GradientDrawable()
    drawable.setCornerRadius(radius)
    drawable.setColor(color)
    return drawable


class _ThemeExtras:
    @staticmethod
    def createSimpleSelectorRoundRectDrawable(radius, default_color=0, *args):
        return _rounded(radius, default_color)

    @staticmethod
    def createRoundRectDrawable(radius, *args):
        return _rounded(radius, args[-1] if args else 0)

    @staticmethod
    def createSelectorDrawable(*args):
        return _rounded(0, 0)

    @staticmethod
    def createSelectorWithBackgroundDrawable(background=0, *args):
        return _rounded(0, background)

    @staticmethod
    def createRadSelectorDrawable(*args):
        return _rounded(0, 0)

    @staticmethod
    def getSelectorDrawable(*args):
        return _rounded(0, 0)

    @staticmethod
    def isCurrentThemeDark(*args):
        return _luminance(android_views.theme_color("windowBg")) < 0.5

    @staticmethod
    def isCurrentThemeNight(*args):
        return _luminance(android_views.theme_color("windowBg")) < 0.5

    @staticmethod
    def multAlpha(color, multiplier):
        color = int(color) & 0xFFFFFFFF
        alpha = int(((color >> 24) & 0xFF) * float(multiplier))
        return (max(0, min(255, alpha)) << 24) | (color & 0xFFFFFF)

    @staticmethod
    def adaptHSV(color, *args):
        return int(color)

    @staticmethod
    def blendOver(a, b):
        return int(b)

    @staticmethod
    def getActiveTheme(*args):
        return _compat.Stub("Theme.ThemeInfo")

    @staticmethod
    def getCurrentTheme(*args):
        return _compat.Stub("Theme.ThemeInfo")


def _patch_theme():
    type(android_views._Theme).__getattr__ = _theme_meta_getattr
    android_views._Theme.__getattr__ = _theme_getattr
    for name, value in vars(_ThemeExtras).items():
        if not name.startswith("_"):
            setattr(android_views._Theme, name, value)
    for key, palette in _THEME_KEYS.items():
        if key not in vars(android_views._Theme):
            setattr(android_views._Theme, key, palette)


# Utilities

def _launch():
    return _compat._LaunchActivity.instance


class _AndroidUtilitiesExtras:
    TYPEFACE_ROBOTO_MEDIUM = "fonts/rmedium.ttf"
    TYPEFACE_ROBOTO_MONO = "fonts/rmono.ttf"
    TYPEFACE_MERRIWEATHER_BOLD = "fonts/mw_bold.ttf"
    statusBarHeight = 0
    navigationBarHeight = 0

    @staticmethod
    def bold():
        return android_views.Typeface.DEFAULT_BOLD

    @staticmethod
    def getTypeface(name=None):
        text = str(name or "")
        if "medium" in text or "bold" in text:
            return android_views.Typeface.DEFAULT_BOLD
        if "mono" in text:
            return android_views.Typeface.MONOSPACE
        return android_views.Typeface.DEFAULT

    @staticmethod
    def showKeyboard(view=None):
        request = getattr(view, "requestFocus", None)
        if callable(request):
            request()
        return True

    @staticmethod
    def hideKeyboard(view=None):
        return None

    @staticmethod
    def cancelRunOnUIThread(runnable):
        return None

    @staticmethod
    def findActivity(context=None):
        return _launch()

    @staticmethod
    def replaceTags(text):
        return re.sub(r"\*\*(.+?)\*\*", r"\1", str(text or ""))

    @staticmethod
    def shakeView(view, *args):
        return None

    @staticmethod
    def makeAccessibilityAnnouncement(text):
        return None

    @staticmethod
    def getPhotoSize(*args):
        return 1280

    @staticmethod
    def setAdjustResizeToNothing(*args):
        return None

    @staticmethod
    def requestAdjustResize(*args):
        return None

    @staticmethod
    def removeAdjustResize(*args):
        return None

    @staticmethod
    def getStatusBarHeight(*args):
        return 0

    @staticmethod
    def isTablet(*args):
        return False

    @staticmethod
    def isDarkColor(color):
        return _luminance(color) < 0.5

    @staticmethod
    def computePerceivedBrightness(color):
        return _luminance(color)

    @staticmethod
    def lerp(a, b, f):
        return a + (b - a) * float(f)

    @staticmethod
    def isRTL(*args):
        return False

    @staticmethod
    def readRes(*args):
        return None

    @staticmethod
    def openSharing(*args):
        return None

    @staticmethod
    def formatFileSize(size, *args):
        size = float(size or 0)
        if size < 1024:
            return "%d B" % size
        for unit in ("KB", "MB", "GB"):
            size /= 1024.0
            if size < 1024 or unit == "GB":
                return "%.1f %s" % (size, unit)

    @staticmethod
    def getHostAuthority(url, *args):
        from urllib.parse import urlparse
        try:
            return urlparse(str(url)).netloc.lower()
        except Exception:
            return None


class _DisplayMetrics(JavaObject):
    _java_name = "android.util.DisplayMetrics"

    @property
    def density(self):
        return float(android_views._metrics_data().get("dp", 2.0))

    @property
    def widthPixels(self):
        return int(android_views._metrics_data().get("width", 1080))

    @property
    def heightPixels(self):
        return int(android_views._metrics_data().get("height", 1920))

    @property
    def densityDpi(self):
        return int(self.density * 160)


def _patch_utilities():
    target = _compat._AndroidUtilities
    for name, value in vars(_AndroidUtilitiesExtras).items():
        if not name.startswith("_") and name not in vars(target):
            setattr(target, name, value)
    target.displayMetrics = _DisplayMetrics()
    target.decelerateInterpolator = android_views.DecelerateInterpolator()
    target.overshootInterpolator = android_views.OvershootInterpolator()
    target.accelerateInterpolator = android_views.DecelerateInterpolator()


# Intent

def _patch_intent():
    intent = _compat._Intent
    extras = {
        "FLAG_ACTIVITY_CLEAR_TASK": 0x8000,
        "FLAG_ACTIVITY_CLEAR_TOP": 0x4000000,
        "FLAG_ACTIVITY_SINGLE_TOP": 0x20000000,
        "FLAG_ACTIVITY_NO_HISTORY": 0x40000000,
        "FLAG_GRANT_WRITE_URI_PERMISSION": 2,
        "ACTION_BATTERY_CHANGED": "android.intent.action.BATTERY_CHANGED",
        "ACTION_MAIN": "android.intent.action.MAIN",
        "ACTION_DIAL": "android.intent.action.DIAL",
        "ACTION_SENDTO": "android.intent.action.SENDTO",
        "ACTION_CHOOSER": "android.intent.action.CHOOSER",
        "ACTION_SCREEN_ON": "android.intent.action.SCREEN_ON",
        "ACTION_SCREEN_OFF": "android.intent.action.SCREEN_OFF",
        "CATEGORY_LAUNCHER": "android.intent.category.LAUNCHER",
        "CATEGORY_DEFAULT": "android.intent.category.DEFAULT",
        "EXTRA_SUBJECT": "android.intent.extra.SUBJECT",
        "EXTRA_TITLE": "android.intent.extra.TITLE",
        "EXTRA_EMAIL": "android.intent.extra.EMAIL",
    }
    for key, value in extras.items():
        if key not in vars(intent):
            setattr(intent, key, value)
    if "createChooser" not in vars(intent):
        intent.createChooser = staticmethod(lambda target, *args: target)


class TruncateAt(JavaObject):
    _java_name = "android.text.TextUtils$TruncateAt"
    START = "START"
    MIDDLE = "MIDDLE"
    END = "END"
    MARQUEE = "MARQUEE"


# Preferences

_prefs_lock = threading.RLock()
_prefs = {}


class _Editor(JavaObject):
    _java_name = "android.content.SharedPreferences$Editor"

    def __init__(self, owner):
        self._owner = owner
        self._changes = {}
        self._removed = set()
        self._clear = False

    def _put(self, key, value):
        self._changes[str(key)] = value
        self._removed.discard(str(key))
        return self

    def putString(self, key, value):
        return self._put(key, None if value is None else str(value))

    def putInt(self, key, value):
        return self._put(key, int(value))

    def putLong(self, key, value):
        return self._put(key, int(value))

    def putFloat(self, key, value):
        return self._put(key, float(value))

    def putBoolean(self, key, value):
        return self._put(key, bool(value))

    def putStringSet(self, key, value):
        return self._put(key, sorted(str(v) for v in (value or [])))

    def remove(self, key):
        self._removed.add(str(key))
        self._changes.pop(str(key), None)
        return self

    def clear(self):
        self._clear = True
        return self

    def commit(self):
        self._owner._apply(self)
        return True

    def apply(self):
        self._owner._apply(self)


class SharedPreferences(JavaObject):
    _java_name = "android.content.SharedPreferences"
    Editor = _Editor

    def __init__(self, name="mainconfig"):
        self._name = re.sub(r"[^A-Za-z0-9_.-]", "_", str(name or "mainconfig"))
        self._data = None

    def _path(self):
        base = os.path.join(_compat._plugin_files_dir().getAbsolutePath(), "shared_prefs")
        os.makedirs(base, exist_ok=True)
        return os.path.join(base, self._name + ".json")

    def _load(self):
        if self._data is None:
            try:
                with open(self._path(), encoding="utf-8") as handle:
                    self._data = json.load(handle)
            except Exception:
                self._data = {}
        return self._data

    def _apply(self, editor):
        with _prefs_lock:
            data = self._load()
            if editor._clear:
                data.clear()
            for key in editor._removed:
                data.pop(key, None)
            data.update(editor._changes)
            try:
                with open(self._path(), "w", encoding="utf-8") as handle:
                    json.dump(data, handle, ensure_ascii=False)
            except Exception:
                pass

    def _get(self, key, default):
        with _prefs_lock:
            return self._load().get(str(key), default)

    def getString(self, key, default=None):
        return self._get(key, default)

    def getInt(self, key, default=0):
        return int(self._get(key, default))

    def getLong(self, key, default=0):
        return int(self._get(key, default))

    def getFloat(self, key, default=0.0):
        return float(self._get(key, default))

    def getBoolean(self, key, default=False):
        return bool(self._get(key, default))

    def getStringSet(self, key, default=None):
        value = self._get(key, None)
        return set(value) if value is not None else default

    def contains(self, key):
        with _prefs_lock:
            return str(key) in self._load()

    def getAll(self):
        with _prefs_lock:
            return dict(self._load())

    def edit(self):
        return _Editor(self)

    def registerOnSharedPreferenceChangeListener(self, *args):
        return None

    def unregisterOnSharedPreferenceChangeListener(self, *args):
        return None


def shared_preferences(name="mainconfig", *args):
    try:
        import extera_host
        owner = extera_host.current_plugin_id() or ""
    except Exception:
        owner = ""
    key = (owner, str(name))
    with _prefs_lock:
        if key not in _prefs:
            _prefs[key] = SharedPreferences(name)
        return _prefs[key]


def _patch_preferences():
    controller = tgnet_bridge.MessagesController
    controller.getGlobalMainSettings = staticmethod(lambda *args: shared_preferences("mainconfig"))
    controller.getNotificationsSettings = staticmethod(lambda *args: shared_preferences("Notifications"))
    controller.getGlobalNotificationsSettings = staticmethod(lambda *args: shared_preferences("Notifications"))
    controller.getMainSettings = staticmethod(lambda *args: shared_preferences("mainconfig"))
    controller.getEmojiSettings = staticmethod(lambda *args: shared_preferences("emoji"))
    controller.UPDATE_MASK_NAME = 1
    controller.UPDATE_MASK_AVATAR = 2
    controller.UPDATE_MASK_STATUS = 4
    controller.UPDATE_MASK_ALL = -1
    context = _compat._Context
    if "getSharedPreferences" not in vars(context):
        context.getSharedPreferences = lambda self, name="mainconfig", *args: shared_preferences(name)
    tgnet_bridge.UserConfig.MAX_ACCOUNT_COUNT = 1
    tgnet_bridge.UserConfig.FAKE_MAX_ACCOUNT_COUNT = 1
    tgnet_bridge.UserConfig.getActivatedAccountsCount = staticmethod(lambda *args: 1)


# Controllers

class _Singleton(tgnet_bridge._Instance):
    @classmethod
    def getInstance(cls, account=0):
        account = int(account or 0)
        store = cls.__dict__.get("_store")
        if store is None:
            store = {}
            cls._store = store
        if account not in store:
            store[account] = cls(account)
        return store[account]

    def getCurrentAccount(self):
        return self.currentAccount

    def getMessagesController(self):
        return tgnet_bridge.MessagesController.getInstance(self.currentAccount)

    def getConnectionsManager(self):
        return tgnet_bridge.ConnectionsManager.getInstance(self.currentAccount)

    def getUserConfig(self):
        return tgnet_bridge.UserConfig.getInstance(self.currentAccount)

    def getMediaDataController(self):
        return MediaDataController.getInstance(self.currentAccount)

    def getMessagesStorage(self):
        return MessagesStorage.getInstance(self.currentAccount)

    def getNotificationsController(self):
        return NotificationsController.getInstance(self.currentAccount)

    def getNotificationCenter(self):
        import exd_android
        return exd_android.NotificationCenter.getInstance(self.currentAccount)

    def getSendMessagesHelper(self):
        import exd_android
        return exd_android.SendMessagesHelper.getInstance(self.currentAccount)

    def getFileLoader(self):
        import exd_android
        return exd_android.FileLoader.getInstance(self.currentAccount)


class MediaDataController(_Singleton):
    _java_name = "org.telegram.messenger.MediaDataController"
    TYPE_IMAGE = 0
    TYPE_MASK = 1
    TYPE_FAVE = 2
    TYPE_FEATURED = 3
    TYPE_EMOJI = 4
    TYPE_EMOJIPACKS = 5
    TYPE_FEATURED_EMOJIPACKS = 6
    TYPE_PREMIUM_STICKERS = 7

    def getStickerSets(self, *args):
        return []

    def getRecentStickers(self, *args):
        return []

    def getRecentGifs(self, *args):
        return []

    def getStickerSet(self, *args):
        return None

    def getStickerSetById(self, *args):
        return None

    def getEmojiStatuses(self, *args):
        return []

    def getEntities(self, message, *args):
        return []

    def loadStickers(self, *args):
        return None

    def addRecentSticker(self, *args):
        return None

    def addRecentGif(self, *args):
        return None

    def getEmojiSuggestions(self, *args):
        return None


class MessagesStorage(_Singleton):
    _java_name = "org.telegram.messenger.MessagesStorage"

    def getStorageQueue(self):
        import exd_android
        return exd_android._DispatchQueue("storageQueue")

    def getDatabase(self):
        return _compat.Stub("SQLiteDatabase")

    def getDialogFolderId(self, *args):
        return 0

    def getUser(self, user_id):
        return tgnet_bridge.get_user(user_id)

    def getChat(self, chat_id):
        return tgnet_bridge.get_chat(chat_id)

    def putUsersAndChats(self, *args):
        return None


class AccountInstance(_Singleton):
    _java_name = "org.telegram.messenger.AccountInstance"


class NotificationsController(_Singleton):
    _java_name = "org.telegram.messenger.NotificationsController"
    SETTING_MUTE_FOREVER = 2
    SETTING_MUTE_UNMUTE = 4
    TYPE_PRIVATE = 1
    TYPE_GROUP = 0
    TYPE_CHANNEL = 2

    def isGlobalNotificationsEnabled(self, *args):
        return True

    def showNotifications(self, *args):
        return None

    def processNewMessages(self, *args):
        return None


class _CropState(JavaObject):
    _java_name = "org.telegram.messenger.MediaController$CropState"

    def __init__(self, *args):
        self.cropPx = 0.0
        self.cropPy = 0.0
        self.cropScale = 1.0
        self.cropRotate = 0.0
        self.cropPw = 1.0
        self.cropPh = 1.0
        self.transformRotation = 0
        self.mirrored = False


class _PhotoEntry(JavaObject):
    _java_name = "org.telegram.messenger.MediaController$PhotoEntry"

    def __init__(self, bucket_id=0, image_id=0, date_taken=0, path="", orientation=0, is_video=False, *args):
        self.bucketId = bucket_id
        self.imageId = image_id
        self.dateTaken = date_taken
        self.path = path
        self.orientation = orientation
        self.isVideo = is_video
        self.caption = None
        self.entities = None


def _downloads_dir():
    folder = os.path.join(os.path.expanduser("~"), "Downloads")
    os.makedirs(folder, exist_ok=True)
    return folder


def _path_of(value):
    getter = getattr(value, "getAbsolutePath", None)
    if callable(getter):
        return str(getter())
    return str(value or "")


class MediaController(_Singleton):
    _java_name = "org.telegram.messenger.MediaController"
    CropState = _CropState
    PhotoEntry = _PhotoEntry
    allMediaAlbumEntry = None
    allPhotosAlbumEntry = None
    allVideosAlbumEntry = None

    @staticmethod
    def saveFile(path, context=None, kind=0, name=None, mime=None, *args):
        source = _path_of(path)
        if not source or not os.path.isfile(source):
            return None
        target = os.path.join(_downloads_dir(), str(name) if name else os.path.basename(source))
        base, ext = os.path.splitext(target)
        index = 1
        while os.path.exists(target):
            target = "%s (%d)%s" % (base, index, ext)
            index += 1
        try:
            shutil.copyfile(source, target)
        except Exception:
            return None
        callback = args[-1] if args else None
        run = getattr(callback, "run", callback)
        if callable(run):
            try:
                run(target)
            except TypeError:
                run()
        return target

    REPEAT_MODE_NONE = 0
    REPEAT_MODE_LIST = 1
    REPEAT_MODE_ONE = 2

    @staticmethod
    def _state():
        try:
            _extera = sys.modules["_extera"]
            return json.loads(_extera.player_state() or "{}")
        except Exception:
            return {}

    @staticmethod
    def _action(name, value=0.0, dialog=0):
        try:
            _extera = sys.modules["_extera"]
            _extera.player_action(str(name), float(value), int(dialog))
            return True
        except Exception:
            return False

    @staticmethod
    def _same(state, message):
        data = state.get("message") or {}
        if message is None or not data:
            return message is None and bool(data)
        try:
            return int(message.getId()) == int(data.get("id") or 0) and                 int(message.getDialogId()) == int(data.get("dialog_id") or 0)
        except Exception:
            return False

    def getPlayingMessageObject(self, *args):
        data = self._state().get("message")
        if not data:
            return None
        message = exd_java.message_object(data, self.currentAccount)
        state = self._state()
        duration = float(state.get("duration_ms") or 0)
        if duration > 0:
            message.audioProgress = float(state.get("position_ms") or 0) / duration
            message.audioProgressSec = int(float(state.get("position_ms") or 0) / 1000)
        return message

    def isMessagePaused(self, *args):
        state = self._state()
        return not state.get("playing", False)

    def isPlayingMessage(self, message=None, *args):
        state = self._state()
        return bool(state.get("playing")) and self._same(state, message)

    def isCurrentPlayer(self, *args):
        return bool(self._state().get("message"))

    def pauseMessage(self, message=None, *args):
        state = self._state()
        if not state.get("playing") or (message is not None and not self._same(state, message)):
            return False
        return self._action("pause")

    def resumeAudio(self, message=None, *args):
        state = self._state()
        if message is not None and not self._same(state, message):
            return False
        return self._action("play")

    def playMessage(self, message=None, *args):
        if message is None:
            return False
        state = self._state()
        if self._same(state, message):
            return self._action("play")
        try:
            return self._action("play_msg", int(message.getId()), int(message.getDialogId()))
        except Exception:
            return False

    def playNextMessage(self, *args):
        return self._action("next")

    def playNextMessageWithoutOrder(self, *args):
        return self._action("next")

    def playPreviousMessage(self, *args):
        return self._action("prev")

    def cleanupPlayer(self, *args):
        return self._action("close")

    def seekToProgress(self, message=None, progress=0.0, *args):
        if isinstance(message, (int, float)) and not args:
            message, progress = None, message
        return self._action("seek", float(progress))

    def seekToProgressMs(self, message=None, ms=0, *args):
        duration = float(self._state().get("duration_ms") or 0)
        return self._action("seek", float(ms) / duration) if duration > 0 else False

    def getDuration(self, *args):
        return int(self._state().get("duration_ms") or 0)

    def getPlaylist(self, *args):
        message = self.getPlayingMessageObject()
        return exd_java.ArrayList([message] if message is not None else [])

    def getCurrentPlaylistNum(self, *args):
        return 0

    def setPlaylist(self, messages=None, current=None, *args):
        return self.playMessage(current)

    def isDownloadingCurrentMessage(self, *args):
        return False

    def getRepeatMode(self, *args):
        return 0

    def isShuffleEnabled(self, *args):
        return False

    def getPlaybackSpeed(self, *args):
        return 1.0

    def getWaveform(self, *args):
        return None

    def isRecordingAudio(self, *args):
        return False


# Locations

class ImageLocation(JavaObject):
    _java_name = "org.telegram.messenger.ImageLocation"
    TYPE_BIG = 0
    TYPE_SMALL = 1
    TYPE_STRIPPED = 2
    TYPE_VIDEO_SMALL = 3
    TYPE_VIDEO_BIG = 4

    def __init__(self, kind="", source=None, extra=None):
        self.kind = kind
        self.source = source
        self.extra = extra
        self.path = source if kind == "path" else None
        self.document = source if kind == "document" else None
        self.photoSize = extra if kind in ("photo", "document") else None
        self.imageType = 0

    @staticmethod
    def getForDocument(first, second=None, *args):
        if second is not None:
            return ImageLocation("document", second, first)
        return ImageLocation("document", first) if first is not None else None

    @staticmethod
    def getForPhoto(size, photo=None, *args):
        return ImageLocation("photo", photo, size) if size is not None else None

    @staticmethod
    def getForPath(path):
        return ImageLocation("path", str(path)) if path else None

    @staticmethod
    def getForUser(user, kind=0):
        return ImageLocation("user", user, kind) if user is not None else None

    @staticmethod
    def getForChat(chat, kind=0):
        return ImageLocation("chat", chat, kind) if chat is not None else None

    @staticmethod
    def getForUserOrChat(peer, kind=0, *args):
        return ImageLocation("peer", peer, kind) if peer is not None else None

    @staticmethod
    def getForObject(obj, parent=None):
        return ImageLocation("object", obj, parent) if obj is not None else None

    @staticmethod
    def getForSticker(size, sticker=None, *args):
        return ImageLocation("sticker", sticker, size) if size is not None else None

    def getKey(self, *args):
        return "%s_%s" % (self.kind, id(self.source))

    def getSize(self):
        return int(getattr(self.extra, "size", 0) or 0)


# Bulletins

class _Bulletin(JavaObject):
    _java_name = "org.telegram.ui.Components.Bulletin"
    DURATION_SHORT = 1500
    DURATION_LONG = 2750
    DURATION_PROLONG = 5000

    def __init__(self, kind="info", text="", subtitle=None, button=None, action=None):
        self._kind = kind
        self._text = str(text or "")
        self._subtitle = subtitle
        self._button = button
        self._action = action

    def show(self, *args):
        from ui.bulletin import BulletinHelper
        if self._subtitle:
            BulletinHelper.show_two_line(self._text, str(self._subtitle))
        elif self._button and self._action is not None:
            BulletinHelper.show_with_button(self._text, None, str(self._button), getattr(self._action, "run", self._action))
        elif self._kind == "error":
            BulletinHelper.show_error(self._text)
        elif self._kind == "success":
            BulletinHelper.show_success(self._text)
        else:
            BulletinHelper.show_info(self._text)
        return self

    def setDuration(self, *args):
        return self

    def hide(self, *args):
        return None

    def hideAfterBottomSheet(self, *args):
        return self

    def ignoreDetach(self, *args):
        return self


def _texts(args):
    return [a for a in args if isinstance(a, str)]


class BulletinFactory(JavaObject):
    _java_name = "org.telegram.ui.Components.BulletinFactory"

    def __init__(self, *args):
        self._args = args

    @staticmethod
    def of(*args):
        return BulletinFactory(*args)

    @staticmethod
    def canShowBulletin(*args):
        return True

    def createSimpleBulletin(self, *args):
        texts = _texts(args)
        actions = [a for a in args if not isinstance(a, (str, int, float)) and (callable(a) or hasattr(a, "run"))]
        if len(texts) >= 2 and actions:
            return _Bulletin("info", texts[0], button=texts[1], action=actions[-1])
        if len(texts) >= 2:
            return _Bulletin("info", texts[0], subtitle=texts[1])
        return _Bulletin("info", texts[0] if texts else "")

    def createErrorBulletin(self, *args):
        texts = _texts(args)
        return _Bulletin("error", texts[0] if texts else "")

    def createSuccessBulletin(self, *args):
        texts = _texts(args)
        return _Bulletin("success", texts[0] if texts else "")

    def createCopyBulletin(self, *args):
        texts = _texts(args)
        return _Bulletin("success", texts[0] if texts else "Copied")

    def createCopyLinkBulletin(self, *args):
        return _Bulletin("success", "Link copied")

    def createUndoBulletin(self, *args):
        texts = _texts(args)
        return _Bulletin("info", texts[0] if texts else "")

    def createEmojiBulletin(self, *args):
        texts = _texts(args)
        return _Bulletin("info", texts[0] if texts else "")

    def showForError(self, *args):
        return _Bulletin("error", "Error").show()


setattr(BulletinFactory, "global", staticmethod(lambda *args: BulletinFactory()))


# Constants

class HapticFeedbackConstants(JavaObject):
    _java_name = "android.view.HapticFeedbackConstants"
    LONG_PRESS = 0
    VIRTUAL_KEY = 1
    KEYBOARD_TAP = 3
    CLOCK_TICK = 4
    CONTEXT_CLICK = 6
    KEYBOARD_PRESS = 3
    KEYBOARD_RELEASE = 7
    CONFIRM = 16
    REJECT = 17
    FLAG_IGNORE_VIEW_SETTING = 1
    FLAG_IGNORE_GLOBAL_SETTING = 2


class Modifier(JavaObject):
    _java_name = "java.lang.reflect.Modifier"
    PUBLIC = 1
    PRIVATE = 2
    PROTECTED = 4
    STATIC = 8
    FINAL = 16
    SYNCHRONIZED = 32
    VOLATILE = 64
    TRANSIENT = 128
    NATIVE = 256
    INTERFACE = 512
    ABSTRACT = 1024

    @staticmethod
    def isStatic(mod):
        return bool(int(mod) & 8)

    @staticmethod
    def isAbstract(mod):
        return bool(int(mod) & 1024)

    @staticmethod
    def isPrivate(mod):
        return bool(int(mod) & 2)

    @staticmethod
    def isPublic(mod):
        return bool(int(mod) & 1)

    @staticmethod
    def isFinal(mod):
        return bool(int(mod) & 16)

    @staticmethod
    def isProtected(mod):
        return bool(int(mod) & 4)


class ReflectArray(JavaObject):
    _java_name = "java.lang.reflect.Array"

    @staticmethod
    def newInstance(kind, *dims):
        return [None] * (int(dims[0]) if dims else 0)

    @staticmethod
    def get(array, index):
        return array[int(index)]

    @staticmethod
    def set(array, index, value):
        array[int(index)] = value

    @staticmethod
    def getLength(array):
        return len(array)

    @staticmethod
    def getInt(array, index):
        return int(array[int(index)])

    @staticmethod
    def getBoolean(array, index):
        return bool(array[int(index)])


_JAVA_DATE = (
    ("yyyy", "{Y}"), ("yy", "{y}"), ("MMMM", "{B}"), ("MMM", "{b}"), ("MM", "{m}"),
    ("dd", "{d}"), ("EEEE", "{A}"), ("EEE", "{a}"), ("HH", "{H}"), ("hh", "{I}"),
    ("mm", "{M}"), ("ss", "{S}"), ("SSS", "{f}"), ("a", "{p}"), ("d", "{dn}"),
    ("M", "{mn}"), ("H", "{Hn}"), ("h", "{In}"), ("Z", "{z}"), ("z", "{Z}"),
)


def _java_template(pattern):
    out = []
    i = 0
    pattern = str(pattern)
    while i < len(pattern):
        if pattern[i] == "'":
            j = pattern.find("'", i + 1)
            j = len(pattern) if j < 0 else j
            literal = pattern[i + 1:j] or "'"
            out.append(literal.replace("{", "{{").replace("}", "}}"))
            i = j + 1
            continue
        for java, token in _JAVA_DATE:
            if pattern.startswith(java, i):
                out.append(token)
                i += len(java)
                break
        else:
            out.append(pattern[i].replace("{", "{{").replace("}", "}}"))
            i += 1
    return "".join(out)


def _fields(moment):
    values = {code: moment.strftime("%" + code) for code in ("Y", "y", "B", "b", "m", "d", "A", "a", "H", "I", "M", "S", "p", "z", "Z")}
    values["f"] = "%03d" % (moment.microsecond // 1000)
    values["dn"] = str(moment.day)
    values["mn"] = str(moment.month)
    values["Hn"] = str(moment.hour)
    values["In"] = str(int(moment.strftime("%I")))
    return values


class SimpleDateFormat(JavaObject):
    _java_name = "java.text.SimpleDateFormat"

    def __init__(self, pattern="yyyy-MM-dd HH:mm:ss", *args):
        self._pattern = str(pattern)
        self._template = _java_template(pattern)

    def format(self, value):
        import datetime
        if hasattr(value, "getTime"):
            value = value.getTime()
        moment = datetime.datetime.fromtimestamp(float(value) / 1000.0)
        return self._template.format(**_fields(moment))

    def parse(self, text):
        import datetime
        strp = self._template
        for token, code in (("{dn}", "%d"), ("{mn}", "%m"), ("{Hn}", "%H"), ("{In}", "%I")):
            strp = strp.replace(token, code)
        strp = re.sub(r"\{(\w)\}", r"%\1", strp).replace("{{", "{").replace("}}", "}")
        parsed = datetime.datetime.strptime(str(text), strp)
        return exd_java.Date(int(parsed.timestamp() * 1000))

    def toPattern(self):
        return self._pattern

    def setTimeZone(self, *args):
        return None

    def setLenient(self, *args):
        return None


class _HttpConnection(JavaObject):
    _java_name = "java.net.HttpURLConnection"
    HTTP_OK = 200
    HTTP_NOT_FOUND = 404

    def __init__(self, url):
        self._url = url
        self._method = "GET"
        self._headers = {}
        self._body = None
        self._response = None
        self._timeout = 30

    def setRequestMethod(self, method):
        self._method = str(method)

    def setRequestProperty(self, key, value):
        self._headers[str(key)] = str(value)

    def addRequestProperty(self, key, value):
        self._headers[str(key)] = str(value)

    def setConnectTimeout(self, value):
        self._timeout = max(1, int(value) / 1000.0)

    def setReadTimeout(self, value):
        self._timeout = max(1, int(value) / 1000.0)

    def setDoOutput(self, *args):
        return None

    def setDoInput(self, *args):
        return None

    def setUseCaches(self, *args):
        return None

    def setInstanceFollowRedirects(self, *args):
        return None

    def getOutputStream(self):
        connection = self

        class _Out:
            def write(self, data, *args):
                if isinstance(data, str):
                    data = data.encode("utf-8")
                connection._body = (connection._body or b"") + bytes(data)

            def flush(self):
                return None

            def close(self):
                return None

        return _Out()

    def _open(self):
        if self._response is None:
            import urllib.error
            import urllib.request
            request = urllib.request.Request(self._url, data=self._body, headers=self._headers, method=self._method)
            try:
                self._response = urllib.request.urlopen(request, timeout=self._timeout)
            except urllib.error.HTTPError as error:
                self._response = error
        return self._response

    def connect(self):
        self._open()

    def getResponseCode(self):
        response = self._open()
        return int(getattr(response, "status", None) or getattr(response, "code", 0))

    def getInputStream(self):
        return self._open()

    def getErrorStream(self):
        return self._open()

    def getHeaderField(self, name):
        return self._open().headers.get(str(name))

    def disconnect(self):
        if self._response is not None:
            try:
                self._response.close()
            except Exception:
                pass


class URL(JavaObject):
    _java_name = "java.net.URL"

    def __init__(self, spec, *args):
        from urllib.parse import urlparse
        self._spec = str(spec)
        self._parsed = urlparse(self._spec)

    def toString(self):
        return self._spec

    def __str__(self):
        return self._spec

    def getHost(self):
        return self._parsed.hostname or ""

    def getPath(self):
        return self._parsed.path

    def getQuery(self):
        return self._parsed.query or None

    def getProtocol(self):
        return self._parsed.scheme

    def getPort(self):
        return self._parsed.port or -1

    def getRef(self):
        return self._parsed.fragment or None

    def openStream(self):
        import urllib.request
        return urllib.request.urlopen(self._spec, timeout=30)

    def openConnection(self, *args):
        return _HttpConnection(self._spec)


# Graphics

class _TileMode(JavaObject):
    _java_name = "android.graphics.Shader$TileMode"
    CLAMP = "CLAMP"
    REPEAT = "REPEAT"
    MIRROR = "MIRROR"
    DECAL = "DECAL"


class Shader(JavaObject):
    _java_name = "android.graphics.Shader"
    TileMode = _TileMode

    def __init__(self, *args):
        self._args = args

    def setLocalMatrix(self, *args):
        return None


class LinearGradient(Shader):
    _java_name = "android.graphics.LinearGradient"


class RadialGradient(Shader):
    _java_name = "android.graphics.RadialGradient"


class Rect(JavaObject):
    _java_name = "android.graphics.Rect"

    def __init__(self, left=0, top=0, right=0, bottom=0):
        if isinstance(left, Rect):
            left, top, right, bottom = left.left, left.top, left.right, left.bottom
        self.left = int(left)
        self.top = int(top)
        self.right = int(right)
        self.bottom = int(bottom)

    def set(self, left, top=0, right=0, bottom=0):
        Rect.__init__(self, left, top, right, bottom)

    def width(self):
        return self.right - self.left

    def height(self):
        return self.bottom - self.top

    def centerX(self):
        return (self.left + self.right) // 2

    def centerY(self):
        return (self.top + self.bottom) // 2

    def contains(self, x, y):
        return self.left <= x < self.right and self.top <= y < self.bottom

    def isEmpty(self):
        return self.left >= self.right or self.top >= self.bottom

    def offset(self, dx, dy):
        self.left += int(dx)
        self.right += int(dx)
        self.top += int(dy)
        self.bottom += int(dy)

    def inset(self, dx, dy):
        self.left += int(dx)
        self.right -= int(dx)
        self.top += int(dy)
        self.bottom -= int(dy)

    def setEmpty(self):
        Rect.__init__(self)


_IDENTITY = [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0]


class Matrix(JavaObject):
    _java_name = "android.graphics.Matrix"

    def __init__(self, *args):
        self._values = list(_IDENTITY)

    def reset(self):
        self._values = list(_IDENTITY)

    def setTranslate(self, dx, dy):
        self.reset()
        self._values[2] = float(dx)
        self._values[5] = float(dy)

    def postTranslate(self, dx, dy):
        self._values[2] += float(dx)
        self._values[5] += float(dy)
        return True

    def setScale(self, sx, sy, *args):
        self.reset()
        self._values[0] = float(sx)
        self._values[4] = float(sy)

    def postScale(self, sx, sy, *args):
        for i in (0, 1, 2):
            self._values[i] *= float(sx)
        for i in (3, 4, 5):
            self._values[i] *= float(sy)
        return True

    def preScale(self, sx, sy, *args):
        return self.postScale(sx, sy)

    def setRotate(self, *args):
        return None

    def postRotate(self, *args):
        return True

    def getValues(self, out):
        out[:] = list(self._values)

    def setValues(self, values):
        self._values = [float(v) for v in values][:9]

    def isIdentity(self):
        return self._values == _IDENTITY

    def mapPoints(self, points, *args):
        for i in range(0, len(points) - 1, 2):
            x, y = points[i], points[i + 1]
            points[i] = self._values[0] * x + self._values[1] * y + self._values[2]
            points[i + 1] = self._values[3] * x + self._values[4] * y + self._values[5]


# System

class Activity(JavaObject):
    _java_name = "android.app.Activity"
    RESULT_OK = -1
    RESULT_CANCELED = 0
    RESULT_FIRST_USER = 1


class Window(JavaObject):
    _java_name = "android.view.Window"
    FEATURE_NO_TITLE = 1
    FEATURE_OPTIONS_PANEL = 0
    ID_ANDROID_CONTENT = 0x01020002


class _WindowLayoutParams(JavaObject):
    _java_name = "android.view.WindowManager$LayoutParams"
    MATCH_PARENT = -1
    WRAP_CONTENT = -2
    FLAG_DIM_BEHIND = 2
    FLAG_NOT_FOCUSABLE = 8
    FLAG_NOT_TOUCHABLE = 16
    FLAG_NOT_TOUCH_MODAL = 32
    FLAG_KEEP_SCREEN_ON = 128
    FLAG_LAYOUT_NO_LIMITS = 512
    FLAG_FULLSCREEN = 1024
    FLAG_SECURE = 8192
    FLAG_HARDWARE_ACCELERATED = 0x1000000
    FLAG_TRANSLUCENT_STATUS = 0x4000000
    FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS = -0x80000000
    TYPE_APPLICATION = 2
    TYPE_APPLICATION_PANEL = 1000
    TYPE_APPLICATION_OVERLAY = 2038
    SOFT_INPUT_STATE_HIDDEN = 2
    SOFT_INPUT_ADJUST_RESIZE = 16
    SOFT_INPUT_ADJUST_PAN = 32

    def __init__(self, *args):
        self.width = int(args[0]) if args else -1
        self.height = int(args[1]) if len(args) > 1 else -1
        self.flags = 0
        self.gravity = 0
        self.x = 0
        self.y = 0
        self.dimAmount = 0.0


class WindowManager(JavaObject):
    _java_name = "android.view.WindowManager"
    LayoutParams = _WindowLayoutParams

    def addView(self, *args):
        return None

    def removeView(self, *args):
        return None

    def removeViewImmediate(self, *args):
        return None

    def updateViewLayout(self, *args):
        return None


class KeyEvent(JavaObject):
    _java_name = "android.view.KeyEvent"
    ACTION_DOWN = 0
    ACTION_UP = 1
    ACTION_MULTIPLE = 2
    KEYCODE_BACK = 4
    KEYCODE_DPAD_UP = 19
    KEYCODE_DPAD_DOWN = 20
    KEYCODE_DPAD_LEFT = 21
    KEYCODE_DPAD_RIGHT = 22
    KEYCODE_VOLUME_UP = 24
    KEYCODE_VOLUME_DOWN = 25
    KEYCODE_TAB = 61
    KEYCODE_SPACE = 62
    KEYCODE_ENTER = 66
    KEYCODE_DEL = 67
    KEYCODE_ESCAPE = 111
    META_SHIFT_ON = 1
    META_CTRL_ON = 0x1000

    def __init__(self, action=0, code=0, *args):
        self._action = int(action)
        self._code = int(code)

    def getAction(self):
        return self._action

    def getKeyCode(self):
        return self._code


class ViewOutlineProvider(JavaObject):
    _java_name = "android.view.ViewOutlineProvider"
    BACKGROUND = "BACKGROUND"
    BOUNDS = "BOUNDS"
    PADDED_BOUNDS = "PADDED_BOUNDS"

    def getOutline(self, *args):
        return None


class AudioManager(JavaObject):
    _java_name = "android.media.AudioManager"
    STREAM_VOICE_CALL = 0
    STREAM_SYSTEM = 1
    STREAM_RING = 2
    STREAM_MUSIC = 3
    STREAM_ALARM = 4
    STREAM_NOTIFICATION = 5
    RINGER_MODE_SILENT = 0
    RINGER_MODE_VIBRATE = 1
    RINGER_MODE_NORMAL = 2
    AUDIOFOCUS_GAIN = 1
    AUDIOFOCUS_GAIN_TRANSIENT = 2
    AUDIOFOCUS_LOSS = -1
    AUDIOFOCUS_REQUEST_GRANTED = 1
    ADJUST_LOWER = -1
    ADJUST_SAME = 0
    ADJUST_RAISE = 1
    FLAG_SHOW_UI = 1

    def getRingerMode(self):
        return 2

    def getStreamVolume(self, *args):
        return 0

    def getStreamMaxVolume(self, *args):
        return 15

    def isMusicActive(self):
        return False

    def requestAudioFocus(self, *args):
        return 1

    def abandonAudioFocus(self, *args):
        return 1


class _NotificationBuilder(JavaObject):
    _java_name = "android.app.Notification$Builder"

    def __init__(self, *args):
        self._title = ""
        self._text = ""

    def setContentTitle(self, title):
        self._title = str(title or "")
        return self

    def setContentText(self, text):
        self._text = str(text or "")
        return self

    def __getattr__(self, item):
        if item.startswith(("set", "add")):
            return lambda *args, **kwargs: self
        return JavaObject.__getattr__(self, item)

    def build(self):
        result = Notification()
        result._title = self._title
        result._text = self._text
        return result


class Notification(JavaObject):
    _java_name = "android.app.Notification"
    Builder = _NotificationBuilder
    PRIORITY_LOW = -1
    PRIORITY_DEFAULT = 0
    PRIORITY_HIGH = 1
    PRIORITY_MAX = 2
    FLAG_ONGOING_EVENT = 2
    FLAG_AUTO_CANCEL = 16
    DEFAULT_ALL = -1
    DEFAULT_SOUND = 1
    DEFAULT_VIBRATE = 2
    VISIBILITY_PUBLIC = 1

    def __init__(self, *args):
        self._title = ""
        self._text = ""


class NotificationManager(JavaObject):
    _java_name = "android.app.NotificationManager"
    IMPORTANCE_NONE = 0
    IMPORTANCE_MIN = 1
    IMPORTANCE_LOW = 2
    IMPORTANCE_DEFAULT = 3
    IMPORTANCE_HIGH = 4
    IMPORTANCE_MAX = 5

    def notify(self, *args):
        notification = args[-1] if args else None
        title = str(getattr(notification, "_title", "") or "")
        text = str(getattr(notification, "_text", "") or "")
        message = (title + (": " if title and text else "") + text).strip()
        if message:
            from ui.bulletin import BulletinHelper
            BulletinHelper.show_info(message)

    def cancel(self, *args):
        return None

    def cancelAll(self):
        return None

    def createNotificationChannel(self, *args):
        return None

    def deleteNotificationChannel(self, *args):
        return None

    def areNotificationsEnabled(self):
        return True


class NotificationChannel(JavaObject):
    _java_name = "android.app.NotificationChannel"

    def __init__(self, channel_id="", name="", importance=3):
        self._id = channel_id
        self._name = name
        self._importance = importance

    def getId(self):
        return self._id


class PendingIntent(JavaObject):
    _java_name = "android.app.PendingIntent"
    FLAG_UPDATE_CURRENT = 0x8000000
    FLAG_IMMUTABLE = 0x4000000
    FLAG_MUTABLE = 0x2000000
    FLAG_ONE_SHOT = 0x40000000
    FLAG_CANCEL_CURRENT = 0x10000000

    def __init__(self, intent=None):
        self._intent = intent

    @staticmethod
    def getActivity(context, code, intent, flags=0, *args):
        return PendingIntent(intent)

    @staticmethod
    def getBroadcast(context, code, intent, flags=0, *args):
        return PendingIntent(intent)

    @staticmethod
    def getService(context, code, intent, flags=0, *args):
        return PendingIntent(intent)

    def send(self, *args):
        if self._intent is not None:
            _launch().startActivity(self._intent)

    def cancel(self):
        return None


class FileProvider(JavaObject):
    _java_name = "androidx.core.content.FileProvider"

    @staticmethod
    def getUriForFile(context, authority, file, *args):
        return _compat._Uri.fromFile(file)


class ContextCompat(JavaObject):
    _java_name = "androidx.core.content.ContextCompat"

    @staticmethod
    def getDrawable(context, res_id):
        return android_views.ColorDrawable(0)

    @staticmethod
    def getColor(context, res_id):
        return 0

    @staticmethod
    def checkSelfPermission(*args):
        return 0

    @staticmethod
    def startForegroundService(*args):
        return None


class LinkMovementMethod(JavaObject):
    _java_name = "android.text.method.LinkMovementMethod"
    _instance = None

    @staticmethod
    def getInstance():
        if LinkMovementMethod._instance is None:
            LinkMovementMethod._instance = LinkMovementMethod()
        return LinkMovementMethod._instance


class MediaScannerConnection(JavaObject):
    _java_name = "android.media.MediaScannerConnection"

    @staticmethod
    def scanFile(context, paths, mimes=None, callback=None):
        run = getattr(callback, "onScanCompleted", callback)
        if callable(run):
            for path in list(paths or []):
                try:
                    run(path, _compat._Uri.fromFile(path))
                except Exception:
                    pass


class _SettingsSecure(JavaObject):
    _java_name = "android.provider.Settings$Secure"
    ANDROID_ID = "android_id"

    @staticmethod
    def getString(resolver, key):
        if key == "android_id":
            import uuid
            return "%016x" % uuid.getnode()
        return None

    @staticmethod
    def getInt(resolver, key, default=0):
        return default


class _SettingsGlobal(_SettingsSecure):
    _java_name = "android.provider.Settings$Global"


class _SettingsSystem(_SettingsSecure):
    _java_name = "android.provider.Settings$System"


class Settings(JavaObject):
    _java_name = "android.provider.Settings"
    Secure = _SettingsSecure
    Global = _SettingsGlobal
    System = _SettingsSystem
    ACTION_SETTINGS = "android.settings.SETTINGS"
    ACTION_APPLICATION_DETAILS_SETTINGS = "android.settings.APPLICATION_DETAILS_SETTINGS"
    ACTION_MANAGE_OVERLAY_PERMISSION = "android.settings.action.MANAGE_OVERLAY_PERMISSION"
    ACTION_NOTIFICATION_POLICY_ACCESS_SETTINGS = "android.settings.NOTIFICATION_POLICY_ACCESS_SETTINGS"
    ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION = "android.settings.MANAGE_ALL_FILES_ACCESS_PERMISSION"

    @staticmethod
    def canDrawOverlays(*args):
        return True


class MediaStore(JavaObject):
    _java_name = "android.provider.MediaStore"
    ACTION_IMAGE_CAPTURE = "android.media.action.IMAGE_CAPTURE"
    ACTION_VIDEO_CAPTURE = "android.media.action.VIDEO_CAPTURE"
    EXTRA_OUTPUT = "output"


# Animators

class _AnimatorListener(JavaObject):
    _java_name = "android.animation.Animator$AnimatorListener"

    def onAnimationStart(self, *args):
        return None

    def onAnimationEnd(self, *args):
        return None

    def onAnimationCancel(self, *args):
        return None

    def onAnimationRepeat(self, *args):
        return None


class _AnimatorUpdateListener(JavaObject):
    _java_name = "android.animation.ValueAnimator$AnimatorUpdateListener"

    def onAnimationUpdate(self, *args):
        return None


def _call_listener(listener, name, animator):
    method = getattr(listener, name, None)
    if method is None and name == "onAnimationUpdate" and callable(listener):
        method = listener
    if callable(method):
        try:
            method(animator)
        except TypeError:
            method()


class Animator(JavaObject):
    _java_name = "android.animation.Animator"
    AnimatorListener = _AnimatorListener

    def __init__(self, values=None, target=None, prop=None):
        self._values = list(values or [0])
        self._target = target
        self._prop = prop
        self._update = []
        self._listeners = []
        self._duration = 300
        self._running = False
        self._animated = self._values[-1] if self._values else 0

    def setDuration(self, duration):
        self._duration = int(duration)
        return self

    def getDuration(self):
        return self._duration

    def setInterpolator(self, *args):
        return self

    def setStartDelay(self, *args):
        return self

    def setRepeatCount(self, *args):
        return self

    def setRepeatMode(self, *args):
        return self

    def addUpdateListener(self, listener):
        self._update.append(listener)

    def removeAllUpdateListeners(self):
        self._update = []

    def addListener(self, listener):
        self._listeners.append(listener)

    def removeListener(self, listener):
        if listener in self._listeners:
            self._listeners.remove(listener)

    def removeAllListeners(self):
        self._listeners = []

    def getAnimatedValue(self, *args):
        return self._animated

    def getAnimatedFraction(self):
        return 1.0

    def isRunning(self):
        return self._running

    def isStarted(self):
        return self._running

    def _apply_target(self):
        if self._target is None or not self._prop:
            return
        prop = str(self._prop)
        setter = getattr(self._target, "set" + prop[:1].upper() + prop[1:], None)
        if callable(setter):
            try:
                setter(self._animated)
            except Exception:
                pass

    def start(self):
        self._running = True
        for listener in list(self._listeners):
            _call_listener(listener, "onAnimationStart", self)
        self._animated = self._values[-1] if self._values else 0
        self._apply_target()
        for listener in list(self._update):
            _call_listener(listener, "onAnimationUpdate", self)
        self._running = False
        for listener in list(self._listeners):
            _call_listener(listener, "onAnimationEnd", self)

    def cancel(self):
        if self._running:
            self._running = False
            for listener in list(self._listeners):
                _call_listener(listener, "onAnimationCancel", self)

    def end(self):
        self._running = False

    def pause(self):
        return None

    def resume(self):
        return None


class AnimatorListenerAdapter(_AnimatorListener):
    _java_name = "android.animation.AnimatorListenerAdapter"


class ValueAnimator(Animator):
    _java_name = "android.animation.ValueAnimator"
    AnimatorUpdateListener = _AnimatorUpdateListener
    INFINITE = -1
    RESTART = 1
    REVERSE = 2

    @staticmethod
    def ofFloat(*values):
        return ValueAnimator(values)

    @staticmethod
    def ofInt(*values):
        return ValueAnimator(values)

    @staticmethod
    def ofArgb(*values):
        return ValueAnimator(values)

    def setFloatValues(self, *values):
        self._values = list(values)

    def setIntValues(self, *values):
        self._values = list(values)


class ObjectAnimator(ValueAnimator):
    _java_name = "android.animation.ObjectAnimator"

    @staticmethod
    def ofFloat(target, prop, *values):
        return ObjectAnimator(values, target, getattr(prop, "_name", prop))

    @staticmethod
    def ofInt(target, prop, *values):
        return ObjectAnimator(values, target, getattr(prop, "_name", prop))

    @staticmethod
    def ofArgb(target, prop, *values):
        return ObjectAnimator(values, target, getattr(prop, "_name", prop))


class AnimatorSet(Animator):
    _java_name = "android.animation.AnimatorSet"

    def __init__(self, *args):
        Animator.__init__(self)
        self._children = []

    def playTogether(self, *animators):
        if len(animators) == 1 and isinstance(animators[0], (list, tuple)):
            animators = animators[0]
        self._children.extend(animators)

    def playSequentially(self, *animators):
        self.playTogether(*animators)

    def start(self):
        self._running = True
        for listener in list(self._listeners):
            _call_listener(listener, "onAnimationStart", self)
        for child in list(self._children):
            try:
                child.start()
            except Exception:
                pass
        self._running = False
        for listener in list(self._listeners):
            _call_listener(listener, "onAnimationEnd", self)


# Widgets

class HorizontalScrollView(android_views.ScrollView):
    _java_name = "android.widget.HorizontalScrollView"


class NestedScrollView(android_views.ScrollView):
    _java_name = "androidx.core.widget.NestedScrollView"


class EditTextBoldCursor(android_views.EditText):
    _java_name = "org.telegram.ui.Components.EditTextBoldCursor"


class Button(android_views.Button):
    _java_name = "android.widget.Button"


class PluginSettingsActivity(JavaObject):
    _java_name = "com.exteragram.messenger.plugins.ui.PluginSettingsActivity"

    def __init__(self, plugin=None, *args):
        self.plugin = plugin

    def _exd_present(self):
        plugin_id = getattr(self.plugin, "id", None) or getattr(self.plugin, "getId", lambda: None)()
        if isinstance(plugin_id, str) and plugin_id:
            _extera = sys.modules["_extera"]
            _extera.open_plugin_settings(plugin_id)

    def onFragmentCreate(self):
        return True

    def onFragmentDestroy(self):
        return None


class _GridSpec(JavaObject):
    _java_name = "android.widget.GridLayout$Spec"

    def __init__(self, start=0, size=1, alignment=None, weight=0.0):
        self.start = int(start) if start is not None and start >= 0 else -1
        self.size = int(size or 1)
        self.alignment = alignment
        self.weight = float(weight or 0)


class _GridParams(android_views.LayoutParams):
    _java_name = "android.widget.GridLayout$LayoutParams"

    def __init__(self, row=None, column=None, *args):
        if isinstance(row, _GridSpec) or row is None:
            android_views.LayoutParams.__init__(self, -2, -2)
            self.rowSpec = row or _GridSpec(-1)
            self.columnSpec = column or _GridSpec(-1)
        else:
            android_views.LayoutParams.__init__(self, row, column, *args)
            self.rowSpec = _GridSpec(-1)
            self.columnSpec = _GridSpec(-1)


class GridLayout(android_views.LinearLayout):
    _java_name = "android.widget.GridLayout"
    UNDEFINED = -2147483648
    HORIZONTAL = 0
    VERTICAL = 1
    LayoutParams = _GridParams
    Spec = _GridSpec
    FILL = "fill"
    CENTER = "center"
    START = "start"
    END = "end"

    def __init__(self, context=None, *args):
        android_views.LinearLayout.__init__(self, context)
        self._columns = 0
        self._rows = 0

    @staticmethod
    def spec(start=-1, size=1, *rest):
        alignment = None
        weight = 0.0
        if not isinstance(size, int):
            alignment, size = size, 1
        for value in rest:
            if isinstance(value, float):
                weight = value
            else:
                alignment = value
        return _GridSpec(start, size, alignment, weight)

    def setColumnCount(self, count):
        self._columns = int(count)

    def getColumnCount(self):
        return self._columns

    def setRowCount(self, count):
        self._rows = int(count)

    def getRowCount(self):
        return self._rows

    def setUseDefaultMargins(self, value):
        return None

    def setAlignmentMode(self, value):
        return None

    def _exd_rows(self):
        cells = {}
        auto = 0
        columns = self._columns
        for entry in self._children:
            params = entry.get("params")
            row = getattr(getattr(params, "rowSpec", None), "start", -1)
            column = getattr(getattr(params, "columnSpec", None), "start", -1)
            if row is None or row < 0 or column is None or column < 0:
                width = columns or max(1, len(self._children))
                row, column = divmod(auto, width)
                auto += 1
            cells.setdefault(row, {})[column] = entry
        return [cells[r] for r in sorted(cells)]


class BackupImageView(android_views.ImageView):
    _java_name = "org.telegram.ui.Components.BackupImageView"


class ProgressBar(android_views.ProgressBar):
    _java_name = "android.widget.ProgressBar"


class LineProgressView(android_views.ProgressBar):
    _java_name = "org.telegram.ui.Components.LineProgressView"

    def setProgress(self, value, *args):
        android_views.ProgressBar.setProgress(self, int(float(value) * 100) if isinstance(value, float) else value)


# Plugins

class PluginsConstants(JavaObject):
    _java_name = "com.exteragram.messenger.plugins.PluginsConstants"
    PYTHON = "python"
    Xposed = "xposed"
    PLUGINS_EXT = ".plugin"


class AppEvent(JavaObject):
    _java_name = "com.exteragram.messenger.plugins.AppEvent"
    START = "START"
    STOP = "STOP"
    PAUSE = "PAUSE"
    RESUME = "RESUME"


class UItem(JavaObject):
    _java_name = "org.telegram.ui.Components.UItem"

    def __init__(self, kind="custom", *args):
        self.viewType = kind
        self.args = args
        self.text = next((a for a in args if isinstance(a, str)), None)
        self.view = next((a for a in args if isinstance(a, android_views.View)), None)
        self.checked = False
        self.enabled = True
        self.id = int(args[0]) if args and isinstance(args[0], int) and not isinstance(args[0], bool) else 0

    def setChecked(self, value):
        self.checked = bool(value)
        return self

    def setEnabled(self, value):
        self.enabled = bool(value)
        return self

    def pad(self, *args):
        self.padded = True
        return self

    def __getattr__(self, item):
        if item.startswith("set") and len(item) > 3:
            key = item[3].lower() + item[4:]

            def setter(*args, **kwargs):
                self.__dict__[key] = args[0] if len(args) == 1 else args
                return self
            return setter
        if item.startswith(("get", "is")) and len(item) > 2:
            key = item[3:] if item.startswith("get") else item[2:]
            key = key[:1].lower() + key[1:]
            if key in self.__dict__:
                return lambda *args: self.__dict__[key]
        return JavaObject.__getattr__(self, item)


class _UItemFactory(JavaObject):
    _java_name = "org.telegram.ui.Components.UItem$UItemFactory"
    _registry = {}

    def __init__(self, *args):
        self._args = args

    @staticmethod
    def setup(factory):
        key = getattr(factory, "_java_name", None) or type(factory).__name__
        _UItemFactory._registry[key] = factory
        try:
            _UItemFactory._registry[type(factory)] = factory
        except TypeError:
            pass
        return factory

    @staticmethod
    def get(key):
        return _UItemFactory._registry.get(getattr(key, "_class", key)) or _UItemFactory._registry.get(
            getattr(key, "_name", None))


def _uitem_factory(kind):
    return staticmethod(lambda *args: UItem(kind, *args))


for _kind in ("Custom", "FullyCustom", "Shadow", "Button", "ButtonCheck", "Check", "Header",
              "SlideView", "ExteraExpandableSwitch", "RoundCheckbox", "Space", "Radio",
              "RadioButton", "Flicker", "Plugin", "TopView", "LargeShadow", "Switch",
              "IntSlideView", "Filter", "Order", "Settings", "SettingsCell"):
    setattr(UItem, "as" + _kind, _uitem_factory(_kind.lower()))
UItem.UItemFactory = _UItemFactory
UItem.ofFactory = staticmethod(lambda factory, *args: UItem("factory", _UItemFactory.get(factory) or factory, *args))


def _run_action(action):
    run = getattr(action, "run", action)
    if callable(run):
        run()


class ItemOptions(JavaObject):
    _java_name = "org.telegram.ui.Components.ItemOptions"

    def __init__(self, *args):
        self._items = []

    @staticmethod
    def makeOptions(*args):
        return ItemOptions(*args)

    def add(self, *args):
        text = next((a for a in args if isinstance(a, str)), "")
        action = next((a for a in reversed(args) if not isinstance(a, (str, int, float)) and (callable(a) or hasattr(a, "run"))), None)
        self._items.append((text, action))
        return self

    def addIf(self, condition, *args):
        if condition:
            self.add(*args)
        return self

    def show(self):
        items = [(t, a) for t, a in self._items if t]
        if items:
            from ui.alert import AlertDialogBuilder
            builder = AlertDialogBuilder(None)
            builder.set_items([t for t, _ in items], lambda dialog, index: _run_action(items[index][1]))
            builder.show()
        return self

    def __getattr__(self, item):
        if item.startswith(("set", "add", "translate", "force", "needs", "allow")):
            return lambda *args, **kwargs: self
        return JavaObject.__getattr__(self, item)


class PhotoViewer(JavaObject):
    _java_name = "org.telegram.ui.PhotoViewer"
    _exd_stub = True
    _instance = None

    @staticmethod
    def getInstance():
        if PhotoViewer._instance is None:
            PhotoViewer._instance = PhotoViewer()
        return PhotoViewer._instance

    @staticmethod
    def hasInstance():
        return False

    def isVisible(self):
        return False


class ActionBar(JavaObject):
    _java_name = "org.telegram.ui.ActionBar.ActionBar"

    @staticmethod
    def getCurrentActionBarHeight(*args):
        return android_views.dp(56)


class CodeHighlighting(JavaObject):
    _java_name = "org.telegram.messenger.CodeHighlighting"

    @staticmethod
    def highlight(text, *args):
        return text


# Stubs

_STUB_NAMES = (
    "com.exteragram.messenger.plugins.ui.PluginsActivity",
    "com.exteragram.messenger.plugins.ui.components.PluginCell",
    "com.exteragram.messenger.plugins.ui.components.templates.UniversalFragment",
    "com.exteragram.messenger.plugins.models.HeaderSetting",
    "org.telegram.ui.Cells.ChatMessageCell",
    "org.telegram.ui.Components.Premium.StarParticlesView",
    "org.telegram.ui.ActionBar.BaseFragment",
    "org.telegram.ui.ActionBar.ActionBarPopupWindow",
    "org.telegram.ui.Components.UniversalAdapter",
    "org.telegram.ui.Components.AvatarDrawable",
    "org.telegram.ui.Components.ShareAlert",
    "org.telegram.ui.Components.StickerImageView",
    "org.telegram.ui.Stories.recorder.ButtonWithCounterView",
    "org.telegram.ui.Gifts.GiftSheet",
    "org.telegram.ui.MessageSendPreview",
    "org.telegram.ui.SettingsActivity",
    "org.telegram.ui.Components.AlertsCreator",
    "dalvik.system.InMemoryDexClassLoader",
    "android.app.Dialog",
    "android.app.ActivityManager",
    "android.graphics.drawable.RippleDrawable",
    "com.exteragram.messenger.badges.BadgesController",
    "com.exteragram.messenger.api.dto.BadgeDTO",
    "com.exteragram.messenger.pillstack.core.PillRegistry",
    "com.exteragram.messenger.pillstack.core.PillStackConfig",
    "com.exteragram.messenger.utils.AppUtils",
    "com.exteragram.messenger.utils.ChatUtils",
)


def _stub(name):
    short = name.rsplit(".", 1)[-1]
    cls = type(short, (JavaObject,), {"_java_name": name, "_exd_stub": True})
    holder = {}

    def get_instance(*args):
        if "value" not in holder:
            holder["value"] = cls()
        return holder["value"]

    cls.getInstance = staticmethod(get_instance)
    return cls


_stubs = {name: _stub(name) for name in _STUB_NAMES}
_badges = _stubs["com.exteragram.messenger.badges.BadgesController"]
_badges.INSTANCE = _badges()


# Install

for _patch in (_patch_theme, _patch_utilities, _patch_intent, _patch_preferences):
    try:
        _patch()
    except Exception as _error:
        print("exd_java_more2:", _patch.__name__, _error, file=sys.stderr)

exd_java.Thread.MIN_PRIORITY = 1
exd_java.Thread.NORM_PRIORITY = 5
exd_java.Thread.MAX_PRIORITY = 10

exd_java._register(
    TruncateAt, SharedPreferences, _Editor, _DisplayMetrics, MediaDataController,
    MessagesStorage, AccountInstance, NotificationsController, MediaController,
    _CropState, _PhotoEntry, ImageLocation, _Bulletin, BulletinFactory,
    HapticFeedbackConstants, Modifier, ReflectArray, SimpleDateFormat, URL,
    _HttpConnection, _TileMode, Shader, LinearGradient, RadialGradient, Rect,
    Matrix, Activity, Window, _WindowLayoutParams, WindowManager, KeyEvent,
    ViewOutlineProvider, AudioManager, NotificationManager, NotificationChannel,
    _NotificationBuilder, Notification, PendingIntent, FileProvider, ContextCompat,
    LinkMovementMethod, MediaScannerConnection, Settings, _SettingsSecure,
    _SettingsGlobal, _SettingsSystem, MediaStore, Animator, _AnimatorListener,
    _AnimatorUpdateListener, AnimatorListenerAdapter, ValueAnimator, ObjectAnimator,
    AnimatorSet, HorizontalScrollView, NestedScrollView, EditTextBoldCursor, Button,
    GridLayout, BackupImageView, ProgressBar, LineProgressView, PluginsConstants, PluginSettingsActivity,
    AppEvent, UItem, _UItemFactory, ItemOptions, PhotoViewer, ActionBar,
    CodeHighlighting, *_stubs.values())
