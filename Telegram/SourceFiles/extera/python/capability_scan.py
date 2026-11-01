import ast
import base64
import json
import re
import struct
import zipfile
import zlib
from typing import Dict, List

PERM_MESSAGES_READ = "messages.read"
PERM_MESSAGES_SEND = "messages.send"
PERM_NETWORK = "network"
PERM_FILES = "files"
PERM_INTENTS = "intents"
PERM_SETTINGS = "settings"
PERM_HOOKS = "hooks"
PERM_NATIVE = "native"

_MARKERS = (
    ("import requests", PERM_NETWORK, "requests"),
    ("urllib.request", PERM_NETWORK, "urllib"),
    ("http.client", PERM_NETWORK, "http.client"),
    ("import socket", PERM_NETWORK, "socket"),
    ("websocket", PERM_NETWORK, "websocket"),
    ("java.net", PERM_NETWORK, "java.net"),
    ("HttpURLConnection", PERM_NETWORK, "HttpURLConnection"),
    ("OkHttpClient", PERM_NETWORK, "okhttp"),
    ("WebView", PERM_NETWORK, "WebView"),
    ("DownloadManager", PERM_NETWORK, "DownloadManager"),
    ("loadHttpFile", PERM_NETWORK, "loadHttpFile"),
    ("setDataSource", PERM_NETWORK, "setDataSource"),
    ("MessagesStorage", PERM_MESSAGES_READ, "MessagesStorage"),
    ("SQLiteDatabase", PERM_MESSAGES_READ, "MessagesStorage"),
    ("queryFinalized", PERM_MESSAGES_READ, "SQLite"),
    ("on_update", PERM_MESSAGES_READ, "on_update"),
    ("add_request_hook", PERM_MESSAGES_READ, "request hooks"),
    ("send_request(", PERM_NETWORK, "send_request"),
    ("send_request(", PERM_MESSAGES_READ, "send_request"),
    ("sendRequest(", PERM_MESSAGES_READ, "sendRequest"),
    ("get_messages", PERM_MESSAGES_READ, "getMessages"),
    ("getMessages", PERM_MESSAGES_READ, "getMessages"),
    ("SendMessagesHelper", PERM_MESSAGES_SEND, "SendMessagesHelper"),
    ("send_message(", PERM_MESSAGES_SEND, "send_message"),
    ("send_text(", PERM_MESSAGES_SEND, "send_text"),
    ("send_photo(", PERM_MESSAGES_SEND, "send_photo"),
    ("send_video(", PERM_MESSAGES_SEND, "send_video"),
    ("send_document(", PERM_MESSAGES_SEND, "send_document"),
    ("send_audio(", PERM_MESSAGES_SEND, "send_audio"),
    ("edit_message(", PERM_MESSAGES_SEND, "edit_message"),
    ("get_send_messages_helper", PERM_MESSAGES_SEND, "SendMessagesHelper"),
    ("prepareSending", PERM_MESSAGES_SEND, "prepareSending"),
    ("sendSticker", PERM_MESSAGES_SEND, "sendSticker"),
    ("sendGame", PERM_MESSAGES_SEND, "sendGame"),
    ("sendCurrentLocation", PERM_MESSAGES_SEND, "sendCurrentLocation"),
    ("retrySendMessage", PERM_MESSAGES_SEND, "retrySendMessage"),
    ("editMessage(", PERM_MESSAGES_SEND, "editMessage"),
    ("on_send_message", PERM_MESSAGES_SEND, "on_send_message"),
    ("TL_messages_send", PERM_MESSAGES_SEND, "TL_messages_send"),
    ("TL_messages_edit", PERM_MESSAGES_SEND, "TL_messages_edit"),
    ("TL_messages_delete", PERM_MESSAGES_SEND, "TL_messages_delete"),
    ("ConnectionsManager", PERM_MESSAGES_SEND, "ConnectionsManager"),
    ("java.io.File", PERM_FILES, "java.io.File"),
    ("FileOutputStream", PERM_FILES, "FileOutputStream"),
    ("ContentResolver", PERM_FILES, "ContentResolver"),
    ("MediaStore", PERM_FILES, "MediaStore"),
    ("import shutil", PERM_FILES, "shutil"),
    ("os.remove", PERM_FILES, "os.remove"),
    ("os.listdir", PERM_FILES, "os.listdir"),
    ("sqlite3", PERM_FILES, "sqlite3"),
    ("get_files_dir", PERM_FILES, "get_files_dir"),
    ("get_cache_dir", PERM_FILES, "get_cache_dir"),
    ("get_images_dir", PERM_FILES, "get_images_dir"),
    ("get_videos_dir", PERM_FILES, "get_videos_dir"),
    ("get_audios_dir", PERM_FILES, "get_audios_dir"),
    ("get_documents_dir", PERM_FILES, "get_documents_dir"),
    ("getFilesDir", PERM_FILES, "getFilesDir"),
    ("getCacheDir", PERM_FILES, "getCacheDir"),
    ("getExternalFilesDir", PERM_FILES, "getExternalFilesDir"),
    ("getExternalStorage", PERM_FILES, "getExternalStorage"),
    ("startActivity", PERM_INTENTS, "startActivity"),
    ("android.content.Intent", PERM_INTENTS, "Intent"),
    ("register_intent_handler", PERM_INTENTS, "intent hooks"),
    ("NekoConfig", PERM_SETTINGS, "app config"),
    ("NaConfig", PERM_SETTINGS, "app config"),
    ("ExteraConfig", PERM_SETTINGS, "app config"),
    ("MethodHook", PERM_HOOKS, "Xposed"),
    ("hook_method", PERM_HOOKS, "Xposed"),
    ("hook_all_methods", PERM_HOOKS, "Xposed"),
    ("hook_all_constructors", PERM_HOOKS, "Xposed"),
    ("XposedBridge", PERM_HOOKS, "Xposed"),
    ("InMemoryDexClassLoader", PERM_HOOKS, "DexClassLoader"),
    ("DexClassLoader", PERM_HOOKS, "DexClassLoader"),
    ("generate_proxy_class", PERM_HOOKS, "class proxy"),
    ("java_subclass", PERM_HOOKS, "class proxy"),
    ("joverride", PERM_HOOKS, "class proxy"),
    ("allocate_instance", PERM_HOOKS, "class proxy"),
    ("deoptimize", PERM_HOOKS, "deoptimize"),
    ("TYPE_APPLICATION_OVERLAY", PERM_HOOKS, "overlay window"),
    ("TYPE_SYSTEM_ALERT", PERM_HOOKS, "overlay window"),
    ("TYPE_SYSTEM_OVERLAY", PERM_HOOKS, "overlay window"),
    ("LayoutParams.TYPE_PHONE", PERM_HOOKS, "overlay window"),
    ("import ctypes", PERM_NATIVE, "ctypes"),
    ("ctypes.CDLL", PERM_NATIVE, "ctypes"),
    ("CDLL(", PERM_NATIVE, "ctypes"),
    ("loadLibrary", PERM_NATIVE, "loadLibrary"),
    (".so\"", PERM_NATIVE, "native library"),
    (".so'", PERM_NATIVE, "native library"),
)

KEY_OBFUSCATION = "obfuscation"
KEY_DEX = "dex"

_DEX_BLOCK = re.compile(r"^[ \t]*#[ \t]*__DEX_BEGIN__[ \t\r]*$(.*?)^[ \t]*#[ \t]*__DEX_END__[ \t\r]*$",
                        re.M | re.S)
_DEX_LITERAL = re.compile(r"(\"\"\"|'''|\"|')([A-Za-z0-9+/=\s]{512,})\1")
_DEX_LITERAL_PREFIXES = ("ZGV4C", "eJ", "eN", "eA", "eF")
_DEX_LOADERS = ("InMemoryDexClassLoader", "DexClassLoader", "PathClassLoader")
_MAX_DEX_BYTES = 8 * 1024 * 1024
_DEX_CLASS_LIMIT = 6


def _uleb128(data: bytes, offset: int):
    result = 0
    shift = 0
    while True:
        byte = data[offset]
        offset += 1
        result |= (byte & 0x7F) << shift
        if byte < 0x80:
            return result, offset
        shift += 7


def _dex_strings(data: bytes) -> List[str]:
    count, table = struct.unpack_from("<II", data, 0x38)
    strings = []
    for index in range(count):
        offset, = struct.unpack_from("<I", data, table + index * 4)
        _, start = _uleb128(data, offset)
        end = data.index(b"\0", start)
        strings.append(data[start:end].decode("utf-8", errors="replace"))
    return strings


def _dex_symbols(data: bytes):
    strings = _dex_strings(data)
    type_count, type_table = struct.unpack_from("<II", data, 0x40)
    types = [strings[struct.unpack_from("<I", data, type_table + i * 4)[0]]
             for i in range(type_count)]
    method_count, method_table = struct.unpack_from("<II", data, 0x58)
    methods = []
    for i in range(method_count):
        class_index, _, name_index = struct.unpack_from("<HHI", data, method_table + i * 8)
        methods.append((types[class_index], strings[name_index]))
    class_count, class_table = struct.unpack_from("<II", data, 0x60)
    defined = {types[struct.unpack_from("<I", data, class_table + i * 32)[0]]
               for i in range(class_count)}
    return defined, types, methods


def _dotted(descriptor: str) -> str:
    if descriptor.startswith("L") and descriptor.endswith(";"):
        return descriptor[1:-1].replace("/", ".")
    return descriptor


def _decode_dex_block(body: str):
    text = "".join(line.strip().lstrip("#").strip() for line in body.splitlines())
    raw = base64.b64decode(text)
    try:
        raw = zlib.decompress(raw)
    except zlib.error:
        pass
    if not raw.startswith(b"dex\n") or len(raw) > _MAX_DEX_BYTES:
        return None
    return raw


def _decode_dex_literal(body: str):
    text = "".join(body.split())
    if not text.startswith(_DEX_LITERAL_PREFIXES) or len(text) > _MAX_DEX_BYTES * 2:
        return None
    try:
        raw = base64.b64decode(text)
    except Exception:
        return None
    if not raw.startswith(b"dex\n"):
        try:
            if not zlib.decompressobj().decompress(raw, 4).startswith(b"dex\n"):
                return None
            raw = zlib.decompressobj().decompress(raw, _MAX_DEX_BYTES + 1)
        except zlib.error:
            return None
    if len(raw) > _MAX_DEX_BYTES:
        return None
    return raw


def _scan_embedded_dex(source: str) -> Dict[str, List[str]]:
    payloads = []
    spans = []
    for block in _DEX_BLOCK.finditer(source):
        try:
            payloads.append(_decode_dex_block(block.group(1)))
        except Exception:
            payloads.append(None)
        spans.append(block.span())
    for literal in _DEX_LITERAL.finditer(source):
        raw = _decode_dex_literal(literal.group(2))
        if raw is not None:
            payloads.append(raw)
            spans.append(literal.span(2))
    if not payloads:
        return {}
    parts = []
    cursor = 0
    for start, end in sorted(spans):
        if start >= cursor:
            parts.append(source[cursor:start])
            cursor = end
    parts.append(source[cursor:])
    outside = "".join(parts)
    loaded = any(loader in outside for loader in _DEX_LOADERS)
    summary: List[str] = []
    found: Dict[str, List[str]] = {}
    total = 0
    defined_all: List[str] = []
    for raw in payloads:
        if raw is None:
            _note_opaque(found, "unreadable DEX block")
            continue
        total += len(raw)
        try:
            defined, types, methods = _dex_symbols(raw)
        except Exception:
            _note_opaque(found, "unreadable DEX block")
            continue
        defined_all.extend(sorted(_dotted(name) for name in defined))
        if not loaded:
            _note_opaque(found, "embedded DEX binary")
            continue
        external = [_dotted(name) for name in types if name not in defined]
        calls = [f"{_dotted(owner)}.{name}" for owner, name in methods if owner not in defined]
        text = "\n".join(external + calls)
        for marker, permission, evidence in _MARKERS:
            label = f"DEX: {evidence}"
            if marker in text and label not in found.setdefault(permission, []):
                found[permission].append(label)
        if any(".exec" in call for call in calls if call.startswith("java.lang.Runtime")) \
                or "java.lang.ProcessBuilder" in external:
            bucket = found.setdefault(PERM_NATIVE, [])
            if "DEX: Runtime.exec" not in bucket:
                bucket.append("DEX: Runtime.exec")
    if total == 0 and not found:
        return {}
    summary.append(f"size:{total}")
    summary.append("loaded:" + ("yes" if loaded else "no"))
    for name in defined_all[:_DEX_CLASS_LIMIT]:
        summary.append(f"class:{name}")
    if len(defined_all) > _DEX_CLASS_LIMIT:
        summary.append(f"more:{len(defined_all) - _DEX_CLASS_LIMIT}")
    found[KEY_DEX] = summary
    return {perm: names for perm, names in found.items() if names}


_OBF_PACKERS = (
    ("__pyarmor__", "pyarmor"),
    ("pytransform", "pyarmor"),
    ("pyarmor_runtime", "pyarmor"),
    ("PYARMOR", "pyarmor"),
    ("pyminifier", "pyminifier"),
    ("Sourcedefender", "sourcedefender"),
    ("sourcedefender", "sourcedefender"),
)

_OBF_DECODERS = (
    ("marshal.loads(", "marshal"),
    ("zlib.decompress(", "zlib"),
    ("lzma.decompress(", "lzma"),
    ("bz2.decompress(", "bz2"),
    ("b64decode(", "base64"),
    ("b85decode(", "base85"),
    ("a85decode(", "base85"),
    ("b32decode(", "base32"),
    ("b16decode(", "base16"),
    ("unhexlify(", "unhexlify"),
    ("codecs.decode(", "codecs"),
)

_OBF_EXEC = re.compile(r"(?<![\w.])(?:exec|eval|compile)\s*\(")
_OBF_NAME = re.compile(r"\b_[A-Za-z]{0,2}[0-9a-z]{16,}\b")
_OBF_HEX = re.compile(r"\\x[0-9a-fA-F]{2}")
_OBF_NAME_LIMIT = 8
_OBF_LINE_LIMIT = 2000
_OBF_HEX_LIMIT = 50


_DECODER_CALLS = {
    "b64decode": "base64", "b85decode": "base85", "a85decode": "base85",
    "b32decode": "base32", "b16decode": "base16", "unhexlify": "unhexlify",
    "decompress": "zlib", "loads": "marshal", "fromhex": "hex",
    "decode": "codecs", "frombytes": "bytes",
}


def _call_name(node) -> str:
    func = getattr(node, "func", None)
    if isinstance(func, ast.Name):
        return func.id
    if isinstance(func, ast.Attribute):
        return func.attr
    return ""


def _exec_of_decoded(source: str) -> List[str]:
    try:
        tree = ast.parse(source)
    except Exception:
        return []
    result: List[str] = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        if not isinstance(node.func, ast.Name)                 or node.func.id not in ("exec", "eval", "compile"):
            continue
        for arg in node.args[:1]:
            for inner in ast.walk(arg):
                if not isinstance(inner, ast.Call):
                    continue
                name = _call_name(inner)
                owner = getattr(getattr(inner.func, "value", None), "id", "")
                if name == "decode" and owner != "codecs":
                    continue
                if name == "loads" and owner != "marshal":
                    continue
                kind = _DECODER_CALLS.get(name)
                if kind and ("exec+" + kind) not in result:
                    result.append("exec+" + kind)
    if "marshal.loads" in source and "types.FunctionType" in source:
        result.append("marshal code")
    return result


def _detect_obfuscation(source: str) -> List[str]:
    strong: List[str] = []
    weak: List[str] = []

    def note(bucket: List[str], name: str) -> None:
        if name not in bucket:
            bucket.append(name)

    for marker, evidence in _OBF_PACKERS:
        if marker in source:
            note(strong, evidence)
    for evidence in _exec_of_decoded(source):
        note(strong, evidence)
    names = set(_OBF_NAME.findall(source))
    if len(names) >= _OBF_NAME_LIMIT:
        note(strong, "mangled names")
    lines = source.splitlines() or [""]
    if max(len(line) for line in lines) >= _OBF_LINE_LIMIT:
        note(weak, "long lines")
    if len(_OBF_HEX.findall(source)) >= _OBF_HEX_LIMIT:
        note(weak, "escaped strings")
    if strong:
        return strong + weak
    if len(weak) >= 2:
        return weak
    return []


_MAX_SOURCE_BYTES = 4 * 1024 * 1024
_MAX_ARCHIVE_BYTES = 16 * 1024 * 1024
_SOURCE_MEMBER_SUFFIXES = (".py", ".pyc")


_IMPORT_PERMISSIONS = {
    "requests": (PERM_NETWORK, "requests"),
    "urllib": (PERM_NETWORK, "urllib"),
    "urllib3": (PERM_NETWORK, "urllib3"),
    "http": (PERM_NETWORK, "http"),
    "socket": (PERM_NETWORK, "socket"),
    "ssl": (PERM_NETWORK, "ssl"),
    "aiohttp": (PERM_NETWORK, "aiohttp"),
    "httpx": (PERM_NETWORK, "httpx"),
    "websocket": (PERM_NETWORK, "websocket"),
    "websockets": (PERM_NETWORK, "websockets"),
    "ftplib": (PERM_NETWORK, "ftplib"),
    "smtplib": (PERM_NETWORK, "smtplib"),
    "shutil": (PERM_FILES, "shutil"),
    "pathlib": (PERM_FILES, "pathlib"),
    "glob": (PERM_FILES, "glob"),
    "sqlite3": (PERM_FILES, "sqlite3"),
    "tempfile": (PERM_FILES, "tempfile"),
    "subprocess": (PERM_NATIVE, "subprocess"),
    "ctypes": (PERM_NATIVE, "ctypes"),
    "cffi": (PERM_NATIVE, "cffi"),
    "multiprocessing": (PERM_NATIVE, "multiprocessing"),
    "winreg": (PERM_NATIVE, "winreg"),
}


def _scan_imports(source: str) -> Dict[str, List[str]]:
    found: Dict[str, List[str]] = {}
    try:
        tree = ast.parse(source)
    except Exception:
        return found
    for node in ast.walk(tree):
        names = []
        if isinstance(node, ast.Import):
            names = [alias.name for alias in node.names]
        elif isinstance(node, ast.ImportFrom) and node.module and not node.level:
            names = [node.module]
        elif isinstance(node, ast.Call) and isinstance(node.func, ast.Name)                 and node.func.id == "__import__" and node.args                 and isinstance(node.args[0], ast.Constant):
            names = [str(node.args[0].value)]
        for name in names:
            entry = _IMPORT_PERMISSIONS.get(name.split(".")[0])
            if entry:
                bucket = found.setdefault(entry[0], [])
                if entry[1] not in bucket:
                    bucket.append(entry[1])
    return found


def _merge(target: Dict[str, List[str]], addition: Dict[str, List[str]]) -> None:
    for permission, evidence in addition.items():
        bucket = target.setdefault(permission, [])
        for item in evidence:
            if item not in bucket:
                bucket.append(item)


def _scan_source(source: str) -> Dict[str, List[str]]:
    found: Dict[str, List[str]] = {}
    for marker, permission, evidence in _MARKERS:
        if marker in source and evidence not in found.setdefault(permission, []):
            found[permission].append(evidence)
    _merge(found, _scan_imports(source))
    obfuscation = _detect_obfuscation(source)
    if obfuscation:
        found[KEY_OBFUSCATION] = obfuscation
    _merge(found, _scan_embedded_dex(source))
    return {perm: names for perm, names in found.items() if names}


def _note_opaque(found: Dict[str, List[str]], evidence: str) -> None:
    bucket = found.setdefault(KEY_OBFUSCATION, [])
    if evidence not in bucket:
        bucket.append(evidence)


def _is_bytecode(name: str, raw: bytes) -> bool:
    if name.endswith(".pyc"):
        return True
    if len(raw) < 4 or raw[2:4] != b"\r\n":
        return False
    try:
        raw.decode("utf-8")
    except UnicodeDecodeError:
        return True
    return False


def _scan_archive(path: str) -> Dict[str, List[str]]:
    found: Dict[str, List[str]] = {}
    budget = _MAX_ARCHIVE_BYTES
    with zipfile.ZipFile(path) as archive:
        members = [info for info in archive.infolist()
                   if not info.is_dir()
                   and info.filename.endswith(_SOURCE_MEMBER_SUFFIXES)]
        for index, info in enumerate(members):
            if budget <= 0:
                _note_opaque(found, f"unscanned files ({len(members) - index})")
                break
            limit = min(_MAX_SOURCE_BYTES, budget)
            try:
                with archive.open(info) as handle:
                    raw = handle.read(limit + 1)
            except Exception:
                continue
            truncated = len(raw) > limit
            raw = raw[:limit]
            budget -= len(raw)
            if _is_bytecode(info.filename, raw):
                _note_opaque(found, f"compiled bytecode ({info.filename})")
                continue
            if truncated:
                _note_opaque(found, f"truncated source ({info.filename})")
            _merge(found, _scan_source(raw.decode("utf-8", errors="replace")))
    return {perm: names for perm, names in found.items() if names}


def scan(path: str) -> Dict[str, List[str]]:
    try:
        if zipfile.is_zipfile(path):
            return _scan_archive(path)
        with open(path, "rb") as handle:
            raw = handle.read(_MAX_SOURCE_BYTES + 1)
    except Exception:
        return {}
    truncated = len(raw) > _MAX_SOURCE_BYTES
    raw = raw[:_MAX_SOURCE_BYTES]
    if _is_bytecode(path, raw):
        return {KEY_OBFUSCATION: ["compiled bytecode"]}
    found = _scan_source(raw.decode("utf-8", errors="replace"))
    if truncated:
        _note_opaque(found, "truncated source")
    return found


# Locations

_IMPORT_LINE = re.compile(r"^\s*(?:from\s+([\w.]+)\s+import|import\s+([\w.]+))")
_LOCATION_LIMIT = 4
_LOCATION_TEXT = 160


def _read_sources(path: str):
    if zipfile.is_zipfile(path):
        results = []
        with zipfile.ZipFile(path) as archive:
            for info in archive.infolist():
                if info.filename.endswith(".py") and not info.is_dir():
                    try:
                        with archive.open(info) as handle:
                            text = handle.read(_MAX_SOURCE_BYTES).decode(
                                "utf-8", errors="replace")
                            results.append(text)
                    except Exception:
                        continue
        return results
    try:
        with open(path, "rb") as handle:
            return [handle.read(_MAX_SOURCE_BYTES).decode("utf-8", errors="replace")]
    except Exception:
        return []


def locations(path: str) -> Dict[str, dict]:
    try:
        sources = _read_sources(path)
    except Exception:
        return {}
    found: Dict[str, dict] = {}

    def note(permission, number, text):
        entry = found.setdefault(permission, {"lines": [], "total": 0, "seen": set()})
        key = (number, text)
        if key in entry["seen"]:
            return
        entry["seen"].add(key)
        entry["total"] += 1
        if len(entry["lines"]) < _LOCATION_LIMIT:
            entry["lines"].append([number, text.strip()[:_LOCATION_TEXT]])

    for source in sources:
        for number, line in enumerate(source.splitlines(), 1):
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            for marker, permission, _ in _MARKERS:
                if marker in line:
                    note(permission, number, line)
            match = _IMPORT_LINE.match(line)
            if match:
                name = (match.group(1) or match.group(2)).split(".")[0]
                entry = _IMPORT_PERMISSIONS.get(name)
                if entry:
                    note(entry[0], number, line)
    for entry in found.values():
        entry.pop("seen", None)
    return found


def scan_json(path: str) -> str:
    try:
        return json.dumps(scan(path), ensure_ascii=False)
    except Exception as e:
        return json.dumps({"error": str(e)}, ensure_ascii=False)
