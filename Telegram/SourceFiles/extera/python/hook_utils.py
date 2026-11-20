from typing import Any


def find_class(name: str) -> Any:
    import _compat
    import exd_java
    found = exd_java.lookup(str(name))
    if found is not None:
        return found
    return _compat.Missing(str(name))


def _as_class(obj):
    import exd_java
    if isinstance(obj, exd_java.JavaClassInfo) and isinstance(obj._class, type):
        return obj._class
    return obj


def get_private_field(obj: Any, field_name: str) -> Any:
    import exd_xposed
    try:
        return exd_xposed.get_field(obj, field_name)
    except Exception:
        return None


def set_private_field(obj: Any, field_name: str, new_value: Any) -> bool:
    import exd_xposed
    if obj is None:
        return False
    try:
        exd_xposed.set_field(obj, field_name, new_value)
        return True
    except Exception:
        return False


def get_static_private_field(clazz: Any, field_name: str) -> Any:
    if isinstance(clazz, str):
        clazz = find_class(clazz)
    return get_private_field(_as_class(clazz), field_name)


def set_static_private_field(clazz: Any, field_name: str, new_value: Any) -> bool:
    if isinstance(clazz, str):
        clazz = find_class(clazz)
    return set_private_field(_as_class(clazz), field_name, new_value)


get_field = get_private_field
set_field = set_private_field


def jarray(kind):
    import _compat
    return lambda values: _compat._jarray(kind, values)


def __getattr__(name):
    if name in ("JavaClass", "JavaObject"):
        import exd_java
        return exd_java.JavaClassInfo if name == "JavaClass" else exd_java.JavaObject
    if name in ("MethodHook", "MethodReplacement", "XposedHook", "HookResult", "HookStrategy"):
        import base_plugin
        return getattr(base_plugin, name)
    raise AttributeError(name)
