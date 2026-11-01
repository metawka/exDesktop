import json
import os
import threading
from dataclasses import dataclass
from typing import Callable, List, Optional


def get_plugins_dir() -> Optional[str]:
    import extera_host
    return extera_host.PLUGINS_DIR or None


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


class FilesController:
    class ExtensionAlreadyRegistered(Exception):
        pass

    class ExtensionNotRegistered(Exception):
        pass

    class SecretInvalid(Exception):
        pass

    class Place:
        UNKNOWN = "UNKNOWN"
        ChatActivity = "ChatActivity"
        FilteredSearchView = "FilteredSearchView"
        SharedMediaLayout = "SharedMediaLayout"
        SearchDownloadsContainer = "SearchDownloadsContainer"
        ChannelAdminLogActivity = "ChannelAdminLogActivity"

    @dataclass
    class FileInfo:
        ext: str
        on_click: Optional[Callable] = None
        whitelist_places: Optional[List[str]] = None
        blacklist_places: Optional[List[str]] = None
        get_icon: Optional[Callable] = None

    _lock = threading.RLock()
    _registrations = {}
    _secret_counter = [0]

    @staticmethod
    def register(info=None, on_click: Optional[Callable] = None,
                 whitelist_places: Optional[List] = None,
                 blacklist_places: Optional[List] = None,
                 get_icon: Optional[Callable] = None,
                 whitelist: Optional[List] = None, blacklist: Optional[List] = None,
                 icon: bool = False) -> str:
        if isinstance(info, FilesController.FileInfo):
            ext = info.ext
            on_click = info.on_click if on_click is None else on_click
            whitelist_places = info.whitelist_places \
                if whitelist_places is None else whitelist_places
            blacklist_places = info.blacklist_places \
                if blacklist_places is None else blacklist_places
            get_icon = info.get_icon if get_icon is None else get_icon
        else:
            ext = info
        whitelist_places = whitelist if whitelist is not None else whitelist_places
        blacklist_places = blacklist if blacklist is not None else blacklist_places
        if not ext:
            raise ValueError("register() requires a file extension")
        ext = str(ext).lstrip(".").lower()
        if not callable(on_click):
            raise ValueError("register() requires an on_click callback")
        if whitelist_places and blacklist_places:
            raise ValueError("whitelist_places and blacklist_places are mutually exclusive")
        plugin_id = _current_plugin_id()
        if not plugin_id:
            raise RuntimeError(
                "FilesController.register must be called from a plugin context "
                "(on_plugin_load / a hook callback)")
        key = (plugin_id, ext)
        with FilesController._lock:
            if key in FilesController._registrations:
                raise FilesController.ExtensionAlreadyRegistered(
                    f"extension {ext!r} is already registered by {plugin_id!r}")
            FilesController._secret_counter[0] += 1
            secret = f"{plugin_id}:{ext}:{FilesController._secret_counter[0]}"
            FilesController._registrations[key] = {
                "secret": secret, "on_click": on_click, "get_icon": get_icon,
            }
        import extera_host
        extera_host.notify_file_handler_registered(plugin_id, ext)
        return secret

    @staticmethod
    def unregister(ext: str, secret: str) -> None:
        ext = str(ext).lstrip(".").lower()
        plugin_id = _current_plugin_id()
        with FilesController._lock:
            key = (plugin_id, ext)
            entry = FilesController._registrations.get(key)
            if entry is None and plugin_id is None:
                matches = [k for k in FilesController._registrations if k[1] == ext]
                if len(matches) == 1:
                    key = matches[0]
                    entry = FilesController._registrations.get(key)
            if entry is None:
                raise FilesController.ExtensionNotRegistered(
                    f"extension {ext!r} is not registered")
            if entry["secret"] != str(secret):
                raise FilesController.SecretInvalid(
                    f"invalid secret for extension {ext!r}")
            del FilesController._registrations[key]

    @staticmethod
    def icons_supported() -> bool:
        return False


def _unregister_all_for_plugin(plugin_id: str):
    if not plugin_id:
        return
    with FilesController._lock:
        keys = [key for key in FilesController._registrations if key[0] == plugin_id]
        for key in keys:
            del FilesController._registrations[key]
