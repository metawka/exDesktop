import builtins
import importlib.abc
import os

ANDROID_ROOTS = (
    "/storage/emulated/0/android/data/com.exteragram.messenger/files",
    "/storage/emulated/0/android/data/com.exteragram.messenger",
    "/data/user/0/com.exteragram.messenger/files",
    "/data/user/0/com.exteragram.messenger",
    "/data/data/com.exteragram.messenger/files",
    "/data/data/com.exteragram.messenger",
    "/storage/emulated/0",
    "/storage/self/primary",
    "/mnt/sdcard",
    "/sdcard",
)


def data_dir(plugin_id):
    import extera_host
    path = os.path.join(extera_host.PLUGINS_DIR, ".data", str(plugin_id))
    os.makedirs(path, exist_ok=True)
    return path


def map_path(plugin_id, path):
    if isinstance(path, int):
        return path
    raw = os.fspath(path)
    if isinstance(raw, bytes):
        return raw
    if raw in ("", ":memory:") or raw.startswith("file:"):
        return raw
    text = raw.replace("\\", "/")
    lowered = text.lower()
    for root in ANDROID_ROOTS:
        if lowered == root or lowered.startswith(root + "/"):
            rest = text[len(root):].strip("/")
            target = os.path.join(data_dir(plugin_id), "android", *[p for p in rest.split("/") if p])
            os.makedirs(os.path.dirname(target), exist_ok=True)
            return target
    if os.path.isabs(raw) or lowered in ("nul", "con") or raw == os.devnull:
        return raw
    if text.startswith("/"):
        return raw
    return os.path.join(data_dir(plugin_id), raw)


_os_mapping_installed = False


def _plugin_of_frame(frame):
    import extera_host
    name = frame.f_code.co_filename or ""
    root = os.path.normcase(os.path.abspath(extera_host.PLUGINS_DIR or "")) + os.sep
    path = os.path.normcase(os.path.abspath(name)) if name and not name.startswith("<") else ""
    if not path.startswith(root):
        return None
    rest = path[len(root):].split(os.sep)
    if rest[0] == "elyxplugins" and len(rest) > 1:
        for record in list(extera_host._records.values()):
            if os.path.normcase(record.id) == rest[1]:
                return record.id
        return None
    for record in list(extera_host._records.values()):
        if os.path.normcase(os.path.abspath(record.path)) == path:
            return record.id
    return None


def install_os_mapping():
    global _os_mapping_installed
    if _os_mapping_installed:
        return
    _os_mapping_installed = True
    import sys

    def wrap(owner, name, count):
        original = getattr(owner, name, None)
        if original is None:
            return

        def mapped(*args, **kwargs):
            frame = sys._getframe(1)
            plugin_id = _plugin_of_frame(frame)
            if plugin_id is not None and args:
                args = list(args)
                for index in range(min(count, len(args))):
                    value = args[index]
                    if isinstance(value, (str, os.PathLike)):
                        args[index] = map_path(plugin_id, value)
            return original(*args, **kwargs)

        mapped.__name__ = getattr(original, "__name__", name)
        mapped.__doc__ = getattr(original, "__doc__", None)
        setattr(owner, name, mapped)

    for name in ("makedirs", "mkdir", "listdir", "scandir", "remove", "unlink", "rmdir", "stat",
                 "chmod", "utime", "truncate"):
        wrap(os, name, 1)
    for name in ("rename", "replace"):
        wrap(os, name, 2)
    for name in ("exists", "isfile", "isdir", "getsize", "getmtime", "getctime", "getatime", "islink"):
        wrap(os.path, name, 1)
    import shutil
    for name in ("copy", "copy2", "copyfile", "move", "copytree"):
        wrap(shutil, name, 2)
    wrap(shutil, "rmtree", 1)
    try:
        import sqlite3
        wrap(sqlite3, "connect", 1)
    except Exception:
        pass


def make_builtins(plugin_id):
    custom = dict(builtins.__dict__)
    original_open = builtins.open

    def plugin_open(file, *args, **kwargs):
        return original_open(map_path(plugin_id, file), *args, **kwargs)

    custom["open"] = plugin_open
    return custom


class PluginLoader(importlib.abc.Loader):
    def __init__(self, loader, plugin_id):
        self.loader = loader
        self.plugin_id = plugin_id

    def create_module(self, spec):
        return self.loader.create_module(spec)

    def exec_module(self, module):
        module.__dict__["__builtins__"] = make_builtins(self.plugin_id)
        try:
            import base_plugin
            for name in ("BasePlugin", "MethodHook", "MethodReplacement", "HookResult", "HookStrategy",
                         "MenuItemData", "MenuItemType", "AppEvent", "XposedHook"):
                if hasattr(base_plugin, name):
                    module.__dict__.setdefault(name, getattr(base_plugin, name))
        except Exception:
            pass
        self.loader.exec_module(module)

    def get_source(self, fullname):
        return self.loader.get_source(fullname)
