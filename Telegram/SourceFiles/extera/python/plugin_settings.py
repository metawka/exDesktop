import json as _json
import os as _os
import threading as _threading

_lock = _threading.RLock()
_settings_cache = {}
_settings_dir = ""
_notifier = None


def configure(settings_dir):
    global _settings_dir
    _settings_dir = settings_dir


def set_notifier(fn):
    global _notifier
    _notifier = fn


def _path(plugin_id):
    return _os.path.join(_settings_dir, plugin_id + ".json")


def _decode(raw, default=None):
    if raw is None:
        return default
    try:
        return _json.loads(raw)
    except (ValueError, TypeError):
        return default


_TRUE_WORDS = ("true", "1", "yes", "on")
_FALSE_WORDS = ("false", "0", "no", "off", "")


def coerce_value(value, default):
    if value is None or default is None:
        return value
    if isinstance(default, bool):
        if isinstance(value, bool):
            return value
        if isinstance(value, str):
            word = value.strip().lower()
            if word in _TRUE_WORDS:
                return True
            if word in _FALSE_WORDS:
                return False
            return default
        if isinstance(value, (int, float)):
            return bool(value)
        return default
    if isinstance(value, str) and isinstance(default, (int, float)):
        try:
            return type(default)(value.strip())
        except ValueError:
            return default
    return value


def decode_values(raw):
    data = _decode(raw, {})
    if not isinstance(data, dict):
        return {}
    return {key: _decode(value, value) if isinstance(value, str) else value
            for key, value in data.items()}


def encode_values(values):
    return {str(key): _json.dumps(value, ensure_ascii=False)
            for key, value in dict(values).items()}


def _load_plugin(plugin_id):
    data = {}
    try:
        with open(_path(plugin_id), encoding="utf-8") as handle:
            data = _json.load(handle)
    except Exception:
        data = {}
    if not isinstance(data, dict):
        data = {}
    _settings_cache[plugin_id] = data
    return data


def _cached(plugin_id):
    cached = _settings_cache.get(plugin_id)
    if cached is None:
        cached = _load_plugin(plugin_id)
    return cached


def get_setting(plugin_id, key, default=None):
    with _lock:
        return coerce_value(_cached(plugin_id).get(key, default), default)


def set_setting(plugin_id, key, value, reload_settings=False):
    with _lock:
        _cached(plugin_id)[key] = value
        _save_settings_to_file()
    if reload_settings and _notifier is not None:
        try:
            _notifier(plugin_id)
        except Exception:
            pass


def get_settings(plugin_id):
    with _lock:
        return dict(_cached(plugin_id))


def remove_setting(plugin_id, key):
    with _lock:
        if plugin_id in _settings_cache:
            _settings_cache[plugin_id].pop(key, None)
            _save_settings_to_file()


def invalidate(plugin_id=None):
    with _lock:
        if plugin_id is None:
            _settings_cache.clear()
        else:
            _settings_cache.pop(plugin_id, None)


def _save_settings_to_file():
    with _lock:
        try:
            _os.makedirs(_settings_dir, exist_ok=True)
        except Exception:
            return
        for plugin_id, values in list(_settings_cache.items()):
            try:
                with open(_path(plugin_id), "w", encoding="utf-8") as handle:
                    _json.dump(values, handle, ensure_ascii=False)
            except Exception:
                continue
