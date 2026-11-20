import json
import os
import threading
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Callable, List, Optional


def get_plugins_dir() -> Optional[str]:
    import extera_host
    return extera_host.PLUGINS_DIR or None


def pick_file(callback: Callable, title: str = "", filters: str = "", multiple: bool = False) -> str:
    import exd_picker
    return exd_picker.pick(callback, title, filters, multiple)


def _mkdirs(path: str) -> str:
    os.makedirs(path, exist_ok=True)
    return path


def _owner_plugin_id() -> Optional[str]:
    try:
        import extera_host
        return extera_host.current_plugin_id()
    except Exception:
        return None


def get_plugin_dir(plugin_id: Optional[str] = None) -> Optional[str]:
    plugin_id = plugin_id or _owner_plugin_id()
    if not plugin_id:
        return None
    base = get_plugins_dir()
    if not base:
        return None
    try:
        return _mkdirs(os.path.join(base, ".data", str(plugin_id)))
    except Exception:
        return None


def get_plugin_cache_dir(plugin_id: Optional[str] = None) -> Optional[str]:
    plugin_id = plugin_id or _owner_plugin_id()
    if not plugin_id:
        return None
    base = get_plugins_dir()
    if not base:
        return None
    try:
        return _mkdirs(os.path.join(base, ".cache", str(plugin_id)))
    except Exception:
        return None


def get_files_dir() -> Optional[str]:
    base = get_plugins_dir()
    if not base:
        return None
    return os.path.dirname(base)


def get_cache_dir() -> Optional[str]:
    base = get_plugins_dir()
    if not base:
        return None
    try:
        return _mkdirs(os.path.join(os.path.dirname(base), "plugin_cache"))
    except Exception:
        return None


def _media_dir(fallback_name: str) -> Optional[str]:
    base = get_files_dir()
    if base is None:
        return None
    path = os.path.join(base, fallback_name)
    try:
        _mkdirs(path)
    except Exception:
        return None
    return path


def get_images_dir() -> Optional[str]:
    return _media_dir("plugin_images")


def get_videos_dir() -> Optional[str]:
    return _media_dir("plugin_videos")


def get_audios_dir() -> Optional[str]:
    return _media_dir("plugin_audios")


def get_documents_dir() -> Optional[str]:
    return _media_dir("plugin_documents")


def ensure_dir_exists(path: str) -> str:
    os.makedirs(path, exist_ok=True)
    return path


def list_dir(path: str, recursive: bool = False, include_files: bool = True,
             include_dirs: bool = False,
             extensions: Optional[List[str]] = None) -> List[str]:
    result: List[str] = []
    if not path or not os.path.isdir(path):
        return result
    allowed = None
    if extensions:
        allowed = {
            ext.lower() if ext.startswith(".") else "." + ext.lower()
            for ext in extensions
        }

    def _extension_ok(name: str) -> bool:
        if allowed is None:
            return True
        return os.path.splitext(name)[1].lower() in allowed

    if recursive:
        for root, dirs, files in os.walk(path):
            if include_dirs:
                result.extend(os.path.join(root, d) for d in dirs)
            if include_files:
                result.extend(
                    os.path.join(root, f) for f in files if _extension_ok(f)
                )
    else:
        for name in os.listdir(path):
            full = os.path.join(path, name)
            if os.path.isdir(full):
                if include_dirs:
                    result.append(full)
            elif include_files and _extension_ok(name):
                result.append(full)
    return sorted(result)


def write_file(path: str, content) -> bool:
    try:
        if isinstance(content, (bytes, bytearray)):
            with open(path, "wb") as handle:
                handle.write(bytes(content))
        else:
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(str(content))
        return True
    except Exception:
        return False


def read_file(path: str) -> Optional[str]:
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return handle.read()
    except Exception:
        return None


def delete_file(path: str) -> bool:
    try:
        os.remove(path)
        return True
    except Exception:
        return False


def write_file_bytes(path: str, content: bytes) -> bool:
    try:
        with open(path, "wb") as handle:
            handle.write(bytes(content))
        return True
    except Exception:
        return False


def read_file_bytes(path: str) -> Optional[bytes]:
    try:
        with open(path, "rb") as handle:
            return handle.read()
    except Exception:
        return None


# FilesController

def _current_plugin_id() -> Optional[str]:
    return _owner_plugin_id()


class staticproperty:
    def __init__(self, fget):
        self.fget = fget.__func__ if isinstance(fget, staticmethod) else fget

    def __get__(self, obj, owner):
        return self.fget()


class FilesController:
    @staticproperty
    @staticmethod
    def DIRECT_FILE_ICONS() -> bool:
        return False

    @staticproperty
    @staticmethod
    def SUPPORT_ICONS() -> bool:
        return False

    class Place(Enum):
        UNKNOWN = 1
        ChatActivity = 2
        FilteredSearchView = 3
        SharedMediaLayout = 4
        SearchDownloadsContainer = 5
        ChannelAdminLogActivity = 6

    @dataclass
    class FileInfo:
        ext: str
        on_click: Callable = field(compare=False, repr=False, default=None)
        whitelist_places: list = field(default_factory=list, kw_only=True)
        blacklist_places: list = field(default_factory=list, kw_only=True)
        get_icon: Optional[Callable] = field(default=None, compare=False, repr=False, kw_only=True)

        def __post_init__(self):
            self.ext = str(self.ext or "").lstrip(".").lower()
            if not self.ext:
                raise ValueError("FileInfo.ext must not be empty")
            if not callable(self.on_click):
                raise ValueError("FileInfo.on_click must be callable")
            if self.whitelist_places and self.blacklist_places:
                raise ValueError("whitelist_places and blacklist_places are mutually exclusive")
            self.whitelist_places = [FilesController._place(p) for p in self.whitelist_places or ()]
            self.blacklist_places = [FilesController._place(p) for p in self.blacklist_places or ()]

    @dataclass
    class OnClickArgs:
        place: Any
        file: Any = field(compare=False, repr=False)
        file_name: str = ""
        message: Any = field(default=None, compare=False, repr=False)
        activity: Any = field(default=None, compare=False, repr=False)
        parent_fragment: Any = field(default=None, compare=False, repr=False)

    class ExtensionAlreadyRegistered(Exception):
        def __init__(self, ext: str = ""):
            super().__init__(f"extension {ext!r} is already registered")
            self.ext = ext

    class ExtensionNotRegistered(Exception):
        def __init__(self, ext: str = ""):
            super().__init__(f"extension {ext!r} is not registered")
            self.ext = ext

    class SecretInvalid(Exception):
        def __init__(self, ext: str = "", secret: str = ""):
            super().__init__(f"invalid secret for extension {ext!r}")
            self.ext = ext
            self.secret = secret

    _lock = threading.RLock()
    _registrations = {}

    @staticmethod
    def _place(value):
        if isinstance(value, FilesController.Place):
            return value
        if isinstance(value, int):
            return FilesController.Place(value)
        return FilesController.Place[str(value)]

    @classmethod
    def register(cls, file_info=None, on_click: Optional[Callable] = None, **kwargs) -> str:
        if not isinstance(file_info, FilesController.FileInfo):
            whitelist = kwargs.pop("whitelist_places", None) or kwargs.pop("whitelist", None) or []
            blacklist = kwargs.pop("blacklist_places", None) or kwargs.pop("blacklist", None) or []
            file_info = FilesController.FileInfo(file_info, on_click, whitelist_places=list(whitelist),
                                                 blacklist_places=list(blacklist),
                                                 get_icon=kwargs.pop("get_icon", None))
        plugin_id = _current_plugin_id()
        if not plugin_id:
            raise RuntimeError("FilesController.register must be called from a plugin context")
        import secrets
        with cls._lock:
            if file_info.ext in cls._registrations:
                raise FilesController.ExtensionAlreadyRegistered(file_info.ext)
            secret = secrets.token_hex(16)
            cls._registrations[file_info.ext] = {"secret": secret, "info": file_info, "plugin": plugin_id}
        return secret

    @classmethod
    def unregister(cls, ext: str, secret: str):
        ext = str(ext or "").lstrip(".").lower()
        with cls._lock:
            entry = cls._registrations.get(ext)
            if entry is None:
                raise FilesController.ExtensionNotRegistered(ext)
            if entry["secret"] != str(secret):
                raise FilesController.SecretInvalid(ext, str(secret))
            del cls._registrations[ext]

    @staticmethod
    def icons_supported() -> bool:
        return False

    @classmethod
    def dispatch(cls, path: str, place: str = "ChatActivity", message=None, fragment=None, activity=None) -> bool:
        ext = os.path.splitext(str(path))[1].lstrip(".").lower()
        with cls._lock:
            entry = cls._registrations.get(ext)
        if entry is None:
            return False
        info = entry["info"]
        try:
            where = FilesController._place(place)
        except Exception:
            where = FilesController.Place.UNKNOWN
        if info.whitelist_places and where not in info.whitelist_places:
            return False
        if info.blacklist_places and where in info.blacklist_places:
            return False
        import exd_java
        args = FilesController.OnClickArgs(place=where, file=exd_java.File(str(path)),
                                           file_name=os.path.basename(str(path)), message=message,
                                           activity=activity, parent_fragment=fragment)
        import extera_host
        with extera_host.owner_scope(entry["plugin"]):
            info.on_click(args)
        return True


def _unregister_all_for_plugin(plugin_id: str):
    if not plugin_id:
        return
    with FilesController._lock:
        for key in [k for k, v in FilesController._registrations.items() if v["plugin"] == plugin_id]:
            del FilesController._registrations[key]
