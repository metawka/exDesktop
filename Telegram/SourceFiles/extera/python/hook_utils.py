from typing import Any


def find_class(name: str) -> Any:
    import _compat
    import exd_java
    found = exd_java.lookup(str(name))
    if found is not None:
        return found
    return _compat.Missing(str(name))


def _as_class(obj):
    return obj


def get_private_field(obj: Any, name: str) -> Any:
    return None


def set_private_field(obj: Any, name: str, value: Any) -> bool:
    return False


def get_static_private_field(clazz: Any, name: str) -> Any:
    return None


def set_static_private_field(clazz: Any, name: str, value: Any) -> bool:
    return False


get_field = get_private_field
set_field = set_private_field
