import ctypes
import glob
import io
import os
import sys
import threading
import weakref

import _compat
import android_views
import exd_android
import exd_java
import exd_java_more2
import tgnet_bridge
from exd_java import JavaObject


def _to_bytes(value):
    if value is None:
        return b""
    if isinstance(value, (bytes, bytearray)):
        return bytes(value)
    if isinstance(value, str):
        return value.encode("utf-8")
    return bytes(int(x) & 0xFF for x in value)


# Proxy

class ProxyInfo(JavaObject):
    _java_name = "org.telegram.messenger.SharedConfig$ProxyInfo"

    def __init__(self, address="", port=0, username="", password="", secret=""):
        self.address = str(address or "")
        self.port = int(port or 0)
        self.username = str(username or "")
        self.password = str(password or "")
        self.secret = str(secret or "")
        self.available = False
        self.ping = 0
        self.checking = False

    def _data(self):
        return [self.address, self.port, self.username, self.password, self.secret]

    def getLink(self):
        from urllib.parse import urlencode
        if self.secret:
            query = {"server": self.address, "port": self.port, "secret": self.secret}
            return "tg://proxy?" + urlencode(query)
        query = {"server": self.address, "port": self.port}
        if self.username:
            query["user"] = self.username
        if self.password:
            query["pass"] = self.password
        return "tg://socks?" + urlencode(query)


def _proxy_prefs():
    return exd_java_more2.shared_preferences("exd_proxies")


def _open_proxy(info):
    try:
        _extera = sys.modules["_extera"]
        _extera.open_url(info.getLink())
    except Exception:
        pass


class SharedConfig(JavaObject):
    _java_name = "org.telegram.messenger.SharedConfig"
    ProxyInfo = ProxyInfo
    PERFORMANCE_CLASS_LOW = 0
    PERFORMANCE_CLASS_AVERAGE = 1
    PERFORMANCE_CLASS_HIGH = 2
    proxyList = exd_java.ArrayList()
    currentProxy = None
    proxyEnabled = False
    bubbleRadius = 17
    performanceClassName = "HIGH"
    overrideDevicePerformanceClass = -1
    useThreeLinesLayout = False
    allowScreenCapture = True
    noSoundHintShowed = True
    fontSize = 16
    messageSeenHintCount = 0

    @staticmethod
    def loadProxyList(*args):
        import json
        raw = _proxy_prefs().getString("list", "[]")
        try:
            items = json.loads(raw)
        except Exception:
            items = []
        SharedConfig.proxyList = exd_java.ArrayList([ProxyInfo(*item) for item in items])
        SharedConfig.proxyEnabled = _proxy_prefs().getBoolean("enabled", False)
        index = _proxy_prefs().getInt("current", -1)
        SharedConfig.currentProxy = SharedConfig.proxyList[index] if 0 <= index < len(SharedConfig.proxyList) else None

    @staticmethod
    def saveProxyList(*args):
        import json
        editor = _proxy_prefs().edit()
        editor.putString("list", json.dumps([item._data() for item in SharedConfig.proxyList]))
        current = SharedConfig.currentProxy
        editor.putInt("current", SharedConfig.proxyList.index(current) if current in SharedConfig.proxyList else -1)
        editor.putBoolean("enabled", bool(SharedConfig.proxyEnabled))
        editor.apply()

    @staticmethod
    def addProxy(info):
        for existing in SharedConfig.proxyList:
            if existing._data() == info._data():
                return existing
        SharedConfig.proxyList.add(info)
        SharedConfig.saveProxyList()
        return info

    @staticmethod
    def deleteProxy(info):
        if info in SharedConfig.proxyList:
            SharedConfig.proxyList.remove(info)
        if SharedConfig.currentProxy is info:
            SharedConfig.currentProxy = None
            SharedConfig.proxyEnabled = False
        SharedConfig.saveProxyList()

    @staticmethod
    def isProxyEnabled(*args):
        return bool(SharedConfig.proxyEnabled)

    @staticmethod
    def saveConfig(*args):
        SharedConfig.saveProxyList()

    @staticmethod
    def chatBlurEnabled(*args):
        return False

    @staticmethod
    def getDevicePerformanceClass(*args):
        return 2

    @staticmethod
    def getLiteMode(*args):
        return _compat.Stub("LiteMode")

    @staticmethod
    def isAutoplayVideo(*args):
        return True

    @staticmethod
    def isAutoplayGifs(*args):
        return True


def _set_proxy_settings(enabled, address="", port=0, username="", password="", secret="", *args):
    if enabled and address:
        info = SharedConfig.addProxy(ProxyInfo(address, port, username, password, secret))
        SharedConfig.currentProxy = info
        SharedConfig.proxyEnabled = True
        SharedConfig.saveProxyList()
        _open_proxy(info)
    else:
        SharedConfig.proxyEnabled = False
        SharedConfig.saveProxyList()


# Crypto

_libcrypto = None
_libcrypto_lock = threading.Lock()


def _crypto_lib():
    global _libcrypto
    with _libcrypto_lock:
        if _libcrypto is not None:
            return _libcrypto or None
        candidates = []
        for base in {sys.base_prefix, sys.prefix, os.path.dirname(sys.executable)}:
            for pattern in ("libcrypto-3*.dll", "libcrypto-1_1*.dll", "libcrypto.3.dylib", "lib/libcrypto.3.dylib",
                            "lib/libcrypto.so.3", "DLLs/libcrypto-3*.dll", "lib/python3*/lib-dynload/../../libcrypto*"):
                candidates.extend(glob.glob(os.path.join(base, pattern)))
        import ctypes.util
        found = ctypes.util.find_library("crypto") or ctypes.util.find_library("libcrypto-3")
        if found and not found.endswith("/usr/lib/libcrypto.dylib"):
            candidates.append(found)
        for path in candidates:
            try:
                lib = ctypes.CDLL(path)
                lib.EVP_CIPHER_CTX_new.restype = ctypes.c_void_p
                lib.EVP_get_cipherbyname.restype = ctypes.c_void_p
                lib.EVP_get_cipherbyname.argtypes = [ctypes.c_char_p]
                lib.EVP_CipherInit_ex.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
                                                  ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
                lib.EVP_CipherUpdate.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_int),
                                                 ctypes.c_char_p, ctypes.c_int]
                lib.EVP_CipherFinal_ex.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_int)]
                lib.EVP_CIPHER_CTX_set_padding.argtypes = [ctypes.c_void_p, ctypes.c_int]
                lib.EVP_CIPHER_CTX_ctrl.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_void_p]
                lib.EVP_CIPHER_CTX_free.argtypes = [ctypes.c_void_p]
                _libcrypto = lib
                return lib
            except Exception:
                continue
        _libcrypto = False
        return None


class SecretKeySpec(JavaObject):
    _java_name = "javax.crypto.spec.SecretKeySpec"

    def __init__(self, key, algorithm="AES", *args):
        self._key = _to_bytes(key)
        self._algorithm = str(algorithm)

    def getEncoded(self):
        return self._key

    def getAlgorithm(self):
        return self._algorithm


class IvParameterSpec(JavaObject):
    _java_name = "javax.crypto.spec.IvParameterSpec"

    def __init__(self, iv, *args):
        self._iv = _to_bytes(iv)

    def getIV(self):
        return self._iv


class GCMParameterSpec(JavaObject):
    _java_name = "javax.crypto.spec.GCMParameterSpec"

    def __init__(self, tag_bits, iv, *args):
        self._tag = int(tag_bits) // 8
        self._iv = _to_bytes(iv)

    def getIV(self):
        return self._iv

    def getTLen(self):
        return self._tag * 8


class Cipher(JavaObject):
    _java_name = "javax.crypto.Cipher"
    ENCRYPT_MODE = 1
    DECRYPT_MODE = 2
    WRAP_MODE = 3
    UNWRAP_MODE = 4

    def __init__(self, transformation):
        parts = str(transformation).upper().split("/")
        self._algorithm = parts[0]
        self._mode = parts[1] if len(parts) > 1 else "ECB"
        self._padding = parts[2] if len(parts) > 2 else "PKCS5PADDING"
        self._key = b""
        self._iv = b""
        self._tag = 16
        self._encrypt = True
        self._buffer = b""

    @staticmethod
    def getInstance(transformation, *args):
        return Cipher(transformation)

    def init(self, mode, key, spec=None, *args):
        self._encrypt = int(mode) in (1, 3)
        self._key = key.getEncoded() if hasattr(key, "getEncoded") else _to_bytes(key)
        self._iv = spec.getIV() if spec is not None and hasattr(spec, "getIV") else b""
        if isinstance(spec, GCMParameterSpec):
            self._tag = spec._tag
        if self._mode == "GCM" and not self._iv and self._encrypt:
            self._iv = os.urandom(12)
        self._buffer = b""

    def getIV(self):
        return self._iv

    def getBlockSize(self):
        return 16

    def update(self, data, *args):
        self._buffer += _to_bytes(data)
        return b""

    def updateAAD(self, *args):
        raise RuntimeError("AAD is not supported")

    def doFinal(self, data=None, *args):
        payload = self._buffer + _to_bytes(data)
        self._buffer = b""
        return self._run(payload)

    def _cipher_name(self):
        if self._algorithm != "AES":
            raise RuntimeError("Unsupported cipher: " + self._algorithm)
        bits = len(self._key) * 8
        if bits not in (128, 192, 256):
            raise RuntimeError("Invalid AES key length")
        return ("aes-%d-%s" % (bits, self._mode.lower())).encode()

    def _run(self, payload):
        lib = _crypto_lib()
        if lib is None:
            raise RuntimeError("AES is not available")
        cipher = lib.EVP_get_cipherbyname(self._cipher_name())
        if not cipher:
            raise RuntimeError("Unsupported mode: " + self._mode)
        ctx = lib.EVP_CIPHER_CTX_new()
        try:
            gcm = self._mode == "GCM"
            tag = b""
            if gcm and not self._encrypt:
                payload, tag = payload[:-self._tag], payload[-self._tag:]
            if not lib.EVP_CipherInit_ex(ctx, cipher, None, None, None, 1 if self._encrypt else 0):
                raise RuntimeError("Cipher init failed")
            if gcm:
                lib.EVP_CIPHER_CTX_ctrl(ctx, 0x9, len(self._iv), None)
            if not lib.EVP_CipherInit_ex(ctx, None, None, self._key, self._iv or None, 1 if self._encrypt else 0):
                raise RuntimeError("Cipher init failed")
            pad = self._padding.startswith("PKCS") and self._mode in ("ECB", "CBC")
            lib.EVP_CIPHER_CTX_set_padding(ctx, 1 if pad else 0)
            out = ctypes.create_string_buffer(len(payload) + 32)
            size = ctypes.c_int(0)
            if not lib.EVP_CipherUpdate(ctx, out, ctypes.byref(size), payload, len(payload)):
                raise RuntimeError("Cipher update failed")
            result = out.raw[:size.value]
            if gcm and not self._encrypt:
                tag_buffer = ctypes.create_string_buffer(tag, len(tag))
                lib.EVP_CIPHER_CTX_ctrl(ctx, 0x11, len(tag), tag_buffer)
            final = ctypes.create_string_buffer(32)
            if not lib.EVP_CipherFinal_ex(ctx, final, ctypes.byref(size)):
                raise RuntimeError("BadPaddingException" if not gcm else "AEADBadTagException")
            result += final.raw[:size.value]
            if gcm and self._encrypt:
                tag_buffer = ctypes.create_string_buffer(self._tag)
                lib.EVP_CIPHER_CTX_ctrl(ctx, 0x10, self._tag, tag_buffer)
                result += tag_buffer.raw
            return result
        finally:
            lib.EVP_CIPHER_CTX_free(ctx)


class KeyGenerator(JavaObject):
    _java_name = "javax.crypto.KeyGenerator"

    def __init__(self, algorithm="AES"):
        self._algorithm = str(algorithm)
        self._bits = 256

    @staticmethod
    def getInstance(algorithm, *args):
        return KeyGenerator(algorithm)

    def init(self, bits, *args):
        self._bits = int(bits)

    def generateKey(self):
        return SecretKeySpec(os.urandom(self._bits // 8), self._algorithm)


class SecureRandom(JavaObject):
    _java_name = "java.security.SecureRandom"

    def __init__(self, *args):
        pass

    def nextBytes(self, out):
        data = os.urandom(len(out))
        for i, value in enumerate(data):
            out[i] = value - 256 if value > 127 else value

    def nextInt(self, bound=None):
        import secrets
        if bound is None:
            return secrets.randbits(32) - (1 << 31)
        return secrets.randbelow(int(bound))

    def nextLong(self):
        import secrets
        return secrets.randbits(64) - (1 << 63)

    def nextDouble(self):
        import secrets
        return secrets.randbits(53) / float(1 << 53)

    def nextBoolean(self):
        import secrets
        return bool(secrets.randbits(1))

    @staticmethod
    def getInstanceStrong(*args):
        return SecureRandom()

    @staticmethod
    def getInstance(*args):
        return SecureRandom()


# IO

class _Reader(JavaObject):
    def __init__(self, source, *args):
        self._text = None
        self._source = source

    def _content(self):
        if self._text is None:
            source = self._source
            if isinstance(source, _Reader):
                self._text = io.StringIO(source._content().read())
            elif isinstance(source, str):
                self._text = io.StringIO(source)
            else:
                reader = getattr(source, "read", None)
                data = reader() if callable(reader) else b""
                if isinstance(data, int):
                    chunks = bytearray()
                    while data != -1 and data is not None:
                        chunks.append(data & 0xFF)
                        data = reader()
                    data = bytes(chunks)
                if isinstance(data, (bytes, bytearray)):
                    data = bytes(data).decode("utf-8", errors="replace")
                self._text = io.StringIO(str(data or ""))
        return self._text

    def read(self, *args):
        char = self._content().read(1)
        return ord(char) if char else -1

    def readLine(self):
        line = self._content().readline()
        if not line:
            return None
        return line.rstrip("\r\n")

    def lines(self):
        result = []
        while True:
            line = self.readLine()
            if line is None:
                return exd_java.ArrayList(result)
            result.append(line)

    def ready(self):
        return True

    def close(self):
        close = getattr(self._source, "close", None)
        if callable(close):
            try:
                close()
            except Exception:
                pass


class InputStreamReader(_Reader):
    _java_name = "java.io.InputStreamReader"


class BufferedReader(_Reader):
    _java_name = "java.io.BufferedReader"


class StringReader(_Reader):
    _java_name = "java.io.StringReader"


class FileReader(_Reader):
    _java_name = "java.io.FileReader"

    def __init__(self, path, *args):
        target = path.getAbsolutePath() if hasattr(path, "getAbsolutePath") else str(path)
        with open(target, encoding="utf-8", errors="replace") as handle:
            _Reader.__init__(self, handle.read())


class _Writer(JavaObject):
    def __init__(self, target=None, *args):
        self._target = target
        self._parts = []

    def write(self, data, *args):
        self._parts.append(data if isinstance(data, str) else chr(int(data)) if isinstance(data, int) else str(data))

    def append(self, data):
        self.write(str(data))
        return self

    def newLine(self):
        self._parts.append("\n")

    def flush(self):
        if self._target is None:
            return
        text = "".join(self._parts)
        self._parts = []
        if hasattr(self._target, "_parts"):
            self._target._parts.append(text)
        elif isinstance(self._target, str):
            with open(self._target, "a", encoding="utf-8") as handle:
                handle.write(text)
        else:
            writer = getattr(self._target, "write", None)
            if callable(writer):
                writer(text.encode("utf-8"))

    def close(self):
        self.flush()
        close = getattr(self._target, "close", None)
        if callable(close):
            try:
                close()
            except Exception:
                pass

    def toString(self):
        return "".join(self._parts)

    def __str__(self):
        return self.toString()


class OutputStreamWriter(_Writer):
    _java_name = "java.io.OutputStreamWriter"


class BufferedWriter(_Writer):
    _java_name = "java.io.BufferedWriter"


class StringWriter(_Writer):
    _java_name = "java.io.StringWriter"


class PrintWriter(_Writer):
    _java_name = "java.io.PrintWriter"

    def print(self, data):
        self.write(str(data))

    def println(self, data=""):
        self.write(str(data) + "\n")


class FileWriter(_Writer):
    _java_name = "java.io.FileWriter"

    def __init__(self, path, append=False, *args):
        target = path.getAbsolutePath() if hasattr(path, "getAbsolutePath") else str(path)
        if not append:
            open(target, "w", encoding="utf-8").close()
        _Writer.__init__(self, target)


# Concurrency

class CountDownLatch(JavaObject):
    _java_name = "java.util.concurrent.CountDownLatch"

    def __init__(self, count=1):
        self._count = int(count)
        self._cond = threading.Condition()

    def countDown(self):
        with self._cond:
            if self._count > 0:
                self._count -= 1
                if self._count == 0:
                    self._cond.notify_all()

    def getCount(self):
        return self._count

    def await_(self, timeout=None, unit=None):
        with self._cond:
            if timeout is None:
                while self._count > 0:
                    self._cond.wait()
                return True
            millis = int(timeout)
            if unit is not None and hasattr(unit, "toMillis"):
                millis = unit.toMillis(timeout)
            return self._cond.wait_for(lambda: self._count == 0, millis / 1000.0)


setattr(CountDownLatch, "await", CountDownLatch.await_)


class _TimeUnit(JavaObject):
    _java_name = "java.util.concurrent.TimeUnit"

    def __init__(self, name, millis):
        self._name = name
        self._millis = millis

    def toMillis(self, value):
        return int(float(value) * self._millis)

    def toSeconds(self, value):
        return int(float(value) * self._millis / 1000.0)

    def sleep(self, value):
        import time
        time.sleep(float(value) * self._millis / 1000.0)

    def __str__(self):
        return self._name


class TimeUnit(JavaObject):
    _java_name = "java.util.concurrent.TimeUnit"
    NANOSECONDS = _TimeUnit("NANOSECONDS", 1e-6)
    MICROSECONDS = _TimeUnit("MICROSECONDS", 1e-3)
    MILLISECONDS = _TimeUnit("MILLISECONDS", 1)
    SECONDS = _TimeUnit("SECONDS", 1000)
    MINUTES = _TimeUnit("MINUTES", 60000)
    HOURS = _TimeUnit("HOURS", 3600000)
    DAYS = _TimeUnit("DAYS", 86400000)


class _Future(JavaObject):
    _java_name = "java.util.concurrent.Future"

    def __init__(self, fn):
        self._done = threading.Event()
        self._value = None
        self._error = None
        self._cancelled = False

        def run():
            try:
                call = getattr(fn, "call", None) or getattr(fn, "run", None) or fn
                self._value = call()
            except BaseException as error:
                self._error = error
            finally:
                self._done.set()

        threading.Thread(target=run, daemon=True).start()

    def get(self, timeout=None, unit=None):
        wait = None
        if timeout is not None:
            wait = (unit.toMillis(timeout) if hasattr(unit, "toMillis") else int(timeout)) / 1000.0
        if not self._done.wait(wait):
            raise TimeoutError("TimeoutException")
        if self._error is not None:
            raise self._error
        return self._value

    def isDone(self):
        return self._done.is_set()

    def cancel(self, *args):
        self._cancelled = True
        return False

    def isCancelled(self):
        return self._cancelled


class _Executor(JavaObject):
    _java_name = "java.util.concurrent.ExecutorService"

    def __init__(self):
        self._shutdown = False

    def execute(self, task):
        _Future(task)

    def submit(self, task, *args):
        return _Future(task)

    def shutdown(self):
        self._shutdown = True

    def shutdownNow(self):
        self._shutdown = True
        return exd_java.ArrayList()

    def isShutdown(self):
        return self._shutdown

    def awaitTermination(self, *args):
        return True

    def schedule(self, task, delay=0, unit=None):
        millis = unit.toMillis(delay) if hasattr(unit, "toMillis") else int(delay)
        timer = threading.Timer(millis / 1000.0, lambda: _Future(task))
        timer.daemon = True
        timer.start()
        return timer


class Executors(JavaObject):
    _java_name = "java.util.concurrent.Executors"

    @staticmethod
    def newSingleThreadExecutor(*args):
        return _Executor()

    @staticmethod
    def newFixedThreadPool(*args):
        return _Executor()

    @staticmethod
    def newCachedThreadPool(*args):
        return _Executor()

    @staticmethod
    def newScheduledThreadPool(*args):
        return _Executor()

    @staticmethod
    def newSingleThreadScheduledExecutor(*args):
        return _Executor()


class _Atomic(JavaObject):
    def __init__(self, value=0):
        self._value = value
        self._lock = threading.Lock()

    def get(self):
        return self._value

    def set(self, value):
        self._value = value

    def getAndSet(self, value):
        with self._lock:
            old = self._value
            self._value = value
            return old

    def compareAndSet(self, expected, value):
        with self._lock:
            if self._value == expected:
                self._value = value
                return True
            return False

    def lazySet(self, value):
        self._value = value


class AtomicBoolean(_Atomic):
    _java_name = "java.util.concurrent.atomic.AtomicBoolean"

    def __init__(self, value=False):
        _Atomic.__init__(self, bool(value))


class AtomicReference(_Atomic):
    _java_name = "java.util.concurrent.atomic.AtomicReference"

    def __init__(self, value=None):
        _Atomic.__init__(self, value)


class AtomicInteger(_Atomic):
    _java_name = "java.util.concurrent.atomic.AtomicInteger"

    def __init__(self, value=0):
        _Atomic.__init__(self, int(value))

    def _add(self, delta, before):
        with self._lock:
            old = self._value
            self._value += int(delta)
            return old if before else self._value

    def incrementAndGet(self):
        return self._add(1, False)

    def decrementAndGet(self):
        return self._add(-1, False)

    def getAndIncrement(self):
        return self._add(1, True)

    def getAndDecrement(self):
        return self._add(-1, True)

    def addAndGet(self, delta):
        return self._add(delta, False)

    def getAndAdd(self, delta):
        return self._add(delta, True)

    def intValue(self):
        return int(self._value)


class AtomicLong(AtomicInteger):
    _java_name = "java.util.concurrent.atomic.AtomicLong"

    def longValue(self):
        return int(self._value)


class WeakReference(JavaObject):
    _java_name = "java.lang.ref.WeakReference"

    def __init__(self, value=None, *args):
        try:
            self._ref = weakref.ref(value)
        except TypeError:
            self._ref = lambda: value

    def get(self):
        return self._ref() if self._ref is not None else None

    def clear(self):
        self._ref = None


def _signed(value):
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value >= (1 << 31) else value


def _java_hash(value):
    if value is None:
        return 0
    if isinstance(value, bool):
        return 1231 if value else 1237
    if isinstance(value, int):
        if -(1 << 31) <= value < (1 << 31):
            return value
        return _signed(value ^ (value >> 32))
    if isinstance(value, str):
        result = 0
        for unit in memoryview(value.encode("utf-16-le")).cast("H"):
            result = (31 * result + unit) & 0xFFFFFFFF
        return _signed(result)
    code = getattr(value, "hashCode", None)
    if callable(code):
        try:
            return int(code())
        except Exception:
            pass
    return _signed(hash(value))


class Objects(JavaObject):
    _java_name = "java.util.Objects"

    @staticmethod
    def equals(a, b):
        return a == b

    @staticmethod
    def hash(*values):
        result = 1
        for value in values:
            result = (31 * result + _java_hash(value)) & 0xFFFFFFFF
        return _signed(result)

    @staticmethod
    def hashCode(value):
        return _java_hash(value)

    @staticmethod
    def requireNonNull(value, *args):
        if value is None:
            raise ValueError(str(args[0]) if args else "NullPointerException")
        return value

    @staticmethod
    def requireNonNullElse(value, default):
        return default if value is None else value

    @staticmethod
    def isNull(value):
        return value is None

    @staticmethod
    def nonNull(value):
        return value is not None

    @staticmethod
    def toString(value, default="null"):
        return default if value is None else str(value)


class _RuntimeInstance(JavaObject):
    _java_name = "java.lang.Runtime"

    def availableProcessors(self):
        return os.cpu_count() or 1

    def totalMemory(self):
        return 512 * 1024 * 1024

    def freeMemory(self):
        return 256 * 1024 * 1024

    def maxMemory(self):
        return 1024 * 1024 * 1024

    def gc(self):
        import gc
        gc.collect()

    def exec(self, *args):
        raise PermissionError("Runtime.exec is not available")


class Runtime(JavaObject):
    _java_name = "java.lang.Runtime"
    _instance = _RuntimeInstance()

    @staticmethod
    def getRuntime():
        return Runtime._instance


# Android

class ColorUtils(JavaObject):
    _java_name = "androidx.core.graphics.ColorUtils"

    @staticmethod
    def setAlphaComponent(color, alpha):
        return ((int(alpha) & 0xFF) << 24) | (int(color) & 0xFFFFFF)

    @staticmethod
    def blendARGB(a, b, ratio):
        ratio = float(ratio)
        inverse = 1.0 - ratio
        result = 0
        for shift in (24, 16, 8, 0):
            channel = int(((int(a) >> shift) & 0xFF) * inverse + ((int(b) >> shift) & 0xFF) * ratio)
            result |= (channel & 0xFF) << shift
        return result

    @staticmethod
    def calculateLuminance(color):
        def channel(value):
            value = value / 255.0
            return value / 12.92 if value <= 0.03928 else ((value + 0.055) / 1.055) ** 2.4
        color = int(color)
        return 0.2126 * channel((color >> 16) & 0xFF) + 0.7152 * channel((color >> 8) & 0xFF) + 0.0722 * channel(color & 0xFF)

    @staticmethod
    def compositeColors(foreground, background):
        alpha = ((int(foreground) >> 24) & 0xFF) / 255.0
        return ColorUtils.blendARGB(background, foreground | 0xFF000000, alpha) | 0xFF000000


class AccelerateDecelerateInterpolator(JavaObject):
    _java_name = "android.view.animation.AccelerateDecelerateInterpolator"

    def getInterpolation(self, value):
        import math
        return (math.cos((float(value) + 1) * math.pi) / 2.0) + 0.5


class LinearInterpolator(JavaObject):
    _java_name = "android.view.animation.LinearInterpolator"

    def getInterpolation(self, value):
        return float(value)


class AccelerateInterpolator(JavaObject):
    _java_name = "android.view.animation.AccelerateInterpolator"

    def __init__(self, factor=1.0):
        self._factor = float(factor)

    def getInterpolation(self, value):
        return float(value) ** (2 * self._factor)


class _LengthFilter(JavaObject):
    _java_name = "android.text.InputFilter$LengthFilter"

    def __init__(self, maximum):
        self._max = int(maximum)

    def getMax(self):
        return self._max


class InputFilter(JavaObject):
    _java_name = "android.text.InputFilter"
    LengthFilter = _LengthFilter


class VibrationEffect(JavaObject):
    _java_name = "android.os.VibrationEffect"
    DEFAULT_AMPLITUDE = -1
    EFFECT_CLICK = 0
    EFFECT_DOUBLE_CLICK = 1
    EFFECT_TICK = 2
    EFFECT_HEAVY_CLICK = 5

    @staticmethod
    def createOneShot(*args):
        return VibrationEffect()

    @staticmethod
    def createWaveform(*args):
        return VibrationEffect()

    @staticmethod
    def createPredefined(*args):
        return VibrationEffect()


class ComponentName(JavaObject):
    _java_name = "android.content.ComponentName"

    def __init__(self, package="", cls=""):
        self._package = str(getattr(package, "getPackageName", lambda: package)())
        self._class = str(getattr(cls, "getName", lambda: cls)())

    def getPackageName(self):
        return self._package

    def getClassName(self):
        return self._class

    def flattenToString(self):
        return self._package + "/" + self._class


class IntentFilter(JavaObject):
    _java_name = "android.content.IntentFilter"

    def __init__(self, action=None, *args):
        self._actions = [str(action)] if action else []

    def addAction(self, action):
        self._actions.append(str(action))

    def hasAction(self, action):
        return str(action) in self._actions

    def countActions(self):
        return len(self._actions)

    def getAction(self, index):
        return self._actions[int(index)]

    def addCategory(self, *args):
        return None

    def setPriority(self, *args):
        return None


class NotificationManagerCompat(JavaObject):
    _java_name = "androidx.core.app.NotificationManagerCompat"
    IMPORTANCE_DEFAULT = 3
    IMPORTANCE_HIGH = 4
    IMPORTANCE_LOW = 2

    @staticmethod
    def from_(*args):
        return exd_java_more2.NotificationManager()

    def areNotificationsEnabled(self):
        return True


setattr(NotificationManagerCompat, "from", staticmethod(lambda *args: exd_java_more2.NotificationManager()))


class _CompatBuilder(exd_java_more2._NotificationBuilder):
    _java_name = "androidx.core.app.NotificationCompat$Builder"


class NotificationCompat(JavaObject):
    _java_name = "androidx.core.app.NotificationCompat"
    Builder = _CompatBuilder
    PRIORITY_LOW = -1
    PRIORITY_DEFAULT = 0
    PRIORITY_HIGH = 1
    PRIORITY_MAX = 2
    CATEGORY_MESSAGE = "msg"
    VISIBILITY_PUBLIC = 1


class NetworkCapabilities(JavaObject):
    _java_name = "android.net.NetworkCapabilities"
    TRANSPORT_CELLULAR = 0
    TRANSPORT_WIFI = 1
    TRANSPORT_BLUETOOTH = 2
    TRANSPORT_ETHERNET = 3
    TRANSPORT_VPN = 4
    NET_CAPABILITY_INTERNET = 12
    NET_CAPABILITY_VALIDATED = 16

    def hasTransport(self, transport):
        return int(transport) == 3

    def hasCapability(self, capability):
        return True


class _MemoryInfo(JavaObject):
    _java_name = "android.app.ActivityManager$MemoryInfo"

    def __init__(self):
        self.availMem = 4 * 1024 * 1024 * 1024
        self.totalMem = 8 * 1024 * 1024 * 1024
        self.lowMemory = False
        self.threshold = 256 * 1024 * 1024


class ActivityManager(JavaObject):
    _java_name = "android.app.ActivityManager"
    _exd_stub = True
    MemoryInfo = _MemoryInfo

    def getMemoryInfo(self, info):
        return None

    def getMemoryClass(self):
        return 512

    def isLowRamDevice(self):
        return False

    def getRunningAppProcesses(self):
        return exd_java.ArrayList()


class _ImagesMedia(JavaObject):
    _java_name = "android.provider.MediaStore$Images$Media"
    EXTERNAL_CONTENT_URI = "content://media/external/images/media"
    DATA = "_data"
    DISPLAY_NAME = "_display_name"
    MIME_TYPE = "mime_type"
    RELATIVE_PATH = "relative_path"


class _Images(JavaObject):
    _java_name = "android.provider.MediaStore$Images"
    Media = _ImagesMedia


exd_java_more2.MediaStore.Images = _Images


class MediaMetadataRetriever(JavaObject):
    _java_name = "android.media.MediaMetadataRetriever"
    _exd_stub = True
    METADATA_KEY_DURATION = 9
    METADATA_KEY_TITLE = 7
    METADATA_KEY_ARTIST = 2
    METADATA_KEY_ALBUM = 1
    METADATA_KEY_MIMETYPE = 12
    METADATA_KEY_VIDEO_WIDTH = 18
    METADATA_KEY_VIDEO_HEIGHT = 19
    METADATA_KEY_BITRATE = 20

    def __init__(self):
        self._path = None

    def setDataSource(self, path, *args):
        self._path = path.getAbsolutePath() if hasattr(path, "getAbsolutePath") else str(path)

    def extractMetadata(self, key):
        if int(key) == 12 and self._path:
            import mimetypes
            return mimetypes.guess_type(self._path)[0]
        return None

    def getFrameAtTime(self, *args):
        return None

    def release(self):
        self._path = None

    def close(self):
        self._path = None


class Path(JavaObject):
    _java_name = "android.graphics.Path"

    def __init__(self, *args):
        self._ops = []

    def __getattr__(self, item):
        if item.startswith(("moveTo", "lineTo", "quadTo", "cubicTo", "arcTo", "add", "close", "rMoveTo",
                            "rLineTo", "reset", "rewind", "offset", "transform", "setFillType", "op")):
            def record(*args, **kwargs):
                self._ops.append((item, args))
            return record
        return JavaObject.__getattr__(self, item)

    def isEmpty(self):
        return not self._ops


class PathParser(JavaObject):
    _java_name = "androidx.core.graphics.PathParser"

    @staticmethod
    def createPathFromPathData(data):
        path = Path()
        path._ops.append(("svg", (str(data),)))
        return path


class CheckBox(android_views.CheckBox):
    _java_name = "android.widget.CheckBox"


class Emoji(JavaObject):
    _java_name = "org.telegram.messenger.Emoji"

    @staticmethod
    def replaceEmoji(text, *args):
        return text

    @staticmethod
    def isValidEmoji(text):
        return bool(text)

    @staticmethod
    def fixEmoji(text):
        return text


# Interfaces

def _interface(name, *methods):
    short = name.rsplit(".", 1)[-1].split("$")[-1]
    attrs = {"_java_name": name}
    for method in methods:
        attrs[method] = lambda self, *args, **kwargs: None
    return type(short, (JavaObject,), attrs)


_INTERFACES = {
    "android.webkit.ValueCallback": ("onReceiveValue",),
    "android.webkit.WebViewClient": ("shouldOverrideUrlLoading", "onPageStarted", "onPageFinished"),
    "android.widget.PopupWindow$OnDismissListener": ("onDismiss",),
    "org.telegram.ui.Components.RecyclerListView$OnItemClickListener": ("onItemClick",),
    "org.telegram.ui.Components.RecyclerListView$OnItemLongClickListener": ("onItemClick",),
    "org.telegram.ui.Components.SeekBarView$SeekBarViewDelegate": ("onSeekBarDrag", "onSeekBarPressed"),
    "org.telegram.ui.DialogsActivity$DialogsActivityDelegate": ("didSelectDialogs",),
    "com.exteragram.messenger.plugins.ui.components.templates.UniversalFragment$UniversalFragmentDelegate": (),
}
_interfaces = {name: _interface(name, *methods) for name, methods in _INTERFACES.items()}


class PopupWindow(JavaObject):
    _java_name = "android.widget.PopupWindow"
    _exd_stub = True
    OnDismissListener = _interfaces["android.widget.PopupWindow$OnDismissListener"]


class RecyclerListView(android_views.View):
    _java_name = "org.telegram.ui.Components.RecyclerListView"
    OnItemClickListener = _interfaces["org.telegram.ui.Components.RecyclerListView$OnItemClickListener"]
    OnItemLongClickListener = _interfaces["org.telegram.ui.Components.RecyclerListView$OnItemLongClickListener"]


class SeekBarView(android_views.View):
    _java_name = "org.telegram.ui.Components.SeekBarView"
    SeekBarViewDelegate = _interfaces["org.telegram.ui.Components.SeekBarView$SeekBarViewDelegate"]


class WebViewClient(_interfaces["android.webkit.WebViewClient"]):
    _java_name = "android.webkit.WebViewClient"


# Stubs

_STUB_NAMES = (
    "org.telegram.ui.ProxyListActivity",
    "org.telegram.ui.ProxyListActivity$TextDetailProxyCell",
    "android.media.MediaPlayer",
    "android.media.SoundPool",
    "android.media.MediaFormat",
    "android.media.MediaCodec",
    "android.media.MediaExtractor",
    "org.telegram.ui.Components.RLottieImageView",
    "org.telegram.ui.Components.RLottieDrawable",
    "org.telegram.messenger.VideoEditedInfo",
    "org.telegram.ui.Cells.CheckBoxCell",
    "org.telegram.ui.Cells.RadioColorCell",
    "org.telegram.ui.Stars.StarsController",
    "com.exteragram.messenger.badges.source.BadgeInfo",
    "com.exteragram.messenger.badges.source.ApiBadgeSource",
    "com.exteragram.messenger.pillstack.ui.pills.weather.WeatherPill",
    "com.exteragram.messenger.nowplaying.NowPlayingController",
    "org.telegram.messenger.ImageLoader",
    "org.telegram.messenger.ImageReceiver",
    "org.telegram.ui.Components.Paint.ColorPickerBottomSheet",
    "org.telegram.ui.Components.ChatAttachAlert",
    "org.telegram.ui.Components.ChatAttachAlertPhotoLayout",
    "org.telegram.ui.Components.AnimatedTextView",
    "org.telegram.ui.Components.AnimatedEmojiSpan",
    "org.telegram.ui.Components.ReactionsContainerLayout",
    "org.telegram.ui.Components.voip.VoIPHelper",
    "com.exteragram.messenger.plugins.ui.components.InstallPluginBottomSheet",
    "org.telegram.ui.ActionBar.SimpleTextView",
    "android.graphics.RenderEffect",
    "android.graphics.ImageDecoder",
    "android.view.ViewTreeObserver",
    "android.app.AlertDialog$Builder",
)


def _stub(name):
    short = name.rsplit(".", 1)[-1].split("$")[-1]
    cls = type(short, (JavaObject,), {"_java_name": name, "_exd_stub": True})
    holder = {}

    def get_instance(*args):
        if "value" not in holder:
            holder["value"] = cls()
        return holder["value"]

    cls.getInstance = staticmethod(get_instance)
    return cls


_stubs = {name: _stub(name) for name in _STUB_NAMES}
_stubs["android.media.MediaFormat"].KEY_MIME = "mime"
_stubs["android.media.MediaFormat"].KEY_BIT_RATE = "bitrate"
_stubs["android.media.MediaFormat"].KEY_SAMPLE_RATE = "sample-rate"
_stubs["android.media.MediaFormat"].KEY_CHANNEL_COUNT = "channel-count"
_stubs["android.media.MediaFormat"].KEY_MAX_INPUT_SIZE = "max-input-size"
_stubs["org.telegram.ui.ProxyListActivity"].TextDetailProxyCell = _stubs["org.telegram.ui.ProxyListActivity$TextDetailProxyCell"]


def _unsupported(*args, **kwargs):
    raise RuntimeError("Not available on desktop")


_stubs["android.graphics.RenderEffect"].createBlurEffect = staticmethod(lambda *args: None)
_stubs["android.graphics.ImageDecoder"].createSource = staticmethod(lambda *args: None)
_stubs["android.graphics.ImageDecoder"].decodeDrawable = staticmethod(_unsupported)
_stubs["android.media.MediaCodec"].createEncoderByType = staticmethod(_unsupported)
_stubs["android.media.MediaCodec"].createDecoderByType = staticmethod(_unsupported)
_stubs["android.media.MediaFormat"].createAudioFormat = staticmethod(_unsupported)


# Patches

def _patch_message_object():
    message = exd_java.MessageObject

    def media(self, *args):
        return getattr(self.messageOwner, "media", None)

    def document(self, *args):
        value = media(self)
        return getattr(value, "document", None) if value is not None else None

    def doc_flag(name):
        def check(self, *args):
            doc = document(self) if not args or isinstance(args[0], bool) else args[0]
            mime = str(getattr(doc, "mime_type", "") or "")
            if name == "gif":
                return mime in ("video/mp4", "image/gif") and any(
                    type(attr).__name__.endswith("Animated") for attr in (getattr(doc, "attributes", None) or []))
            if name == "static_sticker":
                return mime == "image/webp"
            if name == "video_sticker":
                return mime == "video/webm"
            return False
        return check

    extras = {
        "getMedia": media,
        "getDocument": document,
        "isGifDocument": staticmethod(lambda doc, *args: doc_flag("gif")(None, doc)),
        "isStaticStickerDocument": staticmethod(lambda doc, *args: doc_flag("static_sticker")(None, doc)),
        "isVideoSticker": staticmethod(lambda doc, *args: doc_flag("video_sticker")(None, doc)),
        "isGif": doc_flag("gif"),
        "isSticker": doc_flag("static_sticker"),
        "replaceAnimatedEmoji": staticmethod(lambda text, *args: text),
        "getTopicId": staticmethod(lambda *args: 0),
        "getMessageSize": staticmethod(lambda *args: 0),
        "isVoice": lambda self, *args: False,
        "isMusic": lambda self, *args: False,
        "isVideo": lambda self, *args: False,
        "isPhoto": lambda self, *args: False,
        "isForwarded": lambda self, *args: getattr(self.messageOwner, "fwd_from", None) is not None,
        "getReplyMsgId": lambda self, *args: 0,
    }
    for name, value in extras.items():
        if name not in vars(message):
            setattr(message, name, value)


def _patch_misc():
    exd_java.TextUtils.TruncateAt = exd_java_more2.TruncateAt
    exd_java.ChatObject.isNotInChat = staticmethod(
        lambda chat, *args: bool(getattr(chat, "left", False) or getattr(chat, "kicked", False)))
    loader = _compat._ApplicationLoader
    loader.getApplicationId = staticmethod(lambda *args: "com.exteragram.messenger")
    context = _compat._Context
    if "getLastFragment" not in vars(context):
        context.getLastFragment = lambda self, *args: tgnet_bridge.current_fragment()
        context.getSafeLastFragment = lambda self, *args: tgnet_bridge.current_fragment()
        context.getActionBarLayout = lambda self, *args: _compat.Stub("ActionBarLayout")
    tgnet_bridge.ConnectionsManager.setProxySettings = staticmethod(_set_proxy_settings)
    controller = exd_android.PluginsController
    controller.openPluginSettings = staticmethod(lambda plugin_id, *args: controller.getInstance().openPluginSettings(plugin_id))
    controller.isPlugin = staticmethod(lambda path, *args: str(path).endswith((".plugin", ".py")))
    controller.engines = exd_java.HashMap({"python": exd_android.PythonPluginsEngine})
    typeface = android_views.Typeface
    if "create" not in vars(typeface):
        typeface.create = staticmethod(lambda family=None, style=0, *args: int(style or 0))
        typeface.createFromAsset = staticmethod(lambda *args: android_views.Typeface.DEFAULT)
        typeface.createFromFile = staticmethod(lambda *args: android_views.Typeface.DEFAULT)


for _patch in (_patch_message_object, _patch_misc):
    try:
        _patch()
    except Exception as _error:
        print("exd_java_more3:", _patch.__name__, _error, file=sys.stderr)


exd_java._register(
    ProxyInfo, SharedConfig, SecretKeySpec, IvParameterSpec, GCMParameterSpec, Cipher,
    KeyGenerator, SecureRandom, InputStreamReader, BufferedReader, StringReader, FileReader,
    OutputStreamWriter, BufferedWriter, StringWriter, PrintWriter, FileWriter, CountDownLatch,
    TimeUnit, _Future, _Executor, Executors, AtomicBoolean, AtomicReference, AtomicInteger,
    AtomicLong, WeakReference, Objects, Runtime, ColorUtils, AccelerateDecelerateInterpolator,
    LinearInterpolator, AccelerateInterpolator, _LengthFilter, InputFilter, VibrationEffect,
    ComponentName, IntentFilter, NotificationManagerCompat, _CompatBuilder, NotificationCompat,
    NetworkCapabilities, _MemoryInfo, ActivityManager, _ImagesMedia, _Images,
    MediaMetadataRetriever, Path, PathParser, CheckBox, Emoji, PopupWindow, RecyclerListView,
    SeekBarView, WebViewClient, *_interfaces.values(), *_stubs.values())
