import ast
import builtins
import math
import re
import zipfile

import capability_scan


# Normalize

class _Norm(ast.NodeTransformer):
    TARGET_KEYWORDS = ('DEX', 'SO', 'LIB', 'PAYLOAD', 'BASE64', 'ELF', 'BINARY', 'ZIP', 'APK', 'JAR')
    PRESERVED_NAMES = {
        'os', 'sys', 'socket', 'subprocess', 'urllib', 'requests', 'aiohttp', 'httpx',
        'sqlite3', 'zlib', 'base64', 'struct', 'hashlib', 'hmac', 'uuid', 'threading',
        'time', 're', 'json', 'math', 'glob', 'traceback',
        'UserConfig', 'MessagesStorage', 'MessagesController', 'SendMessagesHelper',
        'AccountInstance', 'ExteraConfig', 'TLRPC', 'ApplicationLoader', 'NotificationCenter',
        'BasePlugin', 'MethodHook', 'HookResult', 'HookStrategy', 'AppEvent',
        'BulletinHelper', 'AlertDialogBuilder', 'Header', 'Input', 'Switch', 'Text',
        'find_class', 'dynamic_proxy', 'run_on_queue', 'run_on_ui_thread', 'log', 'PLUGINS_QUEUE',
        'eval', 'exec', '__import__', 'compile', 'getattr', 'setattr', 'hasattr', 'open',
        'globals', 'locals', 'int', 'str', 'float', 'bytes', 'bytearray', 'len', 'range',
        'print', 'sum', 'min', 'max', 'super', 'self', 'cls', 'Exception', 'BaseException',
        'RuntimeError', 'TypeError', 'ValueError', 'KeyError', 'AttributeError', 'True', 'False', 'None',
        'phone', 'current_user', 'currentUser', 'get_user_config', 'getCurrentUser',
        'send_message', 'send_text', 'send_request',
        'InstallPluginBottomSheet', 'trusted', 'b85decode', 'rc4', 'marshal', 'loads'
    }
    _B64 = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=_-.")

    def _mapped(self, name):
        return name if name in self.PRESERVED_NAMES else "_v_"

    def visit_Assign(self, node):
        self.generic_visit(node)
        for target in getattr(node, 'targets', []):
            if isinstance(target, ast.Name):
                tid = target.id.upper()
                if any(kw in tid for kw in self.TARGET_KEYWORDS):
                    node.value = ast.Constant(value="")
        return node

    def visit_Name(self, node):
        node.id = self._mapped(node.id)
        return node

    def visit_arg(self, node):
        node.arg = self._mapped(node.arg)
        return node

    def visit_Expr(self, node):
        if isinstance(node.value, ast.Constant) and isinstance(node.value.value, str):
            return None
        return self.generic_visit(node)

    def visit_Constant(self, node):
        if isinstance(node.value, str):
            val = node.value
            if len(val) > 60 or val.startswith("ZGV4") or val.startswith("f0VMRg") or ".so" in val[:20] or "ELF" in val[:20]:
                return ast.Constant(value="")
            if val.startswith(("TL_", "payments.", "messages.", "org.telegram.")) or any(k in val for k in ("StarGift", "PaidReaction", "StarsBalance", "StarsStatus", "StarsAmount")):
                return node
            if len(val) >= 16 and " " not in val and all(c in self._B64 for c in val):
                return ast.Constant(value="")
        elif isinstance(node.value, bytes):
            val = node.value
            if len(val) > 40 or val.startswith(b"dex\n") or val.startswith(b"\x7fELF") or len(val) >= 16:
                return ast.Constant(value=b"")
        return node


# Chunks

_CFG_PARENTS = frozenset({'Try', 'ExceptHandler', 'If', 'While', 'For', 'With', 'Call'})
_CFG_CHILDREN = frozenset({'Call', 'Try', 'If', 'While', 'For', 'Raise', 'Return', 'Yield', 'Import', 'ImportFrom', 'Attribute'})
_SEQ = ("Assign", "Expr", "If", "Try", "For", "While", "Return", "Raise")


def _ngrams(node):
    tokens = set()
    for parent in ast.walk(node):
        p_name = type(parent).__name__
        if p_name in _CFG_PARENTS:
            for child in ast.iter_child_nodes(parent):
                c_name = type(child).__name__
                if c_name in _CFG_CHILDREN:
                    tokens.add(f"cfg_{p_name}_{c_name}")
        for attr in ("body", "orelse", "finalbody"):
            seq = getattr(parent, attr, None)
            if isinstance(seq, list) and len(seq) > 1:
                for s1, s2 in zip(seq[:-1], seq[1:]):
                    t1, t2 = type(s1).__name__, type(s2).__name__
                    if t1 in _SEQ and t2 in _SEQ:
                        tokens.add(f"cfg_seq_{t1}_{t2}")
    return " ".join(sorted(tokens))


def _call_name(call):
    if isinstance(call.func, ast.Name):
        return call.func.id
    if isinstance(call.func, ast.Attribute):
        return call.func.attr
    return ""


def _behavior(node):
    tags = []
    if isinstance(node, (ast.While, ast.For)):
        has_send = False
        has_fast_sleep = False
        for sub in ast.walk(node):
            if isinstance(sub, ast.Call):
                fn = _call_name(sub)
                if "send_message" in fn.lower():
                    has_send = True
                if fn == "sleep" and sub.args:
                    arg = sub.args[0]
                    if isinstance(arg, ast.Constant):
                        try:
                            if float(arg.value) <= 0.25:
                                has_fast_sleep = True
                        except Exception:
                            pass
                    elif isinstance(arg, ast.BinOp):
                        has_fast_sleep = True
        if has_send and has_fast_sleep:
            tags.extend(["token_flood_loop", "token_rapid_send", "token_unsolicited_spammer"])
    has_db = has_dyn = has_stars = False
    stars = ("sendpaidreaction", "paidreactionprivacy", "updatestargiftprice", "transferstargift", "inputsavedstargift")
    for sub in ast.walk(node):
        if isinstance(sub, ast.Constant) and isinstance(sub.value, str):
            low = sub.value.lower()
            if any(k in low for k in ("cache4.db", "tgnet.dat", "userconfig.xml", "getauthkey", "getsecretpkeys", "auth_key", "key_datas", "tdata")):
                has_db = True
            if any(k in low for k in stars):
                has_stars = True
        elif isinstance(sub, ast.Attribute):
            if any(k in sub.attr.lower() for k in stars):
                has_stars = True
        elif isinstance(sub, ast.Call):
            if _call_name(sub) in ("b85decode", "rc4", "compile"):
                has_dyn = True
    if has_db:
        tags.extend(["token_session_extract", "token_db_theft"])
    if has_dyn:
        tags.extend(["token_dynamic_stager", "token_payload_exec"])
    if has_stars:
        tags.extend(["token_stars_drainer", "token_gift_hijack"])
    return tags


_STRONG_TAGS = {"token_stars_drainer", "token_gift_hijack", "token_flood_loop"}


def _danger(node, tags):
    if _STRONG_TAGS.intersection(tags) or _drops(node):
        return True
    for sub in ast.walk(node):
        if isinstance(sub, ast.Constant) and isinstance(sub.value, str) and _SESSION_TOKEN.search(sub.value.lower()):
            return True
    return False


_DOWNLOAD_CALLS = {"get", "request", "stream"}
_SAVE_CALLS = {"extractall", "copyfileobj", "write_bytes", "urlretrieve"}
_RUN_ANY = {"exec", "startfile", "Popen", "CDLL", "WinDLL", "LoadLibrary", "import_module", "__import__", "spawnv",
            "execv", "check_output", "check_call"}
_RUN_OWNED = {"run": ("subprocess",), "call": ("subprocess",), "system": ("os",)}


def _owner_name(call):
    owner = call.func.value if isinstance(call.func, ast.Attribute) else None
    if isinstance(owner, ast.Name):
        return owner.id
    if isinstance(owner, ast.Attribute):
        return owner.attr
    return ""


def _drops(node):
    download = save = run = path = False
    for sub in ast.walk(node):
        if not isinstance(sub, ast.Call):
            continue
        name = _call_name(sub)
        owner = _owner_name(sub)
        if name in ("urlopen", "urlretrieve") or (name in _DOWNLOAD_CALLS and owner in ("requests", "httpx", "session", "client")):
            download = True
        if name in _SAVE_CALLS:
            save = True
        elif name == "open" and len(sub.args) > 1 and isinstance(sub.args[1], ast.Constant)                 and isinstance(sub.args[1].value, str) and "b" in sub.args[1].value and "w" in sub.args[1].value:
            save = True
        if name in _RUN_ANY or owner in _RUN_OWNED.get(name, ()):
            run = True
        elif name in ("insert", "append") and isinstance(sub.func, ast.Attribute)                 and isinstance(sub.func.value, ast.Attribute) and sub.func.value.attr == "path":
            path = True
    return download and save and (run or path)


def _chunk_text(node_target, b_tags, normalize):
    raw = ast.unparse(node_target)
    tree = _Norm().visit(ast.parse(raw)) if normalize else node_target
    code = ast.unparse(tree) if normalize else raw
    canonical = " ".join(line.strip() for line in code.splitlines() if line.strip())
    if len(canonical) <= 20:
        return None
    parts = [canonical]
    structural = _ngrams(tree)
    if structural:
        parts.append(structural)
    if b_tags:
        parts.append(" ".join(b_tags * 3))
    return " ".join(parts)


def extract_chunks(source, limit=64):
    tagged = []
    plain = []
    try:
        tree = ast.parse(source)
    except Exception:
        return []
    for node in ast.walk(tree):
        if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef, ast.Try, ast.For, ast.While)):
            continue
        if isinstance(node, (ast.Try, ast.For, ast.While)) and len(getattr(node, "body", [])) < 2:
            continue
        if len(tagged) + len(plain) >= limit * 3:
            break
        if isinstance(node, ast.Try):
            name = "block_try"
        elif isinstance(node, ast.For):
            name = "block_for"
        elif isinstance(node, ast.While):
            name = "block_while"
        else:
            name = getattr(node, "name", type(node).__name__)
        line = getattr(node, "lineno", 0)
        b_tags = _behavior(node)
        target = node
        if isinstance(node, ast.ClassDef):
            try:
                body = [m if not isinstance(m, (ast.FunctionDef, ast.AsyncFunctionDef)) else ast.FunctionDef(
                    name=m.name, args=m.args, body=[ast.Pass()], decorator_list=m.decorator_list,
                    returns=getattr(m, "returns", None)) for m in node.body]
                target = ast.ClassDef(name=node.name, bases=node.bases, keywords=node.keywords,
                                      body=body, decorator_list=node.decorator_list)
            except Exception:
                target = node
        text = None
        try:
            text = _chunk_text(target, b_tags, True)
        except Exception:
            try:
                text = _chunk_text(target, b_tags, False)
            except Exception:
                text = None
        if text:
            (tagged if b_tags else plain).append([name, line, text, _danger(node, b_tags)])
    result = []
    seen = set()
    for entry in tagged + plain:
        if entry[2] not in seen:
            seen.add(entry[2])
            result.append(entry)
        if len(result) >= limit:
            break
    return result


# Rules

_BOT_TOKEN = re.compile(r"\b\d{8,10}:[A-Za-z0-9_-]{35}\b")
_URL = re.compile(r"""https?://[^\s'"<>)]+""")
_RAW_IP_FULL = re.compile(r"https?://((?:\d{1,3}\.){3}\d{1,3})")


def _is_local_ip(ip):
    try:
        parts = [int(x) for x in ip.split(".")]
    except ValueError:
        return False
    if len(parts) != 4:
        return False
    a, b = parts[0], parts[1]
    return (a in (10, 127, 0)
            or (a == 192 and b == 168)
            or (a == 172 and 16 <= b <= 31)
            or (a == 169 and b == 254)
            or (a == 100 and 64 <= b <= 127))
_HOOK_HOSTS = ("discord.com/api/webhooks", "discordapp.com/api/webhooks", "webhook.site", "pipedream", "requestbin", "ngrok", "iplogger", "grabify", "2no.co")
_IP_SERVICES = ("api.ipify.org", "ipinfo.io", "ip-api.com", "ifconfig.me", "icanhazip.com")
_SESSION_FILES = ("key_datas", "tdata", "cache4.db", "tgnet.dat", "userconfig.xml", "auth_key", "getauthkey")
_CRYPTO_ADDR = re.compile(r"\b(?:T[1-9A-HJ-NP-Za-km-z]{33}|0x[a-fA-F0-9]{40}|(?:EQ|UQ)[A-Za-z0-9_-]{46})\b")


def _entropy(text):
    if not text:
        return 0.0
    counts = {}
    for c in text:
        counts[c] = counts.get(c, 0) + 1
    total = len(text)
    return -sum((n / total) * math.log2(n / total) for n in counts.values())


def _literals(tree):
    for node in ast.walk(tree):
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            yield node.value


_SESSION_TOKEN = re.compile(r"(?<![a-z0-9_])(key_datas|tdata|cache4\.db|tgnet\.dat|userconfig\.xml|auth_key|getauthkey)(?![a-z0-9_])")
_NETWORK = re.compile(r"\b(requests|urllib|urllib3|http\.client|aiohttp|httpx|socket|okhttpclient|httpurlconnection|websocket)\b")
_UPLOAD = re.compile(r"(\.post\(|\.put\(|senddocument|send_file|sendfile|\bfiles\s*=|upload|sendall\(|\.send\(|urlopen\()")
_FILE_READ = re.compile(r"(\bopen\(|read_bytes|shutil\.copy|copyfile|copytree|make_archive|zipfile|glob\.glob|os\.walk|listdir|sqlite3|fileinputstream)")
_REMOTE_SOURCE = re.compile(r"(requests\.|urlopen|urllib|httpx|aiohttp|\.recv\(|b64decode|b85decode|a85decode|decompress\(|marshal\.loads|fromhex|okhttp|httpurlconnection)")
_CLIP_READ = re.compile(r"(getprimaryclip|getclipboardtext|clipboard\.paste|clipboard\(\)\.text|getclipboard\(\)|\.gettext\(\)\s*.*clip|pyperclip\.paste)")
_CLIP_WRITE = re.compile(r"(setprimaryclip|addtoclipboard|clipboard\.copy|pyperclip\.copy|settext\()")
_ADDR_PATTERN = re.compile(r"(T\[1-9A-HJ|0x\[a-fA-F0-9\]\{40\}|\[A-Za-z0-9_-\]\{4[68]\}|\(\?:EQ\|UQ\)|bc1\[)")
_KNOWN_DNS = ("1.1.1.1", "1.0.0.1", "8.8.8.8", "8.8.4.4", "9.9.9.9", "149.112.112.112")


def _segment(lines, node):
    try:
        first = node.lineno - 1
        last = node.end_lineno - 1
        if first == last:
            return lines[first][node.col_offset:node.end_col_offset]
        if last - first > 40:
            last = first + 40
        return "\n".join(lines[first:last + 1])
    except Exception:
        return ""


_SCOPES = (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda)


def _scope_nodes(scope):
    stack = list(ast.iter_child_nodes(scope))
    while stack:
        node = stack.pop()
        yield node
        if not isinstance(node, _SCOPES):
            stack.extend(ast.iter_child_nodes(node))


def _names(node):
    return {n.id for n in ast.walk(node) if isinstance(n, ast.Name)}


def _taint(lines, assigns):
    tainted = set()
    for _ in range(4):
        changed = False
        for node in assigns:
            seg = _segment(lines, node.value).lower()
            if not (_REMOTE_SOURCE.search(seg) or _names(node.value) & tainted):
                continue
            targets = node.targets if isinstance(node, ast.Assign) else [node.target]
            for target in targets:
                for name in _names(target):
                    if name not in tainted:
                        tainted.add(name)
                        changed = True
        if not changed:
            break
    return tainted


def _exec_calls(tree, source):
    lines = source.splitlines()
    scopes = [tree] + [n for n in ast.walk(tree) if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef))]
    collected = []
    everywhere = set()
    for scope in scopes:
        assigns = []
        execs = []
        for node in _scope_nodes(scope):
            if isinstance(node, (ast.Assign, ast.AnnAssign, ast.AugAssign)) and node.value is not None:
                assigns.append(node)
            elif (isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
                    and node.func.id in ("exec", "eval") and node.args):
                execs.append(node)
        if not assigns and not execs:
            continue
        local = _taint(lines, assigns)
        everywhere |= local
        collected.append((execs, local))
    remote = []
    indirect = []
    plain = []
    for execs, local in collected:
        for node in execs:
            arg = node.args[0]
            seg = _segment(lines, arg).lower()
            names = _names(arg)
            if _REMOTE_SOURCE.search(seg) or names & local:
                remote.append(node.lineno)
            elif names & everywhere:
                indirect.append(node.lineno)
            else:
                plain.append(node.lineno)
    return remote, indirect, plain


_ESCAPE_ATTRS = {"__closure__", "cell_contents", "__globals__", "f_back", "f_locals", "f_globals", "tb_frame",
                 "gi_frame", "cr_frame", "ag_frame", "__subclasses__", "__builtins__"}
_ESCAPE_NAMES = {"_extera", "_plugin_context", "_METHODS", "_records", "_imp", "_overlapped", "ProactorEventLoop",
                 "IocpProactor", "ExtensionFileLoader", "create_dynamic", "create_builtin", "owner_scope",
                 "owner_now", "_owners", "_sandbox_api", "FunctionType", "CodeType", "_start_new_thread"}
_ESCAPE_MODULES = {"dev_server", "os", "builtins", "socket", "ssl", "threading", "_thread", "subprocess", "shutil", "io", "platform",
                   "importlib", "ntpath", "posixpath", "pathlib", "nt", "posix", "_socket", "http", "urllib",
                   "extera_host", "exd_sandbox", "exd_runtime", "exd_java", "exd_picker", "exd_elyx", "exd_porter",
                   "plugin_settings", "requirements_installer", "plugin_guard", "capability_scan", "client_utils",
                   "android_utils", "base_plugin", "tl_runtime", "tgnet_bridge", "_compat", "intents", "android_views", "exd_xposed",
                   "log_utils", "file_utils", "hook_utils"}
_ESCAPE_SYS = {"modules", "meta_path", "path_hooks", "path_importer_cache", "audit", "addaudithook", "_getframe"}
_ESCAPE_SEALED = {"nt", "posix", "_thread", "_uuid", "winreg", "_winreg", "_winapi", "msvcrt", "_overlapped", "pwd",
                  "grp", "_imp", "PIL._imaging", "_socket", "_posixsubprocess", "_io", "_extera", "exd_sandbox"}
_ESCAPE_CALLS = {"exec", "eval", "compile", "open", "__import__", "system", "popen", "Popen", "call", "run",
                 "check_output", "check_call", "copyfile", "copy", "copy2", "copytree", "move", "rmtree", "remove",
                 "startfile", "spawnl", "execv", "getattr", "setattr"}


_PATTERN_ANY = {"search", "match", "fullmatch", "findall", "finditer", "startswith", "endswith"}
_PATTERN_RE = {"sub", "subn", "split", "compile", "escape"}
_PATTERN_PASS = {"frozenset", "set", "list", "tuple", "dict", "sorted", "str", "lower", "upper"}
_ACTIVE_CALLS = {"open", "read_bytes", "read_text", "write_bytes", "copy", "copyfile", "copytree", "remove", "unlink",
                 "rmtree", "post", "put", "send", "sendall", "send_message", "senddocument", "urlopen", "urlretrieve",
                 "system", "popen", "Popen", "run", "exec", "eval", "listdir", "walk", "glob"}


def _is_pattern_call(call):
    name = _call_name(call)
    if name in _PATTERN_ANY:
        return True
    return name in _PATTERN_RE and _owner_name(call) == "re"


def _matcher_functions(tree):
    result = set()
    for fn in ast.walk(tree):
        if not isinstance(fn, (ast.FunctionDef, ast.AsyncFunctionDef)):
            continue
        matches = active = False
        for sub in ast.walk(fn):
            if isinstance(sub, ast.Compare):
                matches = True
            elif isinstance(sub, ast.Call):
                if _is_pattern_call(sub):
                    matches = True
                if _call_name(sub) in _ACTIVE_CALLS:
                    active = True
        if matches and not active:
            result.add(fn.name)
    return result


def _matching_body(nodes):
    matches = False
    for node in nodes:
        for sub in ast.walk(node):
            if isinstance(sub, ast.Compare):
                matches = True
            elif isinstance(sub, ast.Call):
                if _call_name(sub) in _ACTIVE_CALLS:
                    return False
                if _is_pattern_call(sub):
                    matches = True
    return matches


def _compare_only(node, parents, matchers, uses, seen):
    cur = node
    while True:
        par = parents.get(cur)
        if par is None:
            return False
        if isinstance(par, ast.Compare):
            return True
        if isinstance(par, ast.Call):
            if cur is par.func:
                return False
            if _is_pattern_call(par):
                return True
            name = par.func.id if isinstance(par.func, ast.Name) else ""
            if name in matchers:
                return True
            if name in _PATTERN_PASS:
                cur = par
                continue
            return False
        if isinstance(par, (ast.BinOp, ast.Tuple, ast.List, ast.Set, ast.Dict, ast.Starred, ast.keyword, ast.IfExp)):
            cur = par
            continue
        if isinstance(par, ast.Attribute) and par.value is cur and par.attr in _PATTERN_PASS:
            cur = par
            continue
        if isinstance(par, ast.comprehension) and par.iter is cur:
            owner = parents.get(par)
            return owner is not None and _matching_body([getattr(owner, "elt", owner)] + list(par.ifs))
        if isinstance(par, ast.For) and par.iter is cur:
            return _matching_body(par.body)
        if isinstance(par, (ast.Assign, ast.AnnAssign)):
            targets = par.targets if isinstance(par, ast.Assign) else [par.target]
            names = [t.id for t in targets if isinstance(t, ast.Name)]
            if not names or len(names) != len(targets):
                return False
            for name in names:
                if name in seen:
                    return False
                found = uses.get(name)
                if not found:
                    return False
                seen = seen | {name}
                if not all(_compare_only(use, parents, matchers, uses, seen) for use in found):
                    return False
            return True
        return False


def _inert_literals(tree):
    parents = {}
    uses = {}
    for node in ast.walk(tree):
        for child in ast.iter_child_nodes(node):
            parents[child] = node
        if isinstance(node, ast.Name) and isinstance(node.ctx, ast.Load):
            uses.setdefault(node.id, []).append(node)
    matchers = _matcher_functions(tree)
    result = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Constant) and isinstance(node.value, str) and not isinstance(parents.get(node), ast.JoinedStr):
            if _compare_only(node, parents, matchers, uses, frozenset()):
                result.append(node)
    return result


def _blank_spans(source, nodes):
    lines = [line.encode("utf-8") for line in source.split("\n")]
    for node in nodes:
        start, end = node.lineno - 1, node.end_lineno - 1
        if start >= len(lines) or end >= len(lines):
            continue
        for index in range(start, end + 1):
            line = lines[index]
            a = node.col_offset if index == start else 0
            b = node.end_col_offset if index == end else len(line)
            lines[index] = line[:a] + b" " * (b - a) + line[b:]
    return b"\n".join(lines).decode("utf-8", "replace")


def _active_text(tree, source):
    try:
        return _blank_spans(source, _inert_literals(tree))
    except Exception:
        return source


def _local_defs(tree):
    return {n.name for n in ast.walk(tree) if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef))}


_KNOWN_TOOL_NAMES = ("ffmpeg", "ffprobe", "yt-dlp", "yt_dlp", "pip", "pip3")
_PROC_CALLS = {"run", "call", "Popen", "check_output", "check_call"}


def _tool_launches(tree):
    tools = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call) or _call_name(node) not in _PROC_CALLS or _owner_name(node) != "subprocess":
            continue
        if any(k.arg == "shell" for k in node.keywords):
            return None
        first = node.args[0] if node.args else None
        if not isinstance(first, (ast.List, ast.Tuple)) or not first.elts:
            return None
        head = first.elts[0]
        if not (isinstance(head, ast.Constant) and head.value in _KNOWN_TOOL_NAMES):
            return None
        if any(isinstance(e, ast.Constant) and isinstance(e.value, str) and "://" in e.value for e in first.elts):
            return None
        tools.append(head.value)
    return tools or None


def _escape_root(node):
    while isinstance(node, (ast.Attribute, ast.Subscript)):
        node = node.value
    return node.id if isinstance(node, ast.Name) else None


def _escape_callable(node, modules, local=()):
    if isinstance(node, ast.Name):
        return node.id not in local and node.id in ("exec", "eval", "compile", "open", "__import__", "getattr", "setattr", "delattr")
    if isinstance(node, ast.Attribute):
        root = _escape_root(node)
        origin = (modules.get(root) or root or "").split(".")[0]
        return origin in ("os", "subprocess", "shutil", "builtins", "nt", "posix", "pty", "_thread")             and node.attr in _ESCAPE_CALLS
    return False


_SOFT_NAMES = {"_extera", "FunctionType", "CodeType"}
_HARD_MODULES = {"dev_server", "extera_host", "exd_sandbox", "exd_runtime", "plugin_guard", "capability_scan",
                 "requirements_installer", "exd_porter", "builtins", "__builtins__", "sys", "threading", "_thread",
                 "importlib", "nt", "posix"}


def _escape_soft(detail):
    detail = str(detail)
    if detail in _SOFT_NAMES or detail.startswith("store builtins."):
        return True
    if detail.startswith("sys.modules[") and detail.endswith("]"):
        key = detail[len("sys.modules["):-1]
        return key.split(".")[0] not in _HARD_MODULES and key not in _ESCAPE_SEALED
    for prefix in ("patch ", "setattr(", "delattr(", "reload("):
        if detail.startswith(prefix):
            target = detail[len(prefix):].rstrip(")")
            module = target.split(".")[0]
            return module not in _HARD_MODULES and not target.startswith("threading._start_new_thread")
    return False


def _escape_patterns(tree):
    found = []
    modules = {}
    local = _local_defs(tree)
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                modules[alias.asname or alias.name.split(".")[0]] = alias.name.split(".")[0]
        elif isinstance(node, ast.ImportFrom) and node.module:
            for alias in node.names:
                if node.module.split(".")[0] in _ESCAPE_MODULES:
                    modules[alias.asname or alias.name] = node.module.split(".")[0] + "." + alias.name
    for node in ast.walk(tree):
        if isinstance(node, ast.Attribute) and node.attr in _ESCAPE_ATTRS:
            found.append((node.attr, node.lineno))
        if isinstance(node, ast.Name) and node.id in _ESCAPE_NAMES:
            found.append((node.id, node.lineno))
        if isinstance(node, ast.Attribute) and (node.attr in _ESCAPE_NAMES or node.attr.startswith("sbx_")):
            found.append((node.attr, node.lineno))
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            value = node.value
            if (value in _ESCAPE_ATTRS and value != "__builtins__") or value in _ESCAPE_NAMES or value.startswith("sbx_"):
                found.append((value, node.lineno))
        targets = []
        if isinstance(node, ast.Assign):
            targets = node.targets
        elif isinstance(node, (ast.AugAssign, ast.AnnAssign)):
            targets = [node.target]
        elif isinstance(node, ast.Delete):
            targets = node.targets
        for target in targets:
            if isinstance(target, ast.Attribute):
                root = _escape_root(target)
                origin = (modules.get(root) or "").split(".")[0]
                if root in ("builtins", "__builtins__") or origin == "builtins":
                    if hasattr(builtins, target.attr):
                        found.append(("patch %s.%s" % (root, target.attr), node.lineno))
                elif origin == "threading":
                    if target.attr in ("_start_new_thread", "start_new_thread"):
                        found.append(("patch %s.%s" % (root, target.attr), node.lineno))
                elif origin in _ESCAPE_MODULES:
                    found.append(("patch %s.%s" % (modules.get(root) or root, target.attr), node.lineno))
                if root == "sys" and target.attr in _ESCAPE_SYS:
                    found.append(("sys." + target.attr, node.lineno))
            if isinstance(target, ast.Subscript):
                base = target.value
                if isinstance(base, ast.Attribute) and base.attr in _ESCAPE_SYS and _escape_root(base) == "sys":
                    key = target.slice
                    if isinstance(key, ast.Constant) and isinstance(key.value, str) \
                            and (key.value.split(".")[0] in _ESCAPE_MODULES or key.value in _ESCAPE_SEALED):
                        found.append(("sys.%s[%s]" % (base.attr, key.value), node.lineno))
                if isinstance(base, ast.Name) and base.id == "__builtins__":
                    found.append(("__builtins__[...]", node.lineno))
        if isinstance(node, ast.Call):
            func = node.func
            name = func.id if isinstance(func, ast.Name) else func.attr if isinstance(func, ast.Attribute) else ""
            if name in ("setattr", "delattr") and node.args:
                root = _escape_root(node.args[0])
                origin = (modules.get(root) or "").split(".")[0]
                if origin in _ESCAPE_MODULES or root in ("builtins", "__builtins__", "sys"):
                    attr = node.args[1].value if len(node.args) > 1 and isinstance(node.args[1], ast.Constant) else None
                    if isinstance(attr, str) and (root in ("builtins", "__builtins__") or origin == "builtins")                             and not hasattr(builtins, attr):
                        found.append(("store builtins.%s" % attr, node.lineno))
                    else:
                        found.append(("%s(%s)" % (name, modules.get(root) or root), node.lineno))
            if name in ("partial", "methodcaller", "MethodType") and node.args and _escape_callable(node.args[0], modules, local):
                found.append(("%s(...)" % name, node.lineno))
            if name in ("Thread", "Timer", "start_new_thread", "run_on_queue", "run_on_ui_thread", "run_on_main",
                        "submit", "call_soon", "call_later", "run_in_executor", "map", "starmap"):
                for value in list(node.args) + [k.value for k in node.keywords if k.arg in ("target", "function", "fn")]:
                    if _escape_callable(value, modules, local):
                        found.append(("%s(%s)" % (name, ast.unparse(value) if hasattr(ast, "unparse") else "callable"), node.lineno))
            if name in ("__import__", "import_module") and node.args and isinstance(node.args[0], ast.Constant)                     and node.args[0].value in _ESCAPE_SEALED:
                found.append(("%s(%s)" % (name, node.args[0].value), node.lineno))
            if name == "reload" and node.args:
                root = _escape_root(node.args[0])
                if (modules.get(root) or root or "").split(".")[0] in _ESCAPE_MODULES:
                    found.append(("reload(%s)" % root, node.lineno))
    return found


def _dynamic_module_writes(tree):
    for node in ast.walk(tree):
        targets = node.targets if isinstance(node, (ast.Assign, ast.Delete)) else []
        for target in targets:
            if isinstance(target, ast.Subscript) and isinstance(target.value, ast.Attribute) \
                    and target.value.attr == "modules" and _escape_root(target.value) == "sys":
                return True
    return False


_AI_DIRECTED = re.compile(
    r"(?i)\b(?:ai|llm|llms|language models?|chatgpt|gpt|claude|automated agents?)\b[^\n]{0,120}?"
    r"\b(?:must not|do not|don't|never|are not allowed to|forbidden to)\b[^\n]{0,60}?"
    r"\b(?:deobfuscat\w*|decompil\w*|disassembl\w*|decod\w*|reverse[- ]engineer\w*|analy[sz]\w*|scan\w*|flag\w*|report\w*)")


_B64_BLOB = re.compile(r"^[A-Za-z0-9+/=_-]+$")
_NATIVE_LOADERS = re.compile(r"(ctypes|cdll|windll|loadlibrary|system\.load|memfd_create|dlopen|nativelibrary)")
_DEX_LOADERS = re.compile(r"(dexclassloader|inmemorydexclassloader|pathclassloader|opendexfile|loaddex)")


def _blob_kind(value):
    text = "".join(value.split())
    if len(text) < 2048 or not _B64_BLOB.match(text[:4096]):
        return None
    try:
        import base64 as _b64
        import zlib as _zlib
        head = _b64.b64decode(text[:8192] + "=" * (-len(text[:8192]) % 4), validate=False)
        candidates = [head]
        if head[:1] == b"\x78" or head[:2] == b"\x1f\x8b":
            raw = _b64.b64decode(text + "=" * (-len(text) % 4), validate=False)
            wbits = 16 + _zlib.MAX_WBITS if raw[:2] == b"\x1f\x8b" else _zlib.MAX_WBITS
            candidates.append(_zlib.decompressobj(wbits).decompress(raw, 64))
    except Exception:
        return None
    for data in candidates:
        if data[:4] == b"\x7fELF":
            return "ELF"
        if data[:2] == b"MZ":
            return "PE"
        if data[:4] == b"dex\n":
            return "DEX"
    return None


def _embedded_binaries(tree):
    kinds = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Constant) and isinstance(node.value, str) and len(node.value) >= 2048:
            kind = _blob_kind(node.value)
            if kind and kind not in kinds:
                kinds.append(kind)
    return kinds


_CODE_LOADERS = re.compile(r"(inmemorydexclassloader|dexclassloader|pathclassloader|opendexfile|loaddex|"
                           r"system\.load|dlopen|memfd_create|import_module|sys\.path\.insert)")
_CODE_URL = re.compile(r"^https?://\S*(nightly\.link|/releases|/actions/|/artifacts|\.dex\b|\.so\b|"
                       r"\.jar\b|\.apk\b|\.plugin\b|\.py\b|\.elyx\b|\.zip\b)")


def _remote_loader(tree, low):
    download = False
    urls = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Call):
            name = _call_name(node)
            owner = _owner_name(node)
            if name in ("urlopen", "urlretrieve") or (name in _DOWNLOAD_CALLS and owner in ("requests", "httpx", "session", "client")):
                download = True
        elif isinstance(node, ast.Constant) and isinstance(node.value, str) and len(node.value) < 500:
            if _CODE_URL.search(node.value.strip().lower()):
                urls.append(node.value.strip())
    if not download or not urls:
        return None
    match = _CODE_LOADERS.search(low)
    return ("downloads code from %s and loads it with %s" % (urls[0][:240], match.group(1))) if match else None


def _codes_forwarded(tree):
    for scope in ast.walk(tree):
        if not isinstance(scope, (ast.FunctionDef, ast.AsyncFunctionDef)):
            continue
        text = ast.unparse(scope).lower() if hasattr(ast, "unparse") else ""
        if "777000" not in text:
            continue
        if _NETWORK.search(text) and _UPLOAD.search(text):
            return True
        if "send_message" in text and any(k in text for k in ("message.message", ".text", "msg.message")):
            if not any(k in text for k in ("in (777000", "== 777000", "user_id in")):
                return True
    return False


def _rules(source, capabilities):
    findings = []
    lines = source.lower().splitlines()

    def add(rule, level, detail, line=0):
        if not line:
            needle = str(detail).lower().split(",")[0].strip()
            if needle:
                for index, text in enumerate(lines, 1):
                    if needle in text:
                        line = index
                        break
        findings.append({"rule": rule, "level": level, "detail": detail, "line": line})

    low = source.lower()
    try:
        tree = ast.parse(source)
    except Exception:
        tree = None
    calls = set()
    if tree is not None:
        for node in ast.walk(tree):
            if isinstance(node, ast.Call) and isinstance(node.func, ast.Name):
                calls.add(node.func.id)
    network = bool(_NETWORK.search(low))
    active = _active_text(tree, source).lower() if tree is not None else low
    literal_text = low
    if tree is not None:
        inert = {n.value for n in _inert_literals(tree)}
        literal_text = "\n".join(v for v in _literals(tree) if v not in inert).lower()
    session = sorted({m.group(1) for m in _SESSION_TOKEN.finditer(literal_text)})
    if "getauthkey" in low.replace("_", "") and "getauthkey" not in session:
        session.append("getauthkey")
    if any(k in low for k in ("exd_sandbox", "_exd_sandbox_installed", "_sandbox.json", "addaudithook")):
        add("SANDBOX_TAMPERING", "CRITICAL", "sandbox internals")
    for detail, line in (_escape_patterns(tree) if tree is not None else []):
        if _escape_soft(detail):
            add("SYSTEM_INTEGRATION", "INFO", detail, line)
        elif detail in ("__closure__", "__globals__", "__builtins__"):
            add("SANDBOX_INTROSPECTION", "HIGH", detail, line)
        else:
            add("SANDBOX_TAMPERING", "CRITICAL", detail, line)
            break
    sensitive = bool(session) or ("777000" in active) or any(k in low for k in (
        "getcontacts", "contacts.getcontacts", "phone_number", "getphone", "clientuserid", "get_phone"))
    if _BOT_TOKEN.search(source):
        add("EMBEDDED_TELEGRAM_BOT_TOKEN", "CRITICAL" if sensitive else "HIGH", "bot token in code")
    if "api.telegram.org/bot" in low:
        add("C2_TELEGRAM_BOT_API", "CRITICAL" if sensitive else "HIGH", "api.telegram.org/bot")
    for host in _HOOK_HOSTS:
        if host in active:
            add("UNAUTHORIZED_DATA_EXFILTRATION", "CRITICAL", host)
    public_ips = [m for m in _RAW_IP_FULL.finditer(source) if not _is_local_ip(m.group(1))]
    unknown_ips = [m.group(0) for m in public_ips if m.group(1) not in _KNOWN_DNS]
    if unknown_ips:
        add("RAW_IP_C2_ENDPOINT", "CRITICAL" if session else "HIGH", unknown_ips[0])
    elif public_ips:
        add("RAW_IP_C2_ENDPOINT", "WARNING", public_ips[0].group(0))
    for host in _IP_SERVICES:
        if host in active:
            add("EXTERNAL_IP_QUERY_SERVICE", "WARNING", host)
    file_read = bool(_FILE_READ.search(low))
    upload = bool(_UPLOAD.search(low))
    if session and file_read and network and upload:
        add("TELEGRAM_DATABASE_EXFILTRATION", "CRITICAL", ", ".join(session))
    elif session and file_read:
        add("TELEGRAM_INTERNAL_STORAGE_THEFT", "HIGH", ", ".join(session))
    elif session:
        add("TELEGRAM_STORAGE_MENTION", "WARNING", ", ".join(session))
    if "777000" in active and network and upload:
        if tree is not None and _codes_forwarded(tree):
            add("SERVICE_2FA_CODE_INTERCEPTION", "CRITICAL", "777000")
        else:
            add("SERVICE_2FA_CODE_INTERCEPTION", "WARNING", "777000")
    if "resetauthorizations" in active or "deleteaccount" in active:
        add("UNAUTHORIZED_DATA_EXFILTRATION", "CRITICAL", "account destructive call")
    if tree is not None:
        remote, indirect, local = _exec_calls(tree, source)
        if remote and network:
            add("REMOTE_CODE_EXECUTION_STAGER", "CRITICAL", "exec/eval of downloaded code", remote[0])
        elif remote or indirect:
            add("DYNAMIC_CODE_EXECUTION", "HIGH", "exec/eval of external data", (remote or indirect)[0])
        elif local:
            add("DYNAMIC_CODE_EXECUTION", "WARNING", "exec/eval", local[0])
    elif calls & {"exec", "eval"}:
        add("DYNAMIC_CODE_EXECUTION", "WARNING", "exec/eval")
    if "compile" in calls:
        add("DYNAMIC_CODE_COMPILATION", "WARNING", "compile")
    if _CRYPTO_ADDR.search(source) or _ADDR_PATTERN.search(source):
        reads = bool(_CLIP_READ.search(low))
        writes = bool(_CLIP_WRITE.search(low))
        if reads and writes and _ADDR_PATTERN.search(source):
            add("CLIPBOARD_CRYPTO_CLIPPER", "CRITICAL", "clipboard address replacement")
        elif reads and writes:
            add("CLIPBOARD_CRYPTO_CLIPPER", "WARNING", "clipboard with wallet address")
    if "subprocess" in active or "os.system" in active or "popen" in active:
        shell_exec = "shell=true" in active.replace(" ", "") or "os.system" in active
        known_tools = any(k in active for k in ("ffmpeg", "ffprobe", "yt-dlp", "yt_dlp"))
        launches = _tool_launches(tree) if tree is not None else None
        if launches and not shell_exec:
            add("EXTERNAL_PROGRAM_LAUNCH", "WARNING", launches[0])
        elif known_tools and not shell_exec:
            add("EXTERNAL_PROGRAM_LAUNCH", "WARNING", "ffmpeg")
        elif shell_exec or network:
            add("PRIVILEGED_COMMAND_EXECUTION", "HIGH", "process execution")
        else:
            add("PRIVILEGED_COMMAND_EXECUTION", "WARNING", "process execution")
    if "ctypes" in low:
        add("CTYPES_NATIVE_ACCESS", "WARNING", "ctypes")
    if tree is not None:
        for scope in [n for n in ast.walk(tree) if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef))]:
            if _drops(scope):
                add("PAYLOAD_DROPPER", "CRITICAL", "download, save and run: " + scope.name, scope.lineno)
                break
    if tree is not None and not any(f["rule"] in ("PAYLOAD_DROPPER", "REMOTE_CODE_EXECUTION_STAGER") for f in findings):
        loader = _remote_loader(tree, low)
        if loader:
            add("UNVERIFIED_CODE_LOADER", "CRITICAL", loader)
    if tree is not None and _dynamic_module_writes(tree):
        add("MODULE_REGISTRY_WRITE", "INFO", "sys.modules")
    if tree is not None:
        binaries = _embedded_binaries(tree)
        native = [k for k in binaries if k in ("ELF", "PE")]
        if native and _NATIVE_LOADERS.search(low):
            add("EMBEDDED_NATIVE_PAYLOAD", "CRITICAL", "embedded " + ", ".join(native) + " binary loaded at runtime")
        elif native:
            add("EMBEDDED_NATIVE_PAYLOAD", "HIGH", "embedded " + ", ".join(native) + " binary")
        if "DEX" in binaries:
            add("EMBEDDED_DEX_PAYLOAD", "HIGH" if _DEX_LOADERS.search(low) else "WARNING", "embedded DEX")
    ai_text = _AI_DIRECTED.search(source)
    if ai_text:
        add("AI_DIRECTED_TEXT", "HIGH", ai_text.group(0)[:80])
    obfuscation = capabilities.get("obfuscation") or []
    if obfuscation:
        add("OBFUSCATED_CODE_STRUCTURE", "CRITICAL", ", ".join(obfuscation))
    entropy = "NORMAL"
    if tree is not None:
        worst = 0.0
        worst_value = ""
        for value in _literals(tree):
            if len(value) >= 200 and " " not in value[:200]:
                current = _entropy(value)
                if current > worst:
                    worst, worst_value = current, value
        if worst >= 5.6:
            entropy = "HIGH"
            if _blob_kind(worst_value) is None:
                add("HIGH_ENTROPY_LITERAL_DETECTED", "WARNING", "string of %d characters, entropy %.2f" % (len(worst_value), worst))
        elif worst >= 5.0:
            entropy = "ELEVATED"
    return findings, entropy


def _endpoints(source):
    result = []
    for match in _URL.findall(source):
        url = match.rstrip(".,;")
        if url not in result:
            result.append(url)
        if len(result) >= 12:
            break
    return result


_SLICE_MARKERS = ("requests", "urllib", "socket", "exec", "eval", "compile", "base64", "b64decode",
                  "marshal", "subprocess", "os.system", "ctypes", "open(", "send_message", "http",
                  "token", "phone", "777000", "clipboard", "tdata", "key_data", "auth")


def _slice(source, limit=14000):
    lines = source.splitlines()
    marked = set()
    for index, line in enumerate(lines):
        low = line.lower()
        if any(m in low for m in _SLICE_MARKERS):
            for near in range(max(0, index - 4), min(len(lines), index + 5)):
                marked.add(near)
    if not marked:
        return "\n".join(lines)[:limit]
    out = []
    previous = -2
    size = 0
    for index in sorted(marked):
        if index != previous + 1:
            out.append(f"# line {index + 1}")
        text = lines[index][:300]
        out.append(text)
        size += len(text) + 1
        previous = index
        if size >= limit:
            break
    return "\n".join(out)[:limit]


def _read(path):
    if zipfile.is_zipfile(path):
        parts = []
        with zipfile.ZipFile(path) as archive:
            for info in archive.infolist():
                if info.filename.endswith(".py"):
                    with archive.open(info) as handle:
                        parts.append(handle.read(16 * 1024 * 1024).decode("utf-8", "replace"))
        return "\n".join(parts)
    with open(path, "rb") as handle:
        return handle.read(16 * 1024 * 1024).decode("utf-8", "replace")


_WEAK_OBFUSCATION = ("long lines", "escaped strings")


def deobfuscate(path):
    source = _read(path)
    try:
        capabilities = capability_scan.scan(path)
    except Exception:
        capabilities = {}
    obfuscation = capabilities.get("obfuscation") or []
    if not obfuscation:
        return source, capabilities, None
    import exd_deobf
    try:
        result = exd_deobf.unwrap(source)
    except Exception:
        return source, capabilities, None
    hard = [o for o in obfuscation if not str(o).startswith("exec+") and o not in _WEAK_OBFUSCATION]
    if not result.get("layers"):
        return source, capabilities, None
    if hard or not result.get("complete"):
        return exd_deobf.combined(source, result), capabilities, None
    capabilities = dict(capabilities)
    capabilities["obfuscation"] = []
    return exd_deobf.combined(source, result), capabilities, result


def _collapse(source):
    try:
        tree = ast.parse(source)
    except Exception:
        return source
    lines = source.split("\n")
    spans = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Constant) and isinstance(node.value, (str, bytes)) and len(node.value) >= 2048:
            if getattr(node, "end_lineno", None) is None:
                continue
            value = node.value if isinstance(node.value, str) else node.value.decode("latin-1")
            kind = _blob_kind(value)
            size = len(value) // 1024
            label = ("embedded %s binary, %d KB, not readable" % (kind, size)) if kind else ("encoded data, %d KB" % size)
            spans.append((node.lineno, node.col_offset, node.end_lineno, node.end_col_offset, label))
    for start, col, end, end_col, label in sorted(spans, reverse=True):
        head = lines[start - 1][:col]
        tail = lines[end - 1][end_col:]
        lines[start - 1:end] = [head + '"<exDesktop: ' + label + '>"' + tail]
    return "\n".join(lines)


def preview(path):
    original = _read(path)
    source, capabilities, decoded = deobfuscate(path)
    notes = []
    if decoded:
        notes.append("# exDesktop: the code was encrypted, %d decoded layers are shown below the original" % len(decoded["layers"]))
    elif source != original:
        notes.append("# exDesktop: the code is encrypted and was decoded only partly, the rest cannot be checked")
    elif capabilities.get("obfuscation"):
        notes.append("# exDesktop: the code is obfuscated and cannot be decoded")
    text = _collapse(source)
    if notes:
        text = "\n".join(notes) + "\n\n" + text
    return text[:4 * 1024 * 1024]


def scan(path):
    source, capabilities, decoded = deobfuscate(path)
    findings, entropy = _rules(source, capabilities)
    if decoded:
        findings.append({"rule": "DECODED_OBFUSCATION", "level": "HIGH",
                         "detail": "%d layers" % len(decoded["layers"]), "line": 0})
    return {
        "chunks": extract_chunks(source, 48),
        "findings": findings,
        "entropy": entropy,
        "endpoints": _endpoints(source),
        "slice": _slice(source),
        "decoded": bool(decoded),
        "decoded_layers": len(decoded["layers"]) if decoded else 0,
    }
