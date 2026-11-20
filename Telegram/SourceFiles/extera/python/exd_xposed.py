import inspect
import re
import threading
import weakref


def _host():
    import extera_host
    return extera_host


def _log(text):
    try:
        _host().log(text)
    except Exception:
        pass


# Param

class MethodHookParam:
    _java_name = "de.robv.android.xposed.XC_MethodHook$MethodHookParam"

    def __init__(self, method=None, thisObject=None, args=()):
        self.method = method
        self.thisObject = thisObject
        self.args = list(args or ())
        self.returnEarly = False
        self._result = None
        self._throwable = None
        self._extra = None

    def getResult(self):
        return self._result

    def setResult(self, value):
        self._result = value
        self._throwable = None
        self.returnEarly = True

    def getThrowable(self):
        return self._throwable

    def hasThrowable(self):
        return self._throwable is not None

    def setThrowable(self, throwable):
        self._throwable = throwable
        self._result = None
        self.returnEarly = True

    def getResultOrThrowable(self):
        if isinstance(self._throwable, BaseException):
            raise self._throwable
        return self._result

    def getExtra(self):
        if self._extra is None:
            self._extra = {}
        return self._extra

    def getObjectExtra(self, key):
        return self.getExtra().get(key)

    def setObjectExtra(self, key, value):
        self.getExtra()[key] = value

    def getArgs(self):
        return self.args

    def getThisObject(self):
        return self.thisObject

    def getMethod(self):
        return self.method

    def __repr__(self):
        return "<MethodHookParam %r>" % (self.method,)


# MVEL

class _Package:
    __slots__ = ("_path",)

    def __init__(self, path):
        object.__setattr__(self, "_path", path)

    def __getattr__(self, item):
        if item.startswith("__"):
            raise AttributeError(item)
        path = self._path + "." + item
        found = _resolve_class(path)
        if found is not None:
            return found
        return _Package(path)

    def __eq__(self, other):
        return isinstance(other, _Package) and other._path == self._path

    def __hash__(self):
        return hash(self._path)

    def __repr__(self):
        return "<package %s>" % self._path


def _resolve_class(path):
    try:
        import exd_java
        found = exd_java.lookup(path)
    except Exception:
        found = None
    if found is None:
        return None
    try:
        import _compat
        if isinstance(found, _compat.Missing):
            return None
    except Exception:
        pass
    return found


class _Scope(dict):
    def __init__(self, this, values):
        super().__init__(values)
        self._this = this

    def __missing__(self, key):
        if key in ("org", "java", "android", "androidx", "com", "de", "kotlin", "dalvik"):
            return _Package(key)
        this = self._this
        if this is not None:
            try:
                return getattr(this, key)
            except AttributeError:
                pass
        raise NameError(key)


_MVEL_CACHE = {}
_TOKEN = re.compile(r"""("(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*')""")


def _mvel_to_python(expression):
    parts = _TOKEN.split(expression)
    out = []
    for index, part in enumerate(parts):
        if index % 2:
            out.append(part)
            continue
        part = part.replace("&&", " and ").replace("||", " or ")
        part = re.sub(r"!(?!=)", " not ", part)
        part = re.sub(r"\bnull\b", "None", part)
        part = re.sub(r"\btrue\b", "True", part)
        part = re.sub(r"\bfalse\b", "False", part)
        part = re.sub(r"\bthis\b", "__this__", part)
        part = re.sub(r"(\S+)\s+instanceof\s+([\w.$]+)", r"__instanceof__(\1, \2)", part)
        part = re.sub(r"\bis\s+empty\b", "== __empty__", part)
        out.append(part)
    return "".join(out).strip()


class _Empty:
    def __eq__(self, other):
        if other is None:
            return True
        try:
            return len(other) == 0
        except Exception:
            pass
        size = getattr(other, "size", None) or getattr(other, "isEmpty", None)
        try:
            value = size()
            return value == 0 or value is True
        except Exception:
            return False

    __hash__ = object.__hash__


def _instanceof(value, cls):
    if value is None:
        return False
    check = getattr(cls, "isInstance", None)
    if callable(check):
        try:
            return bool(check(value))
        except Exception:
            pass
    try:
        return isinstance(value, cls)
    except TypeError:
        return False


def eval_condition(expression, param, result, obj):
    code = _MVEL_CACHE.get(expression)
    if code is None:
        try:
            code = compile(_mvel_to_python(expression), "<mvel>", "eval")
        except SyntaxError as e:
            _log("hook filter %r: %s" % (expression, e))
            code = False
        _MVEL_CACHE[expression] = code
    if code is False:
        return False
    this = getattr(param, "thisObject", None)
    scope = _Scope(this, {"param": param, "result": result, "object": obj, "__this__": this,
                          "__instanceof__": _instanceof, "__empty__": _Empty()})
    value = eval(code, {"__builtins__": {"len": len, "str": str, "int": int, "float": float,
                                         "bool": bool, "isinstance": isinstance}}, scope)
    return bool(value)


# HookFilter

class HookFilter:
    _java_name = "com.exteragram.messenger.plugins.hooks.HookFilter"

    def __init__(self, filter_type):
        self.filterType = str(filter_type)
        self.argIndex = None
        self.orFilters = None
        self.mvelExpression = None
        self.instanceOf = None
        self.object = None

    def getFilterType(self):
        return self.filterType

    def setArgIndex(self, value):
        self.argIndex = None if value is None else int(value)

    def getArgIndex(self):
        return self.argIndex

    def setOrFilters(self, value):
        self.orFilters = list(value) if value is not None else None

    def getOrFilters(self):
        return self.orFilters

    def setMvelExpression(self, value):
        self.mvelExpression = value

    def getMvelExpression(self):
        return self.mvelExpression

    def setInstanceOf(self, value):
        self.instanceOf = value

    def getInstanceOf(self):
        return self.instanceOf

    def setObject(self, value):
        self.object = value

    def getObject(self):
        return self.object

    def execute(self, param, is_before):
        kind = self.filterType
        try:
            if kind == "or":
                return any(f.execute(param, is_before) for f in (self.orFilters or ()))
            if kind == "condition":
                if not self.mvelExpression:
                    return False
                result = None if is_before else param.getResult()
                return eval_condition(self.mvelExpression, param, result, self.object)
            if kind.startswith("argument_"):
                args = list(getattr(param, "args", None) or ())
                index = self.argIndex
                if index is None or not (-len(args) <= index < len(args)) or index < 0:
                    return False
                return _compare(kind[len("argument_"):], args[index], self)
            if kind.startswith("result_"):
                if is_before:
                    return False
                return _compare(kind[len("result_"):], param.getResult(), self)
        except Exception as e:
            _log("hook filter %s failed: %r" % (kind, e))
        return False


def _values_equal(a, b):
    if a is b:
        return True
    try:
        if a == b:
            return True
    except Exception:
        pass
    if isinstance(a, (int, float)) and isinstance(b, (int, float)) \
            and not isinstance(a, bool) and not isinstance(b, bool):
        return float(a) == float(b)
    return False


def _compare(kind, value, flt):
    if kind == "equal":
        return _values_equal(value, flt.object)
    if kind == "not_equal":
        return not _values_equal(value, flt.object)
    if kind == "is_instance_of":
        return flt.instanceOf is not None and _instanceof(value, flt.instanceOf)
    if kind == "is_false":
        return isinstance(value, bool) and not value
    if kind == "is_true":
        return isinstance(value, bool) and value
    if kind == "is_null":
        return value is None
    if kind == "not_null":
        return value is not None
    return False


def to_java_filters(filters):
    out = []
    for item in filters or ():
        if item is None:
            continue
        if isinstance(item, HookFilter):
            out.append(item)
            continue
        data = getattr(item, "filter_data", None)
        if data is not None and not callable(data):
            item = data
        convert = getattr(item, "to_java_filter", None)
        if callable(convert):
            out.append(convert())
    return out


def run_filters(filters, param, is_before):
    for flt in filters:
        if not flt.execute(param, is_before):
            return False
    return True


# Handler

def _method_filters(handler, name):
    method = getattr(handler, name, None)
    for target in (method, getattr(method, "__func__", None)):
        values = getattr(target, "__hook_filters__", None) if target is not None else None
        if values:
            return to_java_filters(values)
    return []


class FilteredHandler:
    def __init__(self, handler, plugin_id=None, before_filters=None, after_filters=None):
        self.handler = handler
        self.plugin_id = plugin_id
        self.disabled = False
        self.before_filters = to_java_filters(before_filters) or _method_filters(handler, "before_hooked_method") \
            or _method_filters(handler, "beforeHookedMethod")
        self.after_filters = to_java_filters(after_filters) or _method_filters(handler, "after_hooked_method") \
            or _method_filters(handler, "afterHookedMethod")
        self.replace_filters = _method_filters(handler, "replace_hooked_method") or self.before_filters
        replace = _callback(handler, "replace_hooked_method", "replaceHookedMethod")
        before = _callback(handler, "before_hooked_method", "beforeHookedMethod")
        after = _callback(handler, "after_hooked_method", "afterHookedMethod")
        if replace is not None:
            self.replace_hooked_method = self._wrap(replace, self.replace_filters, True)
        else:
            if before is not None:
                self.before_hooked_method = self._wrap(before, self.before_filters, True)
            if after is not None:
                self.after_hooked_method = self._wrap(after, self.after_filters, False)

    def _wrap(self, callback, filters, is_before):
        def call(param):
            if self.disabled:
                return None if not is_before else _SKIP
            if filters and not run_filters(filters, param, is_before):
                return _SKIP
            return callback(param)
        call._exd_filtered = True
        return call

    def __getattr__(self, item):
        if item.startswith("__"):
            raise AttributeError(item)
        return getattr(self.handler, item)


class _Skip:
    pass


_SKIP = _Skip()


def _callback(handler, python_name, java_name):
    for name in (python_name, java_name):
        method = getattr(handler, name, None)
        if not callable(method):
            continue
        func = getattr(method, "__func__", None)
        if func is not None and getattr(func, "__qualname__", "").split(".")[0] in (
                "XC_MethodHook", "MethodHook", "XposedHook", "BaseHook", "XC_MethodReplacement"):
            continue
        return method
    return None


def wrap(handler, plugin_id=None, before_filters=None, after_filters=None):
    if isinstance(handler, FilteredHandler):
        return handler
    filters_before = before_filters
    filters_after = after_filters
    if filters_before is None:
        filters_before = getattr(handler, "_exd_before_filters", None) or getattr(handler, "before_filters", None)
    if filters_after is None:
        filters_after = getattr(handler, "_exd_after_filters", None) or getattr(handler, "after_filters", None)
    if callable(filters_before):
        filters_before = None
    if callable(filters_after):
        filters_after = None
    return FilteredHandler(handler, plugin_id, filters_before, filters_after)


# Runner

def _plugin_active(plugin_id):
    if not plugin_id:
        return True
    try:
        host = _host()
        record = host._records.get(plugin_id)
        return record is None or bool(getattr(record, "instance", None))
    except Exception:
        return True


def run(entries, param, original):
    host = _host()
    entries = sorted(entries, key=lambda e: -int(e.get("priority") or 0))
    done = []
    for entry in entries:
        plugin = entry.get("plugin")
        if not _plugin_active(plugin):
            continue
        handler = entry["handler"]
        replace = getattr(handler, "replace_hooked_method", None)
        before = getattr(handler, "before_hooked_method", None)
        done.append(entry)
        try:
            with host._plugin_context(plugin):
                if callable(replace):
                    value = replace(param)
                    if value is not _SKIP:
                        param.setResult(value)
                elif callable(before):
                    before(param)
        except Exception as e:
            if callable(replace):
                param.setThrowable(e)
            else:
                _log("[%s] %s before hook failed: %r" % (plugin, param.method, e))
        if param.returnEarly:
            break
    if not param.returnEarly:
        try:
            param._result = original(param)
        except Exception as e:
            param._throwable = e
    for entry in reversed(done):
        handler = entry["handler"]
        after = getattr(handler, "after_hooked_method", None)
        if not callable(after):
            continue
        saved = (param._result, param._throwable)
        try:
            with host._plugin_context(entry.get("plugin")):
                after(param)
        except Exception as e:
            param._result, param._throwable = saved
            _log("[%s] %s after hook failed: %r" % (entry.get("plugin"), param.method, e))
    if isinstance(param._throwable, BaseException):
        raise param._throwable
    return param._result


# Unhook

class Unhook(str):
    _java_name = "de.robv.android.xposed.XC_MethodHook$Unhook"

    def __new__(cls, hook_id, method=None, callback=None, owner=None):
        obj = super().__new__(cls, hook_id)
        obj._method = method
        obj._callback = callback
        obj._owner = owner
        return obj

    def getHookedMethod(self):
        return self._method

    def getCallback(self):
        return self._callback

    def unhook(self):
        owner = self._owner
        if owner is not None and hasattr(owner, "unhook_method"):
            owner.unhook_method(str(self))
            return
        _unhook_id(str(self))


def _unhook_id(hook_id):
    if hook_id.startswith("exd_shim_"):
        import exd_java
        exd_java.unhook_shim(hook_id=hook_id)
        return
    try:
        host = _host()
        for record in list(host._records.values()):
            instance = getattr(record, "instance", None)
            if instance is not None:
                instance.unhook_method(hook_id)
    except Exception:
        pass


# Xposed API

class XC_MethodHook:
    _java_name = "de.robv.android.xposed.XC_MethodHook"
    PRIORITY_DEFAULT = 50
    PRIORITY_LOWEST = -10000
    PRIORITY_HIGHEST = 10000
    MethodHookParam = MethodHookParam
    Unhook = Unhook

    def __init__(self, priority=50):
        self.priority = int(priority)

    def beforeHookedMethod(self, param):
        pass

    def afterHookedMethod(self, param):
        pass

    def getPriority(self):
        return getattr(self, "priority", 50)


class XC_MethodReplacement(XC_MethodHook):
    _java_name = "de.robv.android.xposed.XC_MethodReplacement"

    def replaceHookedMethod(self, param):
        return None

    def beforeHookedMethod(self, param):
        try:
            param.setResult(self.replaceHookedMethod(param))
        except Exception as e:
            param.setThrowable(e)

    @staticmethod
    def returnConstant(value, priority=50):
        replacement = XC_MethodReplacement(priority)
        replacement.replaceHookedMethod = lambda param: value
        return replacement


XC_MethodReplacement.DO_NOTHING = XC_MethodReplacement.returnConstant(None, XC_MethodHook.PRIORITY_HIGHEST * 2)


class PyMethodHook(XC_MethodHook):
    _java_name = "com.exteragram.messenger.plugins.xposed.PyMethodHook"

    def __init__(self, plugin_id, callback, *rest):
        priority = next((r for r in rest if isinstance(r, int) and not isinstance(r, bool)), 50)
        flags = [r for r in rest if isinstance(r, bool)]
        super().__init__(priority)
        self.pluginId = str(plugin_id)
        self.pythonCallback = callback
        has_before = flags[0] if flags else True
        has_after = flags[1] if len(flags) > 1 else True
        self._before = _callback(callback, "before_hooked_method", "beforeHookedMethod") if has_before else None
        self._after = _callback(callback, "after_hooked_method", "afterHookedMethod") if has_after else None
        self.beforeHookedFilters = []
        self.afterHookedFilters = []
        self.closed = False

    def setBeforeHookedFilters(self, filters):
        self.beforeHookedFilters = to_java_filters(list(filters or ()))

    def setAfterHookedFilters(self, filters):
        self.afterHookedFilters = to_java_filters(list(filters or ()))

    def getBeforeHookedFilters(self):
        return self.beforeHookedFilters

    def getAfterHookedFilters(self):
        return self.afterHookedFilters

    def beforeHookedMethod(self, param):
        if self.closed or self._before is None or not _plugin_active(self.pluginId):
            return
        if run_filters(self.beforeHookedFilters, param, True):
            self._before(param)

    def afterHookedMethod(self, param):
        if self.closed or self._after is None or not _plugin_active(self.pluginId):
            return
        if run_filters(self.afterHookedFilters, param, False):
            self._after(param)

    def close(self):
        self.closed = True


class PyMethodReplacement(XC_MethodReplacement):
    _java_name = "com.exteragram.messenger.plugins.xposed.PyMethodReplacement"

    def __init__(self, plugin_id, callback, priority=50):
        super().__init__(priority if isinstance(priority, int) else 50)
        self.pluginId = str(plugin_id)
        self.pythonCallback = callback
        self._replace = _callback(callback, "replace_hooked_method", "replaceHookedMethod")
        self.filters = []
        self.closed = False

    def setFilters(self, filters):
        self.filters = to_java_filters(list(filters or ()))

    def replaceHookedMethod(self, param):
        if self.closed or self._replace is None:
            return None
        return self._replace(param)

    def beforeHookedMethod(self, param):
        if self.closed or not _plugin_active(self.pluginId):
            return
        if self.filters and not run_filters(self.filters, param, True):
            return
        super().beforeHookedMethod(param)

    def close(self):
        self.closed = True


def _owner_plugin():
    host = _host()
    pid = host.current_plugin_id()
    if pid:
        record = host._records.get(pid)
        if record and getattr(record, "instance", None):
            return record.instance
    records = list(getattr(host, "_records", {}).values())
    if len(records) == 1:
        inst = getattr(records[0], "instance", None)
        if inst is not None:
            return inst
    for record in records:
        inst = getattr(record, "instance", None)
        if inst is not None:
            return inst
    try:
        import base_plugin
        return getattr(base_plugin.BasePlugin, "_last_instance", None)
    except Exception:
        return None


def _bridge_hook(member, callback):
    instance = _owner_plugin()
    if instance is None:
        _log("[xposed] hook outside of a plugin context ignored: %r" % (member,))
        return Unhook("exd_unsupported", member, callback)
    priority = getattr(callback, "priority", 50)
    result = instance.hook_method(member, callback, priority)
    return Unhook(str(result), member, callback, instance)


class XposedBridge:
    _java_name = "de.robv.android.xposed.XposedBridge"
    BOOTCLASSLOADER = None
    XPOSED_BRIDGE_VERSION = 82

    @staticmethod
    def log(message):
        if isinstance(message, BaseException):
            message = "%s: %s" % (type(message).__name__, message)
        _log("[xposed] %s" % message)

    @staticmethod
    def getXposedVersion():
        return 82

    @staticmethod
    def hookMethod(member, callback):
        return _bridge_hook(member, callback)

    @staticmethod
    def unhookMethod(member, callback):
        try:
            host = _host()
            for record in list(host._records.values()):
                instance = getattr(record, "instance", None)
                for hook_id in list((instance.__dict__.get("_exd_hook_index") or {}).keys()) if instance else ():
                    member_, callback_ = instance.__dict__["_exd_hook_index"][hook_id]
                    if callback_ is callback and _same_member(member_, member):
                        instance.unhook_method(hook_id)
        except Exception:
            pass

    @staticmethod
    def hookAllMethods(clazz, name, callback):
        out = []
        for member in methods_named(clazz, name):
            out.append(_bridge_hook(member, callback))
        if not out:
            instance = _owner_plugin()
            if instance is not None:
                ids = instance.hook_all_methods(clazz, name, callback, getattr(callback, "priority", 50))
                out = [Unhook(str(i), None, callback, instance) for i in (ids or ())]
        return set(out)

    @staticmethod
    def hookAllConstructors(clazz, callback):
        return {_bridge_hook(ctor, callback) for ctor in constructors_of(clazz)}

    @staticmethod
    def invokeOriginalMethod(member, target, args):
        import exd_java
        if isinstance(member, exd_java.ShimMethod):
            return member.invokeOriginal(target, *list(args or ()))
        invoke = getattr(member, "invoke", None)
        return invoke(target, *list(args or ())) if callable(invoke) else None


def _same_member(a, b):
    if a is b:
        return True
    return getattr(a, "_cls", None) is getattr(b, "_cls", object()) and \
        getattr(a, "_method", None) == getattr(b, "_method", None)


def _python_class(clazz):
    if isinstance(clazz, str):
        return _resolve_class(clazz)
    if isinstance(clazz, type):
        return clazz
    import exd_java
    if isinstance(clazz, exd_java.JavaClassInfo):
        return clazz._class if isinstance(clazz._class, type) else None
    return type(clazz) if clazz is not None else None


def methods_named(clazz, name):
    import exd_java
    cls = _python_class(clazz)
    if cls is None:
        return []
    method = exd_java.shim_method(cls, name)
    return [method] if method is not None else []


def constructors_of(clazz):
    import exd_java
    cls = _python_class(clazz)
    return [exd_java.ShimConstructor(cls)] if isinstance(cls, type) else []


class XposedHelpers:
    _java_name = "de.robv.android.xposed.XposedHelpers"

    class ClassNotFoundError(Exception):
        pass

    _additional = weakref.WeakKeyDictionary()
    _additional_fallback = {}
    _static_additional = {}
    _lock = threading.RLock()

    @staticmethod
    def findClass(name, loader=None):
        found = _resolve_class(str(name))
        if found is None:
            raise XposedHelpers.ClassNotFoundError(str(name))
        return found

    @staticmethod
    def findClassIfExists(name, loader=None):
        return _resolve_class(str(name))

    @staticmethod
    def _cls(clazz, loader=None):
        if isinstance(clazz, str):
            return XposedHelpers.findClass(clazz, loader)
        return clazz

    @staticmethod
    def findMethodExact(clazz, name, *types):
        cls = XposedHelpers._cls(clazz)
        found = methods_named(cls, name)
        if not found:
            raise NoSuchMethodError("%s#%s" % (_name_of(cls), name))
        return found[0]

    @staticmethod
    def findMethodExactIfExists(clazz, name, *types):
        try:
            return XposedHelpers.findMethodExact(clazz, name, *types)
        except Exception:
            return None

    @staticmethod
    def findMethodBestMatch(clazz, name, *types):
        return XposedHelpers.findMethodExact(clazz, name)

    @staticmethod
    def findMethodsByExactParameters(clazz, return_type, *types):
        import exd_java
        cls = _python_class(clazz)
        return exd_java.shim_methods(cls) if cls is not None else []

    @staticmethod
    def findConstructorExact(clazz, *types):
        cls = _python_class(XposedHelpers._cls(clazz))
        return constructors_of(cls)[0]

    findConstructorBestMatch = findConstructorExact

    @staticmethod
    def findAndHookMethod(clazz, *args):
        if not args:
            raise ValueError("no method name")
        loader_given = len(args) >= 2 and not isinstance(args[0], str) and isinstance(clazz, str)
        if loader_given:
            args = args[1:]
        name, rest = args[0], list(args[1:])
        callback = rest[-1] if rest else None
        if callback is None or not (hasattr(callback, "beforeHookedMethod") or hasattr(callback, "before_hooked_method")
                                    or hasattr(callback, "replace_hooked_method") or hasattr(callback, "replaceHookedMethod")):
            raise ValueError("no callback defined")
        member = XposedHelpers.findMethodExact(XposedHelpers._cls(clazz), name)
        return XposedBridge.hookMethod(member, callback)

    @staticmethod
    def findAndHookConstructor(clazz, *args):
        callback = args[-1] if args else None
        member = XposedHelpers.findConstructorExact(XposedHelpers._cls(clazz))
        return XposedBridge.hookMethod(member, callback)

    @staticmethod
    def findField(clazz, name):
        import exd_java
        return exd_java.ShimField(_python_class(XposedHelpers._cls(clazz)), name)

    @staticmethod
    def findFieldIfExists(clazz, name):
        try:
            return XposedHelpers.findField(clazz, name)
        except Exception:
            return None

    @staticmethod
    def getObjectField(obj, name):
        return get_field(obj, name)

    @staticmethod
    def setObjectField(obj, name, value):
        set_field(obj, name, value)

    @staticmethod
    def getStaticObjectField(clazz, name):
        return get_field(XposedHelpers._cls(clazz), name)

    @staticmethod
    def setStaticObjectField(clazz, name, value):
        set_field(XposedHelpers._cls(clazz), name, value)

    @staticmethod
    def callMethod(obj, name, *args):
        method = getattr(obj, name)
        return method(*args)

    @staticmethod
    def callStaticMethod(clazz, name, *args):
        return getattr(XposedHelpers._cls(clazz), name)(*args)

    @staticmethod
    def newInstance(clazz, *args):
        return XposedHelpers._cls(clazz)(*args)

    @staticmethod
    def setAdditionalInstanceField(obj, key, value):
        with XposedHelpers._lock:
            store = _additional_store(obj, True)
            previous = store.get(key)
            store[key] = value
            return previous

    @staticmethod
    def getAdditionalInstanceField(obj, key):
        with XposedHelpers._lock:
            store = _additional_store(obj, False)
            return store.get(key) if store else None

    @staticmethod
    def removeAdditionalInstanceField(obj, key):
        with XposedHelpers._lock:
            store = _additional_store(obj, False)
            return store.pop(key, None) if store else None

    @staticmethod
    def setAdditionalStaticField(obj, key, value):
        cls = obj if isinstance(obj, type) else type(obj)
        store = XposedHelpers._static_additional.setdefault(cls, {})
        previous = store.get(key)
        store[key] = value
        return previous

    @staticmethod
    def getAdditionalStaticField(obj, key):
        cls = obj if isinstance(obj, type) else type(obj)
        return XposedHelpers._static_additional.get(cls, {}).get(key)

    @staticmethod
    def removeAdditionalStaticField(obj, key):
        cls = obj if isinstance(obj, type) else type(obj)
        return XposedHelpers._static_additional.get(cls, {}).pop(key, None)

    @staticmethod
    def getSurroundingThis(obj):
        return get_field(obj, "this$0")

    @staticmethod
    def getParameterTypes(*args):
        return [type(a) for a in args]


def _additional_store(obj, create):
    try:
        store = XposedHelpers._additional.get(obj)
        if store is None and create:
            store = {}
            XposedHelpers._additional[obj] = store
        return store
    except TypeError:
        key = id(obj)
        store = XposedHelpers._additional_fallback.get(key)
        if store is None and create:
            store = {}
            XposedHelpers._additional_fallback[key] = store
        return store


for _kind in ("Int", "Long", "Boolean", "Float", "Double", "Short", "Byte", "Char"):
    setattr(XposedHelpers, "get%sField" % _kind, staticmethod(lambda obj, name: get_field(obj, name)))
    setattr(XposedHelpers, "set%sField" % _kind, staticmethod(lambda obj, name, value: set_field(obj, name, value)))
    setattr(XposedHelpers, "getStatic%sField" % _kind,
            staticmethod(lambda clazz, name: get_field(XposedHelpers._cls(clazz), name)))
    setattr(XposedHelpers, "setStatic%sField" % _kind,
            staticmethod(lambda clazz, name, value: set_field(XposedHelpers._cls(clazz), name, value)))


class NoSuchMethodError(Exception):
    pass


class NoSuchFieldError(Exception):
    pass


def _name_of(cls):
    return getattr(cls, "_java_name", None) or getattr(cls, "__name__", None) or str(cls)


# Fields

def _field_names(name):
    name = str(name)
    yield name
    if name.startswith("m") and len(name) > 1 and name[1].isupper():
        yield name[1].lower() + name[2:]
    yield "_" + name


def _static_attr(obj, name):
    try:
        value = inspect.getattr_static(obj, name)
    except AttributeError:
        return None
    return value.__func__ if isinstance(value, (staticmethod, classmethod)) else value


def get_field(obj, name):
    if obj is None:
        return None
    getter = _static_attr(obj, "_exd_get_field")
    if callable(getter):
        return getter(obj, name) if not isinstance(obj, type) else getter(name)
    for candidate in _field_names(name):
        try:
            value = inspect.getattr_static(obj, candidate)
        except AttributeError:
            continue
        if isinstance(value, (staticmethod, classmethod)):
            continue
        if isinstance(value, property):
            return value.fget(obj)
        if callable(value) and not isinstance(obj, type) and candidate in type(obj).__dict__:
            continue
        try:
            return getattr(obj, candidate)
        except Exception:
            return value
    raise NoSuchFieldError("%s#%s" % (_name_of(obj if isinstance(obj, type) else type(obj)), name))


def set_field(obj, name, value):
    if obj is None:
        raise NoSuchFieldError(str(name))
    setter = _static_attr(obj, "_exd_set_field")
    if callable(setter):
        if isinstance(obj, type):
            setter(name, value)
        else:
            setter(obj, name, value)
        return
    for candidate in _field_names(name):
        try:
            inspect.getattr_static(obj, candidate)
        except AttributeError:
            continue
        setattr(obj, candidate, value)
        return
    setattr(obj, str(name), value)


# Install

def install(shims):
    entries = {
        "de.robv.android.xposed": {"XposedBridge": XposedBridge, "XC_MethodHook": XC_MethodHook,
                                   "XC_MethodReplacement": XC_MethodReplacement, "XposedHelpers": XposedHelpers},
        "com.exteragram.messenger.plugins.xposed": {"PyMethodHook": PyMethodHook,
                                                    "PyMethodReplacement": PyMethodReplacement},
        "com.exteragram.messenger.plugins.hooks": {"HookFilter": HookFilter},
    }
    import exd_java
    entries["java.lang.reflect"] = {"Member": exd_java.ShimMember, "Method": exd_java.ShimMethod,
                                    "Constructor": exd_java.ShimConstructor, "Field": exd_java.ShimField}
    entries["java.chaquopy"] = {"JavaClass": exd_java.JavaClass, "JavaObject": exd_java.ChaquopyJavaObject}
    for module, values in entries.items():
        shims.setdefault(module, {}).update(values)
    try:
        import exd_java
        for module, values in entries.items():
            for name, value in values.items():
                exd_java._CLASSES[module + "." + name] = value
        exd_java._CLASSES["de.robv.android.xposed.XC_MethodHook$MethodHookParam"] = MethodHookParam
        exd_java._CLASSES["de.robv.android.xposed.XC_MethodHook$Unhook"] = Unhook
    except Exception:
        pass
