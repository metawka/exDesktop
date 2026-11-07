import sys


def install(host, plugins_dir, settings_dir, files_dir, deps_dir, interactive=True):
    if getattr(host, "_exd_sandbox_installed", False):
        return None
    host._exd_sandbox_installed = True

    import _imp
    import _io
    import _socket
    import _thread
    import json
    import locale
    import os
    import pathlib
    import threading
    import time
    import importlib.machinery as machinery

    # Captured
    native = sys.modules.get("_extera")
    win = sys.platform == "win32"
    darwin = sys.platform == "darwin"
    osmod = sys.modules["nt" if win else "posix"]
    _str = str
    _bytes = bytes
    _int = int
    _bool = bool
    _tuple = tuple
    _list = list
    _dict = dict
    _set = set
    _type = type
    _len = len
    _range = range
    _none = type(None)
    _Err = Exception
    _AttrErr = AttributeError
    _PermErr = PermissionError
    _ImpErr = ImportError
    def dict_getter(cls):
        for klass in cls.__mro__:
            if "__dict__" in vars(klass):
                return vars(klass)["__dict__"].__get__
        raise TypeError("no instance dictionary")

    _getframe = sys._getframe
    _get_ident = _thread.get_ident
    _lsort = list.sort
    _O_WRITE = os.O_WRONLY | os.O_RDWR | os.O_APPEND | os.O_CREAT | os.O_TRUNC
    import http.client as _http
    _HTTPConnection = _http.HTTPConnection
    _conn_dict = dict_getter(_http.HTTPConnection)
    _controls = {chr(i): "\\u%04x" % i for i in range(32)}
    _n_log = getattr(native, "log", None) if native is not None else None
    _n_toast = getattr(native, "sbx_toast", None) if native is not None else None
    _n_prompt = getattr(native, "sbx_prompt", None) if native is not None else None
    try:
        import subprocess
        import platform
        import uuid
    except Exception:
        pass
    _monotonic = time.monotonic
    _open = _io.open
    _lower = str.lower
    _sw = str.startswith
    _ew = str.endswith
    _split = str.split
    _join = str.join
    _replace = str.replace
    _rstrip = str.rstrip
    _strip = str.strip
    _rfind = str.rfind
    _find = str.find
    _bdecode = bytes.decode
    _bjoin = bytes.join
    _dget = dict.get
    _dpop = dict.pop
    _dset = dict.__setitem__
    _ditems = dict.items
    _sadd = set.add
    _sdiscard = set.discard
    _append = list.append
    _lpop = list.pop
    _mro = type.__dict__["__mro__"].__get__
    _cdict = type.__dict__["__dict__"].__get__
    _stat = osmod.stat
    _getcwd = osmod.getcwd
    _readlink = getattr(osmod, "readlink", None)
    _fullpath = getattr(osmod, "_getfullpathname", None)
    _finalpath = getattr(osmod, "_getfinalpathname", None)
    _lock_new = _thread.allocate_lock
    _inet_pton = _socket.inet_pton
    _getaddrinfo = _socket.getaddrinfo
    _AF_INET = _socket.AF_INET
    _AF_INET6 = _socket.AF_INET6
    _direntry_path = os.DirEntry.__dict__["path"].__get__
    import socket as _socket_module
    _socketpair_code = getattr(getattr(_socket_module, "socketpair", None), "__code__", None)
    _DirEntry = os.DirEntry
    _Spec = machinery.ModuleSpec
    _spec_dict = dict_getter(machinery.ModuleSpec)
    sep = "\\" if win else "/"
    mutex = _thread.RLock()
    UNKNOWN = "<unknown>"

    # Pathlib
    _pl_classes = (pathlib.PurePath, pathlib.PurePosixPath, pathlib.PureWindowsPath,
                   pathlib.Path, pathlib.PosixPath, pathlib.WindowsPath)
    _pl_originals = (("__fspath__", pathlib.PurePath.__dict__["__fspath__"]),
                     ("__str__", pathlib.PurePath.__dict__["__str__"]),
                     ("_format_parsed_parts", pathlib.PurePath.__dict__["_format_parsed_parts"]))
    _pl_slot_str = pathlib.PurePath.__dict__["_str"].__get__
    _pl_slot_drv = pathlib.PurePath.__dict__["_drv"].__get__
    _pl_slot_root = pathlib.PurePath.__dict__["_root"].__get__
    _pl_slot_parts = pathlib.PurePath.__dict__["_parts"].__get__
    _flavours = []
    for flavour in (pathlib._windows_flavour, pathlib._posix_flavour):
        fdict = dict_getter(_type(flavour))
        _append(_flavours, (flavour, fdict, flavour.sep, fdict(flavour).get("join")))

    def lookup(cls, name):
        for klass in _mro(cls):
            mapping = _cdict(klass)
            if name in mapping:
                return mapping[name]
        return None

    def same(a, b):
        return a is b

    def pathlib_str(value):
        cls = _type(value)
        known = False
        for klass in _pl_classes:
            if klass is cls:
                known = True
        if not known:
            return None
        for name, original in _pl_originals:
            if lookup(cls, name) is not original:
                return None
        flavour = lookup(cls, "_flavour")
        current = None
        for entry in _flavours:
            if entry[0] is flavour:
                current = entry
        if current is None or _dget(current[1](flavour), "join") is not current[3] \
                or _dget(current[1](flavour), "sep") is not current[2]:
            return None
        try:
            text = _pl_slot_str(value)
            if _type(text) is _str:
                return text
            return None
        except _AttrErr:
            pass
        drv = _pl_slot_drv(value)
        root = _pl_slot_root(value)
        parts = _pl_slot_parts(value)
        if _type(drv) is not _str or _type(root) is not _str or _type(parts) is not _list:
            return None
        for part in parts:
            if _type(part) is not _str:
                return None
        if drv or root:
            text = drv + root + _join(current[2], parts[1:])
        else:
            text = _join(current[2], parts)
        return text or "."

    def text_of(value):
        cls = _type(value)
        if cls is _str:
            return value
        if cls is _bytes:
            return _bdecode(value, "utf-8", "surrogateescape")
        for klass in _mro(cls):
            if klass is _str:
                return _join("", [value])
            if klass is _bytes:
                return _bdecode(_bjoin(b"", [value]), "utf-8", "surrogateescape")
        return None

    def path_of(value):
        cls = _type(value)
        if value is None or cls is _int or cls is _bool:
            return ""
        text = text_of(value)
        if text is not None:
            return text
        if cls is _DirEntry:
            return _direntry_path(value)
        return pathlib_str(value)

    # Paths
    def norm(path):
        if win:
            return _lower(_replace(path, "/", "\\"))
        return _lower(path) if darwin else path

    def resolve_posix(path):
        parts = [p for p in _split(path, "/") if p and p != "."]
        out = []
        hops = 0
        index = 0
        while index < _len(parts):
            part = parts[index]
            index += 1
            if part == "..":
                if out:
                    _lpop(out)
                continue
            _append(out, part)
            if _readlink is None:
                continue
            current = "/" + _join("/", out)
            try:
                target = _readlink(current)
            except _Err:
                continue
            hops += 1
            if hops > 40:
                return current
            rest = parts[index:]
            if _sw(target, "/"):
                out = []
            else:
                _lpop(out)
            parts = [p for p in _split(target, "/") if p and p != "."] + rest
            index = 0
        return "/" + _join("/", out)

    def absolute(path, base=None):
        if _type(path) is not _str or not path:
            return None
        if "\x00" in path:
            return None
        if win:
            if base is not None and not (_len(path) > 1 and (path[1] == ":" or _sw(path, "\\") or _sw(path, "/"))):
                path = base + "\\" + path
            try:
                full = _fullpath(path)
            except _Err:
                return None
            if _finalpath is not None:
                probe = full
                tail = ""
                resolved = None
                for _ in _range(64):
                    try:
                        resolved = _finalpath(probe)
                        break
                    except _Err:
                        cut = _rfind(probe, "\\")
                        if cut <= 2:
                            break
                        tail = probe[cut:] + tail
                        probe = probe[:cut]
                if resolved is not None:
                    if _sw(resolved, "\\\\?\\UNC\\"):
                        resolved = "\\\\" + resolved[8:]
                    elif _sw(resolved, "\\\\?\\"):
                        resolved = resolved[4:]
                    full = resolved + tail
            return norm(full)
        if not _sw(path, "/"):
            path = (base if base is not None else _getcwd()) + "/" + path
        return norm(resolve_posix(path))

    def inside(path, root):
        return _bool(root) and (path == root or _sw(path, _rstrip(root, sep) + sep))

    def is_dir(path):
        try:
            return (_stat(path).st_mode & 0o170000) == 0o040000
        except _Err:
            return False

    def is_file(path):
        try:
            return (_stat(path).st_mode & 0o170000) == 0o100000
        except _Err:
            return False

    def parent(path):
        cut = _rfind(path, sep)
        return path[:cut] if cut > 0 else path

    def base_name(path):
        return path[_rfind(path, sep) + 1:]

    original_env = dict(os.environ)
    home = absolute(os.path.expanduser("~"))
    plugins_root = absolute(plugins_dir)
    settings_root = absolute(settings_dir)
    files_root = absolute(files_dir)
    tdata_root = parent(files_root) if base_name(files_root) == "plugins" else files_root
    sdk_root = absolute(os.path.dirname(os.path.abspath(__file__)))
    runtime_root = parent(sdk_root)
    executable = sys.executable or ""
    if executable and (win or _sw(_lower(os.path.basename(executable)), ("telegram", "exdesktop"))):
        app_root = absolute(os.path.dirname(executable))
    else:
        app_root = parent(runtime_root)
    deps_root = absolute(deps_dir)
    fake_home = absolute(os.path.join(files_dir, "plugin_home"))
    read_roots = tuple(r for r in {
        absolute(sys.prefix), absolute(sys.base_prefix), sdk_root, deps_root, runtime_root,
        absolute(os.path.join(app_root, "python")), absolute(os.path.join(app_root, "python311")),
    } if r)
    shared_rw = tuple(r for r in (
        absolute(os.path.join(files_dir, "plugin_cache")),
        absolute(os.path.join(files_dir, "plugin_images")),
        absolute(os.path.join(files_dir, "plugin_videos")),
        absolute(os.path.join(files_dir, "plugin_audios")),
        absolute(os.path.join(files_dir, "plugin_documents")),
        absolute(os.path.join(files_dir, "plugin_tmp")),
        fake_home,
    ) if r)
    tmp_root = absolute(os.path.join(files_dir, "plugin_tmp"))
    for folder in (os.path.join(files_dir, "plugin_tmp"), os.path.join(files_dir, "plugin_home"),
                   os.path.join(plugins_dir, ".data"), os.path.join(plugins_dir, ".cache")):
        try:
            os.makedirs(folder, exist_ok=True)
        except Exception:
            pass
    try:
        import tempfile
        tempfile.tempdir = os.path.join(files_dir, "plugin_tmp")
    except Exception:
        pass
    if win:
        system_root = absolute(original_env.get("SYSTEMROOT") or original_env.get("WINDIR") or "C:\\Windows")
        hard_roots = tuple(r for r in (
            system_root,
            absolute(original_env.get("PROGRAMDATA") or "C:\\ProgramData"),
            absolute(original_env.get("PROGRAMFILES") or "C:\\Program Files"),
            absolute(original_env.get("PROGRAMFILES(X86)") or "C:\\Program Files (x86)"),
            absolute(os.path.join(home or "C:\\", "AppData")),
            absolute(os.path.join(home or "C:\\", "NTUSER.DAT")),
        ) if r)
        font_roots = tuple(r for r in (absolute(os.path.join(system_root, "Fonts")),
                                       absolute(os.path.join(original_env.get("LOCALAPPDATA") or "", "Microsoft", "Windows", "Fonts"))) if r)
    else:
        hard_roots = tuple(norm(p) for p in ("/proc", "/sys", "/dev", "/etc", "/var", "/boot", "/root", "/run",
                                             "/private/etc", "/private/var", "/Library/Keychains",
                                             "/System/Library/Keychains"))
        font_roots = tuple(r for r in (norm("/usr/share/fonts"), norm("/usr/local/share/fonts"),
                                       norm("/Library/Fonts"), norm("/System/Library/Fonts"),
                                       absolute(os.path.join(home or "/", ".fonts")),
                                       absolute(os.path.join(home or "/", "Library", "Fonts"))) if r)
    sensitive_names = (
        "tdata", "telegram desktop", "ayugram", ".ssh", ".gnupg", ".aws", ".kube", ".docker",
        "google\\chrome", "google/chrome", "chromium", "microsoft\\edge", "microsoft/edge",
        "bravesoftware", "yandex\\yandexbrowser", "opera software", "vivaldi", "mozilla",
        "discord", "exodus", "electrum", "atomic", "ethereum", "bitcoin", "monero",
        "metamask", "trust wallet", "ledger live", "keepass", "1password", "bitwarden",
        "microsoft\\credentials", "microsoft\\protect", "microsoft\\vault", "steam\\config",
        "filezilla", "winscp", "putty", "openvpn", "wireguard", "keychains", ".netrc", ".git-credentials",
    )
    user_roots = tuple(r for r in (
        absolute(os.path.join(home, "Desktop")), absolute(os.path.join(home, "Downloads")),
        absolute(os.path.join(home, "Documents")), absolute(os.path.join(home, "Pictures")),
        absolute(os.path.join(home, "Music")), absolute(os.path.join(home, "Videos")),
        absolute(os.path.join(home, "OneDrive")),
    ) if r and home)
    blocked_suffixes = (".pyd", ".so", ".dll", ".dylib", ".exe", ".com", ".scr", ".bat", ".cmd", ".ps1",
                        ".vbs", ".vbe", ".js", ".jse", ".wsf", ".wsh", ".msi", ".msc", ".lnk", ".pif",
                        ".hta", ".cpl", ".reg", ".sh", ".command", ".app", ".pth")
    source_exts = (".plugin", ".py", ".pyc", ".elyx", ".eaf", ".zip")
    sealed = {"nt", "posix", "_thread", "_uuid", "winreg", "_winreg", "_winapi", "msvcrt", "_overlapped",
              "pwd", "grp", "cffi", "_cffi_backend", "_imp", "PIL._imaging", "_socket",
              "_posixsubprocess", "_tkinter", "_io"}

    # State
    state_path = os.path.join(settings_dir, "_sandbox.json")
    try:
        with open(state_path, encoding="utf-8") as f:
            stored = json.load(f)
    except Exception:
        stored = {}
    paths_allowed = {}
    for key, values in (stored.get("paths") or {}).items():
        if _type(key) is _str and _type(values) is _list:
            paths_allowed[key] = {v for v in values if _type(v) is _str}
    net_rules = {}
    for key, rules in (stored.get("net") or {}).items():
        if _type(key) is _str and _type(rules) is _dict:
            net_rules[key] = {d: r for d, r in rules.items() if _type(d) is _str and r in ("allow", "deny")}
    files_granted = {}
    once = set()
    pending = {}
    pending_keys = {}
    allowed_ips = {}
    toasts = {}
    counter = [0]

    lang = ""
    try:
        import exd_java
        lang = exd_java.language() or ""
    except Exception:
        pass
    if not lang:
        try:
            lang = (locale.getlocale()[0] or "").lower()
        except Exception:
            pass
    russian = _sw(lang, ("ru", "russian", "uk", "ukrainian", "be", "kk"))

    texts = {
        "net_text": ("Плагин «{name}» запрашивает подключение к {target}.",
                     "Plugin \"{name}\" requests a connection to {target}."),
        "net_always": ("Разрешить всегда", "Always Allow"),
        "net_once": ("Один раз", "Allow Once"),
        "deny": ("Запретить", "Deny"),
        "path_text": ("Плагин «{name}» запрашивает доступ к папке {target}. Разрешение даётся на свой страх и риск: плагин получит доступ ко всем файлам этой папки.",
                      "Plugin \"{name}\" requests access to the folder {target}. The permission is granted at your own risk: the plugin will access all files in this folder."),
        "path_allow": ("Разрешить на свой страх и риск", "Allow at My Own Risk"),
        "hard": ("Плагин «{name}» пытался открыть служебные файлы. Доступ заблокирован.",
                 "Plugin \"{name}\" tried to open protected files. Access blocked."),
        "process": ("Плагин «{name}» пытался запустить программу. Запуск заблокирован.",
                    "Plugin \"{name}\" tried to start a program. Launch blocked."),
        "native": ("Плагин «{name}» пытался получить прямой доступ к системе. Действие заблокировано.",
                   "Plugin \"{name}\" tried to access the system directly. Action blocked."),
        "denied_net": ("Плагину «{name}» запрещено подключение к {target}.",
                       "Plugin \"{name}\" is not allowed to connect to {target}."),
        "unknown": ("Заблокировано действие плагина из неизвестного контекста.",
                    "A plugin action from an unknown context was blocked."),
    }

    def text(key, target=""):
        value = texts[key][0 if russian else 1]
        return _replace(value, "{target}", target)

    def log(message):
        if _n_log is not None:
            try:
                _n_log("sandbox: " + message)
            except _Err:
                pass

    def toast(plugin_id, key, target=""):
        stamp = plugin_id + "\x00" + key
        now = _monotonic()
        if now - _dget(toasts, stamp, -100.0) < 30.0:
            return
        _dset(toasts, stamp, now)
        if _n_toast is None:
            return
        try:
            if plugin_id == UNKNOWN:
                _n_toast(text("unknown"), "")
            else:
                _n_toast(text(key, target), plugin_id)
        except _Err:
            pass

    def escape(value):
        out = []
        for ch in value:
            if ch == "\\" or ch == "\"":
                _append(out, "\\" + ch)
            elif ch < " ":
                _append(out, _controls[ch])
            else:
                _append(out, ch)
        return "\"" + _join("", out) + "\""

    def save_state():
        parts = []
        for plugin_id, folders in _ditems(paths_allowed):
            _append(parts, escape(plugin_id) + ":[" + _join(",", [escape(f) for f in folders]) + "]")
        nets = []
        for plugin_id, rules in _ditems(net_rules):
            _append(nets, escape(plugin_id) + ":{" + _join(",", [escape(d) + ":" + escape(r) for d, r in _ditems(rules)]) + "}")
        data = "{\"paths\":{" + _join(",", parts) + "},\"net\":{" + _join(",", nets) + "}}"
        try:
            with _open(state_path, "w", encoding="utf-8") as handle:
                handle.write(data)
        except _Err:
            pass

    # Owner
    def plugin_of(original, normalized):
        rest = normalized[_len(plugins_root):]
        tail = original[_len(original) - _len(rest):] if _len(original) >= _len(rest) else rest
        pieces = [p for p in _split(_replace(tail, "/", sep), sep) if p]
        if not pieces:
            return UNKNOWN
        head = _lower(pieces[0])
        if head in (".data", ".cache", "elyxplugins") and _len(pieces) > 1:
            return pieces[1]
        name = pieces[0]
        cut = _rfind(name, ".")
        return name[:cut] if cut > 0 else name

    def scan(frame):
        plugin = None
        anon = False
        importing = False
        chain = []
        while frame is not None:
            name = frame.f_code.co_filename
            if _type(name) is not _str:
                anon = True
            elif _sw(name, ("<frozen importlib", "<frozen zipimport")):
                importing = True
            elif _sw(name, "<"):
                if not _sw(name, "<frozen"):
                    anon = True
            else:
                path = norm(name)
                if inside(path, plugins_root):
                    found = plugin_of(name, path)
                    if plugin is None:
                        plugin = found
                    _append(chain, norm(found))
            frame = frame.f_back
        return plugin, anon, importing, chain

    if native is not None and hasattr(native, "sbx_state"):
        state_of = native.sbx_state
    else:
        state_of = lambda: (None, True, True)

    def owner_at(frame):
        plugin, anon, importing, chain = scan(frame)
        if plugin is not None:
            return plugin, importing, chain
        forced, marked, main = state_of()
        if forced is not None:
            return forced, importing, chain
        if anon or (not main and not marked):
            return UNKNOWN, importing, chain
        return "", importing, chain

    def owner_now():
        try:
            return owner_at(_getframe(1))[0]
        except _Err:
            return UNKNOWN

    def own_paths(plugin_id):
        key = norm(plugin_id)
        return (plugins_root + sep + ".data" + sep + key, plugins_root + sep + ".cache" + sep + key)

    def owned_name(plugin_id, name):
        key = _join("", [c for c in _lower(plugin_id) if ("a" <= c <= "z") or ("0" <= c <= "9")])
        flat = _join("", [c for c in _lower(name) if ("a" <= c <= "z") or ("0" <= c <= "9")])
        return _len(key) >= 3 and key in flat

    def own_source(plugin_id, path):
        key = plugins_root + sep + norm(plugin_id)
        return path == key + ".plugin" or path == key + ".elyx" or path == key + ".eaf"

    def classify(plugin_id, path, write, importing, listing=False, chain=()):
        if plugin_id == UNKNOWN:
            if not write:
                for root in read_roots:
                    if inside(path, root):
                        return "allow"
            return "hard"
        name = base_name(path)
        if path in ("\\\\.\\nul", "\\\\.\\con") or (not win and path == "/dev/null"):
            return "allow"
        if win and (_sw(path, "\\\\.\\") or _sw(path, "\\\\?\\")):
            return "hard"
        if win and name in ("nul", "con"):
            return "allow"
        if write:
            plain = _rstrip(_split(name, ":")[0], ". ")
            for suffix in blocked_suffixes:
                if _ew(plain, suffix):
                    return "hard"
        for root in own_paths(plugin_id) + shared_rw:
            if root and inside(path, root):
                return "allow"
        for caller_id in chain:
            for root in own_paths(caller_id):
                if inside(path, root):
                    return "allow"
            if path == settings_root + sep + caller_id + ".json":
                return "allow"
        if inside(path, settings_root):
            if path == settings_root + sep + norm(plugin_id) + ".json":
                return "allow"
            if not write and (path == settings_root + sep + "_state.json" or path == settings_root + sep + "_trusted.json"):
                return "allow"
            return "hard"
        if inside(path, plugins_root):
            if own_source(plugin_id, path):
                return "hard" if write else "allow"
            rest = [p for p in _split(path[_len(plugins_root):], sep) if p]
            if not rest:
                return "allow" if listing else "hard"
            head = rest[0]
            if head in (".data", ".cache", "elyxplugins"):
                if _len(rest) == 1:
                    return "allow" if (listing or not write) else "hard"
                if rest[1] == norm(plugin_id):
                    return "allow"
                if head == "elyxplugins" and not write and importing:
                    return "allow"
                return "hard"
            if not write and importing:
                for ext in source_exts:
                    if _ew(head, ext) or _ew(path, ext):
                        return "allow"
                if "__pycache__" in rest:
                    return "allow"
            if _ew(head, source_exts) or _sw(head, "."):
                return "hard"
            return "allow"
        if inside(path, files_root):
            rest = [p for p in _split(path[_len(files_root):], sep) if p]
            if rest and owned_name(plugin_id, rest[0]):
                return "allow"
            if not write and _len(rest) > 1 and rest[0] == "elyx_local_libs" and rest[1] == norm(plugin_id):
                return "allow"
        if not write:
            for root in read_roots:
                if inside(path, root):
                    return "allow"
            for root in font_roots:
                if inside(path, root):
                    return "allow"
        if inside(path, tdata_root) or inside(path, files_root):
            return "hard"
        if inside(path, app_root):
            return "hard" if write else "user"
        for root in hard_roots:
            if inside(path, root):
                return "hard"
        if home and inside(path, home):
            rest = [p for p in _split(path[_len(home):], sep) if p]
            if rest and _sw(rest[0], ".") and not owned_name(plugin_id, rest[0]):
                return "hard"
        for marker in sensitive_names:
            if marker in path:
                return "hard"
        if not win and _sw(path, "/") and _len([p for p in _split(path, "/") if p]) <= 1 and not listing:
            return "hard"
        return "user"

    def folder_key(path):
        for root in user_roots:
            if inside(path, root):
                rest = [p for p in _split(path[_len(root):], sep) if p]
                if rest and is_dir(root + sep + rest[0]):
                    return root + sep + rest[0]
                return root
        return path if is_dir(path) else parent(path)

    def path_allowed(plugin_id, path):
        if path in _dget(files_granted, plugin_id, ()):
            return True
        for folder in _dget(paths_allowed, plugin_id, ()):
            if inside(path, folder):
                return True
        return False

    # Prompts
    def ask(kind, plugin_id, target, buttons):
        key = kind + "\x00" + plugin_id + "\x00" + target
        with mutex:
            token = _dget(pending_keys, key)
            if token is None:
                counter[0] += 1
                token = "s%d" % counter[0]
                entry = {"kind": kind, "plugin": plugin_id, "target": target, "locks": [], "answer": None}
                _dset(pending, token, entry)
                _dset(pending_keys, key, token)
                fresh = True
            else:
                entry = pending[token]
                fresh = False
            waiter = _lock_new()
            waiter.acquire()
            _append(entry["locks"], waiter)
        if not interactive or _n_prompt is None:
            answer(token, "deny")
            return
        if fresh:
            message = text("net_text" if kind == "net" else "path_text", target)
            data = "[" + _join(",", ["{\"which\":%d,\"text\":%s,\"value\":%s}" % (which, escape(label), escape(value))
                                     for label, value, which in buttons]) + "]"
            try:
                _n_prompt(kind, plugin_id, target, message, data, token)
            except _Err:
                answer(token, "deny")
                return
        if _get_ident() == main_ident:
            return
        waiter.acquire(True, 120)

    def answer(token, value):
        if _type(token) is not _str or _type(value) is not _str:
            return None
        with mutex:
            entry = _dpop(pending, token, None)
            if entry is None:
                return None
            _dpop(pending_keys, entry["kind"] + "\x00" + entry["plugin"] + "\x00" + entry["target"], None)
            plugin_id = entry["plugin"]
            target = entry["target"]
            changed = False
            if entry["kind"] == "net":
                if value == "always":
                    _dset(net_rules.setdefault(plugin_id, {}), target, "allow")
                    changed = True
                elif value == "once":
                    _sadd(once, plugin_id + "\x00" + target)
                elif value == "deny":
                    _dset(net_rules.setdefault(plugin_id, {}), target, "deny")
                    changed = True
            elif value == "allow":
                _sadd(paths_allowed.setdefault(plugin_id, _set()), target)
                changed = True
            if changed:
                save_state()
            for waiter in entry["locks"]:
                try:
                    waiter.release()
                except _Err:
                    pass
        return None

    main_ident = _get_ident()

    # Network
    def is_ip(name):
        for family in (_AF_INET, _AF_INET6):
            try:
                _inet_pton(family, name)
                return True
            except _Err:
                pass
        return False

    def is_loopback(name):
        if name in ("localhost", "ip6-localhost", "ip6-loopback"):
            return True
        if _sw(name, "127.") and is_ip(name):
            return True
        return name in ("::1", "0:0:0:0:0:0:0:1")

    def domain_key(name):
        if is_ip(name):
            return name
        parts = [p for p in _split(name, ".") if p]
        if _len(parts) > 2 and parts[-2] in ("co", "com", "org", "net", "gov", "edu", "ac") and _len(parts[-1]) == 2:
            return _join(".", parts[-3:])
        return _join(".", parts[-2:]) if _len(parts) >= 2 else name

    def remember_ips(plugin_id, name):
        try:
            records = _getaddrinfo(name, None)
        except _Err:
            return
        bucket = allowed_ips.setdefault(plugin_id, {})
        stamp = _monotonic()
        for record in records:
            address = record[4][0]
            if _type(address) is _str:
                _dset(bucket, address, stamp)

    def check_net(plugin_id, host_name):
        name = _lower(_strip(_strip(host_name), "[]"))
        if not name:
            return None
        if plugin_id == UNKNOWN:
            return "exDesktop sandbox: network access from an unknown context is blocked"
        if is_loopback(name):
            domain = "localhost"
        else:
            domain = domain_key(_rstrip(name, "."))
            if is_ip(name):
                seen = _dget(_dget(allowed_ips, plugin_id, {}), name)
                if seen is not None and _monotonic() - seen < 3600:
                    return None
        rule = _dget(_dget(net_rules, plugin_id, {}), domain)
        if rule == "allow":
            if not is_ip(name) and domain != "localhost":
                remember_ips(plugin_id, name)
            return None
        stamp = plugin_id + "\x00" + domain
        if stamp in once:
            _sdiscard(once, stamp)
            if not is_ip(name) and domain != "localhost":
                remember_ips(plugin_id, name)
            return None
        if rule == "deny":
            toast(plugin_id, "denied_net", domain)
            return "exDesktop sandbox: connection to %s is denied" % domain
        ask("net", plugin_id, domain, (
            (text("net_always"), "always", -1),
            (text("net_once"), "once", -3),
            (text("deny"), "deny", -2),
        ))
        rule = _dget(_dget(net_rules, plugin_id, {}), domain)
        if rule == "allow" or stamp in once:
            _sdiscard(once, stamp)
            if not is_ip(name) and domain != "localhost":
                remember_ips(plugin_id, name)
            return None
        log("%s: network denied %s" % (plugin_id, domain))
        return "exDesktop sandbox: connection to %s is not allowed" % domain

    def url_host(url):
        value = _strip(url)
        cut = _find(value, "://")
        if cut < 0:
            return ""
        rest = value[cut + 3:]
        for stop in ("/", "?", "#", "\\"):
            index = _find(rest, stop)
            if index >= 0:
                rest = rest[:index]
        at = _rfind(rest, "@")
        if at >= 0:
            rest = rest[at + 1:]
        if _sw(rest, "["):
            end = _find(rest, "]")
            return rest[1:end] if end > 0 else rest[1:]
        colon = _find(rest, ":")
        return rest[:colon] if colon >= 0 else rest

    def address_host(address):
        if _type(address) is _tuple and _len(address) >= 2:
            host_name = text_of(address[0])
            if host_name is None or _type(address[1]) is not _int:
                return None
            return host_name
        return None

    # Decisions
    def deny_path(plugin_id, raw, write, importing, listing=False, base=None, chain=()):
        text_path = path_of(raw)
        if text_path is None:
            return "exDesktop sandbox: unsupported path object"
        if text_path == "":
            return None
        path = absolute(text_path, base)
        if path is None:
            return "exDesktop sandbox: invalid path"
        verdict = classify(plugin_id, path, write, importing, listing, chain)
        if verdict == "allow":
            return None
        if verdict == "hard":
            log("%s: protected path %s" % (plugin_id, path))
            toast(plugin_id, "hard")
            return "exDesktop sandbox: access to protected files is blocked"
        if path_allowed(plugin_id, path):
            return None
        folder = folder_key(path)
        ask("path", plugin_id, folder, (
            (text("path_allow"), "allow", -1),
            (text("deny"), "deny", -2),
        ))
        if path_allowed(plugin_id, path):
            return None
        log("%s: path denied %s" % (plugin_id, path))
        return "exDesktop sandbox: access to %s is not allowed" % folder

    read_events = {"os.listdir", "os.scandir", "glob.glob"}
    write_events = {"os.remove", "os.rmdir", "os.mkdir", "os.chmod", "os.chown", "os.utime", "os.truncate",
                    "os.mkfifo", "os.mknod", "os.setxattr", "os.removexattr", "shutil.rmtree", "shutil.chown",
                    "tempfile.mkstemp", "tempfile.mkdtemp"}
    pair_events = {"os.rename", "shutil.move", "shutil.copyfile", "shutil.copytree", "shutil.copymode",
                   "shutil.copystat", "os.link"}
    process_events = {"subprocess.Popen", "os.system", "os.exec", "os.spawn", "os.posix_spawn", "os.startfile",
                      "os.fork", "os.forkpty", "os.kill", "os.killpg", "pty.spawn"}
    internals = {"code.__new__", "marshal.loads", "marshal.load", "sys.settrace", "sys.setprofile",
                 "gc.get_objects", "gc.get_referrers", "gc.get_referents", "sys._current_frames",
                 "sys.addaudithook", "cpython.PyInterpreterState_Clear", "os.chdir", "os.chroot",
                 "os.add_dll_directory", "os.putenv", "os.unsetenv", "sqlite3.enable_load_extension",
                 "sqlite3.load_extension", "socket.sethostname"}
    net_name_events = {"socket.getaddrinfo", "socket.gethostbyname", "socket.gethostbyname_ex",
                       "socket.gethostbyaddr"}
    connect_events = {"ftplib.connect", "smtplib.connect", "poplib.connect", "imaplib.open", "nntplib.connect",
                      "telnetlib.Telnet.open"}

    def handle(event, args, frame):
        if event == "socket.gethostname":
            return "exDesktop sandbox: host name is not available"
        owner, importing, chain = owner_at(frame)
        if owner == "":
            return None

        def deny_here(raw, write, importing, listing=False, base=None):
            return deny_path(owner, raw, write, importing, listing, base, chain)

        if _type(args) is not _tuple:
            return "exDesktop sandbox: invalid audit arguments"
        if event == "open":
            raw = args[0] if args else None
            mode = args[1] if _len(args) > 1 else "r"
            flags = args[2] if _len(args) > 2 else 0
            mode_text = text_of(mode) if mode is not None else "r"
            write = mode_text is None
            if not write:
                for c in "wax+":
                    if c in mode_text:
                        write = True
            if not write and _type(flags) is _int and (flags & _O_WRITE):
                write = True
            return deny_here(raw, write, importing)
        if event in read_events:
            return deny_here(args[0] if args else None, False, importing, event != "glob.glob")
        if event in write_events:
            return deny_here(args[0] if args else None, True, importing)
        if event == "os.symlink":
            target = args[1] if _len(args) > 1 else None
            verdict = deny_here(target, True, importing)
            if verdict:
                return verdict
            target_text = path_of(target)
            base = parent(absolute(target_text)) if target_text else None
            return deny_here(args[0] if args else None, True, importing, base=base)
        if event in pair_events:
            if args:
                verdict = deny_here(args[0], event in ("os.rename", "shutil.move", "os.link"), importing)
                if verdict:
                    return verdict
            if _len(args) > 1:
                return deny_here(args[1], True, importing)
            return None
        if event in ("shutil.make_archive", "shutil.unpack_archive"):
            for index in _range(_len(args)):
                verdict = deny_here(args[index], index != 0 if event == "shutil.unpack_archive" else index == 0,
                                    importing)
                if verdict:
                    return verdict
            return None
        if event == "sqlite3.connect":
            database = args[0] if args else None
            text_db = path_of(database)
            if text_db is None:
                return "exDesktop sandbox: unsupported database path"
            if text_db in ("", ":memory:"):
                return None
            if _sw(text_db, "file:"):
                target = text_db[5:]
                cut = _find(target, "?")
                if cut >= 0:
                    if "mode=memory" in target[cut:]:
                        return None
                    target = target[:cut]
                if _sw(target, "///"):
                    target = target[3:]
                elif _sw(target, "//"):
                    target = target[2:]
                if target in ("", ":memory:"):
                    return None
                text_db = target
            return deny_here(text_db, True, importing)
        if event == "import":
            name = args[0] if args else ""
            if _type(name) is not _str:
                return "import:exDesktop sandbox: invalid module name"
            top = _split(name, ".")[0]
            if name in sealed or top in sealed:
                log("%s: import %s blocked" % (owner, name))
                if top in ("cffi", "_cffi_backend", "_ctypes"):
                    toast(owner, "native")
                return "import:exDesktop sandbox: module %s is not available to plugins" % name
            return None
        if event == "compile":
            filename = args[1] if _len(args) > 1 else None
            if _type(filename) is not _str or _sw(filename, "<") or not filename:
                return None
            if _sw(frame.f_code.co_filename, ("<frozen importlib", "<frozen zipimport")):
                return None
            path = absolute(filename)
            if path is None:
                return "exDesktop sandbox: invalid code file name"
            if owner == UNKNOWN:
                return "exDesktop sandbox: compile from an unknown context is blocked"
            allowed = own_source(owner, path) or inside(path, plugins_root + sep + "elyxplugins" + sep + norm(owner))
            for root in own_paths(owner):
                if inside(path, root):
                    allowed = True
            if not allowed and importing and inside(path, plugins_root):
                allowed = True
            if not allowed:
                log("%s: compile as %s blocked" % (owner, path))
                return "exDesktop sandbox: code can be compiled only under the plugin's own name"
            return None
        if event in ("exd.create_dynamic", "exd.create_builtin"):
            spec = args[0] if args else None
            if _type(spec) is not _Spec:
                return "import:exDesktop sandbox: invalid module spec"
            data = _spec_dict(spec)
            name = _dget(data, "name")
            if _type(name) is not _str:
                return "import:exDesktop sandbox: invalid module name"
            top = _split(name, ".")[0]
            if name in sealed or top in sealed:
                return "import:exDesktop sandbox: module %s is not available to plugins" % name
            if event == "exd.create_dynamic":
                origin = _dget(data, "origin")
                path = absolute(origin) if _type(origin) is _str else None
                trusted = False
                if path is not None:
                    for root in read_roots:
                        if inside(path, root):
                            trusted = True
                if not trusted:
                    log("%s: native module %s blocked" % (owner, name))
                    toast(owner, "native")
                    return "import:exDesktop sandbox: native modules outside exDesktop are not available to plugins"
            return None
        if event in ("exd.disk_usage", "exd.statvfs"):
            return deny_here(args[0] if args else None, False, importing)
        if event == "exd.getlogin":
            return "exDesktop sandbox: user name is not available"
        if _sw(event, "ctypes.") or _sw(event, "winreg.") or _sw(event, "_winapi.") or _sw(event, "msvcrt.") \
                or _sw(event, "fcntl."):
            log("%s: %s blocked" % (owner, event))
            toast(owner, "native")
            return "exDesktop sandbox: native access is blocked"
        if event in process_events:
            log("%s: %s blocked" % (owner, event))
            toast(owner, "process")
            return "exDesktop sandbox: starting programs is blocked"
        if event in internals:
            if event in ("marshal.loads", "marshal.load", "code.__new__")                     and _sw(frame.f_code.co_filename, ("<frozen importlib", "<frozen zipimport")):
                return None
            log("%s: %s blocked" % (owner, event))
            return "exDesktop sandbox: %s is not available to plugins" % event
        if event == "urllib.Request":
            url = text_of(args[0]) if args else None
            if url is None:
                return "exDesktop sandbox: invalid URL"
            host_name = url_host(url)
            return check_net(owner, host_name) if host_name else None
        if event in net_name_events:
            name = text_of(args[0]) if args else None
            if name is None:
                return None if (args and args[0] is None) else "exDesktop sandbox: invalid host"
            return check_net(owner, name)
        if event == "socket.getnameinfo":
            return check_net(owner, address_host(args[0]) or "") if args else None
        if event == "http.client.connect":
            if _len(args) > 1:
                name = text_of(args[1])
                if name is None:
                    return "exDesktop sandbox: invalid host"
                if is_loopback(_lower(name)):
                    connection = args[0]
                    tunnel = None
                    for klass in _mro(_type(connection)):
                        if klass is _HTTPConnection:
                            try:
                                tunnel = _dget(_conn_dict(connection), "_tunnel_host")
                            except _Err:
                                tunnel = None
                    tunnel_text = text_of(tunnel) if tunnel is not None else None
                    if tunnel_text:
                        verdict = check_net(owner, tunnel_text)
                        if verdict:
                            return verdict
                return check_net(owner, name)
            return None
        if event in connect_events:
            if _len(args) > 1:
                name = text_of(args[1])
                return check_net(owner, name) if name is not None else "exDesktop sandbox: invalid host"
            return None
        if event in ("socket.connect", "socket.sendto", "socket.sendmsg"):
            address = args[1] if _len(args) > 1 else None
            if _socketpair_code is not None and frame.f_code is _socketpair_code and _type(address) is _tuple                     and address and _type(address[0]) is _str and is_loopback(_lower(address[0])):
                return None
            if address is None and event == "socket.sendmsg":
                return None
            if _type(address) is _tuple:
                name = address_host(address)
                if name is None:
                    return "exDesktop sandbox: invalid address"
                return check_net(owner, name)
            if text_of(address) is not None:
                return "exDesktop sandbox: local sockets are blocked"
            return "exDesktop sandbox: invalid address"
        if event == "socket.bind":
            address = args[1] if _len(args) > 1 else None
            if _type(address) is _tuple:
                name = address_host(address)
                if name is not None and (is_loopback(_lower(name)) or name == ""):
                    return None if name else "exDesktop sandbox: listening on network is blocked"
            log("%s: bind blocked" % owner)
            return "exDesktop sandbox: listening on network is blocked"
        return None

    def decide(event, args, frame=None):
        if frame is None:
            frame = _getframe(1)
        try:
            return handle(event, args, frame)
        except _Err:
            try:
                owner = owner_at(frame)[0]
            except _Err:
                owner = UNKNOWN
            return None if owner == "" else "exDesktop sandbox: blocked"

    def query(method, first, second, third, paths):
        with mutex:
            if method == "permissions":
                net = [("{\"key\":%s,\"rule\":%s}" % (escape(k), escape(v)))
                       for k, v in _ditems(_dget(net_rules, first, {}))]
                folders = [("{\"key\":%s}" % escape(p)) for p in _dget(paths_allowed, first, ())]
                _lsort(net)
                _lsort(folders)
                return "{\"net\":[" + _join(",", net) + "],\"paths\":[" + _join(",", folders) + "]}"
            if method == "revoke":
                changed = False
                if second == "net" and third in _dget(net_rules, first, {}):
                    _dpop(net_rules[first], third, None)
                    if not net_rules[first]:
                        _dpop(net_rules, first, None)
                    _sdiscard(once, first + "\x00" + third)
                    changed = True
                elif second == "path" and third in _dget(paths_allowed, first, ()):
                    _sdiscard(paths_allowed[first], third)
                    if not paths_allowed[first]:
                        _dpop(paths_allowed, first, None)
                    changed = True
                elif second == "all":
                    changed = _bool(_dpop(net_rules, first, None)) | _bool(_dpop(paths_allowed, first, None))
                    _dpop(allowed_ips, first, None)
                if changed:
                    save_state()
                return "true" if changed else "false"
            if method == "grant":
                granted = False
                for raw in paths if _type(paths) is _list else ():
                    path = absolute(raw) if _type(raw) is _str else None
                    if path and classify(first, path, False, False) != "hard":
                        _sadd(files_granted.setdefault(first, _set()), path)
                        granted = True
                return "true" if granted else "false"
        return "false"

    # Hide
    keep = {"LANG", "LANGUAGE", "LC_ALL", "LC_CTYPE", "LC_MESSAGES", "TZ", "TERM", "HTTP_PROXY", "HTTPS_PROXY",
            "ALL_PROXY", "NO_PROXY", "http_proxy", "https_proxy", "all_proxy", "no_proxy", "SSL_CERT_FILE",
            "SSL_CERT_DIR", "REQUESTS_CA_BUNDLE", "CURL_CA_BUNDLE", "SYSTEMROOT", "SystemRoot", "WINDIR",
            "OS", "PROCESSOR_ARCHITECTURE", "NUMBER_OF_PROCESSORS", "PATHEXT", "COMSPEC", "PYTHONIOENCODING",
            "PYTHONUTF8", "XDG_RUNTIME_DIR", "DISPLAY", "WAYLAND_DISPLAY"}
    home_dir = os.path.join(files_dir, "plugin_home")
    fakes = {"HOME": home_dir, "USERPROFILE": home_dir, "APPDATA": os.path.join(home_dir, "AppData", "Roaming"),
             "LOCALAPPDATA": os.path.join(home_dir, "AppData", "Local"), "TEMP": os.path.join(files_dir, "plugin_tmp"),
             "TMP": os.path.join(files_dir, "plugin_tmp"), "TMPDIR": os.path.join(files_dir, "plugin_tmp"),
             "USERNAME": "user", "USER": "user", "LOGNAME": "user", "COMPUTERNAME": "exdesktop",
             "HOSTNAME": "exdesktop", "HOMEDRIVE": "", "HOMEPATH": ""}
    try:
        data = os.environ._data
        keep_keys = {os.environ.encodekey(k) for k in keep}
        for key in list(data):
            if key not in keep_keys:
                del data[key]
        for key, value in fakes.items():
            data[os.environ.encodekey(key)] = os.environ.encodevalue(value)
        raw_env = getattr(osmod, "environ", None)
        if raw_env is not None:
            raw_env.clear()
    except Exception:
        pass
    try:
        if "platform" in sys.modules:
            sys.modules["platform"]._uname_cache = None
    except Exception:
        pass
    try:
        import uuid
        import random
        uuid._node = random.getrandbits(48) | (1 << 40)
        uuid._generate_time_safe = None
        uuid._UuidCreate = None
        for name in ("generate_time_safe", "UuidCreate"):
            module = sys.modules.get("_uuid")
            if module is not None and hasattr(module, name):
                delattr(module, name)
    except Exception:
        pass

    def no_login():
        raise OSError("exDesktop sandbox: user name is not available")

    for module in (os, osmod):
        if hasattr(module, "getlogin"):
            module.getlogin = no_login

    real_disk = getattr(osmod, "_getdiskusage", None)
    if real_disk is not None:
        def disk_usage(path, _real=real_disk, _audit=sys.audit):
            _audit("exd.disk_usage", path)
            return _real(path)
        osmod._getdiskusage = disk_usage
    real_statvfs = getattr(osmod, "statvfs", None)
    if real_statvfs is not None:
        def statvfs(path, _real=real_statvfs, _audit=sys.audit):
            _audit("exd.statvfs", path)
            return _real(path)
        osmod.statvfs = statvfs
        os.statvfs = statvfs
    real_uname = getattr(osmod, "uname", None)
    if real_uname is not None:
        try:
            current = real_uname()
            masked = os.uname_result((current.sysname, "localhost", current.release, current.version, current.machine))
            osmod.uname = lambda: masked
            os.uname = osmod.uname
        except Exception:
            pass
    if not win:
        try:
            import pwd
            entry = pwd.getpwuid(os.getuid())
            masked_entry = pwd.struct_passwd(("user", "x", entry.pw_uid, entry.pw_gid, "user", home_dir, entry.pw_shell))
            pwd.getpwuid = lambda uid: masked_entry
            pwd.getpwnam = lambda name: masked_entry
            pwd.getpwall = lambda: [masked_entry]
        except Exception:
            pass
    try:
        from PIL import _imaging

        def no_capture(*args, **kwargs):
            raise PermissionError("exDesktop sandbox: screen capture is not available to plugins")

        for name in dir(_imaging):
            if name.startswith("grab"):
                setattr(_imaging, name, no_capture)
    except Exception:
        pass
    if win:
        try:
            import asyncio
            asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())
        except Exception:
            pass

    # Register
    if native is not None and hasattr(native, "sbx_install"):
        native.sbx_install(decide, owner_now, query, answer, _imp.create_dynamic, _imp.create_builtin,
                           PermissionError, ImportError)
        _imp.create_dynamic = native.sbx_create_dynamic
        _imp.create_builtin = native.sbx_create_builtin
        real_start = _thread.start_new_thread

        def start_new_thread(function, args, kwargs=None, _real=real_start, _ticket=native.sbx_ticket,
                             _entry=native.sbx_thread_entry):
            return _real(_entry, (_ticket(), function, args, kwargs if kwargs is not None else {}))

        _thread.start_new_thread = start_new_thread
        _thread.start_new = start_new_thread
        threading._start_new_thread = start_new_thread
    else:
        watched = read_events | write_events | pair_events | process_events | internals | net_name_events \
            | connect_events | {"open", "import", "compile", "sqlite3.connect", "socket.connect", "socket.bind",
                                "socket.sendto", "socket.sendmsg", "socket.getnameinfo", "socket.gethostname",
                                "urllib.Request", "http.client.connect", "os.symlink", "shutil.make_archive",
                                "shutil.unpack_archive"}
        local = threading.local()

        def hook(event, args):
            if getattr(local, "depth", 0):
                return
            if event not in watched and not event.startswith(("ctypes.", "winreg.", "_winapi.", "msvcrt.",
                                                              "fcntl.", "exd.")):
                return
            local.depth = 1
            try:
                verdict = decide(event, args, sys._getframe(1))
            finally:
                local.depth = 0
            if verdict:
                if verdict.startswith("import:"):
                    raise ImportError(verdict[7:])
                raise PermissionError(verdict)

        sys.addaudithook(hook)
    log("installed")
    return None
