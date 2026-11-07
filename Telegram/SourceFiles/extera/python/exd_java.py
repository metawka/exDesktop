import base64 as _b64
import locale as _locale
import os
import random as _random
import struct
import threading
import time
import uuid as _uuid

import tl_runtime

_language = None


def set_language(code):
    global _language
    _language = (str(code or "").split("-")[0].split("_")[0].lower()) or None


def language():
    if _language:
        return _language
    try:
        name = (_locale.getlocale()[0] or "").lower()
    except Exception:
        name = ""
    for prefix, code in (("ru", "ru"), ("russian", "ru"), ("uk", "uk"), ("ukrainian", "uk"),
                         ("be", "be"), ("kk", "kk"), ("de", "de"), ("german", "de"),
                         ("es", "es"), ("spanish", "es"), ("fr", "fr"), ("french", "fr"),
                         ("it", "it"), ("pt", "pt"), ("tr", "tr"), ("pl", "pl")):
        if name.startswith(prefix):
            return code
    return "en"


class hybridmethod:
    def __init__(self, fn):
        self.fn = fn

    def __get__(self, obj, owner):
        target = obj if obj is not None else owner
        return lambda *args, **kwargs: self.fn(target, *args, **kwargs)


def _java_name_of(target):
    cls = target if isinstance(target, type) else type(target)
    return getattr(cls, "_java_name", cls.__name__)


def _missing(name):
    import _compat
    return _compat.Missing(name)


class JavaClassInfo:
    def __init__(self, name, python_class=None):
        self._name = name
        self._class = python_class

    def getName(self):
        return self._name

    def getSimpleName(self):
        return self._name.split(".")[-1].split("$")[-1]

    def getCanonicalName(self):
        return self._name.replace("$", ".")

    def isInstance(self, value):
        return self._class is not None and isinstance(value, self._class)

    def newInstance(self, *args):
        return self._class(*args) if self._class else None

    def getSuperclass(self):
        if self._class is None:
            return None
        for base in self._class.__mro__[1:]:
            name = base.__dict__.get("_java_name")
            if name and name != self._name:
                return JavaClassInfo(name, base)
        return None

    def getDeclaredMethods(self):
        return []

    def getDeclaredFields(self):
        return []

    def getMethods(self):
        return []

    def getFields(self):
        return []

    def getInterfaces(self):
        return []

    def __call__(self, *args, **kwargs):
        if self._class is None:
            raise TypeError("class %s is not available on desktop" % self._name)
        return self._class(*args, **kwargs)

    def __getattr__(self, item):
        if item.startswith("__") or self._class is None:
            raise AttributeError(item)
        return getattr(self._class, item)

    def __repr__(self):
        return "<class %s>" % self._name


class _LenientJavaMeta(type):
    def __getattr__(cls, item):
        if item.startswith("__") and item.endswith("__"):
            raise AttributeError(item)
        import _compat
        return _compat.lenient(getattr(cls, "_java_name", cls.__name__), item)


class JavaObject(metaclass=_LenientJavaMeta):
    def __getattr__(self, item):
        if item.startswith("__") and item.endswith("__"):
            raise AttributeError(item)
        import _compat
        return _compat.lenient(_java_name_of(self) + "()", item)

    @hybridmethod
    def getClass(target):
        cls = target if isinstance(target, type) else type(target)
        return JavaClassInfo(getattr(cls, "_java_name", cls.__name__), cls)

    @hybridmethod
    def getDeclaredMethod(target, name, *args):
        return _missing("%s.%s" % (_java_name_of(target), name))

    @hybridmethod
    def getMethod(target, name, *args):
        return _missing("%s.%s" % (_java_name_of(target), name))

    @hybridmethod
    def getDeclaredField(target, name):
        return _missing("%s.%s" % (_java_name_of(target), name))

    @hybridmethod
    def getField(target, name):
        return _missing("%s.%s" % (_java_name_of(target), name))

    @hybridmethod
    def getDeclaredConstructor(target, *args):
        return _missing("%s.<init>" % _java_name_of(target))

    @hybridmethod
    def getDeclaredMethods(target):
        return []

    @hybridmethod
    def getDeclaredFields(target):
        return []

    def hashCode(self):
        return hash(self) & 0x7FFFFFFF

    def equals(self, other):
        return self == other

    def toString(self):
        return str(self)


# Lang

class Object(JavaObject):
    _java_name = "java.lang.Object"


class Runnable(JavaObject):
    _java_name = "java.lang.Runnable"

    def __init__(self, fn=None):
        self._fn = fn

    def run(self):
        if self._fn:
            self._fn()


class Integer(JavaObject):
    _java_name = "java.lang.Integer"
    MAX_VALUE = 2147483647
    MIN_VALUE = -2147483648
    SIZE = 32
    TYPE = "int"

    def __new__(cls, value=0):
        return int(value)

    @staticmethod
    def valueOf(value, radix=10):
        return int(value, radix) if isinstance(value, str) else int(value)

    @staticmethod
    def parseInt(value, radix=10):
        return int(str(value).strip(), radix)

    @staticmethod
    def toString(value=0, radix=10):
        if radix == 16:
            return format(int(value) & 0xFFFFFFFF, "x") if int(value) < 0 else format(int(value), "x")
        return str(int(value))

    @staticmethod
    def toHexString(value):
        return format(int(value) & 0xFFFFFFFF, "x")

    @staticmethod
    def toBinaryString(value):
        return format(int(value) & 0xFFFFFFFF, "b")

    @staticmethod
    def compare(a, b):
        return (a > b) - (a < b)

    @staticmethod
    def max(a, b):
        return max(a, b)

    @staticmethod
    def min(a, b):
        return min(a, b)

    @staticmethod
    def signum(value):
        return (value > 0) - (value < 0)


class Long(Integer):
    _java_name = "java.lang.Long"
    MAX_VALUE = 9223372036854775807
    MIN_VALUE = -9223372036854775808
    SIZE = 64
    TYPE = "long"

    @staticmethod
    def parseLong(value, radix=10):
        return int(str(value).strip(), radix)

    @staticmethod
    def toHexString(value):
        return format(int(value) & 0xFFFFFFFFFFFFFFFF, "x")


class Short(Integer):
    _java_name = "java.lang.Short"
    TYPE = "short"


class Byte(Integer):
    _java_name = "java.lang.Byte"
    TYPE = "byte"


class Float(JavaObject):
    _java_name = "java.lang.Float"
    TYPE = "float"
    MAX_VALUE = 3.4028235e38
    MIN_VALUE = 1.4e-45

    def __new__(cls, value=0.0):
        return float(value)

    @staticmethod
    def valueOf(value):
        return float(value)

    @staticmethod
    def parseFloat(value):
        return float(str(value).strip())

    @staticmethod
    def compare(a, b):
        return (a > b) - (a < b)


class Double(Float):
    _java_name = "java.lang.Double"
    TYPE = "double"

    @staticmethod
    def parseDouble(value):
        return float(str(value).strip())


class Boolean(JavaObject):
    _java_name = "java.lang.Boolean"
    TRUE = True
    FALSE = False
    TYPE = "boolean"

    def __new__(cls, value=False):
        return bool(value)

    @staticmethod
    def valueOf(value):
        if isinstance(value, str):
            return value.strip().lower() == "true"
        return bool(value)

    @staticmethod
    def parseBoolean(value):
        return str(value).strip().lower() == "true"

    @staticmethod
    def toString(value=False):
        return "true" if value else "false"


class Character(JavaObject):
    _java_name = "java.lang.Character"
    TYPE = "char"

    @staticmethod
    def isDigit(c):
        return str(c).isdigit()

    @staticmethod
    def isLetter(c):
        return str(c).isalpha()

    @staticmethod
    def isLetterOrDigit(c):
        return str(c).isalnum()

    @staticmethod
    def isWhitespace(c):
        return str(c).isspace()

    @staticmethod
    def isUpperCase(c):
        return str(c).isupper()

    @staticmethod
    def isLowerCase(c):
        return str(c).islower()

    @staticmethod
    def toUpperCase(c):
        return str(c).upper()

    @staticmethod
    def toLowerCase(c):
        return str(c).lower()


class String(JavaObject):
    _java_name = "java.lang.String"
    CASE_INSENSITIVE_ORDER = None

    def __new__(cls, value="", *args):
        if isinstance(value, (bytes, bytearray)):
            encoding = args[0] if args and isinstance(args[0], str) else "utf-8"
            return bytes(value).decode(encoding, "replace")
        if isinstance(value, (list, tuple)):
            return "".join(str(c) for c in value)
        return str(value)

    @staticmethod
    def valueOf(value):
        if isinstance(value, bool):
            return "true" if value else "false"
        if value is None:
            return "null"
        return str(value)

    @staticmethod
    def format(pattern, *args):
        text = str(pattern)
        for spec in ("%d", "%s", "%f"):
            text = text.replace("%1$" + spec[1], spec)
        try:
            return text % tuple(args)
        except Exception:
            return text

    @staticmethod
    def join(separator, values):
        return str(separator).join(str(v) for v in values)


class StringBuilder(JavaObject):
    _java_name = "java.lang.StringBuilder"

    def __init__(self, value=""):
        self._parts = [str(value)] if value not in ("", None) and not isinstance(value, int) else []

    def append(self, value):
        self._parts.append("true" if value is True else "false" if value is False else str(value))
        return self

    def length(self):
        return len(self.toString())

    def setLength(self, size):
        text = self.toString()[:size]
        self._parts = [text]

    def insert(self, index, value):
        text = self.toString()
        self._parts = [text[:index] + str(value) + text[index:]]
        return self

    def reverse(self):
        self._parts = [self.toString()[::-1]]
        return self

    def toString(self):
        return "".join(self._parts)

    __str__ = toString


class System(JavaObject):
    _java_name = "java.lang.System"
    _start = time.monotonic()

    class out:
        @staticmethod
        def println(value=""):
            _log("System.out: %s" % value)

        print = println

    err = out

    @staticmethod
    def currentTimeMillis():
        return int(time.time() * 1000)

    @staticmethod
    def nanoTime():
        return time.monotonic_ns()

    @staticmethod
    def getProperty(name, default=None):
        values = {
            "os.name": "Windows" if os.name == "nt" else os.uname().sysname,
            "line.separator": "\n",
            "file.separator": os.sep,
            "user.language": language(),
            "java.vm.name": "exDesktop",
            "http.agent": "exDesktop",
        }
        return values.get(str(name), default)

    @staticmethod
    def arraycopy(src, src_pos, dest, dest_pos, length):
        dest[dest_pos:dest_pos + length] = src[src_pos:src_pos + length]

    @staticmethod
    def gc():
        return None

    @staticmethod
    def exit(code=0):
        raise PermissionError("System.exit is not available to plugins")

    @staticmethod
    def identityHashCode(value):
        return id(value) & 0x7FFFFFFF


class Thread(JavaObject):
    _java_name = "java.lang.Thread"

    def __init__(self, target=None, name=None):
        run = getattr(target, "run", target)
        self._thread = threading.Thread(target=run or self.run, name=name, daemon=True)

    def start(self):
        self._thread.start()

    def run(self):
        return None

    def join(self, timeout=None):
        self._thread.join(timeout / 1000 if timeout else None)

    def setDaemon(self, value):
        return None

    def setName(self, name):
        self._thread.name = str(name)

    def interrupt(self):
        return None

    def isAlive(self):
        return self._thread.is_alive()

    @staticmethod
    def sleep(millis, nanos=0):
        time.sleep(max(0, int(millis)) / 1000.0)

    @staticmethod
    def currentThread():
        return _CurrentThread()


class _CurrentThread(JavaObject):
    def getName(self):
        return threading.current_thread().name

    def getId(self):
        return threading.get_ident()

    def isInterrupted(self):
        return False


class Math(JavaObject):
    _java_name = "java.lang.Math"
    PI = 3.141592653589793
    E = 2.718281828459045

    @staticmethod
    def max(a, b):
        return max(a, b)

    @staticmethod
    def min(a, b):
        return min(a, b)

    @staticmethod
    def abs(a):
        return abs(a)

    @staticmethod
    def round(a):
        return int(a + 0.5) if a >= 0 else -int(-a + 0.5)

    @staticmethod
    def floor(a):
        import math
        return float(math.floor(a))

    @staticmethod
    def ceil(a):
        import math
        return float(math.ceil(a))

    @staticmethod
    def sqrt(a):
        import math
        return math.sqrt(a)

    @staticmethod
    def pow(a, b):
        return float(a) ** b

    @staticmethod
    def random():
        return _random.random()

    @staticmethod
    def sin(a):
        import math
        return math.sin(a)

    @staticmethod
    def cos(a):
        import math
        return math.cos(a)


class Class(JavaObject):
    _java_name = "java.lang.Class"

    @staticmethod
    def forName(name, *args):
        found = lookup(str(name))
        if found is not None:
            return found
        import _compat
        return _compat.Missing(str(name))


# Util

class ArrayList(tl_runtime.ArrayList, JavaObject):
    _java_name = "java.util.ArrayList"

    def __init__(self, values=None):
        super().__init__(values if isinstance(values, (list, tuple, set)) else [])

    def remove(self, value):
        if isinstance(value, int) and not isinstance(value, bool) and 0 <= value < len(self):
            return self.pop(value)
        try:
            list.remove(self, value)
            return True
        except ValueError:
            return False

    def indexOf(self, value):
        try:
            return self.index(value)
        except ValueError:
            return -1

    def iterator(self):
        return _Iterator(list(self))

    def stream(self):
        return list(self)


LinkedList = ArrayList
Vector = ArrayList
CopyOnWriteArrayList = ArrayList


class _Iterator:
    def __init__(self, values):
        self._values = values
        self._index = 0

    def hasNext(self):
        return self._index < len(self._values)

    def next(self):
        value = self._values[self._index]
        self._index += 1
        return value

    def __iter__(self):
        return iter(self._values[self._index:])


class _Entry:
    def __init__(self, key, value):
        self._key = key
        self._value = value

    def getKey(self):
        return self._key

    def getValue(self):
        return self._value


class HashMap(dict, JavaObject):
    _java_name = "java.util.HashMap"

    def __init__(self, values=None):
        super().__init__(values if isinstance(values, dict) else {})

    def put(self, key, value):
        old = dict.get(self, key)
        self[key] = value
        return old

    def get(self, key, default=None):
        return dict.get(self, key, default)

    def getOrDefault(self, key, default):
        return dict.get(self, key, default)

    def containsKey(self, key):
        return key in self

    def containsValue(self, value):
        return value in self.values()

    def remove(self, key):
        return self.pop(key, None)

    def size(self):
        return len(self)

    def isEmpty(self):
        return len(self) == 0

    def keySet(self):
        return HashSet(self.keys())

    def entrySet(self):
        return [_Entry(k, v) for k, v in self.items()]

    def putAll(self, other):
        self.update(other)

    def putIfAbsent(self, key, value):
        return self.setdefault(key, value)


LinkedHashMap = HashMap
TreeMap = HashMap
ConcurrentHashMap = HashMap
Hashtable = HashMap


class HashSet(set, JavaObject):
    _java_name = "java.util.HashSet"

    def __init__(self, values=None):
        super().__init__(values if values is not None and not isinstance(values, int) else ())

    def add(self, value):
        if value in self:
            return False
        set.add(self, value)
        return True

    def contains(self, value):
        return value in self

    def remove(self, value):
        if value in self:
            set.discard(self, value)
            return True
        return False

    def size(self):
        return len(self)

    def isEmpty(self):
        return len(self) == 0

    def iterator(self):
        return _Iterator(list(self))

    def addAll(self, values):
        self.update(values)
        return True


LinkedHashSet = HashSet
TreeSet = HashSet


class Locale(JavaObject):
    _java_name = "java.util.Locale"

    def __init__(self, language_code="en", country="", variant=""):
        self._language = str(language_code or "en").lower()
        self._country = str(country or "").upper()

    def getLanguage(self):
        return self._language

    def getCountry(self):
        return self._country

    def getDisplayLanguage(self, *args):
        return self._language

    def toLanguageTag(self):
        return self._language + ("-" + self._country if self._country else "")

    def toString(self):
        return self._language + ("_" + self._country if self._country else "")

    __str__ = toString

    @staticmethod
    def getDefault():
        return Locale(language())

    @staticmethod
    def forLanguageTag(tag):
        parts = str(tag).replace("_", "-").split("-")
        return Locale(parts[0], parts[1] if len(parts) > 1 else "")


Locale.ENGLISH = Locale("en")
Locale.US = Locale("en", "US")
Locale.ROOT = Locale("")
Locale.GERMAN = Locale("de")
Locale.FRENCH = Locale("fr")


class Date(JavaObject):
    _java_name = "java.util.Date"

    def __init__(self, millis=None):
        self._millis = int(time.time() * 1000) if millis is None else int(millis)

    def getTime(self):
        return self._millis

    def toString(self):
        return time.strftime("%a %b %d %H:%M:%S %Y", time.localtime(self._millis / 1000))

    __str__ = toString


class Calendar(JavaObject):
    _java_name = "java.util.Calendar"
    ERA, YEAR, MONTH, WEEK_OF_YEAR, WEEK_OF_MONTH, DATE, DAY_OF_MONTH, DAY_OF_YEAR, DAY_OF_WEEK = 0, 1, 2, 3, 4, 5, 5, 6, 7
    AM_PM, HOUR, HOUR_OF_DAY, MINUTE, SECOND, MILLISECOND = 9, 10, 11, 12, 13, 14
    SUNDAY, MONDAY, TUESDAY, WEDNESDAY, THURSDAY, FRIDAY, SATURDAY = 1, 2, 3, 4, 5, 6, 7

    def __init__(self, millis=None):
        self._millis = int(time.time() * 1000) if millis is None else int(millis)

    @staticmethod
    def getInstance(*args):
        return Calendar()

    def _parts(self):
        import datetime
        return datetime.datetime.fromtimestamp(self._millis / 1000)

    def get(self, field):
        value = self._parts()
        if field == 1:
            return value.year
        if field == 2:
            return value.month - 1
        if field == 5:
            return value.day
        if field == 6:
            return value.timetuple().tm_yday
        if field == 7:
            return (value.isoweekday() % 7) + 1
        if field == 10:
            return value.hour % 12
        if field == 11:
            return value.hour
        if field == 12:
            return value.minute
        if field == 13:
            return value.second
        if field == 14:
            return value.microsecond // 1000
        if field == 9:
            return 0 if value.hour < 12 else 1
        if field == 3:
            return value.isocalendar()[1]
        return 0

    def set(self, *args):
        import datetime
        value = self._parts()
        if len(args) == 2:
            field, amount = args
            names = {1: "year", 2: "month", 5: "day", 11: "hour", 12: "minute", 13: "second"}
            if field in names:
                amount = amount + 1 if field == 2 else amount
                value = value.replace(**{names[field]: int(amount)})
            elif field == 14:
                value = value.replace(microsecond=int(amount) * 1000)
        elif len(args) >= 3:
            parts = list(args) + [0] * (6 - len(args))
            value = datetime.datetime(int(parts[0]), int(parts[1]) + 1, int(parts[2]), int(parts[3]),
                                      int(parts[4]), int(parts[5]))
        self._millis = int(value.timestamp() * 1000)

    def add(self, field, amount):
        seconds = {5: 86400, 6: 86400, 7: 86400, 3: 604800, 4: 604800, 11: 3600, 10: 3600, 12: 60, 13: 1}
        if field in seconds:
            self._millis += int(amount) * seconds[field] * 1000
        elif field == 14:
            self._millis += int(amount)
        elif field in (1, 2):
            value = self._parts()
            months = value.month - 1 + int(amount) * (12 if field == 1 else 1)
            year = value.year + months // 12
            import calendar as _calendar
            day = min(value.day, _calendar.monthrange(year, months % 12 + 1)[1])
            self._millis = int(value.replace(year=year, month=months % 12 + 1, day=day).timestamp() * 1000)

    def getTimeInMillis(self):
        return self._millis

    def setTimeInMillis(self, millis):
        self._millis = int(millis)

    def getTime(self):
        return Date(self._millis)

    def setTime(self, date):
        self._millis = int(date.getTime() if hasattr(date, "getTime") else date)


class Random(JavaObject):
    _java_name = "java.util.Random"

    def __init__(self, seed=None):
        self._random = _random.Random(seed)

    def nextInt(self, bound=None):
        if bound is None:
            return self._random.randint(-2147483648, 2147483647)
        return self._random.randrange(int(bound))

    def nextLong(self):
        return self._random.randint(-(1 << 63), (1 << 63) - 1)

    def nextBoolean(self):
        return self._random.random() < 0.5

    def nextFloat(self):
        return self._random.random()

    def nextDouble(self):
        return self._random.random()


class UUID(JavaObject):
    _java_name = "java.util.UUID"

    def __init__(self, value=None):
        self._value = value or _uuid.uuid4()

    @staticmethod
    def randomUUID():
        return UUID()

    @staticmethod
    def fromString(text):
        return UUID(_uuid.UUID(str(text)))

    def toString(self):
        return str(self._value)

    __str__ = toString


class Collections(JavaObject):
    _java_name = "java.util.Collections"

    @staticmethod
    def emptyList():
        return ArrayList()

    @staticmethod
    def emptyMap():
        return HashMap()

    @staticmethod
    def singletonList(value):
        return ArrayList([value])

    @staticmethod
    def sort(values, comparator=None):
        if comparator is None:
            values.sort()
        else:
            import functools
            compare = getattr(comparator, "compare", comparator)
            values.sort(key=functools.cmp_to_key(compare))

    @staticmethod
    def reverse(values):
        values.reverse()

    @staticmethod
    def shuffle(values):
        _random.shuffle(values)

    @staticmethod
    def unmodifiableList(values):
        return ArrayList(values)

    @staticmethod
    def synchronizedList(values):
        return values

    @staticmethod
    def synchronizedMap(values):
        return values


class Arrays(JavaObject):
    _java_name = "java.util.Arrays"

    @staticmethod
    def asList(*values):
        if len(values) == 1 and isinstance(values[0], (list, tuple)):
            return ArrayList(list(values[0]))
        return ArrayList(list(values))

    @staticmethod
    def toString(values):
        return "[" + ", ".join(str(v) for v in values) + "]"

    @staticmethod
    def fill(values, value):
        for i in range(len(values)):
            values[i] = value

    @staticmethod
    def copyOf(values, length):
        result = list(values[:length])
        result += [0] * (length - len(result))
        return result

    @staticmethod
    def sort(values):
        values.sort()


# IO

class File(JavaObject):
    _java_name = "java.io.File"
    separator = os.sep
    pathSeparator = os.pathsep

    def __init__(self, parent, child=None):
        base = parent.getPath() if isinstance(parent, File) else os.fspath(parent) if parent is not None else ""
        self._path = os.path.join(base, str(child)) if child is not None else str(base)

    def __fspath__(self):
        return self._path

    def getPath(self):
        return self._path

    def getAbsolutePath(self):
        return os.path.abspath(self._path)

    def getCanonicalPath(self):
        return os.path.realpath(self._path)

    def getAbsoluteFile(self):
        return File(self.getAbsolutePath())

    def getName(self):
        return os.path.basename(self._path)

    def getParent(self):
        return os.path.dirname(self._path)

    def getParentFile(self):
        return File(os.path.dirname(self._path))

    def exists(self):
        return os.path.exists(self._path)

    def isFile(self):
        return os.path.isfile(self._path)

    def isDirectory(self):
        return os.path.isdir(self._path)

    def canRead(self):
        return os.access(self._path, os.R_OK)

    def canWrite(self):
        return os.access(self._path, os.W_OK)

    def length(self):
        try:
            return os.path.getsize(self._path)
        except OSError:
            return 0

    def lastModified(self):
        try:
            return int(os.path.getmtime(self._path) * 1000)
        except OSError:
            return 0

    def mkdirs(self):
        try:
            os.makedirs(self._path, exist_ok=True)
            return True
        except OSError:
            return False

    def mkdir(self):
        try:
            os.mkdir(self._path)
            return True
        except OSError:
            return False

    def delete(self):
        try:
            if os.path.isdir(self._path):
                os.rmdir(self._path)
            else:
                os.remove(self._path)
            return True
        except OSError:
            return False

    def createNewFile(self):
        if os.path.exists(self._path):
            return False
        with open(self._path, "w"):
            pass
        return True

    def renameTo(self, other):
        try:
            os.replace(self._path, os.fspath(other))
            return True
        except OSError:
            return False

    def list(self):
        try:
            return os.listdir(self._path)
        except OSError:
            return None

    def listFiles(self, *args):
        names = self.list()
        if names is None:
            return None
        return [File(self._path, name) for name in names]

    def toURI(self):
        return "file:///" + self.getAbsolutePath().replace(os.sep, "/")

    def toString(self):
        return self._path

    __str__ = toString

    def __eq__(self, other):
        return isinstance(other, File) and os.path.normcase(self.getAbsolutePath()) == os.path.normcase(other.getAbsolutePath())

    def __hash__(self):
        return hash(os.path.normcase(self.getAbsolutePath()))


# Nio

class ByteOrder(JavaObject):
    _java_name = "java.nio.ByteOrder"

    def __init__(self, name):
        self._name = name

    def toString(self):
        return self._name

    @staticmethod
    def nativeOrder():
        return ByteOrder.LITTLE_ENDIAN


ByteOrder.BIG_ENDIAN = ByteOrder("BIG_ENDIAN")
ByteOrder.LITTLE_ENDIAN = ByteOrder("LITTLE_ENDIAN")


class ByteBuffer(JavaObject):
    _java_name = "java.nio.ByteBuffer"

    def __init__(self, data):
        self._data = bytearray(data)
        self._position = 0
        self._limit = len(self._data)
        self._order = ">"

    @staticmethod
    def wrap(data, offset=0, length=None):
        buffer = ByteBuffer(bytes(data))
        buffer._position = int(offset)
        if length is not None:
            buffer._limit = int(offset) + int(length)
        return buffer

    @staticmethod
    def allocate(size):
        return ByteBuffer(bytes(int(size)))

    allocateDirect = allocate

    def order(self, value=None):
        if value is None:
            return ByteOrder.BIG_ENDIAN if self._order == ">" else ByteOrder.LITTLE_ENDIAN
        self._order = "<" if value is ByteOrder.LITTLE_ENDIAN else ">"
        return self

    def _read(self, fmt, index=None):
        size = struct.calcsize(fmt)
        start = self._position if index is None else int(index)
        value = struct.unpack_from(self._order + fmt, self._data, start)[0]
        if index is None:
            self._position += size
        return value

    def _write(self, fmt, value, index=None):
        size = struct.calcsize(fmt)
        start = self._position if index is None else int(index)
        struct.pack_into(self._order + fmt, self._data, start, value)
        if index is None:
            self._position += size
        return self

    def get(self, index=None):
        if isinstance(index, (bytearray, list)):
            for i in range(len(index)):
                index[i] = self._read("b")
            return self
        return self._read("b", index)

    def getInt(self, index=None):
        return self._read("i", index)

    def getLong(self, index=None):
        return self._read("q", index)

    def getShort(self, index=None):
        return self._read("h", index)

    def getFloat(self, index=None):
        return self._read("f", index)

    def getDouble(self, index=None):
        return self._read("d", index)

    def put(self, value, *args):
        if isinstance(value, (bytes, bytearray, list)):
            data = bytes(bytearray(v & 0xFF for v in value)) if isinstance(value, list) else bytes(value)
            self._data[self._position:self._position + len(data)] = data
            self._position += len(data)
            return self
        return self._write("b", ((int(value) + 128) % 256) - 128)

    def putInt(self, value, *args):
        return self._write("i", int(value))

    def putLong(self, value, *args):
        return self._write("q", int(value))

    def putShort(self, value, *args):
        return self._write("h", int(value))

    def putFloat(self, value, *args):
        return self._write("f", float(value))

    def putDouble(self, value, *args):
        return self._write("d", float(value))

    def array(self):
        return bytes(self._data)

    def position(self, value=None):
        if value is None:
            return self._position
        self._position = int(value)
        return self

    def limit(self, value=None):
        if value is None:
            return self._limit
        self._limit = int(value)
        return self

    def capacity(self):
        return len(self._data)

    def remaining(self):
        return self._limit - self._position

    def hasRemaining(self):
        return self._position < self._limit

    def flip(self):
        self._limit = self._position
        self._position = 0
        return self

    def rewind(self):
        self._position = 0
        return self

    def clear(self):
        self._position = 0
        self._limit = len(self._data)
        return self


# Android

class Color(JavaObject):
    _java_name = "android.graphics.Color"
    BLACK = -16777216
    DKGRAY = -12303292
    GRAY = -7829368
    LTGRAY = -3355444
    WHITE = -1
    RED = -65536
    GREEN = -16711936
    BLUE = -16776961
    YELLOW = -256
    CYAN = -16711681
    MAGENTA = -65281
    TRANSPARENT = 0
    _names = {"black": BLACK, "white": WHITE, "red": RED, "green": GREEN, "blue": BLUE,
              "yellow": YELLOW, "cyan": CYAN, "magenta": MAGENTA, "gray": GRAY, "grey": GRAY,
              "darkgray": DKGRAY, "lightgray": LTGRAY, "transparent": TRANSPARENT}

    @staticmethod
    def _signed(value):
        value &= 0xFFFFFFFF
        return value - (1 << 32) if value >= (1 << 31) else value

    @staticmethod
    def argb(a, r, g, b):
        if isinstance(a, float):
            a, r, g, b = (int(round(x * 255)) for x in (a, r, g, b))
        return Color._signed((int(a) & 255) << 24 | (int(r) & 255) << 16 | (int(g) & 255) << 8 | (int(b) & 255))

    @staticmethod
    def rgb(r, g, b):
        return Color.argb(255, r, g, b)

    @staticmethod
    def alpha(color):
        return (int(color) >> 24) & 255

    @staticmethod
    def red(color):
        return (int(color) >> 16) & 255

    @staticmethod
    def green(color):
        return (int(color) >> 8) & 255

    @staticmethod
    def blue(color):
        return int(color) & 255

    @staticmethod
    def parseColor(text):
        value = str(text).strip()
        if value.startswith("#"):
            digits = value[1:]
            if len(digits) == 6:
                return Color._signed(0xFF000000 | int(digits, 16))
            if len(digits) == 8:
                return Color._signed(int(digits, 16))
            if len(digits) == 3:
                r, g, b = (int(c * 2, 16) for c in digits)
                return Color.rgb(r, g, b)
            raise ValueError("Unknown color")
        lowered = value.lower()
        if lowered in Color._names:
            return Color._names[lowered]
        raise ValueError("Unknown color")

    @staticmethod
    def valueOf(color):
        return int(color)

    @staticmethod
    def luminance(color):
        r, g, b = Color.red(color) / 255, Color.green(color) / 255, Color.blue(color) / 255
        return 0.2126 * r + 0.7152 * g + 0.0722 * b

    @staticmethod
    def colorToHSV(color, hsv):
        import colorsys
        h, s, v = colorsys.rgb_to_hsv(Color.red(color) / 255, Color.green(color) / 255, Color.blue(color) / 255)
        hsv[0], hsv[1], hsv[2] = h * 360, s, v

    @staticmethod
    def HSVToColor(*args):
        import colorsys
        alpha, hsv = (255, args[0]) if len(args) == 1 else (args[0], args[1])
        r, g, b = colorsys.hsv_to_rgb(hsv[0] / 360, hsv[1], hsv[2])
        return Color.argb(alpha, int(r * 255), int(g * 255), int(b * 255))


class _BuildVersion(JavaObject):
    _java_name = "android.os.Build$VERSION"
    SDK_INT = 34
    RELEASE = "14"
    CODENAME = "REL"
    INCREMENTAL = "exdesktop"


class _BuildCodes(JavaObject):
    _java_name = "android.os.Build$VERSION_CODES"
    LOLLIPOP = 21
    M = 23
    N = 24
    O = 26
    P = 28
    Q = 29
    R = 30
    S = 31
    TIRAMISU = 33
    UPSIDE_DOWN_CAKE = 34


class Build(JavaObject):
    _java_name = "android.os.Build"
    VERSION = _BuildVersion
    VERSION_CODES = _BuildCodes
    MODEL = "exDesktop"
    MANUFACTURER = "exDesktop"
    BRAND = "exDesktop"
    DEVICE = "desktop"
    PRODUCT = "exDesktop"
    HARDWARE = "desktop"
    BOARD = "desktop"
    FINGERPRINT = "exDesktop/desktop"
    SUPPORTED_ABIS = ["x86_64"]


class Looper(JavaObject):
    _java_name = "android.os.Looper"
    _main = None

    @staticmethod
    def getMainLooper():
        if Looper._main is None:
            Looper._main = Looper()
        return Looper._main

    @staticmethod
    def myLooper():
        if threading.current_thread() is threading.main_thread():
            return Looper.getMainLooper()
        return None

    @staticmethod
    def prepare():
        return None

    def getThread(self):
        return _CurrentThread()


class Handler(JavaObject):
    _java_name = "android.os.Handler"

    def __init__(self, looper=None, callback=None):
        self._callback = callback

    def post(self, runnable):
        return self.postDelayed(runnable, 0)

    def postDelayed(self, runnable, delay=0):
        import android_utils
        run = getattr(runnable, "run", runnable)
        android_utils.run_on_ui_thread(run, int(delay or 0))
        return True

    def removeCallbacks(self, *args):
        return None

    def removeCallbacksAndMessages(self, *args):
        return None

    def sendEmptyMessage(self, *args):
        return True


class Bundle(HashMap):
    _java_name = "android.os.Bundle"

    def __getattr__(self, item):
        if item.startswith("put"):
            return lambda key, value=None: self.put(key, value)
        if item.startswith("get"):
            return lambda key, default=None: dict.get(self, key, default)
        raise AttributeError(item)


class SystemClock(JavaObject):
    _java_name = "android.os.SystemClock"

    @staticmethod
    def elapsedRealtime():
        return int(time.monotonic() * 1000)

    @staticmethod
    def uptimeMillis():
        return int(time.monotonic() * 1000)

    @staticmethod
    def sleep(millis):
        time.sleep(millis / 1000)


class TypedValue(JavaObject):
    _java_name = "android.util.TypedValue"
    COMPLEX_UNIT_PX = 0
    COMPLEX_UNIT_DIP = 1
    COMPLEX_UNIT_SP = 2
    COMPLEX_UNIT_PT = 3
    COMPLEX_UNIT_IN = 4
    COMPLEX_UNIT_MM = 5

    @staticmethod
    def applyDimension(unit, value, metrics=None):
        try:
            import android_views
            density = float(android_views.density())
        except Exception:
            density = 1.0
        if unit in (1, 2):
            return float(value) * density
        if unit == 3:
            return float(value) * density * 160 / 72
        if unit == 4:
            return float(value) * density * 160
        if unit == 5:
            return float(value) * density * 160 / 25.4
        return float(value)


class MotionEvent(JavaObject):
    _java_name = "android.view.MotionEvent"
    ACTION_DOWN = 0
    ACTION_UP = 1
    ACTION_MOVE = 2
    ACTION_CANCEL = 3
    ACTION_OUTSIDE = 4
    ACTION_POINTER_DOWN = 5
    ACTION_POINTER_UP = 6
    ACTION_HOVER_MOVE = 7
    ACTION_SCROLL = 8
    ACTION_HOVER_ENTER = 9
    ACTION_HOVER_EXIT = 10
    ACTION_MASK = 255


class Log(JavaObject):
    _java_name = "android.util.Log"

    @staticmethod
    def _write(level, tag, message, *args):
        _log("%s/%s: %s" % (level, tag, message))
        return 0

    @staticmethod
    def d(tag, message="", *args):
        return Log._write("D", tag, message)

    @staticmethod
    def i(tag, message="", *args):
        return Log._write("I", tag, message)

    @staticmethod
    def w(tag, message="", *args):
        return Log._write("W", tag, message)

    @staticmethod
    def e(tag, message="", *args):
        return Log._write("E", tag, message)

    @staticmethod
    def v(tag, message="", *args):
        return Log._write("V", tag, message)


class Base64(JavaObject):
    _java_name = "android.util.Base64"
    DEFAULT = 0
    NO_PADDING = 1
    NO_WRAP = 2
    CRLF = 4
    URL_SAFE = 8
    NO_CLOSE = 16

    @staticmethod
    def encodeToString(data, flags=0, *args):
        raw = bytes(data) if not isinstance(data, str) else data.encode()
        text = (_b64.urlsafe_b64encode if flags & 8 else _b64.b64encode)(raw).decode()
        if flags & 1:
            text = text.rstrip("=")
        if not flags & 2:
            text = "\n".join(text[i:i + 76] for i in range(0, len(text), 76)) + "\n"
        return text

    @staticmethod
    def encode(data, flags=0):
        return Base64.encodeToString(data, flags).encode()

    @staticmethod
    def decode(data, flags=0):
        text = data.decode() if isinstance(data, (bytes, bytearray)) else str(data)
        text = "".join(text.split())
        text += "=" * (-len(text) % 4)
        return (_b64.urlsafe_b64decode if flags & 8 or "-" in text or "_" in text else _b64.b64decode)(text)


class TextUtils(JavaObject):
    _java_name = "android.text.TextUtils"

    @staticmethod
    def isEmpty(value):
        return value is None or len(str(value)) == 0

    @staticmethod
    def join(separator, values):
        return str(separator).join(str(v) for v in values)

    @staticmethod
    def equals(a, b):
        return a == b

    @staticmethod
    def htmlEncode(value):
        import html
        return html.escape(str(value))


# Telegram

class UserObject(JavaObject):
    _java_name = "org.telegram.messenger.UserObject"

    @staticmethod
    def _get(user, name, default=None):
        if user is None:
            return default
        if isinstance(user, dict):
            return user.get(name, default)
        return getattr(user, name, default)

    @staticmethod
    def getUserName(user):
        first = UserObject._get(user, "first_name", "") or ""
        last = UserObject._get(user, "last_name", "") or ""
        return (str(first) + (" " + str(last) if last else "")).strip() or "Deleted Account"

    @staticmethod
    def getFirstName(user, *args):
        return str(UserObject._get(user, "first_name", "") or UserObject.getUserName(user))

    @staticmethod
    def getPublicUsername(user, *args):
        return UserObject._get(user, "username")

    @staticmethod
    def isUserSelf(user):
        import tgnet_bridge
        return int(UserObject._get(user, "id", 0) or 0) == tgnet_bridge.self_id()

    @staticmethod
    def isDeleted(user):
        return bool(UserObject._get(user, "deleted", False))

    @staticmethod
    def isBot(user):
        return bool(UserObject._get(user, "bot", False))

    @staticmethod
    def isPremium(user):
        return bool(UserObject._get(user, "premium", False))

    @staticmethod
    def getEmojiStatusDocumentId(user):
        status = UserObject._get(user, "emoji_status")
        return int(getattr(status, "document_id", 0) or 0) if status is not None else 0


class ChatObject(JavaObject):
    _java_name = "org.telegram.messenger.ChatObject"

    @staticmethod
    def _get(chat, name, default=None):
        if chat is None:
            return default
        if isinstance(chat, dict):
            return chat.get(name, default)
        return getattr(chat, name, default)

    @staticmethod
    def isChannel(chat, *args):
        return bool(ChatObject._get(chat, "broadcast", False) or ChatObject._get(chat, "megagroup", False)
                    or type(chat).__name__.startswith("TL_channel"))

    @staticmethod
    def isChannelAndNotMegaGroup(chat, *args):
        return bool(ChatObject._get(chat, "broadcast", False))

    @staticmethod
    def isMegagroup(chat, *args):
        return bool(ChatObject._get(chat, "megagroup", False))

    @staticmethod
    def isForum(chat, *args):
        return bool(ChatObject._get(chat, "forum", False))

    @staticmethod
    def canSendMessages(chat):
        return True

    @staticmethod
    def hasAdminRights(chat):
        return bool(ChatObject._get(chat, "admin_rights") or ChatObject._get(chat, "creator", False))

    @staticmethod
    def getPublicUsername(chat, *args):
        return ChatObject._get(chat, "username")


class BuildVars(JavaObject):
    _java_name = "org.telegram.messenger.BuildVars"
    DEBUG_VERSION = False
    DEBUG_PRIVATE_VERSION = False
    LOGS_ENABLED = False
    BUILD_VERSION = 0
    BUILD_VERSION_STRING = "12.5.1"
    APP_ID = 0
    APP_HASH = ""
    isStandaloneApp = staticmethod(lambda: True)
    isBetaApp = staticmethod(lambda: False)
    isHuaweiStoreApp = staticmethod(lambda: False)


class FileLog(JavaObject):
    _java_name = "org.telegram.messenger.FileLog"

    @staticmethod
    def d(message):
        _log("FileLog: %s" % message)

    @staticmethod
    def e(message, *args):
        _log("FileLog error: %s" % message)

    w = d


class Utilities(JavaObject):
    _java_name = "org.telegram.messenger.Utilities"
    random = Random()

    @staticmethod
    def parseInt(value):
        import re
        match = re.search(r"-?\d+", str(value or ""))
        return int(match.group(0)) if match else 0

    @staticmethod
    def parseLong(value):
        return Utilities.parseInt(value)

    @staticmethod
    def MD5(text):
        import hashlib
        return hashlib.md5(str(text).encode()).hexdigest()

    @staticmethod
    def bytesToHex(data):
        return bytes(data).hex()

    @staticmethod
    def hexToBytes(text):
        return bytes.fromhex(str(text))

    @staticmethod
    def clamp(value, top, bottom):
        return max(bottom, min(top, value))


class _LocaleInfo(JavaObject):
    def __init__(self, code):
        self.shortName = code
        self.pluralLangCode = code

    def getLangCode(self):
        return self.shortName

    def getKey(self):
        return self.shortName


class _LocaleControllerInstance(JavaObject):
    def getCurrentLocale(self):
        return Locale(language())

    def getCurrentLocaleInfo(self):
        return _LocaleInfo(language())

    def getLanguageOverride(self):
        return language()

    def __getattr__(self, item):
        if item.startswith("__"):
            raise AttributeError(item)
        import _compat
        return _compat.Stub("LocaleController.getInstance." + item)


class LocaleController(JavaObject):
    _java_name = "org.telegram.messenger.LocaleController"
    isRTL = False
    _instance = None

    @staticmethod
    def getInstance():
        if LocaleController._instance is None:
            LocaleController._instance = _LocaleControllerInstance()
        return LocaleController._instance

    @staticmethod
    def getString(*args):
        for value in args:
            if isinstance(value, str):
                return value
        return ""

    @staticmethod
    def formatString(*args):
        texts = [a for a in args if isinstance(a, str)]
        if not texts:
            return ""
        pattern = texts[-1] if len(texts) > 1 else texts[0]
        rest = args[args.index(pattern) + 1:]
        try:
            return String.format(pattern, *rest)
        except Exception:
            return pattern

    @staticmethod
    def getPluralString(key, count):
        return str(key)

    @staticmethod
    def formatPluralString(key, count, *args):
        return "%s %s" % (count, key)

    @staticmethod
    def getCurrentLanguageName():
        return language()


def _log(message):
    try:
        import extera_host
        extera_host.log(message)
    except Exception:
        pass


# Messages

class _PeerRef(JavaObject):
    _java_name = "org.telegram.tgnet.TLRPC$Peer"

    def __init__(self, dialog_id):
        dialog_id = int(dialog_id or 0)
        self.user_id = dialog_id if dialog_id > 0 else 0
        self.chat_id = -dialog_id if dialog_id < 0 else 0
        self.channel_id = -dialog_id if dialog_id < 0 else 0


class _MessageOwner(JavaObject):
    _java_name = "org.telegram.tgnet.TLRPC$TL_message"

    def __init__(self, data):
        self.id = int(data.get("id") or 0)
        self.message = str(data.get("text") or "")
        self.date = int(data.get("date") or 0)
        self.out = bool(data.get("out"))
        self.dialog_id = int(data.get("dialog_id") or 0)
        self.peer_id = _PeerRef(self.dialog_id)
        self.from_id = _PeerRef(int(data.get("sender_id") or 0))
        self.entities = ArrayList()
        self.media = None
        self.reply_to = None
        self.fwd_from = None
        self.flags = 0


class MessageObject(JavaObject):
    _java_name = "org.telegram.messenger.MessageObject"

    def __init__(self, account=0, owner=None, *args):
        if isinstance(owner, dict):
            owner = _MessageOwner(owner)
        self.currentAccount = int(account or 0) if isinstance(account, int) else 0
        self.messageOwner = owner if owner is not None else _MessageOwner({})
        self.messageText = String(getattr(self.messageOwner, "message", "") or "")
        self.caption = None
        self.type = 0

    def getId(self):
        return self.messageOwner.id

    def getDialogId(self):
        return self.messageOwner.dialog_id

    def isOut(self):
        return bool(self.messageOwner.out)

    def isOutOwner(self):
        return bool(self.messageOwner.out)

    def getFromChatId(self):
        peer = self.messageOwner.from_id
        return peer.user_id or -(peer.chat_id or 0)

    def getSenderId(self):
        return self.getFromChatId()

    def getChatId(self):
        return self.messageOwner.peer_id.chat_id

    def isFromUser(self):
        return self.messageOwner.from_id.user_id > 0

    def isSent(self):
        return True

    def isUnread(self):
        return not self.messageOwner.out

    def getMessageText(self):
        return self.messageText

    def isMediaEmpty(self):
        return True

    def hasValidReplyMessageObject(self):
        return False


def message_object(data, account=0):
    return MessageObject(account, _MessageOwner(data or {}))


# Registry

_CLASSES = {}


def _register(*classes):
    for cls in classes:
        name = getattr(cls, "_java_name", None)
        if name:
            _CLASSES[name] = cls


_register(Object, Runnable, Integer, Long, Short, Byte, Float, Double, Boolean, Character, String,
          StringBuilder, System, Thread, Math, Class, ArrayList, HashMap, HashSet, Locale, Date,
          Random, UUID, Collections, Arrays, File, Calendar, ByteOrder, ByteBuffer, Color, _BuildVersion,
          _BuildCodes, Build, Looper, Handler, Bundle, SystemClock, TypedValue, MotionEvent, Log,
          Base64, TextUtils, UserObject, ChatObject, BuildVars, FileLog, Utilities, LocaleController,
          MessageObject)
for _alias, _cls in (("java.util.LinkedList", ArrayList), ("java.util.List", ArrayList),
                     ("java.util.Vector", ArrayList), ("java.util.LinkedHashMap", HashMap),
                     ("java.util.TreeMap", HashMap), ("java.util.Map", HashMap),
                     ("java.util.concurrent.ConcurrentHashMap", HashMap), ("java.util.Hashtable", HashMap),
                     ("java.util.LinkedHashSet", HashSet), ("java.util.TreeSet", HashSet),
                     ("java.util.Set", HashSet), ("java.util.concurrent.CopyOnWriteArrayList", ArrayList),
                     ("java.lang.Number", Integer), ("java.lang.CharSequence", String),
                     ("java.util.GregorianCalendar", Calendar)):
    _CLASSES[_alias] = _cls


class ShimMethod:
    def __init__(self, cls, name):
        self._cls = cls
        self._method = name
        self._java_name = "%s.%s" % (getattr(cls, "_java_name", cls.__name__), name)

    def getName(self, *args):
        return self._method

    def getDeclaringClass(self, *args):
        return self._cls

    def setAccessible(self, *args):
        return None

    def getParameterTypes(self, *args):
        return []

    def getParameterCount(self, *args):
        return 0

    def getReturnType(self, *args):
        return None

    def invoke(self, target=None, *args):
        owner = target if target is not None else self._cls
        return getattr(owner, self._method)(*args)

    def __repr__(self):
        return "<method %s>" % self._java_name


_SHIM_HOOKS = {}
_SHIM_ORIGINALS = {}
_SHIM_COUNTER = [0]


def shim_method(cls, name):
    if not isinstance(cls, type) or not name or name.startswith("_"):
        return None
    import inspect
    try:
        raw = inspect.getattr_static(cls, name)
    except AttributeError:
        return None
    if isinstance(raw, (staticmethod, classmethod)):
        raw = raw.__func__
    if not callable(raw) or getattr(raw, "_exd_default", False):
        return None
    return ShimMethod(cls, name)


def _shim_dispatch(cls, name):
    import inspect
    raw = inspect.getattr_static(cls, name)
    kind = "static" if isinstance(raw, staticmethod) else "class" if isinstance(raw, classmethod) else "method"
    original = raw.__func__ if kind != "method" else raw
    _SHIM_ORIGINALS[(cls, name)] = raw
    method = ShimMethod(cls, name)

    def call(*args, **kwargs):
        import base_plugin
        import extera_host
        if kind == "method":
            this, params = (args[0] if args else None), list(args[1:])
        else:
            this, params = None, list(args)
        entries = sorted(_SHIM_HOOKS.get((cls, name), ()), key=lambda e: -e["priority"])
        param = base_plugin.VirtualParam(params, this, method)
        for entry in entries:
            before = getattr(entry["handler"], "before_hooked_method", None)
            replace = getattr(entry["handler"], "replace_hooked_method", None)
            try:
                with extera_host._plugin_context(entry["plugin"]):
                    if callable(replace):
                        param.setResult(replace(param))
                    elif callable(before):
                        before(param)
            except Exception as e:
                extera_host.log("[%s] %s hook failed: %r" % (entry["plugin"], method._java_name, e))
            if param.returnEarly:
                break
        if not param.returnEarly:
            if kind == "method":
                param._result = original(this, *param.args, **kwargs)
            elif kind == "class":
                param._result = original(cls, *param.args, **kwargs)
            else:
                param._result = original(*param.args, **kwargs)
        for entry in entries:
            after = getattr(entry["handler"], "after_hooked_method", None)
            if callable(after):
                try:
                    with extera_host._plugin_context(entry["plugin"]):
                        after(param)
                except Exception as e:
                    extera_host.log("[%s] %s hook failed: %r" % (entry["plugin"], method._java_name, e))
        if isinstance(param._throwable, BaseException):
            raise param._throwable
        return param._result

    call._exd_shim = True
    if kind == "static":
        setattr(cls, name, staticmethod(call))
    elif kind == "class":
        setattr(cls, name, staticmethod(call))
    else:
        setattr(cls, name, call)


def hook_shim(cls, name, plugin_id, handler, priority=50):
    if shim_method(cls, name) is None and (cls, name) not in _SHIM_ORIGINALS:
        return None
    key = (cls, name)
    if key not in _SHIM_ORIGINALS:
        _shim_dispatch(cls, name)
    _SHIM_COUNTER[0] += 1
    hook_id = "exd_shim_%d" % _SHIM_COUNTER[0]
    _SHIM_HOOKS.setdefault(key, []).append({
        "id": hook_id, "plugin": plugin_id, "handler": handler, "priority": int(priority or 0)})
    return hook_id


def has_shim_hooks(cls, name):
    return bool(_SHIM_HOOKS.get((cls, name)))


def _restore(key):
    if not _SHIM_HOOKS.get(key) and key in _SHIM_ORIGINALS:
        _SHIM_HOOKS.pop(key, None)
        try:
            setattr(key[0], key[1], _SHIM_ORIGINALS.pop(key))
        except Exception:
            pass


def unhook_shim(hook_id=None, plugin_id=None):
    for key in list(_SHIM_HOOKS):
        _SHIM_HOOKS[key] = [e for e in _SHIM_HOOKS[key]
                            if not ((hook_id and e["id"] == hook_id) or (plugin_id and e["plugin"] == plugin_id))]
        _restore(key)


def _ensure_reflection(cls, name):
    if cls.__dict__.get("_exd_reflection"):
        return
    try:
        if issubclass(cls, JavaObject):
            return
    except TypeError:
        return
    simple = name.split(".")[-1].split("$")[-1]
    defaults = {
        "getDeclaredMethods": lambda *a: [],
        "getMethods": lambda *a: [],
        "getDeclaredFields": lambda *a: [],
        "getFields": lambda *a: [],
        "getDeclaredConstructors": lambda *a: [],
        "getInterfaces": lambda *a: [],
        "getDeclaredMethod": lambda method="", *a: shim_method(cls, method) or _missing("%s.%s" % (name, method)),
        "getMethod": lambda method="", *a: shim_method(cls, method) or _missing("%s.%s" % (name, method)),
        "getDeclaredField": lambda field="", *a: _missing("%s.%s" % (name, field)),
        "getField": lambda field="", *a: _missing("%s.%s" % (name, field)),
        "getDeclaredConstructor": lambda *a: _missing("%s.<init>" % name),
        "getName": lambda *a: name,
        "getSimpleName": lambda *a: simple,
        "getCanonicalName": lambda *a: name.replace("$", "."),
        "getSuperclass": lambda *a: None,
        "getClassLoader": lambda *a: _missing("ClassLoader"),
        "isInstance": lambda value=None, *a: isinstance(value, cls),
    }
    for value in defaults.values():
        try:
            value._exd_default = True
        except Exception:
            pass
    for key, value in defaults.items():
        named = key in ("getName", "getSimpleName", "getCanonicalName", "getDeclaredMethod", "getMethod",
                        "getDeclaredField", "getField", "getDeclaredConstructor", "isInstance")
        inherited = any(key in base.__dict__ and not base.__dict__.get("_exd_reflection")
                        for base in cls.__mro__[1:] if base is not object)
        if key not in cls.__dict__ and (named or not inherited):
            try:
                setattr(cls, key, staticmethod(value))
            except Exception:
                pass
    if not hasattr(cls, "_java_name"):
        try:
            cls._java_name = name
        except Exception:
            pass
    try:
        cls._exd_reflection = True
    except Exception:
        pass


def lookup(name):
    name = str(name)
    cls = _CLASSES.get(name)
    if cls is not None:
        return cls
    try:
        import _compat
        module, _, attr = name.rpartition(".")
        outer, _, inner = attr.partition("$")
        value = _compat.SHIMS.get(module, {}).get(attr)
        if value is None:
            value = _compat.SHIMS.get(module, {}).get(outer)
            if value is not None and inner:
                value = getattr(value, inner, None)
        if isinstance(value, type):
            _ensure_reflection(value, name)
            return value
        if value is not None:
            return value
    except Exception:
        pass
    return None


def install(shims):
    import tgnet_bridge
    tgnet_bridge.ChatActivity.getClass = hybridmethod(
        lambda target: tl_runtime._ClassInfo("ChatActivity"))
    for _fragment in ("ProfileActivity", "DialogsActivity"):
        getattr(tgnet_bridge, _fragment).getClass = hybridmethod(
            (lambda name: lambda target: tl_runtime._ClassInfo(name))(_fragment))
    original = tl_runtime.TLObject.getClass

    def tl_get_class(target):
        if isinstance(target, type):
            return tl_runtime._ClassInfo(target.__name__)
        return original(target)

    tl_runtime.TLObject.getClass = hybridmethod(tl_get_class)

    def class_info_getattr(self, item):
        if item.startswith("__") and item.endswith("__"):
            raise AttributeError(item)
        import _compat
        return _compat.Missing("class." + item)

    if not hasattr(tl_runtime._ClassInfo, "__getattr__"):
        tl_runtime._ClassInfo.__getattr__ = class_info_getattr
    for name, cls in _CLASSES.items():
        if "$" in name:
            module, _, attr = name.rpartition(".")
            shims.setdefault(module, {})[attr] = cls
            continue
        module, _, attr = name.rpartition(".")
        current = shims.setdefault(module, {})
        if attr not in current or attr in ("LocaleController", "ArrayList"):
            current[attr] = cls
