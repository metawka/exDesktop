import ast
import base64
import binascii
import bz2
import codecs
import hashlib
import lzma
import time
import zlib

MAX_LAYERS = 8
MAX_BYTES = 16 * 1024 * 1024
MAX_STEPS = 6000000
MAX_SECONDS = 4.0
MAX_RANGE = 4 * 1024 * 1024
_HASHES = {"sha256": hashlib.sha256, "sha1": hashlib.sha1, "md5": hashlib.md5, "sha512": hashlib.sha512,
           "blake2b": hashlib.blake2b}


class _Digest:
    def __init__(self, value):
        self.value = value
_EXEC = ("exec", "eval")


class _Unknown(Exception):
    pass


class _Return(Exception):
    def __init__(self, value):
        self.value = value


def _limit(data):
    if isinstance(data, (bytes, bytearray, str)) and len(data) > MAX_BYTES:
        raise _Unknown("too large")
    return data


def _inflate(data, kind):
    if kind == "zlib":
        obj = zlib.decompressobj()
    elif kind == "gzip":
        obj = zlib.decompressobj(16 + zlib.MAX_WBITS)
    elif kind == "lzma":
        obj = lzma.LZMADecompressor()
    else:
        obj = bz2.BZ2Decompressor()
    out = obj.decompress(bytes(data), MAX_BYTES + 1) if kind in ("zlib", "gzip", "lzma") else obj.decompress(bytes(data))
    return _limit(out)


_DECODERS = {
    "b64decode": lambda v: base64.b64decode(v),
    "standard_b64decode": lambda v: base64.standard_b64decode(v),
    "urlsafe_b64decode": lambda v: base64.urlsafe_b64decode(v),
    "b85decode": lambda v: base64.b85decode(v),
    "a85decode": lambda v: base64.a85decode(v),
    "b32decode": lambda v: base64.b32decode(v),
    "b16decode": lambda v: base64.b16decode(v),
    "unhexlify": lambda v: binascii.unhexlify(v),
    "a2b_hex": lambda v: binascii.a2b_hex(v),
    "a2b_base64": lambda v: binascii.a2b_base64(v),
    "fromhex": lambda v: bytes.fromhex(v if isinstance(v, str) else v.decode()),
}
_INFLATE = {"zlib": "zlib", "gzip": "gzip", "lzma": "lzma", "bz2": "bz2"}


class _Eval:
    def __init__(self, env, functions=None):
        self.env = env
        self.functions = functions or {}
        self.steps = [0, time.monotonic()]

    def tick(self, amount=1):
        self.steps[0] += amount
        if self.steps[0] > MAX_STEPS:
            raise _Unknown("steps")
        if not self.steps[0] % 4096 and time.monotonic() - self.steps[1] > MAX_SECONDS:
            raise _Unknown("time")

    def __call__(self, node, depth=0):
        if depth > 60:
            raise _Unknown("deep")
        ev = lambda n: self(n, depth + 1)
        if isinstance(node, ast.Constant) and isinstance(node.value, (str, bytes, int)):
            return node.value
        if isinstance(node, ast.Name):
            if node.id in self.env:
                return self.env[node.id]
            raise _Unknown(node.id)
        if isinstance(node, ast.JoinedStr):
            raise _Unknown("fstring")
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
            value = ev(node.operand)
            if isinstance(value, int):
                return -value
            raise _Unknown("unary")
        if isinstance(node, ast.GeneratorExp) or isinstance(node, ast.ListComp):
            return self._generator(node, depth)
        if isinstance(node, ast.BinOp):
            left, right = ev(node.left), ev(node.right)
            if isinstance(node.op, ast.Mod) and isinstance(left, int) and isinstance(right, int) and right:
                return left % right
            if isinstance(node.op, ast.Sub) and isinstance(left, int) and isinstance(right, int):
                return left - right
            if isinstance(node.op, ast.Add) and type(left) is type(right) and isinstance(left, (str, bytes)):
                return _limit(left + right)
            if isinstance(node.op, ast.Add) and isinstance(left, int) and isinstance(right, int):
                return left + right
            if isinstance(node.op, ast.Mult) and isinstance(left, (str, bytes)) and isinstance(right, int) and right < 1024:
                return _limit(left * right)
            if isinstance(node.op, ast.BitXor) and isinstance(left, int) and isinstance(right, int):
                return left ^ right
            raise _Unknown("binop")
        if isinstance(node, (ast.List, ast.Tuple)):
            return [ev(e) for e in node.elts]
        if isinstance(node, ast.Subscript):
            value = ev(node.value)
            sl = node.slice
            if isinstance(sl, ast.Slice):
                lo = ev(sl.lower) if sl.lower else None
                hi = ev(sl.upper) if sl.upper else None
                st = ev(sl.step) if sl.step else None
                return value[lo:hi:st]
            index = ev(sl)
            if isinstance(index, int) and isinstance(value, (bytes, bytearray, str, list)):
                return value[index]
            raise _Unknown("index")
        if isinstance(node, ast.Call):
            return self._call(node, depth)
        raise _Unknown(type(node).__name__)

    def _generator(self, node, depth):
        if len(node.generators) != 1:
            raise _Unknown("generator")
        comp = node.generators[0]
        if comp.ifs:
            raise _Unknown("generator if")
        items = self(comp.iter, depth + 1)
        if isinstance(items, (bytes, bytearray, str)):
            items = list(items)
        if not isinstance(items, list):
            raise _Unknown("iter")
        self.tick(len(items))
        result = []
        saved = dict(self.env)
        try:
            for item in items:
                self._bind(comp.target, item)
                result.append(self(node.elt, depth + 1))
        finally:
            self.env.clear()
            self.env.update(saved)
        return result

    def _bind(self, target, value):
        if isinstance(target, ast.Name):
            self.env[target.id] = value
        elif isinstance(target, ast.Tuple) and isinstance(value, (tuple, list)) and len(value) == len(target.elts):
            for sub, item in zip(target.elts, value):
                self._bind(sub, item)
        else:
            raise _Unknown("bind")

    def run(self, body, depth=0):
        for stmt in body:
            self.tick()
            if isinstance(stmt, ast.Return):
                raise _Return(self(stmt.value, depth + 1) if stmt.value else None)
            if isinstance(stmt, ast.Assign) and len(stmt.targets) == 1:
                self._bind(stmt.targets[0], self(stmt.value, depth + 1))
            elif isinstance(stmt, ast.For) and not stmt.orelse:
                items = self(stmt.iter, depth + 1)
                if isinstance(items, (bytes, bytearray, str)):
                    items = list(items)
                if not isinstance(items, list) or len(items) > MAX_RANGE:
                    raise _Unknown("loop")
                for item in items:
                    self._bind(stmt.target, item)
                    self.run(stmt.body, depth + 1)
            elif isinstance(stmt, (ast.Import, ast.ImportFrom)):
                for alias in stmt.names:
                    module = (stmt.module if isinstance(stmt, ast.ImportFrom) else alias.name) or ""
                    self.env["__module__" + (alias.asname or alias.name)] = module.split(".")[0]
            elif isinstance(stmt, ast.Expr) and isinstance(stmt.value, ast.Constant):
                continue
            elif isinstance(stmt, ast.Expr) and isinstance(stmt.value, ast.Call)                     and isinstance(stmt.value.func, ast.Attribute)                     and stmt.value.func.attr in ("append", "extend")                     and isinstance(stmt.value.func.value, ast.Name)                     and len(stmt.value.args) == 1 and not stmt.value.keywords:
                target = self.env.get(stmt.value.func.value.id)
                value = self(stmt.value.args[0], depth + 1)
                if isinstance(target, bytearray) and isinstance(value, int) and stmt.value.func.attr == "append":
                    target.append(value)
                elif isinstance(target, (bytearray, list)) and stmt.value.func.attr == "extend"                         and isinstance(value, (bytes, bytearray, list)):
                    target.extend(value)
                elif isinstance(target, list) and stmt.value.func.attr == "append":
                    target.append(value)
                else:
                    raise _Unknown("append")
                _limit(target)
            else:
                raise _Unknown("statement " + type(stmt).__name__)

    def _call(self, node, depth):
        ev = lambda n: self(n, depth + 1)
        func = node.func
        name = func.id if isinstance(func, ast.Name) else func.attr if isinstance(func, ast.Attribute) else ""
        owner = func.value if isinstance(func, ast.Attribute) else None
        owner_name = owner.id if isinstance(owner, ast.Name) else ""
        args = node.args
        if node.keywords:
            raise _Unknown("keywords")
        if isinstance(func, ast.Name) and name in self.functions:
            fn = self.functions[name]
            params = fn.args.args
            if fn.args.vararg or fn.args.kwarg or fn.args.kwonlyargs or len(args) != len(params):
                raise _Unknown("function args")
            values = [ev(a) for a in args]
            inner = _Eval(dict(self.env), self.functions)
            inner.steps = self.steps
            for param, value in zip(params, values):
                inner.env[param.arg] = value
            try:
                inner.run(fn.body, depth + 1)
            except _Return as done:
                return done.value
            raise _Unknown("no return")
        if isinstance(func, ast.Name) and name == "range" and 1 <= len(args) <= 3:
            values = [ev(a) for a in args]
            if not all(isinstance(v, int) for v in values):
                raise _Unknown("range")
            result = list(range(*values))
            if len(result) > MAX_RANGE:
                raise _Unknown("range size")
            return result
        if isinstance(func, ast.Name) and name == "enumerate" and len(args) == 1:
            value = ev(args[0])
            if isinstance(value, (bytes, bytearray, str, list)):
                self.tick(len(value))
                return list(enumerate(value))
            raise _Unknown("enumerate")
        if isinstance(func, ast.Name) and name == "len" and len(args) == 1:
            value = ev(args[0])
            if isinstance(value, (bytes, bytearray, str, list)):
                return len(value)
            raise _Unknown("len")
        if isinstance(func, ast.Name) and name == "str" and len(args) == 1:
            value = ev(args[0])
            if isinstance(value, (int, str)):
                return str(value)
            raise _Unknown("str")
        if name in _HASHES and len(args) <= 1:
            module = self.env.get("__module__" + owner_name, owner_name) if owner_name else ""
            if module == "hashlib" or (not owner_name and self.env.get("__module__" + name) == "hashlib"):
                data = ev(args[0]) if args else b""
                if isinstance(data, (bytes, bytearray)):
                    return _Digest(_HASHES[name](bytes(data)))
            raise _Unknown("hash")
        if name in ("digest", "hexdigest") and owner is not None and not args:
            value = ev(owner)
            if isinstance(value, _Digest):
                return getattr(value.value, name)()
            raise _Unknown("digest")
        if name == "compile" and isinstance(func, ast.Name) and args:
            return ev(args[0])
        if name in _DECODERS and args:
            return _limit(_DECODERS[name](ev(args[0])))
        if name == "decompress" and args:
            module = self.env.get("__module__" + owner_name, owner_name)
            return _inflate(ev(args[0]), _INFLATE.get(module, "zlib"))
        if name in ("decode", "encode") and owner is not None:
            value = ev(owner)
            codec = ev(args[0]) if args else "utf-8"
            if not isinstance(codec, str):
                raise _Unknown("codec")
            if name == "decode" and isinstance(value, (bytes, bytearray)):
                if codec.lower().replace("-", "") in ("utf8", "latin1", "ascii", "utf16", "cp1251"):
                    return value.decode(codec, "replace")
            if name == "encode" and isinstance(value, str):
                if codec.lower().replace("-", "") in ("utf8", "latin1", "ascii"):
                    return value.encode(codec)
            if owner_name == "codecs" or isinstance(value, (str, bytes)):
                pass
            raise _Unknown("decode")
        if name == "decode" and owner_name == "codecs" and len(args) >= 2:
            value, codec = ev(args[0]), ev(args[1])
            if codec in ("rot13", "rot_13") and isinstance(value, str):
                return codecs.decode(value, "rot13")
            if codec in ("base64", "hex", "zlib", "bz2") and isinstance(value, (bytes, str)):
                data = value.encode() if isinstance(value, str) else value
                return _limit(codecs.decode(data, codec))
            raise _Unknown("codec")
        if name in ("bytes", "bytearray") and isinstance(func, ast.Name):
            make = bytearray if name == "bytearray" else bytes
            if not args:
                return make()
            value = ev(args[0])
            if isinstance(value, list) and all(isinstance(v, int) and 0 <= v < 256 for v in value):
                return make(value)
            if isinstance(value, (bytes, bytearray)):
                return make(value)
            if isinstance(value, str) and len(args) > 1:
                return value.encode(ev(args[1]))
            raise _Unknown("bytes")
        if name == "join" and owner is not None and args:
            sep = ev(owner)
            items = ev(args[0])
            if isinstance(sep, (str, bytes)) and isinstance(items, list):
                return _limit(sep.join(items))
            raise _Unknown("join")
        if name in ("chr",) and isinstance(func, ast.Name) and args:
            return chr(ev(args[0]))
        if name == "reversed" and args:
            return list(reversed(ev(args[0])))
        raise _Unknown("call " + name)


def _xor_key_bytes(value):
    if isinstance(value, str):
        return value.encode("latin-1", "replace")
    if isinstance(value, list):
        return bytes(value)
    return bytes(value)


def _xor(data, key):
    data = bytes(data)
    key = _xor_key_bytes(key)
    if not key:
        raise _Unknown("key")
    return bytes(b ^ key[i % len(key)] for i, b in enumerate(data))


def _xor_target(stmt):
    if not isinstance(stmt, ast.For):
        return None
    for sub in ast.walk(stmt):
        if isinstance(sub, ast.Assign) and isinstance(sub.value, ast.BinOp) and isinstance(sub.value.op, ast.BitXor):
            target = sub.targets[0]
            names = [n.id for n in ast.walk(sub.value) if isinstance(n, ast.Name)]
            if isinstance(target, ast.Subscript) and isinstance(target.value, ast.Name):
                return target.value.id, names
    return None


def _env_for(body, env, functions=None):
    local = dict(env)
    evaluate = _Eval(local, functions)
    for stmt in body:
        if isinstance(stmt, ast.Import):
            for alias in stmt.names:
                local["__module__" + (alias.asname or alias.name)] = alias.name.split(".")[0]
        elif isinstance(stmt, ast.ImportFrom) and stmt.module:
            for alias in stmt.names:
                local["__module__" + (alias.asname or alias.name)] = stmt.module.split(".")[0]
        elif isinstance(stmt, ast.Assign) and len(stmt.targets) == 1 and isinstance(stmt.targets[0], ast.Name):
            try:
                local[stmt.targets[0].id] = evaluate(stmt.value)
            except Exception:
                local.pop(stmt.targets[0].id, None)
            value = stmt.value
            if isinstance(value, ast.Call) and _call_name(value) in ("bytes", "bytearray") and value.args:
                arg = value.args[0]
                if isinstance(arg, ast.GeneratorExp):
                    decoded = _xor_generator(arg, local)
                    if decoded is not None:
                        local[stmt.targets[0].id] = decoded
                elif isinstance(arg, ast.Call) and _call_name(arg) == "len":
                    local["__buffer__" + stmt.targets[0].id] = True
        elif isinstance(stmt, ast.Try):
            local.update(_env_for(stmt.body, local, functions))
        elif isinstance(stmt, ast.For):
            found = _xor_target(stmt)
            if found and ("__buffer__" + found[0]) in local:
                values = [local.get(n) for n in found[1]]
                sources = [v for v in values if isinstance(v, (bytes, bytearray))]
                if len(sources) >= 2:
                    sources.sort(key=len)
                    try:
                        local[found[0]] = _xor(sources[-1], sources[0])
                    except Exception:
                        pass
    return local


def _call_name(node):
    func = node.func
    return func.id if isinstance(func, ast.Name) else func.attr if isinstance(func, ast.Attribute) else ""


def _xor_generator(gen, env):
    expr = gen.elt
    if not isinstance(expr, ast.BinOp) or not isinstance(expr.op, ast.BitXor):
        return None
    names = [n.id for n in ast.walk(gen) if isinstance(n, ast.Name)]
    values = [env.get(n) for n in names]
    blobs = [v for v in values if isinstance(v, (bytes, bytearray, str))]
    for comp in gen.generators:
        try:
            it = _Eval(env)(comp.iter.args[0] if isinstance(comp.iter, ast.Call) and comp.iter.args else comp.iter)
            if isinstance(it, (bytes, bytearray, str)):
                blobs.append(it)
        except Exception:
            pass
    if len(blobs) < 2:
        return None
    blobs.sort(key=len)
    try:
        return _xor(blobs[-1] if not isinstance(blobs[-1], str) else blobs[-1].encode("latin-1"), blobs[0])
    except Exception:
        return None


def _own_nodes(scope):
    stack = list(ast.iter_child_nodes(scope))
    while stack:
        node = stack.pop()
        yield node
        if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda, ast.ClassDef)):
            stack.extend(ast.iter_child_nodes(node))


def _payloads(tree):
    scopes = [tree] + [n for n in ast.walk(tree) if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef))]
    functions = {n.name: n for n in tree.body if isinstance(n, ast.FunctionDef)}
    module_env = _env_for(tree.body, {}, functions)
    found = []
    failed = 0
    for scope in scopes:
        env = module_env if scope is tree else _env_for(scope.body, module_env, functions)
        evaluate = _Eval(env, functions)
        for node in _own_nodes(scope):
            if not (isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id in _EXEC and node.args):
                continue
            arg = node.args[0]
            if isinstance(arg, ast.Constant) and isinstance(arg.value, str) and scope is tree:
                found.append((node.lineno, arg.value))
                continue
            try:
                value = evaluate(arg)
            except Exception:
                failed += 1
                continue
            if isinstance(value, (bytes, bytearray)):
                value = bytes(value).decode("utf-8", "replace")
            if isinstance(value, str) and value.strip():
                found.append((node.lineno, value))
    return found, failed


def _looks_encoded(arg):
    for sub in ast.walk(arg):
        if isinstance(sub, ast.Call) and _call_name(sub) in tuple(_DECODERS) + ("decompress", "loads", "compile"):
            return True
        if isinstance(sub, ast.Name) and len(sub.id) <= 4:
            return True
    return False


def unwrap(source):
    layers = []
    pending = [source]
    seen = set()
    complete = True
    total = 0
    depth = 0
    while pending and depth < MAX_LAYERS:
        depth += 1
        nxt = []
        for text in pending:
            try:
                tree = ast.parse(text)
            except Exception:
                complete = False
                continue
            found, failed = _payloads(tree)
            if failed:
                complete = False
            for line, payload in found:
                key = hash(payload)
                if key in seen:
                    continue
                seen.add(key)
                total += len(payload)
                if total > MAX_BYTES:
                    return {"layers": layers, "complete": False, "reason": "too large"}
                layers.append({"depth": depth, "line": line, "source": payload})
                nxt.append(payload)
        pending = nxt
    if pending:
        complete = False
    return {"layers": layers, "complete": complete}


def combined(source, result):
    parts = [source]
    for index, layer in enumerate(result.get("layers") or [], 1):
        parts.append("\n# exDesktop: decoded layer %d (depth %d, line %d)\n%s" % (
            index, layer["depth"], layer["line"], layer["source"]))
    return "\n".join(parts)
