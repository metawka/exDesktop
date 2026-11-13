import os
import secrets
import threading

_pending = {}
_lock = threading.Lock()

ACTION_GET_CONTENT = "android.intent.action.GET_CONTENT"
ACTION_OPEN_DOCUMENT = "android.intent.action.OPEN_DOCUMENT"
ACTION_PICK = "android.intent.action.PICK"
PICK_ACTIONS = (ACTION_GET_CONTENT, ACTION_OPEN_DOCUMENT, ACTION_PICK, "android.intent.action.OPEN_DOCUMENT_TREE")
RESULT_OK = -1
RESULT_CANCELED = 0


# Filters

def _filter_for(mime):
    mime = str(mime or "*/*").lower()
    groups = {
        "image/": "Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp)",
        "video/": "Video (*.mp4 *.mkv *.mov *.webm *.avi)",
        "audio/": "Audio (*.mp3 *.ogg *.wav *.flac *.m4a *.opus)",
        "text/": "Text (*.txt *.json *.csv *.md *.log)",
    }
    for prefix, text in groups.items():
        if mime.startswith(prefix):
            return text + ";;All files (*.*)"
    if "zip" in mime:
        return "Archives (*.zip);;All files (*.*)"
    if "json" in mime:
        return "JSON (*.json);;All files (*.*)"
    return ""


# Streams

class InputStream:
    _java_name = "java.io.InputStream"

    def __init__(self, path):
        self._file = open(path, "rb")

    def read(self, buffer=None, offset=0, length=None):
        if buffer is None:
            data = self._file.read(1)
            return data[0] if data else -1
        size = len(buffer) - int(offset or 0) if length is None else int(length)
        data = self._file.read(max(size, 0))
        if not data:
            return -1
        for index, value in enumerate(data):
            buffer[int(offset or 0) + index] = value if isinstance(buffer, (bytearray, memoryview)) else (
                value - 256 if value > 127 else value)
        return len(data)

    def readAllBytes(self):
        return self._file.read()

    def readBytes(self):
        return self._file.read()

    def available(self):
        current = self._file.tell()
        self._file.seek(0, 2)
        end = self._file.tell()
        self._file.seek(current)
        return end - current

    def skip(self, count):
        self._file.seek(int(count), 1)
        return int(count)

    def close(self):
        try:
            self._file.close()
        except Exception:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


class ContentResolver:
    _java_name = "android.content.ContentResolver"

    def openInputStream(self, uri):
        return InputStream(uri_path(uri))

    def openFileDescriptor(self, uri, mode="r"):
        return _Descriptor(uri_path(uri))

    def getType(self, uri):
        import mimetypes
        return mimetypes.guess_type(uri_path(uri))[0] or "application/octet-stream"

    def query(self, uri, *args):
        return _Cursor(uri_path(uri))

    def takePersistableUriPermission(self, *args):
        return None


class _Descriptor:
    def __init__(self, path):
        self._path = path

    def getStatSize(self):
        return os.path.getsize(self._path)

    def getFileDescriptor(self):
        return self._path

    def close(self):
        return None


class _Cursor:
    def __init__(self, path):
        self._path = path
        self._position = -1

    def moveToFirst(self):
        self._position = 0
        return True

    def moveToNext(self):
        self._position += 1
        return self._position == 0

    def getColumnIndex(self, name):
        return 1 if "size" in str(name).lower() else 0

    def getColumnIndexOrThrow(self, name):
        return self.getColumnIndex(name)

    def getString(self, index):
        return os.path.basename(self._path) if index == 0 else str(os.path.getsize(self._path))

    def getLong(self, index):
        return os.path.getsize(self._path)

    def getCount(self):
        return 1

    def close(self):
        return None


def uri_path(uri):
    value = getattr(uri, "_value", None)
    if value is None:
        value = str(uri)
    value = str(value)
    if value.startswith("file://"):
        value = value[7:]
        if value.startswith("/") and len(value) > 2 and value[2] == ":":
            value = value[1:]
    return value


# Pick

def pick(callback, title="", filters="", multiple=False, plugin_id=None):
    import _extera
    if plugin_id is None:
        try:
            import extera_host
            plugin_id = extera_host.current_plugin_id()
        except Exception:
            plugin_id = None
    token = secrets.token_hex(16)
    with _lock:
        _pending[token] = (plugin_id, callback)
    _extera.pick_file(token, str(title or ""), str(filters or ""), 1 if multiple else 0)
    return token


def picked(token, paths):
    with _lock:
        entry = _pending.pop(str(token or ""), None)
    if entry is None:
        return False
    plugin_id, callback = entry
    paths = [os.path.normpath(str(p)) for p in (paths or []) if p]
    if callback is None:
        return True
    try:
        import extera_host
        context = extera_host._plugin_context(plugin_id) if plugin_id else None
    except Exception:
        context = None
    if context is not None:
        with context:
            callback(paths)
    else:
        callback(paths)
    return True


def start_for_result(intent, request_code, owner=None):
    action = str(getattr(intent, "action", "") or "")
    if action not in PICK_ACTIONS:
        return False
    import exd_java
    multiple = bool(getattr(intent, "_extras", {}).get("android.intent.extra.ALLOW_MULTIPLE"))
    mime = getattr(intent, "_type", None)

    def done(paths):
        _deliver_result(int(request_code or 0), paths, owner)

    pick(done, "", _filter_for(mime), multiple)
    return True


def _deliver_result(request_code, paths, owner):
    import _compat
    import base_plugin
    import extera_host
    intent = None
    if paths:
        intent = _compat._Intent(None, _compat._Uri("file:///" + paths[0].replace("\\", "/")))
        if len(paths) > 1:
            object.__setattr__(intent, "_clip", [_compat._Uri("file:///" + p.replace("\\", "/")) for p in paths])
    result_code = RESULT_OK if paths else RESULT_CANCELED
    for record in list(extera_host._records.values()):
        instance = record.instance
        if instance is None:
            continue
        handlers = list(instance.__dict__.get("_exd_virtual_activity_result") or ())
        for handler in handlers:
            param = base_plugin.VirtualParam([request_code, result_code, intent],
                                             owner or _compat._LaunchActivity.instance,
                                             _compat.Missing("LaunchActivity.onActivityResult"))
            try:
                with extera_host._plugin_context(record.id):
                    before = getattr(handler, "before_hooked_method", None)
                    after = getattr(handler, "after_hooked_method", None)
                    replace = getattr(handler, "replace_hooked_method", None)
                    if callable(replace):
                        replace(param)
                    else:
                        if callable(before):
                            before(param)
                        if callable(after) and not param.returnEarly:
                            after(param)
            except Exception:
                import traceback
                extera_host.log("[%s] activity result failed:\n%s" % (record.id, traceback.format_exc(limit=4)))
