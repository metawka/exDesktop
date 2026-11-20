import sys
import enum
import json
import os
import socket
import tempfile
import threading
import traceback

from base_plugin import BasePlugin


def _log(text):
    try:
        import extera_host
        extera_host.log("[dev_server] %s" % text)
    except Exception:
        pass


def _on_main(fn, timeout=30.0):
    done = threading.Event()
    box = {}

    def run():
        try:
            box["value"] = fn()
        except Exception as error:
            box["error"] = error
        finally:
            done.set()
    try:
        _extera = sys.modules["_extera"]
        _extera.run_on_main(run, 0)
    except Exception:
        run()
    if not done.wait(timeout):
        raise TimeoutError("main thread did not respond")
    if "error" in box:
        raise box["error"]
    return box.get("value")


def _require_host():
    try:
        import extera_host
        owner = extera_host.current_plugin_id()
    except Exception:
        owner = None
    if owner:
        raise PermissionError("dev server can be started only by exDesktop, not by plugin %s" % owner)


# Types

class DebuggerPlatform(enum.Enum):
    PyCharm = "pycharm"
    VSCode = "vscode"


class Callback:
    def __init__(self, fn):
        self._fn = fn

    def run(self, arg=None):
        if self._fn is not None:
            self._fn(arg)


class DebuggerEventListener(BasePlugin):
    def __init__(self, host, port, platform):
        super().__init__()
        self.id = "__dev_server_debugger__"
        self.name = "Debugger"
        self.enabled = True
        self.initialized = True
        self.host = host
        self.port = port
        self.platform = platform

    def on_app_event(self, event_type):
        if str(getattr(event_type, "value", event_type)) == "resume" and not DevServer._active_debugging:
            DevServer.setup_remote_debugging(self.host, self.port, self.platform)


# Server

class DevServer:
    DEFAULT_HOST = "127.0.0.1"
    DEFAULT_PORT = 42690
    SOCKET_TIMEOUT = 1
    BUFFER_SIZE = 65536
    PYCHARM_DEBUGGER_DIR = None

    _server_socket = None
    _server_thread = None
    _is_running = False
    _active_debugging = False
    _active_platform = None
    _clients = []

    @classmethod
    def start_server(cls, host=None, port=None):
        _require_host()
        if cls._is_running and cls._server_thread is not None:
            return cls._server_thread
        host = str(host or cls.DEFAULT_HOST)
        if host not in ("127.0.0.1", "localhost", "::1"):
            raise ValueError("dev server listens only on the local address")
        port = int(port or cls.DEFAULT_PORT)
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", port))
        server.listen(4)
        server.settimeout(cls.SOCKET_TIMEOUT)
        cls._server_socket = server
        cls._is_running = True
        thread = threading.Thread(target=cls._server_thread_function, name="exd-dev-server", daemon=True)
        cls._server_thread = thread
        thread.start()
        _log("Server listening on 127.0.0.1:%d" % port)
        return thread

    @classmethod
    def stop_server(cls):
        if not cls._is_running:
            return False
        cls._is_running = False
        server, cls._server_socket = cls._server_socket, None
        try:
            if server is not None:
                server.close()
        except Exception as error:
            _log("Error closing server socket: %r" % (error,))
        for client in list(cls._clients):
            try:
                client.close()
            except Exception:
                pass
        cls._clients = []
        thread, cls._server_thread = cls._server_thread, None
        if thread is not None and thread is not threading.current_thread():
            thread.join(timeout=3)
        return True

    @classmethod
    def _server_thread_function(cls):
        while cls._is_running:
            server = cls._server_socket
            if server is None:
                break
            try:
                client, address = server.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            if address[0] not in ("127.0.0.1", "::1"):
                client.close()
                continue
            _log("Accepted connection from %s:%s" % address[:2])
            cls._clients.append(client)
            threading.Thread(target=cls._handle_client, args=(client,), daemon=True).start()

    @classmethod
    def _handle_client(cls, client):
        buffer = b""
        client.settimeout(cls.SOCKET_TIMEOUT)
        try:
            while cls._is_running:
                try:
                    data = client.recv(cls.BUFFER_SIZE)
                except socket.timeout:
                    continue
                if not data:
                    break
                buffer = cls._process_buffer(client, buffer + data)
        except OSError:
            pass
        finally:
            try:
                cls._clients.remove(client)
            except ValueError:
                pass
            try:
                client.close()
            except Exception:
                pass

    @classmethod
    def _process_buffer(cls, client, buffer):
        text = buffer.decode("utf-8", errors="replace")
        decoder = json.JSONDecoder()
        pos = 0
        while pos < len(text):
            while pos < len(text) and text[pos].isspace():
                pos += 1
            if pos >= len(text):
                break
            try:
                obj, end = decoder.raw_decode(text[pos:])
            except json.JSONDecodeError:
                break
            pos += end
            if isinstance(obj, dict):
                cls._send(client, cls._process_command(obj))
        return text[pos:].encode("utf-8")

    @staticmethod
    def _send(client, response):
        try:
            client.sendall(json.dumps(response, ensure_ascii=False).encode("utf-8"))
        except Exception as error:
            _log("Error sending response: %r" % (error,))

    @classmethod
    def _process_command(cls, message):
        command = str(message.get("@") or message.get("command") or "")
        request_id = message.get("#", message.get("request_id"))
        handler = getattr(cls, "_handle_" + command, None) if command.isidentifier() else None
        if handler is None:
            result = {"success": False, "error": "unknown command: %s" % command}
        else:
            _log("Received command: %s" % command)
            try:
                result = handler(message) or {}
                result.setdefault("success", True)
            except Exception as error:
                _log(traceback.format_exc(limit=4))
                result = {"success": False, "error": str(error)}
        result["#"] = request_id
        return result

    # Commands

    @staticmethod
    def _get_plugins_map():
        import extera_host

        def collect():
            return {record.id: {"name": record.meta.get("name") or record.id,
                                "version": record.meta.get("version") or "",
                                "enabled": bool(extera_host.is_enabled(record.id)),
                                "active": record.instance is not None,
                                "path": record.path}
                    for record in list(extera_host._records.values())}
        return _on_main(collect)

    @classmethod
    def _handle_ping(cls, message):
        return {"pong": True}

    @classmethod
    def _handle_get_plugins(cls, message):
        return {"plugins": cls._get_plugins_map()}

    @classmethod
    def _handle_write_plugin(cls, message):
        plugin_id = str(message.get("plugin_id") or "")
        content = message.get("content")
        if not plugin_id or not isinstance(content, str):
            raise ValueError("plugin_id and content are required")
        import plugin_guard
        folder = tempfile.mkdtemp(prefix="exd_dev_")
        path = os.path.join(folder, plugin_id + ".plugin")
        with open(path, "w", encoding="utf-8") as tmp_file:
            tmp_file.write(content)
            tmp_file.flush()
            os.fsync(tmp_file.fileno())
        _log("Plugin content written to temporary file: %s" % path)
        report = plugin_guard.scan(path)
        dangerous = [f for f in report.get("findings") or ()
                     if str(f.get("level")).upper() in ("CRITICAL", "HIGH")]
        if dangerous:
            raise PermissionError("blocked by PluginGuard: %s" % ", ".join(
                str(f.get("rule", "?")) for f in dangerous))

        def activate():
            import extera_host
            info = extera_host._install({"path": path})
            try:
                _extera = sys.modules["_extera"]
                _extera.plugins_changed()
            except Exception:
                pass
            return info
        info = _on_main(activate)
        _log("Plugin '%s' written successfully" % plugin_id)
        return {"plugin": info}

    @classmethod
    def _reload_plugin(cls, plugin_id):
        import extera_host

        def reload():
            info = extera_host._reload({"id": plugin_id})
            if not info:
                raise LookupError("Cannot reload plugin '%s': not installed" % plugin_id)
            return info
        return _on_main(reload)

    @classmethod
    def _handle_reload_plugin(cls, message):
        plugin_id = str(message.get("plugin_id") or "")
        info = cls._reload_plugin(plugin_id)
        _log("Plugin '%s' reloaded successfully." % plugin_id)
        return {"plugin": info}

    @classmethod
    def _set_enabled(cls, plugin_id, enabled):
        import extera_host

        def apply():
            info = extera_host._set_enabled({"id": plugin_id, "enabled": bool(enabled)})
            try:
                _extera = sys.modules["_extera"]
                _extera.plugins_changed()
            except Exception:
                pass
            return info
        return {"plugin": _on_main(apply)}

    @classmethod
    def _handle_enable_plugin(cls, message):
        return cls._set_enabled(str(message.get("plugin_id") or ""), True)

    @classmethod
    def _handle_disable_plugin(cls, message):
        return cls._set_enabled(str(message.get("plugin_id") or ""), False)

    @classmethod
    def _handle_remove_plugin(cls, message):
        import extera_host
        plugin_id = str(message.get("plugin_id") or "")

        def remove():
            removed = extera_host._delete({"id": plugin_id})
            try:
                _extera = sys.modules["_extera"]
                _extera.plugins_changed()
            except Exception:
                pass
            return removed
        return {"removed": bool(_on_main(remove))}

    @classmethod
    def _handle_start_debugger(cls, message):
        platform = DebuggerPlatform(str(message.get("platform") or "vscode"))
        ok = cls.setup_remote_debugging(str(message.get("host") or "127.0.0.1"),
                                        int(message.get("port") or 5678), platform)
        return {"success": bool(ok)}

    @classmethod
    def _handle_stop_debugger(cls, message):
        platform = DebuggerPlatform(str(message.get("platform") or "vscode"))
        return {"success": bool(cls.stop_remote_debugging(platform))}

    # Debugging

    @classmethod
    def setup_remote_debugging(cls, host, port, platform):
        try:
            _require_host()
        except PermissionError as error:
            _log(str(error))
            return False
        platform = platform if isinstance(platform, DebuggerPlatform) else DebuggerPlatform(str(platform))
        host = str(host or "127.0.0.1")
        if host not in ("127.0.0.1", "localhost", "::1"):
            _log("Remote debugger is allowed only on the local address")
            return False
        try:
            if platform is DebuggerPlatform.VSCode:
                import debugpy
                debugpy.listen((host, int(port)), in_process_debug_adapter=True)
            else:
                import pydevd
                pydevd.settrace(host, port=int(port), suspend=False, stdoutToServer=True,
                                stderrToServer=True)
        except Exception as error:
            _log("Error setting up %s remote debugger: %r" % (platform.value, error))
            return False
        cls._active_debugging = True
        cls._active_platform = platform
        _log("Starting remote debugger on %s:%s, platform: %s" % (host, port, platform.value))
        return True

    @classmethod
    def stop_remote_debugging(cls, platform):
        if not cls._active_debugging:
            return False
        try:
            from debugpy._vendored.pydevd import pydevd
            pydevd.stoptrace()
        except Exception:
            try:
                import pydevd
                pydevd.stoptrace()
            except Exception as error:
                _log("Error stopping remote debugger: %r" % (error,))
                return False
        cls._active_debugging = False
        cls._active_platform = None
        return True
