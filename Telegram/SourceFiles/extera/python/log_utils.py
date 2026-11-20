import inspect
import os
import threading
import time
import traceback as _traceback
from enum import IntEnum
from typing import Any, Callable, Optional

__all__ = ["LogLevel", "LogRecord", "Logger", "get_logger", "dump"]


class LogLevel(IntEnum):
    VERBOSE = 2
    DEBUG = 3
    INFO = 4
    WARNING = 5
    ERROR = 6
    OFF = 100


_NAMES = {"V": LogLevel.VERBOSE, "VERBOSE": LogLevel.VERBOSE, "D": LogLevel.DEBUG, "DEBUG": LogLevel.DEBUG,
          "I": LogLevel.INFO, "INFO": LogLevel.INFO, "W": LogLevel.WARNING, "WARN": LogLevel.WARNING,
          "WARNING": LogLevel.WARNING, "E": LogLevel.ERROR, "ERROR": LogLevel.ERROR, "OFF": LogLevel.OFF}


def _level(value) -> LogLevel:
    if isinstance(value, LogLevel):
        return value
    if isinstance(value, str):
        found = _NAMES.get(value.strip().upper())
        if found is None:
            raise ValueError("unknown log level %r" % value)
        return found
    number = int(value)
    for level in sorted(LogLevel):
        if number <= level:
            return level
    return LogLevel.OFF


class LogRecord:
    def __init__(self, level, tag, message, location, tb, created):
        self.level = level
        self.tag = tag
        self.message = message
        self.location = location
        self.traceback = tb
        self.created = created

    @property
    def text(self) -> str:
        head = "[%s] %s" % (self.tag, self.message)
        if self.location:
            head = "%s (%s)" % (head, self.location)
        if self.traceback:
            head = head + "\n" + self.traceback
        return head

    def __repr__(self):
        return "LogRecord(%s, %r)" % (self.level.name, self.message)


class _Timer:
    def __init__(self, logger, label, level):
        self._logger = logger
        self._label = label
        self._level = level
        self._started = 0.0

    def __enter__(self):
        self._started = time.perf_counter()
        return self

    def __exit__(self, exc_type, exc, tb):
        elapsed = (time.perf_counter() - self._started) * 1000.0
        self._logger.log(self._level, "%s: %.2f ms%s" % (self._label, elapsed, " (failed)" if exc else ""),
                         stacklevel=2)
        return False


_DEFAULT_LEVEL = [LogLevel.DEBUG]


class Logger:
    def __init__(self, tag: str, *, show_location: bool = True):
        self._tag = str(tag)
        self._level = None
        self._parent = None
        self._show_location = show_location
        self._handlers = []
        self._lock = threading.RLock()

    @property
    def tag(self) -> str:
        return self._tag

    @property
    def level(self) -> Optional[LogLevel]:
        return self._level

    @level.setter
    def level(self, value):
        self._level = None if value is None else _level(value)

    @property
    def effective_level(self) -> LogLevel:
        logger = self
        while logger is not None:
            if logger._level is not None:
                return logger._level
            logger = logger._parent
        return _DEFAULT_LEVEL[0]

    def is_enabled_for(self, level) -> bool:
        level = _level(level)
        return level != LogLevel.OFF and level >= self.effective_level

    def child(self, name: str) -> "Logger":
        child = Logger("%s.%s" % (self._tag, name), show_location=self._show_location)
        child._parent = self
        return child

    def _location(self, stacklevel):
        if not self._show_location:
            return None
        frame = inspect.currentframe()
        try:
            for _ in range(stacklevel + 1):
                if frame is None:
                    return None
                frame = frame.f_back
            if frame is None:
                return None
            return "%s:%d" % (os.path.basename(frame.f_code.co_filename), frame.f_lineno)
        finally:
            del frame

    def log(self, level, msg: Any, *args, exc_info: Any = None, stacklevel: int = 1) -> None:
        level = _level(level)
        if not self.is_enabled_for(level):
            return
        try:
            text = str(msg) % args if args else str(msg)
        except Exception:
            text = " ".join([str(msg)] + [str(a) for a in args])
        tb = None
        if exc_info:
            if isinstance(exc_info, BaseException):
                tb = "".join(_traceback.format_exception(type(exc_info), exc_info, exc_info.__traceback__))
            else:
                tb = _traceback.format_exc()
        record = LogRecord(level, self._tag, text, self._location(stacklevel), tb, time.time())
        _emit(record)
        logger = self
        while logger is not None:
            for handler in list(logger._handlers):
                try:
                    handler(record)
                except Exception:
                    pass
            logger = logger._parent

    def verbose(self, msg, *args, exc_info=None, stacklevel=1):
        self.log(LogLevel.VERBOSE, msg, *args, exc_info=exc_info, stacklevel=stacklevel + 1)

    def debug(self, msg, *args, exc_info=None, stacklevel=1):
        self.log(LogLevel.DEBUG, msg, *args, exc_info=exc_info, stacklevel=stacklevel + 1)

    def info(self, msg, *args, exc_info=None, stacklevel=1):
        self.log(LogLevel.INFO, msg, *args, exc_info=exc_info, stacklevel=stacklevel + 1)

    def warning(self, msg, *args, exc_info=None, stacklevel=1):
        self.log(LogLevel.WARNING, msg, *args, exc_info=exc_info, stacklevel=stacklevel + 1)

    warn = warning

    def error(self, msg, *args, exc_info=None, stacklevel=1):
        self.log(LogLevel.ERROR, msg, *args, exc_info=exc_info, stacklevel=stacklevel + 1)

    def exception(self, msg, *args, stacklevel=1):
        self.log(LogLevel.ERROR, msg, *args, exc_info=True, stacklevel=stacklevel + 1)

    def dump(self, obj, label=None, *, level=LogLevel.DEBUG, depth=2, max_items=20, stacklevel=1):
        text = dump(obj, depth, max_items)
        self.log(level, "%s = %s" % (label, text) if label else text, stacklevel=stacklevel + 1)

    def timed(self, label, level=LogLevel.DEBUG):
        return _Timer(self, label, _level(level))

    def add_handler(self, handler: Callable[[LogRecord], Any]) -> None:
        with self._lock:
            if handler not in self._handlers:
                self._handlers.append(handler)

    def remove_handler(self, handler) -> bool:
        with self._lock:
            if handler in self._handlers:
                self._handlers.remove(handler)
                return True
            return False

    def __repr__(self):
        return "<Logger %s>" % self._tag


_PREFIX = {LogLevel.VERBOSE: "V", LogLevel.DEBUG: "D", LogLevel.INFO: "I", LogLevel.WARNING: "W",
           LogLevel.ERROR: "E"}


def _emit(record):
    try:
        import extera_host
        extera_host.log("%s/%s" % (_PREFIX.get(record.level, "?"), record.text))
    except Exception:
        pass


_loggers = {}
_loggers_lock = threading.Lock()


def get_logger(name: Optional[str] = None) -> Logger:
    if name is None:
        try:
            import extera_host
            name = extera_host.current_plugin_id() or "plugin"
        except Exception:
            name = "plugin"
    with _loggers_lock:
        logger = _loggers.get(name)
        if logger is None:
            logger = Logger(name)
            _loggers[name] = logger
        return logger


class _Unreadable:
    def __init__(self, error):
        self.error = error

    def __repr__(self):
        return "<unreadable: %s>" % type(self.error).__name__


class _Dumper:
    def __init__(self, depth, max_items):
        self._depth = depth
        self._max = max_items
        self._seen = set()

    def render(self, obj) -> str:
        return self._render(obj, self._depth)

    def _render(self, obj, depth):
        if obj is None or isinstance(obj, (bool, int, float, complex)):
            return repr(obj)
        if isinstance(obj, (str, bytes)):
            text = repr(obj)
            return text if len(text) <= 300 else text[:297] + "..."
        if id(obj) in self._seen:
            return "<cycle>"
        if depth < 0:
            return "<%s>" % type(obj).__name__
        self._seen.add(id(obj))
        try:
            if isinstance(obj, dict):
                items = list(obj.items())
                body = ", ".join("%s: %s" % (self._render(k, depth - 1), self._render(v, depth - 1))
                                 for k, v in items[:self._max])
                return "{%s%s}" % (body, ", ..." if len(items) > self._max else "")
            if isinstance(obj, (list, tuple, set, frozenset)):
                items = list(obj)
                body = ", ".join(self._render(v, depth - 1) for v in items[:self._max])
                more = ", ..." if len(items) > self._max else ""
                open_, close = {list: "[]", tuple: "()"}.get(type(obj), "{}")
                return "%s%s%s%s" % (open_, body, more, close)
            fields = {}
            source = getattr(obj, "__dict__", None)
            names = list(source.keys()) if isinstance(source, dict) else \
                [n for n in dir(obj) if not n.startswith("_")]
            for name in names[:self._max]:
                if name.startswith("_"):
                    continue
                try:
                    value = getattr(obj, name)
                except Exception as e:
                    value = _Unreadable(e)
                if callable(value) and not isinstance(value, type):
                    continue
                fields[name] = value
            body = ", ".join("%s=%s" % (k, self._render(v, depth - 1)) for k, v in fields.items())
            return "%s(%s)" % (type(obj).__name__, body)
        finally:
            self._seen.discard(id(obj))


def dump(obj: Any, depth: int = 2, max_items: int = 20) -> str:
    return _Dumper(depth, max_items).render(obj)
