import builtins
import hashlib
import importlib
import importlib.abc
import importlib.machinery
import importlib.util
import json
import os
import pathlib
import re
import shutil
import sys
import types
import zipfile

EXTENSIONS = (".elyx.zip", ".eaf.zip", ".elyx", ".eaf")
PACKAGE = "ElyxPlugins"
RESERVED = {"android", "androidx", "base_plugin", "client_utils", "com", "de", "elyx", "hook_utils",
            "importlib", "java", "org", "ui", "json", "pathlib", "typing", "os", "sys", "re"}
DATA_EXTENSIONS = (".json", ".yaml", ".yml", ".txt")
_contexts = {}
_host = None


class ElyxError(Exception):
    pass


def is_elyx(path):
    lowered = str(path).lower()
    return lowered.endswith(EXTENSIONS)


def canonical_extension(path):
    lowered = str(path).lower()
    return ".eaf" if ".eaf" in lowered else ".elyx"


# Parsing

def _parse_structured(name, data):
    text = data.decode("utf-8-sig", "replace") if isinstance(data, bytes) else data
    if name.lower().endswith(".json"):
        return json.loads(text)
    import yaml
    return yaml.safe_load(text) or {}


def _open_archive(path):
    try:
        archive = zipfile.ZipFile(path)
    except zipfile.BadZipFile:
        raise ElyxError("archive is damaged or not a ZIP file")
    if any(info.flag_bits & 1 for info in archive.infolist()):
        archive.close()
        raise ElyxError("encrypted archives are not supported on desktop")
    return archive


def _clean(path):
    return str(path or "").replace("\\", "/").strip("/")


def _read_refmap(archive):
    names = set(archive.namelist())
    for candidate in ("refmap.yaml", "refmap.yml", "refmap.json"):
        if candidate in names:
            data = _parse_structured(candidate, archive.read(candidate))
            if not isinstance(data, dict):
                raise ElyxError("refmap must be a mapping")
            return {str(k): v for k, v in data.items()}
    raise ElyxError("refmap.yaml, refmap.yml or refmap.json is missing at the archive root")


def _resolve_layout(archive, refmap):
    names = set(archive.namelist())
    main = _clean(refmap.get("main")) or ("main.py" if "main.py" in names else "")
    if not main or main not in names:
        raise ElyxError("entry module %r is missing" % (main or "main.py"))
    meta = _clean(refmap.get("metainfo"))
    if not meta:
        for candidate in ("metainfo.yaml", "metainfo.yml", "metainfo.json"):
            if candidate in names:
                meta = candidate
                break
    if not meta or meta not in names:
        raise ElyxError("metainfo file is missing")
    assets = _clean(refmap.get("assets")) or ("assets" if any(n.startswith("assets/") for n in names) else "")
    return {
        "main": main,
        "metainfo": meta,
        "assets": assets,
        "strings": _clean(refmap.get("strings")),
        "wheels": _clean(refmap.get("wheels")),
    }


def _strings_from_archive(archive, strings_path):
    result = {}
    if not strings_path:
        return result
    names = archive.namelist()
    files = [n for n in names if n == strings_path or n.startswith(strings_path + "/")]
    for name in files:
        if name.endswith("/"):
            continue
        stem, ext = os.path.splitext(os.path.basename(name))
        if ext.lower() not in (".yaml", ".yml", ".json"):
            continue
        locale = stem.rsplit("_", 1)[1].lower() if "_" in stem else "en"
        try:
            data = _parse_structured(name, archive.read(name))
        except Exception:
            continue
        if isinstance(data, dict):
            result.setdefault(locale, {}).update(data)
    return result


def _format_placeholders(text, strings):
    if not isinstance(text, str) or "{" not in text:
        return text
    import exd_java
    locale = exd_java.language()

    def replace(match):
        key = match.group(1)
        for code in (locale, "en"):
            value = strings.get(code, {}).get(key)
            if isinstance(value, str):
                return value
        return key

    return re.sub(r"\{([A-Za-z0-9_.-]+)\}", replace, text)


def read_meta(path):
    import metadata_parser
    with _open_archive(path) as archive:
        refmap = _read_refmap(archive)
        layout = _resolve_layout(archive, refmap)
        info = _parse_structured(layout["metainfo"], archive.read(layout["metainfo"]))
        if not isinstance(info, dict):
            raise ElyxError("metainfo must be a mapping")
        info = {str(k).strip("_") if str(k).startswith("__") else str(k): v for k, v in info.items()}
        strings = _strings_from_archive(archive, layout["strings"])
    plugin_id = metadata_parser._validate_plugin_id(info.get("id"))
    requirements = info.get("requirements") or []
    if isinstance(requirements, str):
        requirements = [r.strip() for r in requirements.split(",") if r.strip()]
    app_version = info.get("app_version")
    if not app_version and info.get("min_version"):
        app_version = ">=%s" % info.get("min_version")
    requires = info.get("requires") or {}
    return {
        "id": plugin_id,
        "name": str(_format_placeholders(info.get("name"), strings) or plugin_id),
        "description": str(_format_placeholders(info.get("description") or "", strings)),
        "author": str(info.get("author") or ""),
        "version": str(info.get("version") or "1.0.0"),
        "icon": info.get("icon"),
        "app_version": app_version,
        "sdk_version": info.get("sdk_version"),
        "beta": False,
        "requirements": list(requirements) if isinstance(requirements, (list, tuple)) else [],
        "permissions": [],
        "permissions_declared": False,
        "format": "elyx",
        "desktop": bool(info.get("desktop", False)),
        "ported_from": str(info.get("ported_from") or ""),
        "requires": dict(requires) if isinstance(requires, dict) else {},
        "elyx": {"refmap": refmap, "layout": layout, "metainfo": info},
    }


# Extraction

def plugins_root():
    return os.path.join(_host.PLUGINS_DIR, PACKAGE)


def plugin_root(plugin_id):
    return os.path.join(plugins_root(), plugin_id)


def _digest(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def extract(path, plugin_id):
    root = plugin_root(plugin_id)
    stamp = os.path.join(root, ".exd_stamp")
    digest = _digest(path)
    try:
        with open(stamp, encoding="utf-8") as f:
            if f.read().strip() == digest:
                return root
    except OSError:
        pass
    if os.path.isdir(root):
        shutil.rmtree(root, ignore_errors=True)
    os.makedirs(root, exist_ok=True)
    real_root = os.path.realpath(root)
    with _open_archive(path) as archive:
        for info in archive.infolist():
            name = info.filename.replace("\\", "/")
            if name.startswith("/") or ".." in name.split("/") or ":" in name:
                raise ElyxError("archive contains an unsafe path: %s" % name)
            target = os.path.realpath(os.path.join(root, name))
            if not (target == real_root or target.startswith(real_root + os.sep)):
                raise ElyxError("archive contains an unsafe path: %s" % name)
            if name.endswith("/"):
                os.makedirs(target, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with archive.open(info) as source, open(target, "wb") as out:
                shutil.copyfileobj(source, out)
    with open(stamp, "w", encoding="utf-8") as f:
        f.write(digest)
    return root


def _install_wheels(context):
    folder = context["wheels_dir"]
    if not folder or not os.path.isdir(folder):
        return
    libs = os.path.join(os.path.dirname(_host.PLUGINS_DIR), "elyx_local_libs", context["id"])
    os.makedirs(libs, exist_ok=True)
    wanted = set()
    for name in sorted(os.listdir(folder)):
        if not name.lower().endswith(".whl"):
            continue
        stem = name[:-4]
        wanted.add(stem)
        target = os.path.join(libs, stem)
        if not os.path.isdir(target):
            with zipfile.ZipFile(os.path.join(folder, name)) as wheel:
                wheel.extractall(target)
        if target not in sys.path:
            sys.path.append(target)
    for name in os.listdir(libs):
        if name not in wanted:
            shutil.rmtree(os.path.join(libs, name), ignore_errors=True)


# Import system

class _DataLoader(importlib.abc.Loader):
    def __init__(self, path):
        self.path = path

    def create_module(self, spec):
        return None

    def exec_module(self, module):
        ext = os.path.splitext(self.path)[1].lower()
        with open(self.path, "rb") as f:
            data = f.read()
        if ext == ".txt":
            module.content = data.decode("utf-8", "replace")
            return
        value = _parse_structured(self.path, data)
        module.content = value
        if isinstance(value, dict):
            for key, item in value.items():
                setattr(module, str(key), _wrap_data(item))

        def getitem(key):
            return _wrap_data(value[key])

        module.__getitem__ = getitem


class _AttrDict(dict):
    def __getattr__(self, item):
        try:
            return _wrap_data(self[item])
        except KeyError:
            raise AttributeError(item)


def _wrap_data(value):
    if isinstance(value, dict) and not isinstance(value, _AttrDict):
        return _AttrDict(value)
    return value


class _WrappedLoader(importlib.abc.Loader):
    def __init__(self, loader, context):
        self.loader = loader
        self.context = context

    def create_module(self, spec):
        return self.loader.create_module(spec)

    def exec_module(self, module):
        module.__dict__["__builtins__"] = self.context["builtins"]
        self.loader.exec_module(module)


class _ElyxFinder(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if not fullname.startswith(PACKAGE + "."):
            return None
        parts = fullname.split(".")
        if len(parts) < 3:
            return None
        context = _contexts.get(parts[1])
        if context is None:
            return None
        parent = sys.modules.get(".".join(parts[:-1]))
        search = list(getattr(parent, "__path__", None) or context["search"])
        spec = importlib.machinery.PathFinder.find_spec(fullname, search)
        if spec is not None and spec.loader is not None:
            spec.loader = _WrappedLoader(spec.loader, context)
            return spec
        for folder in search:
            for ext in DATA_EXTENSIONS:
                candidate = os.path.join(folder, parts[-1] + ext)
                if os.path.isfile(candidate):
                    return importlib.util.spec_from_loader(fullname, _DataLoader(candidate), origin=candidate)
        return None


def _local_exists(context, top):
    for folder in context["search"]:
        base = os.path.join(folder, top)
        if os.path.isdir(base) or os.path.isfile(base + ".py") or os.path.isfile(base + ".pyc"):
            return True
        if any(os.path.isfile(base + ext) for ext in DATA_EXTENSIONS):
            return True
    return False


def _make_import(context):
    original = builtins.__import__
    prefix = context["package"] + "."

    def elyx_import(name, globals=None, locals=None, fromlist=(), level=0):
        if level == 0 and name:
            top = name.split(".")[0]
            if top not in RESERVED and top not in sys.builtin_module_names and _local_exists(context, top):
                module = importlib.import_module(prefix + name)
                if fromlist:
                    return module
                return sys.modules[prefix + top]
        return original(name, globals, locals, fromlist, level)

    return elyx_import


def _ensure_finder():
    if not any(isinstance(f, _ElyxFinder) for f in sys.meta_path):
        sys.meta_path.insert(0, _ElyxFinder())
    if PACKAGE not in sys.modules:
        package = types.ModuleType(PACKAGE)
        package.__path__ = []
        sys.modules[PACKAGE] = package


# Runtime API

class AssetNotFoundException(Exception):
    pass


class AssetsDirNotFoundException(Exception):
    pass


def _normalize(name):
    stem = name.split(".")[0] if "." in name else name
    return re.sub(r"[^A-Za-z0-9]", "_", stem)


def normalize_name(name):
    return _normalize(str(name))


def load_data_file(path, plzip=None):
    if plzip is not None:
        data = plzip.read(path)
        name = getattr(path, "filename", str(path))
    else:
        name = os.fspath(path)
        with open(name, "rb") as f:
            data = f.read()
    return _parse_structured(name, data)


class Asset:
    def __init__(self, dir_path, filename=None, name=None):
        full = pathlib.Path(os.fspath(dir_path) if not hasattr(dir_path, "getAbsolutePath")
                            else str(dir_path.getAbsolutePath()))
        if filename is not None:
            full = full / str(filename)
        self.path = full
        self.dir_path = str(full.parent)
        self.filename = full.name
        self.ext = full.suffix.lstrip(".").lower()
        self.name = str(name) if name else _normalize(self.filename)
        self.path_str = str(full)

    @classmethod
    def temp_asset_from_url(cls, url, filename):
        import tempfile
        import urllib.request
        url = str(url)
        if not url.startswith("https://"):
            raise ValueError("only https:// is allowed")
        folder = tempfile.mkdtemp(prefix="elyx_")
        target = os.path.join(folder, os.path.basename(str(filename)) or "asset")
        with urllib.request.urlopen(url, timeout=30) as response, open(target, "wb") as out:
            out.write(response.read())
        return cls(folder, os.path.basename(target))

    @property
    def java_file(self):
        import exd_java
        return exd_java.File(self.path_str)

    @staticmethod
    def from_path(path):
        return Asset(os.fspath(path))

    def content_bytes(self):
        return self.path.read_bytes()

    def content_string(self):
        return self.path.read_text(encoding="utf-8")

    def content_json(self):
        return json.loads(self.content_string())

    def content_yaml(self):
        import yaml
        return yaml.safe_load(self.content_string())

    def content(self):
        if self.ext == "json":
            return self.content_json()
        if self.ext in ("yaml", "yml"):
            return self.content_yaml()
        try:
            return self.content_string()
        except UnicodeDecodeError:
            return self.content_bytes()

    def to_image_location(self):
        return self.path_str

    def to_drawable(self, *args, **kwargs):
        return self.path_str

    to_bitmap_drawable = to_svg_drawable = to_svg_bitmap = to_svg_thumb = to_lottie_drawable = to_drawable

    def __fspath__(self):
        return self.path_str

    def __repr__(self):
        return "<Asset %s>" % self.filename


class Assets:
    def __init__(self, folder, parent=None):
        if not os.path.isdir(folder):
            raise AssetsDirNotFoundException(folder)
        self.dir_path = pathlib.Path(folder)
        self.parent = parent

    def _entries(self):
        result = {}
        for entry in sorted(os.listdir(self.dir_path)):
            if entry.startswith(".") or entry.endswith(".dexan"):
                continue
            full = os.path.join(self.dir_path, entry)
            key = _normalize(entry) if os.path.isfile(full) else re.sub(r"[^A-Za-z0-9]", "_", entry)
            result.setdefault(key, full)
            result.setdefault(entry, full)
        return result

    def get(self, name):
        name = str(name)
        if "/" in name:
            head, _, rest = name.partition("/")
            child = self.get(head)
            if isinstance(child, Assets):
                return child.get(rest)
            raise AssetNotFoundException(name)
        entries = self._entries()
        full = entries.get(name) or entries.get(_normalize(name))
        if full is None:
            raise AssetNotFoundException(name)
        if os.path.isdir(full):
            return Assets(full, self)
        return Asset(full)

    @property
    def files(self):
        return [entry for entry in self if isinstance(entry, Asset)]

    @property
    def names(self):
        return [entry.name for entry in self.files]

    def __getitem__(self, name):
        return self.get(name)

    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        try:
            return self.get(name)
        except AssetNotFoundException:
            raise AttributeError(name)

    def __contains__(self, name):
        try:
            self.get(name)
            return True
        except AssetNotFoundException:
            return False

    def __iter__(self):
        seen = set()
        for full in self._entries().values():
            if full in seen:
                continue
            seen.add(full)
            yield Assets(full, self) if os.path.isdir(full) else Asset(full)

    def __len__(self):
        return len(set(self._entries().values()))


class Strings:
    def __init__(self, values=None):
        self._values = {str(k).lower(): dict(v) for k, v in (values or {}).items() if isinstance(v, dict)}

    @property
    def all_strings(self):
        return self._values

    @staticmethod
    def get_current_language():
        import exd_java
        return exd_java.language()

    @staticmethod
    def pluralization_string(number, words):
        words = list(words or ())
        if not words:
            return str(number)
        n = abs(int(number))
        if len(words) >= 3:
            if n % 10 == 1 and n % 100 != 11:
                return words[0]
            if 2 <= n % 10 <= 4 and not 12 <= n % 100 <= 14:
                return words[1]
            return words[2]
        return words[0] if n == 1 else words[-1]

    def __setitem__(self, key, value):
        self._values.setdefault(self.get_current_language(), {})[key] = value

    def _locale(self):
        return self.get_current_language()

    def get_with_locale(self, key, locale=None, default=None):
        for code in (locale or self._locale(), "en"):
            value = self._values.get(code, {}).get(key)
            if value is not None:
                return value
        return key if default is None else default

    def get(self, key, default=None):
        return self.get_with_locale(key, None, default)

    def __getitem__(self, key):
        return self.get(key)

    def __getattr__(self, key):
        if key.startswith("__"):
            raise AttributeError(key)
        return self.get(key)

    def __call__(self, key, *args, default=None, **kwargs):
        value = self.get(key, default)
        if isinstance(value, str) and (args or kwargs):
            try:
                return value.format(*args, **kwargs)
            except (IndexError, KeyError):
                return value
        return value

    def pluralize(self, count, key):
        forms = self.get(key)
        if not isinstance(forms, (list, tuple)) or not forms:
            return "%s %s" % (count, key)
        n = abs(int(count))
        if len(forms) >= 3:
            if n % 10 == 1 and n % 100 != 11:
                form = forms[0]
            elif 2 <= n % 10 <= 4 and not 12 <= n % 100 <= 14:
                form = forms[1]
            else:
                form = forms[2]
        else:
            form = forms[0] if n == 1 else forms[-1]
        return "%s %s" % (count, form)

    def __bool__(self):
        return bool(self._values)


class SettingsController:
    def __init__(self, plugin_id):
        self._id = plugin_id

    def get_settings(self):
        return _host.export_settings(self._id)

    def get_setting(self, key, default=None):
        return _host.get_setting(self._id, key, default)

    get = get_setting

    def set_setting(self, key, value, reload_settings=False):
        return _host.set_setting(self._id, key, value, reload_settings)

    set = set_setting

    def clear_settings(self):
        return _host.import_settings(self._id, {}, True)

    def __getitem__(self, key):
        return self.get_setting(key)

    def __setitem__(self, key, value):
        self.set_setting(key, value)

    def __call__(self, key, default=None):
        return self.get_setting(key, default)


def _load_strings(path):
    values = {}
    if not path or not os.path.exists(path):
        return values
    files = [path] if os.path.isfile(path) else [os.path.join(path, n) for n in sorted(os.listdir(path))]
    for name in files:
        stem, ext = os.path.splitext(os.path.basename(name))
        locale = stem.rsplit("_", 1)[1].lower() if "_" in stem else "en"
        try:
            if ext.lower() in (".yaml", ".yml", ".json"):
                with open(name, "rb") as f:
                    data = _parse_structured(name, f.read())
            elif ext.lower() == ".py":
                scope = {}
                with open(name, encoding="utf-8") as f:
                    exec(compile(f.read(), name, "exec"), scope)
                data = {k: v for k, v in scope.items() if not k.startswith("_") and not callable(v)
                        and not isinstance(v, types.ModuleType)}
            else:
                continue
        except Exception:
            continue
        if isinstance(data, dict):
            values.setdefault(locale, {}).update(data)
    return values


def _caller_context():
    root = os.path.normcase(os.path.abspath(plugins_root())) + os.sep
    frame = sys._getframe(2)
    while frame is not None:
        name = os.path.normcase(os.path.abspath(frame.f_code.co_filename)) if frame.f_code.co_filename else ""
        if name.startswith(root):
            plugin_id = name[len(root):].split(os.sep)[0]
            if plugin_id in _contexts:
                return _contexts[plugin_id]
        frame = frame.f_back
    if _host is not None:
        current = _host.current_plugin_id()
        if current in _contexts:
            return _contexts[current]
        try:
            import exd_runtime
            frame = sys._getframe(2)
            while frame is not None and current is None:
                current = exd_runtime._plugin_of_frame(frame)
                frame = frame.f_back
        except Exception:
            pass
        record = getattr(_host, "_records", {}).get(current) if current else None
        if record is not None:
            return {
                "id": record.id,
                "root": os.path.dirname(record.path),
                "package": record.id,
                "search": [],
                "metainfo": {k: v for k, v in record.meta.items() if k != "elyx"},
                "refmap": {},
                "assets": None,
                "strings": Strings({}),
                "wheels_dir": None,
            }
    return None



# Utils

class LazyDict(dict):
    def __setattr__(self, key, value):
        self[key] = value

    def __getattr__(self, item):
        if item.startswith("__"):
            raise AttributeError(item)
        try:
            return self[item]
        except KeyError:
            raise AttributeError(item)


def _convert(value, to_type):
    if to_type is None or value is None:
        return value
    name = to_type if isinstance(to_type, str) else getattr(to_type, "_java_name", None) or getattr(to_type, "__name__", "")
    name = str(name).split(".")[-1].lower()
    if name in ("int", "integer", "long", "short", "byte"):
        return int(value)
    if name in ("float", "double"):
        return float(value)
    if name in ("bool", "boolean"):
        return bool(value)
    if name in ("str", "string", "charsequence"):
        return str(value)
    return value


def mvel_execute(script, data=None, to_type=None, java_instance=None):
    from extera_utils.classes import _run_mvel
    values = dict(data or {})
    if not isinstance(data, dict) and data is not None:
        try:
            values = {str(k): data.get(k) for k in data.keySet()}
        except Exception:
            values = {}
    names = list(values.keys())
    return _convert(_run_mvel(script, java_instance, names, [values[n] for n in names]), to_type)


class _Generated:
    def __init__(self, java_class, methods, return_value, default_value):
        self._java_class = java_class
        self._java_name = getattr(java_class, "_java_name", None) or str(java_class)
        for method_name, fn in methods.items():
            setattr(self, method_name, self._wrap(fn, return_value, default_value))

    @staticmethod
    def _wrap(fn, return_value, default_value):
        def call(*args):
            try:
                result = fn(*args)
            except Exception:
                import traceback
                if _host is not None:
                    _host.log("elyx callback failed: " + traceback.format_exc(limit=4))
                return default_value
            return result if return_value else default_value
        return call


def gen(java_class, method_name, return_value=False, default_value=None):
    return lambda fn: _Generated(java_class, {method_name: fn}, return_value, default_value)


def gen2(java_class, return_value=False, **methods):
    return _Generated(java_class, methods, return_value, None)


def safe_func(fn):
    def call(*args, **kwargs):
        try:
            return fn(*args, **kwargs)
        except Exception:
            import traceback
            if _host is not None:
                _host.log("elyx function failed: " + traceback.format_exc(limit=4))
            return None
    return call


def safe_run_on_queue(fn):
    import client_utils
    client_utils.run_on_queue(safe_func(fn))


def hash_map(dict_):
    import exd_java
    return exd_java.HashMap(dict(dict_ or {}))


def resize_image(image, width, height):
    import io
    from PIL import Image
    source = Image.open(io.BytesIO(image))
    fmt = source.format or "PNG"
    out = io.BytesIO()
    source.resize((int(width), int(height))).save(out, format=fmt)
    return out.getvalue()


def _callback(name, method, return_value=False):
    def factory(fn):
        return _Generated(name, {method: fn}, return_value, None)
    return factory


OnClickListener = _callback("android.view.View$OnClickListener", "onClick")
Runnable = _callback("java.lang.Runnable", "run")
Callback = _callback("org.telegram.messenger.Utilities$Callback", "run")
Callback2 = _callback("org.telegram.messenger.Utilities$Callback2", "run")
Callback3 = _callback("org.telegram.messenger.Utilities$Callback3", "run")
CallbackReturn = _callback("org.telegram.messenger.Utilities$CallbackReturn", "run", True)


def install_api(host):
    global _host
    _host = host
    _ensure_api()


class _ElyxModule(types.ModuleType):
    Strings = Strings
    SettingsController = SettingsController
    Assets = Assets
    Asset = Asset
    AssetNotFoundException = AssetNotFoundException
    AssetsDirNotFoundException = AssetsDirNotFoundException
    LazyDict = LazyDict
    gen = staticmethod(gen)
    gen2 = staticmethod(gen2)
    mvel_execute = staticmethod(mvel_execute)
    OnClickListener = staticmethod(OnClickListener)
    Runnable = staticmethod(Runnable)
    Callback = staticmethod(Callback)
    Callback2 = staticmethod(Callback2)
    Callback3 = staticmethod(Callback3)
    CallbackReturn = staticmethod(CallbackReturn)

    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        context = _caller_context()
        if context is None:
            raise AttributeError(name)
        if name == "metainfo":
            return dict(context["metainfo"])
        if name == "refmap":
            return dict(context["refmap"])
        if name == "settings":
            return SettingsController(context["id"])
        if name == "assets":
            if context["assets"] is None:
                raise AttributeError(name)
            return context["assets"]
        if name == "strings":
            if not context["strings"]:
                raise AttributeError(name)
            return context["strings"]
        if name == "import_module":
            return lambda module_name: importlib.import_module(context["package"] + "." + module_name) \
                if _local_exists(context, module_name.split(".")[0]) else importlib.import_module(module_name)
        if name == "get_environment":
            return lambda: {
                "metainfo": dict(context["metainfo"]),
                "refmap": dict(context["refmap"]),
                "assets": context["assets"],
                "strings": context["strings"] or None,
                "settings": SettingsController(context["id"]),
                "plugin_id": context["id"],
                "root": context["root"],
            }
        if name == "plugin_id":
            return context["id"]
        raise AttributeError(name)


def _ensure_api():
    module = sys.modules.get("elyx")
    if not isinstance(module, _ElyxModule):
        module = _ElyxModule("elyx")
        module.__path__ = []
        sys.modules["elyx"] = module
    if not isinstance(sys.modules.get("elyxcore"), _ElyxModule):
        core = _ElyxModule("elyxcore")
        core.__path__ = []
        sys.modules["elyxcore"] = core
    parts = {
        "utils": {"LazyDict": LazyDict, "gen": gen, "gen2": gen2, "mvel_execute": mvel_execute,
                  "safe_run_on_queue": safe_run_on_queue, "safe_func": safe_func, "hash_map": hash_map,
                  "resize_image": resize_image},
        "assets": {"Asset": Asset, "Assets": Assets, "AssetNotFoundException": AssetNotFoundException,
                   "AssetsDirNotFoundException": AssetsDirNotFoundException},
        "localization": {"Strings": Strings, "OnClickListener": OnClickListener, "Runnable": Runnable,
                         "Callback": Callback, "Callback2": Callback2, "Callback3": Callback3,
                         "CallbackReturn": CallbackReturn, "ELYX_ID": "elyx",
                         "dlog": lambda msg: _host.log("[elyx] %s" % msg) if _host else None},
        "settings": {"SettingsController": SettingsController},
    }
    parts["assets"].update({"load_data_file": load_data_file, "normalize_name": normalize_name})
    top = {"Asset": Asset, "Assets": Assets, "AssetNotFoundException": AssetNotFoundException,
           "AssetsDirNotFoundException": AssetsDirNotFoundException, "Strings": Strings,
           "SettingsController": SettingsController, "LazyDict": LazyDict, "gen": gen, "gen2": gen2,
           "mvel_execute": mvel_execute, "OnClickListener": OnClickListener, "Runnable": Runnable,
           "Callback": Callback, "Callback2": Callback2, "Callback3": Callback3, "CallbackReturn": CallbackReturn,
           "get_environment": get_environment, "import_module": import_module}
    for root in ("elyx", "elyxcore"):
        for key, value in top.items():
            if key not in sys.modules[root].__dict__:
                setattr(sys.modules[root], key, value)
    for root in ("elyx", "elyxcore"):
        for name, values in parts.items():
            full = root + "." + name
            if full not in sys.modules:
                sub = types.ModuleType(full)
                sub.__dict__.update(values)
                sys.modules[full] = sub


def get_environment():
    context = _caller_context()
    if not context:
        return {}
    return {"plugin_id": context.get("id"), "root": context.get("root"),
            "strings": context.get("strings"), "settings": SettingsController(context.get("id"))}


def import_module(name, package=None):
    return importlib.import_module(name, package)


def check_requires(meta):
    problems = []
    records = getattr(_host, "_records", {})
    for raw_key, link in (meta.get("requires") or {}).items():
        match = re.match(r"\s*([A-Za-z0-9_-]+)\s*(?:\(([^)]*)\))?", str(raw_key))
        if not match:
            continue
        dependency, minimum = match.group(1), match.group(2)
        record = records.get(dependency)
        if record is None:
            problems.append("missing %s" % dependency)
        elif minimum:
            from packaging.version import Version, InvalidVersion
            try:
                if Version(str(record.meta.get("version") or "0")) < Version(minimum):
                    problems.append("%s %s or newer is required" % (dependency, minimum))
            except InvalidVersion:
                pass
    return problems


def load(record, host):
    global _host
    _host = host
    _ensure_finder()
    _ensure_api()
    elyx = record.meta.get("elyx") or {}
    layout = elyx.get("layout") or {}
    root = extract(record.path, record.id)
    package_name = PACKAGE + "." + record.id
    main_path = os.path.join(root, *layout["main"].split("/"))
    main_dir = os.path.dirname(main_path)
    search = [main_dir] if os.path.normcase(main_dir) == os.path.normcase(root) else [main_dir, root]
    assets = None
    if layout.get("assets"):
        try:
            assets = Assets(os.path.join(root, *layout["assets"].split("/")))
        except AssetsDirNotFoundException:
            assets = None
    strings = Strings(_load_strings(os.path.join(root, *layout["strings"].split("/"))) if layout.get("strings") else {})
    context = {
        "id": record.id,
        "root": root,
        "package": package_name,
        "search": search,
        "metainfo": dict(elyx.get("metainfo") or {}),
        "refmap": dict(elyx.get("refmap") or {}),
        "assets": assets,
        "strings": strings,
        "wheels_dir": os.path.join(root, *layout["wheels"].split("/")) if layout.get("wheels") else None,
    }
    import exd_runtime
    custom = exd_runtime.make_builtins(record.id)
    context["builtins"] = custom
    custom["__import__"] = _make_import(context)
    _contexts[record.id] = context
    _install_wheels(context)
    for problem in check_requires(record.meta):
        host.log("plugin %s dependency: %s" % (record.id, problem))
    package = types.ModuleType(package_name)
    package.__path__ = search
    package.__package__ = package_name
    package.__file__ = os.path.join(main_dir, "__init__.py")
    sys.modules[package_name] = package
    module_name = package_name + "." + os.path.splitext(os.path.basename(main_path))[0]
    spec = importlib.util.spec_from_file_location(module_name, main_path)
    module = importlib.util.module_from_spec(spec)
    module.__dict__["__builtins__"] = custom
    module.__package__ = package_name
    sys.modules[module_name] = module
    try:
        spec.loader.exec_module(module)
    except BaseException:
        sys.modules.pop(module_name, None)
        raise
    return module


def unload(plugin_id):
    prefix = PACKAGE + "." + plugin_id
    for name in list(sys.modules):
        if name == prefix or name.startswith(prefix + "."):
            sys.modules.pop(name, None)
    _contexts.pop(plugin_id, None)
    importlib.invalidate_caches()
