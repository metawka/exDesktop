import ipaddress
import json
import re
import locale
import os
import sys
import threading
import time
import urllib.parse


def install(host, plugins_dir, settings_dir, files_dir, deps_dir, interactive=True):
    if getattr(host, "_exd_sandbox_installed", False):
        return
    host._exd_sandbox_installed = True

    norm = os.path.normcase
    sep = os.sep

    def absolute(path):
        try:
            return norm(os.path.abspath(path))
        except Exception:
            return None

    def inside(path, root):
        return path == root or path.startswith(root.rstrip(sep) + sep)

    home = absolute(os.path.expanduser("~"))
    plugins_root = absolute(plugins_dir)
    settings_root = absolute(settings_dir)
    files_root = absolute(files_dir)
    tdata_root = os.path.dirname(files_root) if os.path.basename(files_root) == "plugins" else files_root
    sdk_root = absolute(os.path.dirname(os.path.abspath(__file__)))
    runtime_root = absolute(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    executable = sys.executable or ""
    if executable and (sys.platform == "win32"
                       or os.path.basename(executable).lower().startswith(("telegram", "exdesktop"))):
        app_root = absolute(os.path.dirname(executable))
    else:
        app_root = absolute(os.path.dirname(runtime_root))
    deps_root = absolute(deps_dir)
    read_roots = tuple(r for r in {
        absolute(sys.prefix), absolute(sys.base_prefix), sdk_root, deps_root, runtime_root,
        absolute(os.path.join(app_root, "python")), absolute(os.path.join(app_root, "python311")),
    } if r)
    shared_rw = tuple(r for r in (
        absolute(os.path.join(files_root, "plugin_cache")),
        absolute(os.path.join(files_root, "plugin_images")),
        absolute(os.path.join(files_root, "plugin_videos")),
        absolute(os.path.join(files_root, "plugin_audios")),
        absolute(os.path.join(files_root, "plugin_documents")),
        absolute(os.path.join(files_root, "plugin_tmp")),
    ) if r)
    tmp_root = absolute(os.path.join(files_root, "plugin_tmp"))
    try:
        os.makedirs(tmp_root, exist_ok=True)
        import tempfile
        tempfile.tempdir = tmp_root
    except Exception:
        pass

    sensitive_names = (
        "tdata", "telegram desktop", "ayugram", "exdesktop", ".ssh", ".gnupg", ".aws",
        "google\\chrome", "google/chrome", "chromium", "microsoft\\edge", "microsoft/edge",
        "bravesoftware", "yandex\\yandexbrowser", "opera software", "vivaldi", "mozilla",
        "discord", "exodus", "electrum", "atomic", "ethereum", "bitcoin", "monero",
        "metamask", "trust wallet", "ledger live", "keepass", "1password", "bitwarden",
        "microsoft\\credentials", "microsoft\\protect", "microsoft\\vault", "steam\\config",
        "filezilla", "winscp", "putty", "openvpn", "wireguard",
    )
    user_roots = tuple(r for r in (
        absolute(os.path.join(home, "Desktop")), absolute(os.path.join(home, "Downloads")),
        absolute(os.path.join(home, "Documents")), absolute(os.path.join(home, "Pictures")),
        absolute(os.path.join(home, "Music")), absolute(os.path.join(home, "Videos")),
        absolute(os.path.join(home, "OneDrive")),
    ) if r)

    state_path = os.path.join(settings_dir, "_sandbox.json")
    try:
        with open(state_path, encoding="utf-8") as f:
            stored = json.load(f)
    except Exception:
        stored = {}
    paths_allowed = {k: set(v) for k, v in (stored.get("paths") or {}).items()}
    files_granted = {}
    net_rules = {k: dict(v) for k, v in (stored.get("net") or {}).items()}
    once = set()
    pending = {}
    recent_dns = threading.local()
    guard = threading.local()
    toasts = {}
    file_to_id = {}
    lock = threading.RLock()

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
    russian = lang.startswith(("ru", "russian", "uk", "ukrainian", "be", "kk"))

    texts = {
        "net_title": ("Доступ в интернет", "Internet Access"),
        "net_text": ("Плагин «{name}» запрашивает подключение к {target}.",
                     "Plugin \"{name}\" requests a connection to {target}."),
        "net_always": ("Разрешить всегда", "Always Allow"),
        "net_once": ("Один раз", "Allow Once"),
        "deny": ("Запретить", "Deny"),
        "path_title": ("Доступ к файлам", "File Access"),
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
    }

    def text(key, **kwargs):
        value = texts[key][0 if russian else 1]
        return value.format(**kwargs)

    def native():
        return sys.modules.get("_extera")

    def plugin_name(plugin_id):
        record = getattr(host, "_records", {}).get(plugin_id)
        if record is not None:
            return str(record.meta.get("name") or plugin_id)
        return str(plugin_id)

    def save_state():
        data = {
            "paths": {k: sorted(v) for k, v in paths_allowed.items()},
            "net": net_rules,
        }
        try:
            guard.active = True
            os.makedirs(settings_dir, exist_ok=True)
            with open(state_path, "w", encoding="utf-8") as f:
                json.dump(data, f, ensure_ascii=False)
        except Exception:
            pass
        finally:
            guard.active = False

    def log(message):
        try:
            guard.active = True
            host.log("sandbox: " + message)
        finally:
            guard.active = False

    def toast(plugin_id, key, **kwargs):
        stamp = (plugin_id, key)
        now = time.time()
        if now - toasts.get(stamp, 0) < 30:
            return
        toasts[stamp] = now
        message = text(key, name=plugin_name(plugin_id), **kwargs)
        mod = native()
        if mod is None:
            return
        try:
            guard.active = True
            mod.toast(message)
        except Exception:
            pass
        finally:
            guard.active = False

    elyx_root = absolute(os.path.join(plugins_dir, "ElyxPlugins"))

    def plugin_of(path):
        path = norm(path)
        if path in file_to_id:
            return file_to_id[path]
        if inside(path, elyx_root):
            rest = path[len(elyx_root):].strip(sep).split(sep)
            for record in list(getattr(host, "_records", {}).values()):
                if norm(record.id) == rest[0]:
                    file_to_id[path] = record.id
                    return record.id
        for record in list(getattr(host, "_records", {}).values()):
            file_to_id[norm(os.path.abspath(record.path))] = record.id
        if path not in file_to_id:
            stem = os.path.splitext(os.path.basename(path))[0]
            file_to_id[path] = stem
        return file_to_id[path]

    stdlib_roots = tuple(r for r in {
        absolute(os.path.join(sys.base_prefix, "Lib")), absolute(os.path.join(sys.prefix, "Lib")),
        absolute(os.path.join(sys.base_prefix, "python311.zip")),
        absolute(os.path.dirname(getattr(os, "__file__", None) or sys.executable or sys.base_prefix)),
    } | {absolute(p) for p in sys.path if isinstance(p, str) and p.lower().endswith(".zip")} if r)
    internal_sdk = {
        "plugin_settings.py", "extera_host.py", "base_plugin.py", "requirements_installer.py",
        "tl_runtime.py", "metadata_parser.py", "capability_scan.py", "plugin_guard.py",
        "_compat.py", "exd_sandbox.py", "lifecycle.py",
    }

    def caller():
        frame = sys._getframe(2)
        importing = False
        plugin_id = None
        first = None
        while frame is not None:
            name = frame.f_code.co_filename
            if name.startswith("<frozen importlib"):
                importing = True
            elif name and not name.startswith("<"):
                path = norm(name)
                transparent = path.startswith(sdk_root) and os.path.basename(path) == "exd_runtime.py"
                if first is None and not transparent and not any(path.startswith(root) for root in stdlib_roots):
                    first = path
                if plugin_id is None and path.startswith(plugins_root):
                    plugin_id = plugin_of(name)
            frame = frame.f_back
        if plugin_id is not None and first is not None and first.startswith(sdk_root) \
                and os.path.basename(first) in internal_sdk:
            return None, importing
        return plugin_id, importing

    def origin():
        frame = sys._getframe(2)
        chain = []
        while frame is not None and len(chain) < 6:
            name = frame.f_code.co_filename
            if name and not name.startswith("<"):
                path = norm(name)
                if not any(path.startswith(root) for root in stdlib_roots):
                    chain.append("%s:%d" % (os.path.basename(name), frame.f_lineno))
            frame = frame.f_back
        return " < ".join(chain)

    def own_paths(plugin_id):
        return (
            absolute(os.path.join(plugins_dir, ".data", plugin_id)),
            absolute(os.path.join(plugins_dir, ".cache", plugin_id)),
        )

    def owned_name(plugin_id, name):
        key = re.sub(r"[^a-z0-9]", "", str(plugin_id).lower())
        flat = re.sub(r"[^a-z0-9]", "", name.lower())
        return len(key) >= 3 and key in flat

    source_exts = (".plugin", ".py", ".elyx", ".eaf", ".zip")

    def classify(plugin_id, raw, write, importing, listing=False):
        if isinstance(raw, int):
            return "allow"
        if isinstance(raw, bytes):
            try:
                raw = raw.decode("utf-8", "replace")
            except Exception:
                return "allow"
        try:
            raw = os.fspath(raw)
        except Exception:
            return "allow"
        path = absolute(raw)
        if not path:
            return "allow"
        if os.path.basename(path).lower() in ("nul", "con") or path == absolute(os.devnull):
            return "allow"
        for root in own_paths(plugin_id) + shared_rw:
            if root and (inside(path, root) or inside(root, path)):
                if inside(path, root) or not os.path.isfile(path):
                    return "allow"
        if inside(path, settings_root):
            return "hard"
        if inside(path, plugins_root):
            record = getattr(host, "_records", {}).get(plugin_id)
            if record is not None and norm(os.path.abspath(record.path)) == path:
                return "hard" if write else "allow"
            if not write and importing:
                return "allow"
            rest = path[len(plugins_root):].strip(sep).split(sep)
            if not rest or not rest[0]:
                return "allow" if listing else "hard"
            head = rest[0]
            if head in (".data", ".cache", "elyxplugins"):
                if len(rest) == 1:
                    return "allow" if listing or not write else "hard"
                return "allow" if rest[1] == norm(plugin_id) else "hard"
            if head.endswith(source_exts):
                return "hard"
            return "allow"
        if inside(path, files_root):
            rest = path[len(files_root):].strip(sep).split(sep)
            if rest and rest[0] and owned_name(plugin_id, rest[0]):
                return "allow"
        if home and inside(path, home):
            rest = path[len(home):].strip(sep).split(sep)
            if rest and rest[0].startswith(".") and owned_name(plugin_id, rest[0]):
                return "allow"
        if listing and (inside(path, app_root) or path == absolute(os.getcwd())):
            return "allow"
        if not write:
            for root in read_roots:
                if inside(path, root):
                    return "allow"
        if inside(path, settings_root) or inside(path, tdata_root) or inside(path, files_root):
            return "hard"
        if inside(path, app_root):
            return "hard" if write else "user"
        lowered = path.lower()
        if any(name in lowered for name in sensitive_names):
            return "hard"
        return "user"

    def folder_key(path):
        path = absolute(os.fspath(path) if not isinstance(path, bytes) else path.decode("utf-8", "replace"))
        for root in user_roots:
            if inside(path, root):
                rest = path[len(root):].strip(sep)
                first = rest.split(sep)[0] if rest else ""
                return os.path.join(root, first) if first and os.path.isdir(os.path.join(root, first)) else root
        parent = path if os.path.isdir(path) else os.path.dirname(path)
        return parent

    def path_allowed(plugin_id, path):
        key = absolute(path)
        if key in files_granted.get(plugin_id, ()):
            return True
        for folder in paths_allowed.get(plugin_id, ()):
            if inside(key, folder):
                return True
        return False

    def ask(kind, plugin_id, target, on_answer, buttons):
        token = (kind, plugin_id, target)
        with lock:
            if token in pending:
                return pending[token]
            event = threading.Event()
            pending[token] = event
        if not interactive:
            on_answer("deny")
            with lock:
                pending.pop(token, None)
            event.set()
            return event

        def show():
            try:
                guard.active = True
                import ui.alert as alert
                builder = alert.AlertDialogBuilder(None)
                builder._exd_extra = {"kind": kind, "plugin": plugin_id, "target": target}
                if kind == "net":
                    builder.set_title(text("net_title"))
                    builder.set_message(text("net_text", name=plugin_name(plugin_id), target=target))
                else:
                    builder.set_title(text("path_title"))
                    builder.set_message(text("path_text", name=plugin_name(plugin_id), target=target))

                def finish(answer):
                    def run(*_):
                        if caller()[0] is not None:
                            return
                        on_answer(answer)
                        with lock:
                            pending.pop(token, None)
                        event.set()
                    return run

                for label, answer, slot in buttons:
                    if slot == "positive":
                        builder.set_positive_button(label, finish(answer))
                    elif slot == "neutral":
                        builder.set_neutral_button(label, finish(answer))
                    else:
                        builder.set_negative_button(label, finish(answer))
                builder.set_on_dismiss_listener(finish("dismiss"))
                builder.show()
            except Exception as e:
                log("prompt failed: %r" % e)
                with lock:
                    pending.pop(token, None)
                event.set()
            finally:
                guard.active = False

        mod = native()
        try:
            guard.active = True
            if mod is not None and hasattr(mod, "run_on_main"):
                mod.run_on_main(show, 0)
            else:
                show()
        finally:
            guard.active = False
        return event

    def wait_answer(event):
        if threading.current_thread() is threading.main_thread():
            return
        event.wait(120)

    def check_path(plugin_id, raw, write, importing, listing=False):
        verdict = classify(plugin_id, raw, write, importing, listing)
        if verdict == "allow":
            return
        if verdict == "hard":
            log("%s: protected path %r from %s" % (plugin_id, raw, origin()))
            toast(plugin_id, "hard")
            raise PermissionError("exDesktop sandbox: access to protected files is blocked")
        if path_allowed(plugin_id, raw):
            return
        folder = folder_key(raw)

        def answer(value):
            if value == "allow":
                with lock:
                    paths_allowed.setdefault(plugin_id, set()).add(absolute(folder))
                save_state()

        event = ask("path", plugin_id, folder, answer, (
            (text("path_allow"), "allow", "positive"),
            (text("deny"), "deny", "negative"),
        ))
        wait_answer(event)
        if path_allowed(plugin_id, raw):
            return
        log("%s: path denied %r" % (plugin_id, raw))
        raise PermissionError("exDesktop sandbox: access to %s is not allowed" % folder)

    def is_local_host(host_name):
        name = str(host_name or "").strip("[]").lower()
        if name in ("localhost", ""):
            return True
        try:
            return ipaddress.ip_address(name).is_loopback
        except ValueError:
            return False

    def domain_key(host_name):
        name = str(host_name or "").strip("[]").lower().rstrip(".")
        try:
            ipaddress.ip_address(name)
            return name
        except ValueError:
            pass
        parts = name.split(".")
        if len(parts) > 2 and parts[-2] in ("co", "com", "org", "net", "gov", "edu", "ac") and len(parts[-1]) == 2:
            return ".".join(parts[-3:])
        return ".".join(parts[-2:]) if len(parts) >= 2 else name

    def check_net(plugin_id, host_name):
        if is_local_host(host_name):
            return
        domain = domain_key(host_name)
        rule = net_rules.get(plugin_id, {}).get(domain)
        if rule == "allow":
            recent_dns.domain = (domain, time.time())
            return
        if (plugin_id, domain) in once:
            once.discard((plugin_id, domain))
            recent_dns.domain = (domain, time.time())
            return
        if rule == "deny":
            toast(plugin_id, "denied_net", target=domain)
            raise PermissionError("exDesktop sandbox: connection to %s is denied" % domain)

        def answer(value):
            if value == "always":
                with lock:
                    net_rules.setdefault(plugin_id, {})[domain] = "allow"
                save_state()
            elif value == "once":
                once.add((plugin_id, domain))
            elif value == "deny":
                with lock:
                    net_rules.setdefault(plugin_id, {})[domain] = "deny"
                save_state()

        event = ask("net", plugin_id, domain, answer, (
            (text("net_always"), "always", "positive"),
            (text("net_once"), "once", "neutral"),
            (text("deny"), "deny", "negative"),
        ))
        wait_answer(event)
        if net_rules.get(plugin_id, {}).get(domain) == "allow":
            recent_dns.domain = (domain, time.time())
            return
        if (plugin_id, domain) in once:
            once.discard((plugin_id, domain))
            recent_dns.domain = (domain, time.time())
            return
        log("%s: network denied %s" % (plugin_id, domain))
        raise PermissionError("exDesktop sandbox: connection to %s is not allowed" % domain)

    read_events = {"os.listdir", "os.scandir", "glob.glob"}
    write_events = {"os.remove", "os.rmdir", "os.mkdir", "os.chmod", "os.utime", "os.truncate",
                    "shutil.rmtree", "os.symlink", "os.link", "os.unlink"}
    pair_events = {"os.rename", "shutil.move", "shutil.copyfile", "shutil.copytree", "shutil.copymode",
                   "shutil.copystat"}
    process_events = {"subprocess.Popen", "os.system", "os.exec", "os.spawn", "os.posix_spawn",
                      "os.startfile", "os.fork", "os.forkpty", "os.kill", "os.killpg",
                      "_winapi.CreateProcess", "pty.spawn"}
    blocked_imports = {"cffi", "_cffi_backend"}
    internals = {"code.__new__", "marshal.loads", "marshal.load", "sys.settrace", "sys.setprofile",
                 "gc.get_objects", "gc.get_referrers", "gc.get_referents", "sys._current_frames",
                 "sys.addaudithook", "cpython.PyInterpreterState_Clear", "os.chdir", "os.chroot"}
    watched = read_events | write_events | pair_events | process_events | internals | {
        "open", "import", "sqlite3.connect", "socket.getaddrinfo", "socket.connect", "socket.bind", "socket.gethostbyname",
        "socket.gethostbyname_ex", "socket.getnameinfo", "urllib.Request", "http.client.connect",
        "winreg.CreateKey", "winreg.SetValue", "winreg.DeleteKey", "winreg.DeleteValue",
        "winreg.SaveKey", "winreg.LoadKey", "winreg.ConnectRegistry"}

    def hook(event, args):
        if event not in watched and not event.startswith("ctypes."):
            return
        if getattr(guard, "active", False):
            return
        guard.active = True
        try:
            plugin_id, importing = caller()
        finally:
            guard.active = False
        if plugin_id is None:
            return
        guard.active = True
        try:
            handle(event, args, plugin_id, importing)
        finally:
            guard.active = False

    def handle(event, args, plugin_id, importing):
        if event == "open":
            path, mode = args[0], args[1] if len(args) > 1 else "r"
            flags = args[2] if len(args) > 2 else 0
            write = bool(mode and any(c in str(mode) for c in "wax+")) or bool(
                isinstance(flags, int) and flags & (os.O_WRONLY | os.O_RDWR | os.O_APPEND | os.O_CREAT | os.O_TRUNC))
            check_path(plugin_id, path, write, importing)
        elif event in read_events:
            if args and args[0] is not None:
                check_path(plugin_id, args[0], False, importing, event != "glob.glob")
        elif event in write_events:
            if args and args[0] is not None:
                check_path(plugin_id, args[0], True, importing)
        elif event in pair_events:
            if args:
                check_path(plugin_id, args[0], event in ("os.rename", "shutil.move"), importing)
            if len(args) > 1:
                check_path(plugin_id, args[1], True, importing)
        elif event == "sqlite3.connect":
            database = args[0] if args else None
            if isinstance(database, bytes):
                database = database.decode("utf-8", "replace")
            if isinstance(database, os.PathLike):
                database = os.fspath(database)
            if isinstance(database, str) and database not in ("", ":memory:"):
                target = database
                if target.startswith("file:"):
                    target = urllib.parse.unquote(target[5:].split("?", 1)[0])
                    if target.startswith("///"):
                        target = target[3:]
                    elif target.startswith("//"):
                        target = target[2:]
                    if target in ("", ":memory:") or "mode=memory" in database:
                        return
                check_path(plugin_id, target, True, importing)
        elif event == "import":
            name = str(args[0] or "")
            if name.split(".")[0] in blocked_imports:
                log("%s: import %s blocked" % (plugin_id, name))
                toast(plugin_id, "native")
                raise ImportError("exDesktop sandbox: module %s is not available to plugins" % name)
        elif event.startswith("ctypes.") or event.startswith("winreg."):
            log("%s: %s blocked" % (plugin_id, event))
            toast(plugin_id, "native")
            raise PermissionError("exDesktop sandbox: native access is blocked")
        elif event in process_events:
            log("%s: %s blocked" % (plugin_id, event))
            toast(plugin_id, "process")
            raise PermissionError("exDesktop sandbox: starting programs is blocked")
        elif event in internals:
            if importing and event in ("marshal.loads", "marshal.load", "code.__new__"):
                return
            log("%s: %s blocked" % (plugin_id, event))
            raise PermissionError("exDesktop sandbox: %s is not available to plugins" % event)
        elif event == "urllib.Request":
            url = str(args[0] or "")
            host_name = urllib.parse.urlsplit(url).hostname
            if host_name:
                check_net(plugin_id, host_name)
        elif event in ("socket.getaddrinfo", "socket.gethostbyname", "socket.gethostbyname_ex"):
            if args and args[0]:
                name = args[0].decode() if isinstance(args[0], bytes) else str(args[0])
                check_net(plugin_id, name)
        elif event == "http.client.connect":
            if len(args) > 1:
                check_net(plugin_id, str(args[1]))
        elif event == "socket.connect":
            address = args[1] if len(args) > 1 else None
            if isinstance(address, tuple) and address:
                target = str(address[0])
                if is_local_host(target):
                    return
                last = getattr(recent_dns, "domain", None)
                if last and time.time() - last[1] < 60:
                    return
                check_net(plugin_id, target)
        elif event == "socket.bind":
            address = args[1] if len(args) > 1 else None
            if isinstance(address, tuple) and address and not is_local_host(address[0]):
                log("%s: bind %r blocked" % (plugin_id, address))
                raise PermissionError("exDesktop sandbox: listening on network is blocked")

    def list_permissions(plugin_id):
        with lock:
            net = [{"key": k, "rule": v} for k, v in sorted(net_rules.get(plugin_id, {}).items())]
            paths = [{"key": p} for p in sorted(paths_allowed.get(plugin_id, ()))]
        return {"net": net, "paths": paths}

    def revoke(plugin_id, kind, key):
        if caller()[0] is not None:
            return False
        changed = False
        with lock:
            if kind == "net" and key in net_rules.get(plugin_id, {}):
                net_rules[plugin_id].pop(key, None)
                if not net_rules[plugin_id]:
                    net_rules.pop(plugin_id, None)
                once.discard((plugin_id, key))
                changed = True
            elif kind == "path" and key in paths_allowed.get(plugin_id, ()):
                paths_allowed[plugin_id].discard(key)
                if not paths_allowed[plugin_id]:
                    paths_allowed.pop(plugin_id, None)
                changed = True
            elif kind == "all":
                changed = bool(net_rules.pop(plugin_id, None)) | bool(paths_allowed.pop(plugin_id, None))
        if changed:
            save_state()
        return changed

    sys.addaudithook(hook)
    log("installed")
    def grant_file(plugin_id, path):
        if caller()[0] is not None:
            return False
        key = absolute(path)
        if not key or classify(plugin_id, key, False, False) == "hard":
            return False
        with lock:
            files_granted.setdefault(plugin_id, set()).add(key)
        return True

    return list_permissions, revoke, grant_file
