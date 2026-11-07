import ast
import copy
import os
import re
import sys
from typing import Any, Dict, Optional


class PluginMetadataError(Exception):
    pass


_METADATA_KEYS = (
    "__id__", "__name__", "__description__", "__author__", "__version__",
    "__icon__", "__app_version__", "__sdk_version__", "__min_version__",
    "__beta__", "__requirements__", "__permissions__", "__desktop__", "__ported_from__", "__desktop_os__",
)

PERMISSION_UI = "ui"
KNOWN_PERMISSIONS = (
    PERMISSION_UI,
    "messages.read",
    "messages.send",
    "network",
    "files",
    "intents",
    "settings",
    "hooks",
    "native",
)

_ID_PATTERN = re.compile(r"^[A-Za-z][A-Za-z0-9_-]{1,31}$")

_metadata_cache: Dict[str, Any] = {}


def _file_key(path: str):
    try:
        st = os.stat(path)
    except OSError:
        return None
    return (st.st_size, st.st_mtime_ns)


def read_source(path: str) -> str:
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def _extract_constants(path: str) -> Dict[str, Any]:
    source = read_source(path)
    try:
        tree = ast.parse(source, filename=path)
    except SyntaxError as e:
        raise PluginMetadataError(f"plugin file {path!r} has a syntax error: {e}")
    except OSError as e:
        raise PluginMetadataError(f"cannot read plugin file {path!r}: {e}")
    constants: Dict[str, Any] = {}
    for node in tree.body:
        target = None
        value_node = None
        if isinstance(node, ast.Assign) and len(node.targets) == 1 \
                and isinstance(node.targets[0], ast.Name):
            target = node.targets[0].id
            value_node = node.value
        elif isinstance(node, ast.AnnAssign) and isinstance(node.target, ast.Name) \
                and node.value is not None:
            target = node.target.id
            value_node = node.value
        if target is None or target not in _METADATA_KEYS or target in constants:
            continue
        try:
            constants[target] = ast.literal_eval(value_node)
        except (ValueError, SyntaxError, TypeError, MemoryError):
            pass
    return constants


def _validate_plugin_id(plugin_id: Any) -> str:
    if plugin_id is None:
        raise PluginMetadataError("plugin metadata is missing the required __id__ constant")
    if not isinstance(plugin_id, str):
        raise PluginMetadataError(f"__id__ must be a string, got {type(plugin_id).__name__}")
    if not _ID_PATTERN.match(plugin_id):
        raise PluginMetadataError(
            f"invalid __id__ {plugin_id!r}: must be 2-32 characters, start with a letter "
            "and contain only Latin letters, digits, '_' or '-'")
    if plugin_id in sys.stdlib_module_names:
        raise PluginMetadataError(
            f"__id__ {plugin_id!r} collides with a Python standard library module name")
    return plugin_id


def _split_bare(text: str) -> list:
    parts = []
    for chunk in text.replace("\n", ",").split(","):
        chunk = chunk.strip()
        if not chunk:
            continue
        if parts and chunk[0] in "<>=!~":
            parts[-1] = parts[-1] + "," + chunk
        else:
            parts.append(chunk)
    return parts


def _validate_permissions(declared: Any) -> list:
    if isinstance(declared, str):
        declared = _split_bare(declared)
    if not isinstance(declared, (list, tuple, set, frozenset)):
        raise PluginMetadataError("__permissions__ must be a list of permission keys")
    out = []
    for item in declared:
        if not isinstance(item, str):
            raise PluginMetadataError(
                f"__permissions__ entries must be strings, got {type(item).__name__}")
        key = item.strip()
        if key not in KNOWN_PERMISSIONS:
            raise PluginMetadataError(
                f"unknown permission {item!r} in __permissions__; known keys: "
                + ", ".join(KNOWN_PERMISSIONS))
        if key not in out:
            out.append(key)
    return out


def _read_metadata_uncached(path: str) -> Dict[str, Any]:
    constants = _extract_constants(path)
    plugin_id = _validate_plugin_id(constants.get("__id__"))
    name = constants.get("__name__")
    if name is not None and (not isinstance(name, str) or not name.strip()):
        name = plugin_id
    version = constants.get("__version__", "1.0")
    if not isinstance(version, str):
        version = str(version)
    app_version: Optional[str] = constants.get("__app_version__")
    min_version = constants.get("__min_version__")
    if app_version is None and min_version is not None:
        app_version = f">={min_version}"
    requirements = constants.get("__requirements__") or []
    if isinstance(requirements, str):
        requirements = _split_bare(requirements)
    if not isinstance(requirements, (list, tuple)) \
            or not all(isinstance(item, str) for item in requirements):
        requirements = []
    declared_permissions = "__permissions__" in constants
    permissions = []
    if declared_permissions:
        try:
            permissions = _validate_permissions(constants.get("__permissions__") or [])
        except PluginMetadataError:
            permissions = []
    return {
        "id": plugin_id,
        "name": name if name else plugin_id,
        "description": constants.get("__description__") or "",
        "author": constants.get("__author__") or "",
        "version": version,
        "icon": constants.get("__icon__"),
        "app_version": app_version,
        "sdk_version": constants.get("__sdk_version__"),
        "beta": bool(constants.get("__beta__", False)),
        "requirements": list(requirements),
        "permissions": permissions,
        "permissions_declared": declared_permissions,
        "desktop": bool(constants.get("__desktop__", False)),
        "ported_from": str(constants.get("__ported_from__") or ""),
        "desktop_os": str(constants.get("__desktop_os__") or ""),
    }


def read_metadata(path: str) -> Dict[str, Any]:
    key = _file_key(path)
    cached = _metadata_cache.get(path)
    if key is not None and cached is not None and cached[0] == key:
        return copy.deepcopy(cached[1])
    meta = _read_metadata_uncached(path)
    if key is not None:
        _metadata_cache[path] = (key, copy.deepcopy(meta))
    return meta
