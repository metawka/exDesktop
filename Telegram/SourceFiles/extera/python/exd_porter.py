import ast
import os
import re
import tempfile
import urllib.parse
import zipfile

_MAX_SOURCE = 160000
_OS_NAMES = {"windows": "Windows", "linux": "Linux", "macos": "macOS"}
_MISSING_ON = {
    "windows": {"termios", "pwd", "grp", "resource", "posix"},
    "linux": {"msvcrt", "winsound", "winreg", "_winapi", "nt"},
    "macos": {"msvcrt", "winsound", "winreg", "_winapi", "nt"},
}


def current_os():
    import sys
    if sys.platform.startswith("win"):
        return "windows"
    if sys.platform == "darwin":
        return "macos"
    return "linux"
_ID_LIMIT = 32
_FORBIDDEN_MODULES = {"subprocess", "ctypes", "cffi", "_cffi_backend", "winreg", "_winreg", "marshal",
                      "_winapi", "dalvik", "pty", "gc"}
_FORBIDDEN_CALLS = {"hook_method", "hook_all_methods", "hook_all_constructors", "os.system",
                    "os.startfile", "os.popen", "os.execv", "os.execl", "os.spawnl", "os.spawnv",
                    "InMemoryDexClassLoader", "DexClassLoader", "PathClassLoader", "loadLibrary",
                    "sys.settrace", "sys.setprofile", "sys.addaudithook", "LoadLibrary"}
_ANDROID_PATHS = ("/sdcard", "/storage/emulated", "/data/user/", "/data/data/")
_URL = re.compile(r"https?://[^\s'\"<>)]+")


# Prompt

def _here(name):
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), name)


def _read_doc(name):
    try:
        with open(_here(name), encoding="utf-8") as f:
            return f.read()
    except Exception:
        return ""


def sections():
    result = {}
    current = None
    for line in _read_doc("exd_port_reference.md").splitlines():
        if line.startswith("## "):
            current = line[3:].strip().lower()
            result[current] = []
        elif current:
            result[current].append(line)
    return {k: "\n".join(v).strip() for k, v in result.items()}


def system_prompt():
    names = ", ".join(sections())
    return _read_doc("exd_port_core.md").replace("{sections}", names)


_HINTS = (
    (("sendMessage",), "send",
     "Xposed hook on SendMessagesHelper.sendMessage: replace it with the send hook."),
    (("sendReaction",), "requests",
     "Hook on sendReaction: use add_hook(\"TL_messages_sendReaction\") and change the request in pre_request_hook."),
    (("processNewMessages", "didReceivedNewMessages", "processUpdates", "processUpdateArray"), "updates",
     "Hook on incoming messages: use add_hook(\"TL_updateNewMessage\") and on_update_hook."),
    (("sendRequest", "performRequest"), "requests",
     "Hook on a network request: use add_hook(\"TL_<request>\") with pre_request_hook or post_request_hook."),
    (("isPremium", "premiumFeaturesBlocked", "hasPremiumOnAccounts", "isPremiumFeatureAvailable"), None,
     "Premium checks: drop them, exDesktop has its own local Premium settings."),
    (("createView", "onResume", "onPause", "onDraw", "onLayout", "onMeasure", "dispatchDraw",
      "onBindViewHolder", "fillItems", "updateRowsIds", "onAttachedToWindow", "onTouchEvent",
      "dispatchTouchEvent", "updateTitle", "updateSubtitle", "setText"), "menus",
     "Hooks on Android screens and drawing: drop them. If the hook only adds a button or an action, "
     "add a menu item instead."),
    (("onActivityResult", "openForView", "show", "dismiss"), "ui",
     "Android dialogs and activities: use AlertDialogBuilder or BulletinHelper."),
)

_FEATURES = (
    ("create_settings", "settings"),
    ("MenuItemData", "menus"),
    ("add_menu_item", "menus"),
    ("on_send_message_hook", "send"),
    ("add_hook", "requests"),
    ("send_request", "requests"),
    ("BulletinHelper", "ui"),
    ("AlertDialog", "ui"),
    ("send_text", "client"),
    ("send_message", "client"),
    ("requests.", "network"),
    ("urllib", "network"),
    ("open(", "files"),
    ("sqlite3", "files"),
    ("hook_method", "hooks"),
    ("hook_all_methods", "hooks"),
    ("NotificationCenter", "java"),
    ("SendMessagesHelper", "java"),
    ("PluginsController", "java"),
    ("BitmapFactory", "java"),
    ("getSystemService", "java"),
    ("FileOutputStream", "java"),
)

_METHOD_RE = re.compile(r"""(?:getDeclaredMethod|getMethod|hook_all_methods\s*\([^,]+,)\s*\(?\s*["'](\w+)["']""")


def _hints(source):
    methods = set(_METHOD_RE.findall(source))
    chosen = []
    lines = []
    for names, section, text in _HINTS:
        if methods & set(names):
            lines.append(text)
            if section and section not in chosen:
                chosen.append(section)
    for needle, section in _FEATURES:
        if needle in source and section not in chosen:
            chosen.append(section)
    return lines, chosen[:5]


def _strip_comments(source):
    import io
    import tokenize
    try:
        tokens = list(tokenize.generate_tokens(io.StringIO(source).readline))
    except Exception:
        return source
    cuts = {}
    for token in tokens:
        if token.type == tokenize.COMMENT:
            cuts[token.start[0]] = token.start[1]
    result = []
    for index, line in enumerate(source.splitlines(keepends=True), 1):
        if index in cuts:
            kept = line[:cuts[index]].rstrip()
            if not kept.strip():
                continue
            line = kept + "\n"
        result.append(line)
    text = re.sub(r"\n{3,}", "\n\n", "".join(result))
    try:
        ast.parse(text)
    except SyntaxError:
        return source
    return text


def _strip_blobs(source):
    try:
        tree = ast.parse(source)
    except SyntaxError:
        return source
    spans = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Constant) and isinstance(node.value, (str, bytes)) and len(node.value) > 1500 \
                and getattr(node, "end_lineno", None):
            spans.append((node.lineno, node.col_offset, node.end_lineno, node.end_col_offset))
    if not spans:
        return source
    lines = source.splitlines(keepends=True)
    for start, col, end, end_col in sorted(spans, reverse=True):
        head = lines[start - 1][:col]
        tail = lines[end - 1][end_col:]
        lines[start - 1:end] = [head + '"<binary blob removed>"' + tail]
    return "".join(lines)


def _is_library(source):
    try:
        tree = ast.parse(source)
    except SyntaxError:
        return False
    return not any(isinstance(n, ast.ClassDef) and any(_dotted(b).endswith("BasePlugin") for b in n.bases)
                   for n in tree.body)


def _original_source(path):
    if zipfile.is_zipfile(path):
        return None
    with open(path, "rb") as f:
        raw = f.read(_MAX_SOURCE + 1)
    if len(raw) > _MAX_SOURCE:
        return False
    return _strip_blobs(_strip_comments(raw.decode("utf-8", "replace")))


def _hosts(source):
    result = set()
    for match in _URL.findall(source or ""):
        try:
            host = urllib.parse.urlsplit(match.rstrip(".,;")).hostname
        except ValueError:
            host = None
        if host and "{" not in host:
            result.add(host.lower())
    return result


def _target_id(original_id):
    base = re.sub(r"[^A-Za-z0-9_]", "_", str(original_id or "plugin"))
    return base[:_ID_LIMIT - 4] + "_exd"


def prepare(path, meta, capabilities):
    source = _original_source(path)
    if source is None:
        return {"error": "format"}
    if source is False:
        return {"error": "size"}
    original_id = str(meta.get("id") or "")
    version = str(meta.get("version") or "")
    library = _is_library(source)
    target = original_id if library else _target_id(original_id)
    hooks = sorted(set((capabilities or {}).get("hooks") or []))[:40]
    notes = [
        "Original id: %s" % original_id,
        "Original version: %s" % version,
        "Required id of the ported plugin: %s" % target,
        "Required __ported_from__: \"%s %s\"" % (original_id, version),
        "Required __desktop_os__: \"%s\"" % current_os(),
        "The user runs exDesktop on %s. The plugin must work on %s first. Keep it portable to Windows, Linux "
        "and macOS: use os.path or pathlib, no hard-coded drive letters or separators, no modules that exist "
        "on one system only." % (_OS_NAMES[current_os()], _OS_NAMES[current_os()]),
    ]
    hint_lines, chosen = _hints(source)
    extra = []
    if library:
        extra.append("libraries")
        hint_lines.append("This is a library plugin: keep __id__ and all public names unchanged.")
    if re.search(r"DexClassLoader|InMemoryDexClassLoader|binary blob removed", source):
        extra.append("dex")
        hint_lines.append("The original loads compiled Android code. Follow the dex section.")
    if re.search(r"\bctypes\b|\.so[\"']", source):
        extra.append("native")
        hint_lines.append("The original loads a native library. Follow the native section.")
    chosen = (extra + [c for c in chosen if c not in extra])[:6]
    if hint_lines:
        notes.append("Hints:\n- " + "\n- ".join(hint_lines))
    docs = sections()
    included = "\n\n".join("## %s\n%s" % (name, docs[name]) for name in chosen if name in docs)
    user = (
        "\n".join(notes)
        + ("\n\nDocumentation:\n\n" + included if included else "")
        + "\n\nOriginal plugin source (" + os.path.basename(path) + "):\n\n```python\n"
        + source
        + "\n```\n"
    )
    return {
        "system": system_prompt(),
        "user": user,
        "target_id": target,
        "library": library,
        "original_id": original_id,
        "original_version": version,
        "hosts": sorted(_hosts(source)),
        "sections": chosen,
        "hints": hint_lines,
    }


def needed(answer, prepared):
    text = str(answer or "")
    if "```" in text:
        return []
    docs = sections()
    have = set(prepared.get("sections") or [])
    result = []
    for match in re.findall(r"(?im)^\s*NEED:\s*([\w ,-]+)$", text):
        for name in re.split(r"[ ,]+", match.strip().lower()):
            if name in docs and name not in have and name not in result:
                result.append(name)
    return result


def need_prompt(names):
    docs = sections()
    return "\n\n".join("## %s\n%s" % (name, docs[name]) for name in names) + (
        "\n\nNow return the complete ported plugin in the answer format.")


# Check

def extract(answer):
    text = str(answer or "")
    blocks = re.findall(r"```(?:python|py)?[ \t]*\r?\n(.*?)```", text, re.S)
    code = max(blocks, key=len) if blocks else ""
    report = ""
    marker = text.rfind("REPORT")
    if marker >= 0:
        report = text[marker + len("REPORT"):].strip(" :\r\n")
    return code.strip("\r\n") + "\n" if code else "", report[:4000]


def _dotted(node):
    parts = []
    while isinstance(node, ast.Attribute):
        parts.append(node.attr)
        node = node.value
    if isinstance(node, ast.Name):
        parts.append(node.id)
    return ".".join(reversed(parts))


def _literal_meta(tree):
    meta = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            name = node.targets[0].id
            if name.startswith("__") and name.endswith("__"):
                try:
                    meta[name] = ast.literal_eval(node.value)
                except Exception:
                    meta[name] = ast
    return meta


def _guard_errors(code):
    try:
        import plugin_guard
    except Exception:
        return []
    handle, temp = tempfile.mkstemp(suffix=".plugin")
    try:
        with os.fdopen(handle, "w", encoding="utf-8") as f:
            f.write(code)
        result = plugin_guard.scan(temp)
    except Exception:
        return []
    finally:
        try:
            os.remove(temp)
        except OSError:
            pass
    errors = []
    for finding in result.get("findings") or []:
        if str(finding.get("level", "")).upper() == "CRITICAL":
            errors.append("PluginGuard critical finding %s: %s" % (finding.get("rule"), finding.get("detail")))
    return errors


def check(code, prepared):
    errors = []
    if not code.strip():
        return ["The answer has no ```python code block with the plugin source."]
    try:
        tree = ast.parse(code)
        compile(code, "ported.plugin", "exec")
    except SyntaxError as e:
        lines = code.splitlines()
        text = lines[e.lineno - 1].strip() if e.lineno and 0 < e.lineno <= len(lines) else ""
        return ["SyntaxError at line %s: %s. The line is: %s. Return only Python code inside the block." % (
            e.lineno, e.msg, text[:200])]
    meta = _literal_meta(tree)
    target = prepared.get("target_id")
    for key in ("__id__", "__name__", "__version__", "__desktop__", "__ported_from__", "__desktop_os__"):
        if key not in meta:
            errors.append("Metadata %s is missing." % key)
        elif meta[key] is ast:
            errors.append("Metadata %s must be a literal value." % key)
    if meta.get("__id__") not in (None, ast) and meta.get("__id__") != target:
        errors.append("__id__ must be exactly \"%s\"." % target)
    if meta.get("__desktop__") not in (None, ast) and meta.get("__desktop__") is not True:
        errors.append("__desktop__ must be True.")
    system = meta.get("__desktop_os__")
    if isinstance(system, str) and system != current_os():
        errors.append("__desktop_os__ must be \"%s\"." % current_os())
    guarded = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Try):
            for inner in node.body:
                for sub in ast.walk(inner):
                    guarded.add(id(sub))
    for node in ast.walk(tree):
        if isinstance(node, (ast.Import, ast.ImportFrom)) and id(node) not in guarded:
            names = [a.name for a in node.names] if isinstance(node, ast.Import) else [node.module or ""]
            for name in names:
                root = name.split(".")[0]
                if root in _MISSING_ON[current_os()]:
                    errors.append("Line %d: module %s does not exist on %s. Remove it, use a portable "
                                  "alternative or import it inside try/except ImportError." % (
                                      node.lineno, root, _OS_NAMES[current_os()]))
                elif any(root in v for v in _MISSING_ON.values()):
                    errors.append("Line %d: module %s does not exist on every system. Import it inside "
                                  "try/except ImportError so the plugin also loads elsewhere." % (node.lineno, root))
    portable = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            value = node.value
            if "backslash" not in portable and re.search(r"\w\\\w+\.\w{1,5}$|^\w+\\\w+\\", value) and "\n" not in value:
                portable.add("backslash")
                errors.append("Line %d: path %r uses backslashes and breaks on Linux and macOS. Use os.path.join." % (
                    node.lineno, value[:40]))
            if "envpath" not in portable and re.search(r"%(APPDATA|LOCALAPPDATA|USERPROFILE|TEMP)%|^/home/|^~/Library|^/Users/", value):
                portable.add("envpath")
                errors.append("Line %d: system-specific path %r. Use file_utils.get_plugin_dir() or os.path.expanduser." % (
                    node.lineno, value[:40]))
        if isinstance(node, ast.Subscript) and _dotted(node.value) == "os.environ" and "environ" not in portable:
            key = getattr(node.slice, "value", None)
            if key in ("APPDATA", "LOCALAPPDATA", "USERPROFILE", "PROGRAMFILES"):
                portable.add("environ")
                errors.append("Line %d: os.environ[%r] exists only on Windows. Use file_utils.get_plugin_dir()." % (
                    node.lineno, key))
        if isinstance(node, ast.Attribute) and node.attr in ("windll", "WinDLL", "oledll") and "windll" not in portable:
            portable.add("windll")
            errors.append("Line %d: ctypes.%s exists only on Windows and is blocked on desktop. Remove it." % (
                node.lineno, node.attr))
    for node in ast.walk(tree):
        if isinstance(node, ast.Constant) and isinstance(node.value, str) and re.match(r"^[A-Za-z]:[\\/]", node.value):
            errors.append("Line %d: hard-coded drive path %r. Use file_utils.get_plugin_dir() or os.path." % (
                node.lineno, node.value[:40]))
            break
    ported = meta.get("__ported_from__")
    if isinstance(ported, str) and not ported.startswith(prepared.get("original_id") or ""):
        errors.append("__ported_from__ must start with the original id \"%s\"." % prepared.get("original_id"))
    classes = [n for n in tree.body if isinstance(n, ast.ClassDef)
               and any(_dotted(b).endswith("BasePlugin") for b in n.bases)]
    if len(classes) != 1 and not prepared.get("library"):
        errors.append("The file must contain exactly one class that inherits BasePlugin, found %d." % len(classes))
    seen = set()
    supported = _virtual_methods(code)
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                root = alias.name.split(".")[0]
                if root in _FORBIDDEN_MODULES and root not in seen:
                    seen.add(root)
                    errors.append("Module %s is forbidden on desktop." % root)
        elif isinstance(node, ast.ImportFrom):
            root = (node.module or "").split(".")[0]
            if root in _FORBIDDEN_MODULES and root not in seen:
                seen.add(root)
                errors.append("Module %s is forbidden on desktop." % root)
        elif isinstance(node, ast.Call) and _dotted(node.func).endswith("add_hook") and (
                len(node.args) > 1 or (node.args and not (
                    isinstance(node.args[0], ast.Constant) and isinstance(node.args[0].value, str)
                    and node.args[0].value.startswith("TL_")))):
            errors.append("Line %d: add_hook takes one Telegram request or update name, for example "
                          "self.add_hook(\"TL_updateNewMessage\"). It cannot hook Android classes or methods; "
                          "delete this call." % node.lineno)
        elif isinstance(node, ast.Call) and _dotted(node.func).split(".")[-1] in (
                "hook_method", "hook_all_methods") and (
                any(isinstance(a, ast.Constant) and a.value in supported for a in node.args)
                or (_dotted(node.func).endswith("hook_method") and any(
                    f'"{m}"' in code or f"'{m}'" in code for m in supported))):
            continue
        elif isinstance(node, ast.Call):
            name = _dotted(node.func)
            last = name.split(".")[-1]
            if name in ("eval", "exec") or last in _FORBIDDEN_CALLS or name in _FORBIDDEN_CALLS:
                if name not in seen:
                    seen.add(name)
                    errors.append("Line %d: %s does not work on desktop. Delete this call and its handler class; "
                                  "use self.add_hook, the send hook or a menu item instead." % (
                                      node.lineno, name or last))
        elif isinstance(node, ast.Constant) and isinstance(node.value, str):
            lowered = node.value.lower()
            for prefix in _ANDROID_PATHS:
                if prefix in lowered and prefix not in seen:
                    seen.add(prefix)
                    errors.append("Line %d: Android path %s. Use file_utils.get_plugin_dir()." % (
                        node.lineno, prefix))
    errors.extend(_import_errors(tree))
    errors.extend(_undefined_names(tree))
    allowed = set(prepared.get("hosts") or [])
    for host in sorted(_hosts(code) - allowed):
        errors.append("Network host %s is not used by the original plugin. Remove it." % host)
    if not errors:
        errors.extend(_guard_errors(code))
    return errors


_SDK_MODULES = ("base_plugin", "hook_utils", "client_utils", "android_utils", "file_utils",
                "ui.settings", "ui.bulletin", "ui.alert", "markdown_utils", "intents",
                "extera_utils.text_formatting")


def _import_errors(tree):
    import importlib
    errors = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.ImportFrom) or node.level or node.module not in _SDK_MODULES:
            continue
        try:
            module = importlib.import_module(node.module)
        except Exception:
            continue
        for alias in node.names:
            if alias.name in ("add_hook", "add_on_send_message_hook", "add_menu_item", "get_setting",
                              "set_setting", "log"):
                errors.append("Line %d: %s is a plugin method, call it as self.%s()." % (
                    node.lineno, alias.name, alias.name))
                continue
            if alias.name != "*" and not hasattr(module, alias.name):
                owner = next((m for m in _SDK_MODULES if _has(m, alias.name)), None)
                errors.append("Line %d: %s does not exist in %s. %s" % (
                    node.lineno, alias.name, node.module,
                    ("Write: from %s import %s" % (owner, alias.name)) if owner
                    else "Remove %s from this import." % alias.name))
    return errors


def _has(module_name, name):
    import importlib
    import inspect
    try:
        value = getattr(importlib.import_module(module_name), name, None)
    except Exception:
        return False
    if value is None or inspect.ismodule(value) or name.startswith("_"):
        return False
    return getattr(value, "__module__", module_name).split(".")[0] == module_name.split(".")[0]


_USEFUL = ("add_hook", "add_on_send_message_hook", "add_menu_item", "create_settings", "on_update_hook",
           "on_updates_hook", "send_text", "send_message", "send_request", "pre_request_hook",
           "post_request_hook", "on_send_message_hook", "BulletinHelper", "AlertDialogBuilder", "run_on_queue")


_HANDLERS = {"create_settings", "on_update_hook", "on_updates_hook", "pre_request_hook", "post_request_hook",
             "on_send_message_hook"}
_ACTIONS = {"add_hook", "add_on_send_message_hook", "add_menu_item", "send_text", "send_message", "send_request",
            "send_photo", "send_document", "edit_message"}


def has_function(code):
    try:
        tree = ast.parse(code)
    except SyntaxError:
        return True
    for node in ast.walk(tree):
        if isinstance(node, ast.FunctionDef) and node.name in _HANDLERS:
            body = [n for n in node.body if not (isinstance(n, ast.Expr) and isinstance(n.value, ast.Constant))]
            if body and not (len(body) == 1 and isinstance(body[0], (ast.Pass, ast.Return))
                             and (not isinstance(body[0], ast.Return) or body[0].value is None
                                  or _dotted(getattr(body[0].value, "func", body[0].value)).endswith("HookResult"))):
                return True
        if isinstance(node, ast.Call):
            name = _dotted(node.func).split(".")[-1]
            if name in _ACTIONS:
                return True
    return False


def _missing_names(tree):
    import builtins
    bound = set(dir(builtins)) | {"__file__", "__name__", "__builtins__"}
    for node in ast.walk(tree):
        if isinstance(node, (ast.Import, ast.ImportFrom)):
            for alias in node.names:
                bound.add((alias.asname or alias.name).split(".")[0])
        elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            bound.add(node.name)
        elif isinstance(node, ast.arg):
            bound.add(node.arg)
        elif isinstance(node, ast.Name) and isinstance(node.ctx, (ast.Store, ast.Del)):
            bound.add(node.id)
        elif isinstance(node, ast.ExceptHandler) and node.name:
            bound.add(node.name)
        elif isinstance(node, (ast.Global, ast.Nonlocal)):
            bound.update(node.names)
    result = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Name) and isinstance(node.ctx, ast.Load) and node.id not in bound \
                and node.id not in result:
            result.append(node.id)
    return result


_IMPORT_OWNERS = ("base_plugin", "hook_utils", "android_utils", "client_utils", "file_utils",
                  "ui.settings", "ui.bulletin", "ui.alert", "markdown_utils", "intents", "java")


def _owner_of(name):
    import importlib
    for module_name in _IMPORT_OWNERS:
        try:
            module = importlib.import_module(module_name)
        except Exception:
            continue
        if name.startswith("_") or not hasattr(module, name):
            continue
        if module_name in ("java", "base_plugin") or _has(module_name, name):
            return module_name
    return None


_HOOK_CALLS = ("hook_method", "hook_all_methods", "hook_all_constructors")


def _bad_hook_call(node, code, supported):
    if not isinstance(node, ast.Call):
        return False
    last = _dotted(node.func).split(".")[-1]
    if last == "add_hook":
        return len(node.args) > 1 or bool(node.args and not (
            isinstance(node.args[0], ast.Constant) and isinstance(node.args[0].value, str)
            and node.args[0].value.startswith("TL_")))
    if last not in _HOOK_CALLS:
        return False
    if any(isinstance(a, ast.Constant) and a.value in supported for a in node.args):
        return False
    if last == "hook_method" and any(f'"{m}"' in code or f"'{m}'" in code for m in supported):
        return False
    return True


def _strip_hooks(code):
    try:
        tree = ast.parse(code)
    except SyntaxError:
        return code
    supported = _virtual_methods(code)
    simple = (ast.Expr, ast.Assign, ast.AnnAssign, ast.AugAssign)
    spans = []
    for node in ast.walk(tree):
        if isinstance(node, simple) and any(_bad_hook_call(sub, code, supported) for sub in ast.walk(node)):
            spans.append((node.lineno, getattr(node, "end_lineno", node.lineno), node.col_offset))
    if not spans:
        return code
    lines = code.splitlines(keepends=True)
    for start, end, column in sorted(spans, reverse=True):
        if start < 1 or end > len(lines):
            continue
        indent = lines[start - 1][:column]
        if indent.strip():
            continue
        lines[start - 1:end] = [indent + "pass\n"]
    result = "".join(lines)
    try:
        ast.parse(result)
    except SyntaxError:
        return code
    return result


def autofix(code):
    code = _strip_hooks(code)
    try:
        tree = ast.parse(code)
    except SyntaxError:
        return code
    additions = []
    for name in _missing_names(tree):
        owner = _owner_of(name)
        if owner:
            additions.append("from %s import %s" % (owner, name))
    if not additions:
        return code
    lines = code.splitlines(keepends=True)
    position = 0
    for node in tree.body:
        if isinstance(node, (ast.Import, ast.ImportFrom)):
            position = getattr(node, "end_lineno", node.lineno)
        elif position:
            break
    insert = "".join(line + "\n" for line in additions)
    return "".join(lines[:position]) + insert + "".join(lines[position:])


def _virtual_methods(code):
    try:
        import base_plugin
        table = getattr(base_plugin, "_VIRTUAL_HOOKS", {})
    except Exception:
        return set()
    result = {method for (clazz, method) in table if clazz in code}
    if "Browser" in code:
        result.add("openUrl")
    return result


def _undefined_names(tree):
    import builtins
    bound = set(dir(builtins)) | {"__file__", "__name__", "__builtins__", "BasePlugin", "MethodHook",
                                  "MethodReplacement", "HookResult", "HookStrategy", "MenuItemData",
                                  "MenuItemType", "AppEvent", "XposedHook"}
    for node in ast.walk(tree):
        if isinstance(node, (ast.Import, ast.ImportFrom)):
            for alias in node.names:
                bound.add((alias.asname or alias.name).split(".")[0])
        elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            bound.add(node.name)
        elif isinstance(node, ast.arg):
            bound.add(node.arg)
        elif isinstance(node, ast.Name) and isinstance(node.ctx, (ast.Store, ast.Del)):
            bound.add(node.id)
        elif isinstance(node, ast.ExceptHandler) and node.name:
            bound.add(node.name)
        elif isinstance(node, (ast.Global, ast.Nonlocal)):
            bound.update(node.names)
        elif isinstance(node, ast.MatchAs) and node.name:
            bound.add(node.name)
    errors = []
    seen = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Name) and isinstance(node.ctx, ast.Load) and node.id not in bound \
                and node.id not in seen:
            seen.add(node.id)
            errors.append("Line %d: name %s is not defined. Import it or remove the code that uses it." % (
                node.lineno, node.id))
    return errors[:10]


def parse_report(report):
    result = {"kept": [], "changed": [], "dropped": []}
    current = None
    for raw in str(report or "").splitlines():
        line = raw.strip().strip("*").strip()
        lowered = line.lower()
        matched = None
        for key in result:
            if lowered.startswith(key + ":") or lowered.startswith(key + " :") or lowered == key:
                matched = key
        if matched:
            current = matched
            rest = line.split(":", 1)[1].strip() if ":" in line else ""
            if rest and rest.lower() not in ("none", "-", "nothing", "none."):
                result[current].append(rest.lstrip("-*• ").strip())
            continue
        if current and line:
            item = line.lstrip("-*•0123456789. ").strip()
            if item and item.lower() not in ("none", "nothing"):
                result[current].append(item)
    for key in result:
        result[key] = result[key][:20]
    return result


def fix_prompt(errors):
    return (
        "The ported plugin fails the exDesktop checks:\n- "
        + "\n- ".join(errors[:20])
        + "\n\nFix every problem and return the complete corrected plugin in the same answer format."
    )


def save(code, prepared, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, prepared["target_id"] + ".plugin")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(code)
    return path
