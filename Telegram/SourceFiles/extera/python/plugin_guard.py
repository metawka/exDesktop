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
            (tagged if b_tags else plain).append([name, line, text])
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
_ESCAPE_MODULES = {"os", "builtins", "socket", "ssl", "threading", "_thread", "subprocess", "shutil", "io", "platform",
                   "importlib", "ntpath", "posixpath", "pathlib", "nt", "posix", "_socket", "http", "urllib",
                   "extera_host", "exd_sandbox", "exd_runtime", "exd_java", "exd_picker", "exd_elyx", "exd_porter",
                   "plugin_settings", "requirements_installer", "plugin_guard", "capability_scan", "client_utils",
                   "android_utils", "base_plugin", "tl_runtime", "tgnet_bridge", "_compat", "intents", "android_views"}
_ESCAPE_SYS = {"modules", "meta_path", "path_hooks", "path_importer_cache", "audit", "addaudithook", "_getframe"}
_ESCAPE_SEALED = {"nt", "posix", "_thread", "_uuid", "winreg", "_winreg", "_winapi", "msvcrt", "_overlapped", "pwd",
                  "grp", "_imp", "PIL._imaging", "_socket", "_posixsubprocess", "_io", "_extera", "exd_sandbox"}
_ESCAPE_CALLS = {"exec", "eval", "compile", "open", "__import__", "system", "popen", "Popen", "call", "run",
                 "check_output", "check_call", "copyfile", "copy", "copy2", "copytree", "move", "rmtree", "remove",
                 "startfile", "spawnl", "execv", "getattr", "setattr"}


def _escape_root(node):
    while isinstance(node, (ast.Attribute, ast.Subscript)):
        node = node.value
    return node.id if isinstance(node, ast.Name) else None


def _escape_callable(node, modules):
    if isinstance(node, ast.Name):
        return node.id in ("exec", "eval", "compile", "open", "__import__", "getattr", "setattr", "delattr")
    if isinstance(node, ast.Attribute):
        root = _escape_root(node)
        origin = (modules.get(root) or root or "").split(".")[0]
        return origin in ("os", "subprocess", "shutil", "builtins", "nt", "posix", "pty", "_thread")             and node.attr in _ESCAPE_CALLS
    return False


def _escape_patterns(tree):
    modules = {}
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
            return node.attr, node.lineno
        if isinstance(node, ast.Name) and node.id in _ESCAPE_NAMES:
            return node.id, node.lineno
        if isinstance(node, ast.Attribute) and (node.attr in _ESCAPE_NAMES or node.attr.startswith("sbx_")):
            return node.attr, node.lineno
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            value = node.value
            if (value in _ESCAPE_ATTRS and value != "__builtins__") or value in _ESCAPE_NAMES or value.startswith("sbx_"):
                return value, node.lineno
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
                        return "patch %s.%s" % (root, target.attr), node.lineno
                elif origin == "threading":
                    if target.attr in ("_start_new_thread", "start_new_thread"):
                        return "patch %s.%s" % (root, target.attr), node.lineno
                elif origin in _ESCAPE_MODULES:
                    return "patch %s.%s" % (root, target.attr), node.lineno
                if root == "sys" and target.attr in _ESCAPE_SYS:
                    return "sys." + target.attr, node.lineno
            if isinstance(target, ast.Subscript):
                base = target.value
                if isinstance(base, ast.Attribute) and base.attr in _ESCAPE_SYS and _escape_root(base) == "sys":
                    key = target.slice
                    if not isinstance(key, ast.Constant) or not isinstance(key.value, str) \
                            or key.value.split(".")[0] in _ESCAPE_MODULES or key.value in _ESCAPE_SEALED:
                        return "sys.%s[...]" % base.attr, node.lineno
                if isinstance(base, ast.Name) and base.id == "__builtins__":
                    return "__builtins__[...]", node.lineno
        if isinstance(node, ast.Call):
            func = node.func
            name = func.id if isinstance(func, ast.Name) else func.attr if isinstance(func, ast.Attribute) else ""
            if name in ("setattr", "delattr") and node.args:
                root = _escape_root(node.args[0])
                origin = (modules.get(root) or "").split(".")[0]
                if origin in _ESCAPE_MODULES or root in ("builtins", "__builtins__", "sys"):
                    return "%s(%s)" % (name, root), node.lineno
            if name in ("partial", "methodcaller", "MethodType") and node.args and _escape_callable(node.args[0], modules):
                return "%s(...)" % name, node.lineno
            if name in ("Thread", "Timer", "start_new_thread", "run_on_queue", "run_on_ui_thread", "run_on_main",
                        "submit", "call_soon", "call_later", "run_in_executor", "map", "starmap"):
                for value in list(node.args) + [k.value for k in node.keywords if k.arg in ("target", "function", "fn")]:
                    if _escape_callable(value, modules):
                        return "%s(%s)" % (name, ast.unparse(value) if hasattr(ast, "unparse") else "callable"), node.lineno
            if name in ("__import__", "import_module") and node.args and isinstance(node.args[0], ast.Constant)                     and node.args[0].value in _ESCAPE_SEALED:
                return "%s(%s)" % (name, node.args[0].value), node.lineno
            if name == "reload" and node.args:
                root = _escape_root(node.args[0])
                if (modules.get(root) or root or "").split(".")[0] in _ESCAPE_MODULES:
                    return "reload(%s)" % root, node.lineno
    return None


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
    literal_text = "\n".join(_literals(tree)).lower() if tree is not None else low
    session = sorted({m.group(1) for m in _SESSION_TOKEN.finditer(literal_text)})
    if "getauthkey" in low.replace("_", "") and "getauthkey" not in session:
        session.append("getauthkey")
    if any(k in low for k in ("exd_sandbox", "_exd_sandbox_installed", "_sandbox.json", "addaudithook")):
        add("SANDBOX_TAMPERING", "CRITICAL", "sandbox internals")
    escape = _escape_patterns(tree) if tree is not None else None
    if escape is not None:
        add("SANDBOX_TAMPERING", "CRITICAL", escape[0], escape[1])
    if _BOT_TOKEN.search(source):
        add("EMBEDDED_TELEGRAM_BOT_TOKEN", "CRITICAL", "bot token in code")
    if "api.telegram.org/bot" in low:
        add("C2_TELEGRAM_BOT_API", "CRITICAL", "api.telegram.org/bot")
    for host in _HOOK_HOSTS:
        if host in low:
            add("UNAUTHORIZED_DATA_EXFILTRATION", "CRITICAL", host)
    public_ips = [m for m in _RAW_IP_FULL.finditer(source) if not _is_local_ip(m.group(1))]
    unknown_ips = [m.group(0) for m in public_ips if m.group(1) not in _KNOWN_DNS]
    if unknown_ips:
        add("RAW_IP_C2_ENDPOINT", "CRITICAL" if session else "HIGH", unknown_ips[0])
    elif public_ips:
        add("RAW_IP_C2_ENDPOINT", "WARNING", public_ips[0].group(0))
    for host in _IP_SERVICES:
        if host in low:
            add("EXTERNAL_IP_QUERY_SERVICE", "WARNING", host)
    file_read = bool(_FILE_READ.search(low))
    upload = bool(_UPLOAD.search(low))
    if session and file_read and network and upload:
        add("TELEGRAM_DATABASE_EXFILTRATION", "CRITICAL", ", ".join(session))
    elif session and file_read:
        add("TELEGRAM_INTERNAL_STORAGE_THEFT", "HIGH", ", ".join(session))
    elif session:
        add("TELEGRAM_STORAGE_MENTION", "WARNING", ", ".join(session))
    if "777000" in source and network and upload:
        add("SERVICE_2FA_CODE_INTERCEPTION", "CRITICAL", "777000")
    if ("resetauthorizations" in low or "deleteaccount" in low
            or "auth.resetauthorizations" in low or "account.deleteaccount" in low):
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
    if "subprocess" in low or "os.system" in low or "popen" in low:
        shell_exec = "shell=true" in low.replace(" ", "") or "os.system" in low
        known_tools = any(k in low for k in ("ffmpeg", "ffprobe", "yt-dlp", "yt_dlp"))
        if known_tools and not shell_exec:
            add("EXTERNAL_PROGRAM_LAUNCH", "WARNING", "ffmpeg")
        elif shell_exec or network:
            add("PRIVILEGED_COMMAND_EXECUTION", "HIGH", "process execution")
        else:
            add("PRIVILEGED_COMMAND_EXECUTION", "WARNING", "process execution")
    if "ctypes" in low:
        add("CTYPES_NATIVE_ACCESS", "WARNING", "ctypes")
    obfuscation = capabilities.get("obfuscation") or []
    if obfuscation:
        add("OBFUSCATED_CODE_STRUCTURE", "CRITICAL", ", ".join(obfuscation))
    entropy = "NORMAL"
    if tree is not None:
        worst = 0.0
        for value in _literals(tree):
            if len(value) >= 200 and " " not in value[:200]:
                worst = max(worst, _entropy(value))
        if worst >= 5.6:
            entropy = "HIGH"
            add("HIGH_ENTROPY_LITERAL_DETECTED", "WARNING", f"{worst:.2f}")
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


def scan(path):
    source = _read(path)
    try:
        capabilities = capability_scan.scan(path)
    except Exception:
        capabilities = {}
    findings, entropy = _rules(source, capabilities)
    return {
        "chunks": extract_chunks(source, 48),
        "findings": findings,
        "entropy": entropy,
        "endpoints": _endpoints(source),
        "slice": _slice(source),
    }
