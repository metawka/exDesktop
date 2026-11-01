import os
import re
import struct
import threading
import zlib

VECTOR_ID = 0x1CB5C415
GZIP_ID = 0x3072CFA1
BOOL_TRUE = 0x997275B5
BOOL_FALSE = 0xBC799737
PRIMITIVES = {"int", "long", "double", "string", "bytes", "Bool", "true",
              "int128", "int256", "#"}

_LINE = re.compile(r"^([\w.]+)#([0-9a-fA-F]+)\s*(.*?)\s*=\s*([\w.<>%]+)\s*;")
_lock = threading.RLock()
_loaded = False
_by_id = {}
_by_name = {}
_functions = {}
_types = {}


class ArrayList(list):
    def add(self, *args):
        if len(args) == 1:
            self.append(args[0])
        else:
            self.insert(args[0], args[1])
        return True

    def size(self):
        return len(self)

    def get(self, index):
        return self[index]

    def set(self, index, value):
        old = self[index]
        self[index] = value
        return old

    def isEmpty(self):
        return len(self) == 0

    def contains(self, value):
        return value in self

    def clear(self):
        del self[:]

    def addAll(self, values):
        self.extend(values)
        return True

    def toArray(self):
        return list(self)


class _ClassInfo:
    def __init__(self, name):
        self._name = name

    def getSimpleName(self):
        return self._name

    def getName(self):
        return "org.telegram.tgnet.TLRPC$" + self._name


class Param:
    __slots__ = ("name", "type", "flag_field", "flag_bit")

    def __init__(self, name, type_, flag_field=None, flag_bit=None):
        self.name = name
        self.type = type_
        self.flag_field = flag_field
        self.flag_bit = flag_bit


class Combinator:
    __slots__ = ("name", "id", "params", "result", "is_function", "cls")

    def __init__(self, name, id_, params, result, is_function):
        self.name = name
        self.id = id_
        self.params = params
        self.result = result
        self.is_function = is_function
        self.cls = None


class TLObject:
    _tl = None

    def __init__(self, **kwargs):
        combinator = type(self)._tl
        if combinator is not None:
            for param in combinator.params:
                object.__setattr__(self, param.name, _default(param))
        for key, value in kwargs.items():
            setattr(self, key, value)

    @property
    def constructor(self):
        combinator = type(self)._tl
        return combinator.id if combinator else 0

    def getClass(self):
        return _ClassInfo(type(self).__name__)

    def getObjectSize(self):
        return len(serialize(self))

    def serializeToStream(self, stream=None):
        data = serialize(self)
        if stream is not None and hasattr(stream, "write"):
            stream.write(data)
        return data

    def to_dict(self):
        combinator = type(self)._tl
        result = {"_": combinator.name if combinator else type(self).__name__}
        if combinator:
            for param in combinator.params:
                value = getattr(self, param.name, None)
                result[param.name] = _to_plain(value)
        return result

    def __repr__(self):
        combinator = type(self)._tl
        return f"<{type(self).__name__} {combinator.name if combinator else ''}>"


class Vector(TLObject):
    def __init__(self, objects=None):
        super().__init__()
        self.objects = ArrayList(objects or [])


class TL_error(TLObject):
    def __init__(self, code=0, text=""):
        super().__init__()
        self.code = code
        self.text = text


def _to_plain(value):
    if isinstance(value, TLObject):
        return value.to_dict()
    if isinstance(value, list):
        return [_to_plain(v) for v in value]
    if isinstance(value, bytes):
        return value.hex()
    return value


def _default(param):
    t = param.type
    if param.flag_field is not None:
        return False if t == "true" else None
    if t == "#":
        return 0
    if t in ("int", "long"):
        return 0
    if t == "double":
        return 0.0
    if t == "string":
        return ""
    if t == "bytes":
        return b""
    if t == "Bool":
        return False
    if t in ("int128", "int256"):
        return b"\x00" * (16 if t == "int128" else 32)
    if t.startswith("Vector<") or t.startswith("vector<"):
        return ArrayList()
    return None


def class_name(name):
    return "TL_" + name.replace(".", "_")


def type_class_name(name):
    return name.replace(".", "_")


def _parse_param(token):
    if ":" not in token:
        return None
    name, type_ = token.split(":", 1)
    flag_field = flag_bit = None
    match = re.match(r"^(\w+)\.(\d+)\?(.+)$", type_)
    if match:
        flag_field, flag_bit, type_ = match.group(1), int(match.group(2)), match.group(3)
    type_ = type_.lstrip("!%")
    return Param(name, type_, flag_field, flag_bit)


def _type_class(name):
    cls_name = type_class_name(name)
    cls = _types.get(cls_name)
    if cls is None:
        cls = type(cls_name, (TLObject,), {"_tl": None})
        _types[cls_name] = cls
    return cls


def _load_text(text, is_function_section):
    functions = is_function_section
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("---functions---"):
            functions = True
            continue
        if line.startswith("---types---"):
            functions = False
            continue
        if not line or line.startswith("//"):
            continue
        match = _LINE.match(line)
        if not match:
            continue
        name, hex_id, params_text, result = match.groups()
        if name in ("vector", "true", "boolTrue", "boolFalse", "gzip_packed"):
            continue
        if "{" in params_text:
            continue
        params = []
        for token in params_text.split():
            param = _parse_param(token)
            if param is not None:
                params.append(param)
        combinator = Combinator(name, int(hex_id, 16), params, result, functions)
        base = TLObject if functions else _type_class(result)
        cls = type(class_name(name), (base,), {"_tl": combinator})
        combinator.cls = cls
        _by_id[combinator.id] = combinator
        _by_name[cls.__name__] = combinator
        if functions:
            _functions[name] = combinator


def _scheme_paths():
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(here, "tl"),
        os.path.normpath(os.path.join(here, "..", "..", "mtproto", "scheme")),
    ]
    for folder in candidates:
        api = os.path.join(folder, "api.tl")
        if os.path.exists(api):
            return [api]
    return []


def ensure_loaded():
    global _loaded
    with _lock:
        if _loaded:
            return
        for path in _scheme_paths():
            with open(path, encoding="utf-8") as f:
                _load_text(f.read(), False)
        _loaded = True


def lookup(name):
    ensure_loaded()
    combinator = _by_name.get(name)
    if combinator is not None:
        return combinator.cls
    if name in _types:
        return _types[name]
    if name == "Vector":
        return Vector
    if name == "TL_error":
        return TL_error
    return None


# Writing

class _Writer:
    def __init__(self):
        self.parts = []

    def int(self, value):
        self.parts.append(struct.pack("<i", _to_int32(value)))

    def uint(self, value):
        self.parts.append(struct.pack("<I", int(value) & 0xFFFFFFFF))

    def long(self, value):
        self.parts.append(struct.pack("<q", _to_int64(value)))

    def double(self, value):
        self.parts.append(struct.pack("<d", float(value or 0)))

    def bytes(self, value):
        if isinstance(value, str):
            value = value.encode("utf-8")
        value = bytes(value or b"")
        length = len(value)
        if length < 254:
            header = bytes([length])
        else:
            header = bytes([254]) + struct.pack("<I", length)[:3]
        total = len(header) + length
        padding = (-total) % 4
        self.parts.append(header + value + b"\x00" * padding)

    def raw(self, value):
        self.parts.append(bytes(value))

    def data(self):
        return b"".join(self.parts)


def _to_int32(value):
    value = int(value or 0)
    value &= 0xFFFFFFFF
    return value - 0x100000000 if value >= 0x80000000 else value


def _to_int64(value):
    value = int(value or 0)
    value &= 0xFFFFFFFFFFFFFFFF
    return value - 0x10000000000000000 if value >= 0x8000000000000000 else value


def _inner_type(type_):
    match = re.match(r"^[Vv]ector<(.+)>$", type_)
    return match.group(1) if match else None


def _write_value(writer, type_, value):
    inner = _inner_type(type_)
    if inner is not None:
        if type_.startswith("Vector"):
            writer.uint(VECTOR_ID)
        items = list(value or [])
        writer.int(len(items))
        for item in items:
            _write_value(writer, inner, item)
        return
    if type_ == "int":
        writer.int(value)
    elif type_ == "long":
        writer.long(value)
    elif type_ == "double":
        writer.double(value)
    elif type_ in ("string", "bytes"):
        writer.bytes(value)
    elif type_ == "Bool":
        writer.uint(BOOL_TRUE if value else BOOL_FALSE)
    elif type_ == "int128":
        writer.raw((bytes(value or b"") + b"\x00" * 16)[:16])
    elif type_ == "int256":
        writer.raw((bytes(value or b"") + b"\x00" * 32)[:32])
    elif type_ == "true":
        return
    else:
        if value is None:
            raise ValueError(f"missing value of type {type_}")
        _write_object(writer, value)


def _write_object(writer, obj):
    if isinstance(obj, (bool,)):
        writer.uint(BOOL_TRUE if obj else BOOL_FALSE)
        return
    combinator = type(obj)._tl
    if combinator is None:
        raise ValueError(f"cannot serialize {type(obj).__name__}")
    writer.uint(combinator.id)
    flags = {}
    for param in combinator.params:
        if param.type == "#":
            flags[param.name] = int(getattr(obj, param.name, 0) or 0)
    for param in combinator.params:
        if param.flag_field is None:
            continue
        value = getattr(obj, param.name, None)
        bit = 1 << param.flag_bit
        explicit = flags.get(param.flag_field, 0)
        if param.type == "true":
            present = bool(value) or bool(explicit & bit)
        else:
            present = value is not None and not (
                isinstance(value, list) and not value and not (explicit & bit))
        if present:
            flags[param.flag_field] = explicit | bit
        else:
            flags[param.flag_field] = explicit & ~bit
    for param in combinator.params:
        if param.type == "#":
            writer.uint(flags.get(param.name, 0))
            continue
        if param.flag_field is not None:
            if not flags.get(param.flag_field, 0) & (1 << param.flag_bit):
                continue
            if param.type == "true":
                continue
        _write_value(writer, param.type, getattr(obj, param.name, None))


def serialize(obj):
    ensure_loaded()
    writer = _Writer()
    _write_object(writer, obj)
    return writer.data()


# Reading

class _Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def take(self, count):
        if self.pos + count > len(self.data):
            raise ValueError("unexpected end of data")
        chunk = self.data[self.pos:self.pos + count]
        self.pos += count
        return chunk

    def int(self):
        return struct.unpack("<i", self.take(4))[0]

    def uint(self):
        return struct.unpack("<I", self.take(4))[0]

    def long(self):
        return struct.unpack("<q", self.take(8))[0]

    def double(self):
        return struct.unpack("<d", self.take(8))[0]

    def bytes(self):
        first = self.take(1)[0]
        if first == 254:
            length = int.from_bytes(self.take(3), "little")
            header = 4
        else:
            length = first
            header = 1
        value = self.take(length)
        self.take((-(header + length)) % 4)
        return value


def _read_value(reader, type_, top_level=False):
    inner = _inner_type(type_)
    if inner is not None:
        if type_.startswith("Vector"):
            constructor = reader.uint()
            if constructor != VECTOR_ID:
                raise ValueError(f"expected vector, got {constructor:#x}")
        count = reader.int()
        items = ArrayList(_read_value(reader, inner) for _ in range(count))
        return Vector(items) if top_level else items
    if type_ == "int":
        return reader.int()
    if type_ == "long":
        return reader.long()
    if type_ == "double":
        return reader.double()
    if type_ == "string":
        return reader.bytes().decode("utf-8", "replace")
    if type_ == "bytes":
        return reader.bytes()
    if type_ == "int128":
        return reader.take(16)
    if type_ == "int256":
        return reader.take(32)
    if type_ == "Bool":
        value = reader.uint() == BOOL_TRUE
        if top_level:
            return lookup("TL_boolTrue" if value else "TL_boolFalse")()
        return value
    if type_ == "true":
        return True
    return _read_object(reader)


def _read_object(reader):
    constructor = reader.uint()
    if constructor == GZIP_ID:
        packed = reader.bytes()
        return _read_object(_Reader(zlib.decompress(packed, 16 + zlib.MAX_WBITS)))
    if constructor == BOOL_TRUE:
        return True
    if constructor == BOOL_FALSE:
        return False
    if constructor == VECTOR_ID:
        count = reader.int()
        return ArrayList(_read_object(reader) for _ in range(count))
    combinator = _by_id.get(constructor)
    if combinator is None:
        raise ValueError(f"unknown constructor {constructor:#x}")
    obj = combinator.cls.__new__(combinator.cls)
    flags = {}
    for param in combinator.params:
        if param.type == "#":
            value = reader.uint()
            flags[param.name] = value
            object.__setattr__(obj, param.name, value)
            continue
        if param.flag_field is not None:
            if not flags.get(param.flag_field, 0) & (1 << param.flag_bit):
                object.__setattr__(obj, param.name, False if param.type == "true" else None)
                continue
        object.__setattr__(obj, param.name, _read_value(reader, param.type))
    return obj


def deserialize(data, result_type=None):
    ensure_loaded()
    reader = _Reader(data)
    if result_type:
        return _read_value(reader, result_type, top_level=True)
    return _read_object(reader)


def result_type(request):
    combinator = type(request)._tl
    return combinator.result if combinator else None


def is_request(obj):
    combinator = getattr(type(obj), "_tl", None)
    return combinator is not None and combinator.is_function


class _Namespace:
    def __init__(self, name):
        self._name = name

    def __getattr__(self, item):
        if item.startswith("__"):
            raise AttributeError(item)
        cls = lookup(item)
        if cls is None and not item.startswith("TL_"):
            cls = lookup("TL_" + item[0].lower() + item[1:]) if item else None
        if cls is None:
            import _compat
            return _compat.Stub(self._name + "." + item)
        return cls


TLRPC = _Namespace("TLRPC")
