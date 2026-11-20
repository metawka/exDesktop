import inspect
import threading
import weakref

_MISSING = object()
_lock = threading.RLock()
_peers = {}
_counter = [0]


def _owner_id():
    try:
        import extera_host
        return extera_host.current_plugin_id()
    except Exception:
        return None


def _java_base(java_class):
    if java_class is None:
        import exd_java
        return exd_java.JavaObject
    if isinstance(java_class, str):
        import exd_java
        found = exd_java.lookup(java_class)
        return _java_base(found) if found is not None else _named_base(java_class)
    if isinstance(java_class, type) and issubclass(java_class, Base):
        return java_class.__jproxy_class__ or _java_base(java_class.__jbaseclass__)
    if isinstance(java_class, type):
        return java_class
    import exd_java
    if isinstance(java_class, exd_java.JavaClassInfo) and isinstance(java_class._class, type):
        return java_class._class
    name = None
    try:
        name = java_class.getName()
    except Exception:
        pass
    return _named_base(str(name or java_class))


def _named_base(name):
    import exd_java

    class Named(exd_java.JavaObject):
        _java_name = name

    Named.__name__ = str(name).split(".")[-1].split("$")[-1] or "JavaClass"
    return Named


def _register_peer(peer):
    with _lock:
        _counter[0] += 1
        key = _counter[0]
        _peers[key] = (weakref.ref(peer), peer.__dict__.get("_jowner"))
    return key


def collect_peers() -> None:
    with _lock:
        for key in [k for k, (ref, _) in _peers.items() if ref() is None]:
            del _peers[key]


def release_plugin(plugin_id=None):
    plugin_id = plugin_id or _owner_id()
    with _lock:
        targets = [(k, ref()) for k, (ref, owner) in _peers.items() if owner == plugin_id]
        for key, _ in targets:
            _peers.pop(key, None)
    for _, peer in targets:
        if peer is not None:
            try:
                peer.jrelease()
            except Exception:
                pass


# Metadata

class _MethodMetadata:
    def __init__(self, kind, return_type, arg_types, modifiers, throws=None, java_name=None,
                 implementation="python", code=None, arg_names=None):
        self.kind = kind
        self.return_type = return_type
        self.arg_types = list(arg_types or ())
        self.modifiers = modifiers
        self.throws = throws
        self.java_name = java_name
        self.implementation = implementation
        self.code = code
        self.arg_names = list(arg_names or ())

    def resolve(self, fn, python_name):
        names = self.arg_names
        if not names and fn is not None:
            try:
                names = [p for p in inspect.signature(fn).parameters][1:]
            except (TypeError, ValueError):
                names = []
        return _ResolvedMethodMetadata(kind=self.kind, python_name=python_name,
                                       java_name=self.java_name or python_name,
                                       return_type=self.return_type, arg_types=self.arg_types,
                                       modifiers=self.modifiers, throws=self.throws,
                                       implementation=self.implementation, code=self.code, arg_names=names)


class _ResolvedMethodMetadata:
    def __init__(self, *, kind, python_name, java_name, return_type, arg_types, modifiers, throws=None,
                 implementation="python", code=None, arg_names=None):
        self.kind = kind
        self.python_name = python_name
        self.java_name = java_name
        self.return_type = return_type
        self.arg_types = list(arg_types or ())
        self.modifiers = modifiers
        self.throws = throws
        self.implementation = implementation
        self.code = code
        self.arg_names = list(arg_names or ())
        self.signature = (java_name, tuple(str(t) for t in self.arg_types))


class _ConstructorMetadata:
    def __init__(self, arg_types=None, *, phase="post"):
        self.arg_types = list(arg_types or ())
        self.phase = phase

    def resolve(self, fn, python_name):
        return _ResolvedConstructorMetadata(python_name=python_name, arg_types=self.arg_types, phase=self.phase)


class _ResolvedConstructorMetadata:
    def __init__(self, *, python_name, arg_types, phase):
        self.python_name = python_name
        self.arg_types = list(arg_types or ())
        self.phase = phase
        self.signature = tuple(str(t) for t in self.arg_types)


class _ClassBuildMetadata:
    def resolve(self, python_name):
        return _ResolvedClassBuildMetadata(python_name=python_name)


class _ResolvedClassBuildMetadata:
    def __init__(self, *, python_name):
        self.python_name = python_name


class _FieldMetadata:
    def __init__(self, field_type, default=_MISSING, modifiers="public", methods=None):
        self.field_type = field_type
        self.default = None if default is _MISSING else default
        self.modifiers = modifiers
        self.methods = list(methods or ())

    def resolve(self, python_name):
        return _ResolvedFieldMetadata(python_name=python_name, field_type=self.field_type, default=self.default,
                                      modifiers=self.modifiers,
                                      methods=[m.resolve(python_name, self.field_type) for m in self.methods])


class _ResolvedFieldMetadata:
    def __init__(self, *, python_name, field_type, default, modifiers, methods):
        self.python_name = python_name
        self.field_type = field_type
        self.default = default
        self.modifiers = modifiers
        self.methods = list(methods or ())


class _FieldAccessorMetadata:
    def __init__(self, kind, name=None, modifiers="public"):
        self.kind = kind
        self.name = name
        self.modifiers = modifiers

    def resolve(self, field_name, field_type):
        prefix = "get" if self.kind == "get" else "set"
        name = self.name or prefix + field_name[:1].upper() + field_name[1:]
        return _ResolvedFieldAccessorMetadata(kind=self.kind, name=name, modifiers=self.modifiers)


class _ResolvedFieldAccessorMetadata:
    def __init__(self, *, kind, name, modifiers):
        self.kind = kind
        self.name = name
        self.modifiers = modifiers


class JField:
    __jfield__ = True

    def __init__(self, field_type, *, default=_MISSING, modifiers="public", methods=None):
        self._meta = _FieldMetadata(field_type, default, modifiers, methods)
        self.field_type = field_type
        self.default = self._meta.default
        self.name = None
        self.storage_name = None

    def __set_name__(self, owner, name):
        self.name = name
        self.storage_name = "_jfield_" + name

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        return instance.__dict__.get(self.storage_name, self.default)

    def __set__(self, instance, value):
        instance.__dict__[self.storage_name] = value


# MVEL

def _run_mvel(code, this, names, args):
    import exd_xposed
    statements = [part.strip() for part in str(code).replace("\n", ";").split(";") if part.strip()]
    scope = exd_xposed._Scope(this, {"__this__": this, "this": this,
                                     "__instanceof__": exd_xposed._instanceof,
                                     "__empty__": exd_xposed._Empty()})
    for index, name in enumerate(names):
        scope[name] = args[index] if index < len(args) else None
    for index, value in enumerate(args):
        scope.setdefault("arg%d" % index, value)
    builtins = {"__builtins__": {"len": len, "str": str, "int": int, "float": float, "bool": bool}}
    result = None
    for statement in statements:
        if statement.startswith("return "):
            statement = statement[len("return "):]
            return eval(compile(exd_xposed._mvel_to_python(statement), "<mvel>", "eval"), builtins, scope)
        target, sep, value = statement.partition("=")
        if sep and value[:1] != "=" and target and target[-1] not in "!<>=" and target.strip().replace(".", "").isidentifier():
            evaluated = eval(compile(exd_xposed._mvel_to_python(value.strip()), "<mvel>", "eval"), builtins, scope)
            path = target.strip().split(".")
            if path[0] in ("this", "__this__") and len(path) > 1:
                holder = this
                for part in path[1:-1]:
                    holder = getattr(holder, part)
                setattr(holder, path[-1], evaluated)
            elif len(path) > 1:
                holder = scope[path[0]]
                for part in path[1:-1]:
                    holder = getattr(holder, part)
                setattr(holder, path[-1], evaluated)
            else:
                scope[path[0]] = evaluated
            result = evaluated
            continue
        result = eval(compile(exd_xposed._mvel_to_python(statement).strip(), "<mvel>", "eval"), builtins, scope)
    return result


class JMvelMethod:
    __jproxy_method__ = True

    def __init__(self, metadata=None):
        self.metadata = metadata
        self.python_name = None

    def __set_name__(self, owner, name):
        self.python_name = name

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        meta = self.metadata
        target = instance.java if isinstance(instance, Base) else instance

        def call(*args):
            return _run_mvel(meta.code, target, meta.arg_names, args)
        return call


# Proxies

class _ManagedClassBuildHook:
    def __init__(self, pyclass):
        self.pyclass = pyclass

    def apply(self, dex_maker, proxy_type, super_type, interfaces) -> None:
        for name in dir(self.pyclass):
            meta = getattr(getattr(self.pyclass, name, None), "__jclassbuild__", None)
            if meta is not None:
                getattr(self.pyclass, name)(dex_maker, proxy_type, super_type, interfaces)


class ClassInterceptor:
    def __init__(self, fn=None, pyclass=None, **kwargs):
        self.fn = fn
        self.pyclass = pyclass
        self.kwargs = kwargs

    def run(self, instance, method_name, args):
        if self.fn is not None:
            return self.fn(instance, method_name, args)
        return instance._exd_super(method_name, *args)


class _ManagedClassInterceptor:
    def __init__(self, pyclass):
        self._pyclass = weakref.ref(pyclass)
        self.instances = weakref.WeakKeyDictionary()
        self.pending_init_args = {}

    @property
    def pyclass(self):
        return self._pyclass()

    def run(self, instance, method_name, args):
        peer = self.get_peer(instance)
        if peer is None:
            peer = self.attach_peer(instance)
        method = peer._jmethods().get(method_name)
        if method is None:
            return instance._exd_super(method_name, *args)
        return getattr(peer, method)(*args)

    def get_peer(self, instance):
        try:
            return self.instances.get(instance)
        except TypeError:
            return None

    def attach_peer(self, instance, *, raw_name=None, args=None):
        pyclass = self.pyclass
        peer = pyclass.__new__(pyclass)
        peer.__dict__["_java"] = instance
        peer.__dict__["_jowner"] = _owner_id() or getattr(pyclass, "__jowner__", None)
        try:
            self.instances[instance] = peer
        except TypeError:
            pass
        _register_peer(peer)
        return peer

    def detach_peer(self, instance, peer):
        try:
            if self.instances.get(instance) is peer:
                del self.instances[instance]
        except TypeError:
            pass

    def push_pending_init_args(self, init_args):
        _counter[0] += 1
        token = _counter[0]
        self.pending_init_args[token] = tuple(init_args or ())
        return token

    def cancel_pending_init_args(self, token) -> None:
        self.pending_init_args.pop(token, None)


def _proxy_type(base, name, methods, interceptor):
    namespace = {"_java_name": getattr(base, "_java_name", name), "_exd_interceptor": interceptor}

    def make(method_name):
        def proxied(self, *args):
            return self._exd_interceptor.run(self, method_name, list(args))
        proxied.__name__ = method_name
        return proxied

    for method_name in methods:
        namespace[method_name] = make(method_name)

    def _exd_super(self, method_name, *args):
        for klass in type(self).__mro__[1:]:
            if method_name in klass.__dict__:
                value = klass.__dict__[method_name]
                if isinstance(value, staticmethod):
                    return value.__func__(*args)
                return value(self, *args)
        attribute = getattr(super(type(self), self), method_name, None)
        return attribute(*args) if callable(attribute) else None

    namespace["_exd_super"] = _exd_super
    return type(name, (base,), namespace)


def class_proxy(clazz, *, fn=None, interceptor=None, methods=None, constructors=None, custom_name=None, **kwargs):
    base = _java_base(clazz)
    if interceptor is None:
        interceptor = ClassInterceptor(fn, **kwargs)
    if methods is None:
        import exd_java
        methods = [m.getName() for m in exd_java.shim_methods(base)]
    return _proxy_type(base, custom_name or (getattr(base, "__name__", "Proxy") + "Proxy"), list(methods),
                       interceptor)


# Base

class _BaseMeta(type):
    def __call__(cls, *args, **kwargs):
        obj = cls.__new__(cls)
        java_args = args
        try:
            pre = cls.on_pre_init(*args)
        except TypeError:
            pre = None
        if isinstance(pre, (tuple, list)):
            java_args = tuple(pre)
        obj._jattach(java_args)
        cls.__init__(obj, *args, **kwargs)
        try:
            obj.on_post_init(*args)
        except TypeError:
            obj.on_post_init()
        return obj


class Base(metaclass=_BaseMeta):
    __jbaseclass__ = None
    __jinterfaces__ = ()
    __jproxy_class__ = None
    __jowner__ = None
    __jowner_from_caller__ = False

    def __init__(self, *args, **kwargs):
        pass

    def _jattach(self, args):
        if "_java" in self.__dict__:
            return
        cls = type(self)
        proxy = cls._jproxy()
        token = proxy._exd_interceptor.push_pending_init_args(args)
        try:
            java = proxy.__new__(proxy)
            proxy._exd_interceptor.instances[java] = self
            self.__dict__["_java"] = java
            self.__dict__["_jowner"] = _owner_id() or cls.__jowner__
            for name in cls._jconstructors("pre"):
                getattr(self, name)(*args)
            try:
                java.__init__(*args)
            except TypeError:
                try:
                    java.__init__()
                except Exception:
                    pass
            for name in cls._jconstructors("post"):
                getattr(self, name)(*args)
        finally:
            proxy._exd_interceptor.cancel_pending_init_args(token)
        _register_peer(self)

    @classmethod
    def _jmethods(cls):
        cached = cls.__dict__.get("_jmethod_cache")
        if cached is not None:
            return cached
        found = {}
        for klass in reversed(cls.__mro__):
            for name, value in klass.__dict__.items():
                meta = getattr(value, "__jproxy_method__", None)
                if isinstance(meta, _MethodMetadata):
                    found[meta.java_name or name] = name
                elif isinstance(value, JMvelMethod):
                    found[(value.metadata.java_name if value.metadata else None) or name] = name
        type.__setattr__(cls, "_jmethod_cache", found)
        return found

    @classmethod
    def _jconstructors(cls, phase):
        out = []
        for klass in reversed(cls.__mro__):
            for name, value in klass.__dict__.items():
                meta = getattr(value, "__jconstructor__", None)
                if isinstance(meta, _ConstructorMetadata) and meta.phase == phase:
                    out.append(name)
        return out

    @classmethod
    def _jproxy(cls):
        proxy = cls.__dict__.get("__jproxy_class__")
        if proxy is not None:
            return proxy
        base = _java_base(cls.__jbaseclass__)
        interceptor = _ManagedClassInterceptor(cls)
        proxy = _proxy_type(base, cls.__name__ + "$Proxy", list(cls._jmethods()), interceptor)
        for name, value in list(cls.__dict__.items()):
            if isinstance(value, JField):
                for accessor in value._meta.methods:
                    resolved = accessor.resolve(name, value.field_type)
                    if resolved.kind == "get":
                        setattr(proxy, resolved.name, (lambda field: lambda self: getattr(
                            interceptor.get_peer(self), field))(name))
                    else:
                        setattr(proxy, resolved.name, (lambda field: lambda self, v: setattr(
                            interceptor.get_peer(self), field, v))(name))
        type.__setattr__(cls, "__jproxy_class__", proxy)
        return proxy

    @classmethod
    def on_pre_init(cls, *args):
        return None

    def on_post_init(self, *args):
        return None

    @property
    def java(self):
        java = self.__dict__.get("_java")
        if java is None:
            self._jattach(())
            java = self.__dict__.get("_java")
        return java

    @property
    def this(self):
        return self.java

    def __getattr__(self, item):
        if item.startswith("__") or item in ("_java", "_jowner"):
            raise AttributeError(item)
        java = self.__dict__.get("_java")
        if java is None:
            raise AttributeError(item)
        proxy = type(java)
        if item in proxy.__dict__ and item in type(self)._jmethods():
            return lambda *args: java._exd_super(item, *args)
        return getattr(java, item)

    def jrelease(self):
        try:
            self.on_release()
        finally:
            java = self.__dict__.pop("_java", None)
            if java is not None:
                proxy = type(java)
                interceptor = getattr(proxy, "_exd_interceptor", None)
                if interceptor is not None:
                    interceptor.detach_peer(java, self)

    def on_release(self):
        return None

    @classmethod
    def extends(cls, java_class, *interfaces, methods=None, constructors=None, custom_name=None):
        type.__setattr__(cls, "__jbaseclass__", java_class)
        type.__setattr__(cls, "__jinterfaces__", interfaces)
        type.__setattr__(cls, "__jproxy_class__", None)
        type.__setattr__(cls, "_jmethod_cache", None)
        return cls

    @classmethod
    def bind(cls, java_class, *interfaces, methods=None, constructors=None, custom_name=None):
        return cls.extends(java_class, *interfaces, methods=methods, constructors=constructors,
                           custom_name=custom_name)

    @classmethod
    def java_class(cls):
        return cls._jproxy()

    @classmethod
    def from_java(cls, instance, *, init_args=None):
        interceptor = getattr(type(instance), "_exd_interceptor", None)
        if isinstance(interceptor, _ManagedClassInterceptor):
            peer = interceptor.get_peer(instance)
            if peer is not None:
                return peer
            return interceptor.attach_peer(instance, args=init_args)
        peer = cls.__new__(cls)
        peer.__dict__["_java"] = instance
        peer.__dict__["_jowner"] = _owner_id()
        _register_peer(peer)
        return peer

    @classmethod
    def new_java_instance(cls, *args):
        return cls.new_instance(*args).java

    @classmethod
    def new_instance(cls, *args, init_args=None):
        values = list(init_args) if init_args is not None else list(args)
        return cls(*values)


def _mark(kind, signature, **meta):
    def wrap(fn):
        if kind == "constructor":
            fn.__jconstructor__ = _ConstructorMetadata(meta.get("arg_types"), phase=meta.get("phase", "post"))
            return fn
        if kind == "classbuild":
            fn.__jclassbuild__ = _ClassBuildMetadata()
            return fn
        fn.__jproxy_method__ = _MethodMetadata(kind, meta.get("return_type"), meta.get("arg_types"),
                                               meta.get("modifiers", "public"), meta.get("throws"),
                                               meta.get("name"))
        return fn
    if len(signature) == 1 and callable(signature[0]) and not isinstance(signature[0], str) \
            and getattr(signature[0], "__name__", "") not in ("str", "int"):
        candidate = signature[0]
        if inspect.isfunction(candidate):
            return wrap(candidate)
    return wrap


class JHelper:
    @staticmethod
    def Override(*signature, arg_types=None, modifiers="public", throws=None, name=None):
        return _mark("override", signature, arg_types=arg_types, modifiers=modifiers, throws=throws, name=name)

    @staticmethod
    def Overload(name, arg_types=None, *, modifiers="public", throws=None):
        return _mark("overload", (), arg_types=arg_types, modifiers=modifiers, throws=throws, name=name)

    @staticmethod
    def Method(*signature, return_type=None, arg_types=None, modifiers="public", throws=None, name=None):
        return _mark("method", signature, return_type=return_type, arg_types=arg_types, modifiers=modifiers,
                     throws=throws, name=name)

    @staticmethod
    def MVELMethod(*, code, return_type=None, arguments=None, arg_types=None, modifiers="public", throws=None,
                   name=None, override=False):
        return JMvelMethod(_MethodMetadata("override" if override else "method", return_type, arg_types,
                                           modifiers, throws, name, "mvel", code, arguments))

    @staticmethod
    def MVELOverride(*signature, code, arguments=None, arg_types=None, modifiers="public", throws=None,
                     name=None):
        return JMvelMethod(_MethodMetadata("override", None, arg_types, modifiers, throws, name, "mvel", code,
                                           arguments))

    @staticmethod
    def Constructor(*signature, arg_types=None):
        return _mark("constructor", signature, arg_types=arg_types, phase="post")

    @staticmethod
    def PreConstructor(*signature, arg_types=None):
        return _mark("constructor", signature, arg_types=arg_types, phase="pre")

    @staticmethod
    def Field(field_type, *, default=_MISSING, modifiers="public", methods=None):
        return JField(field_type, default=default, modifiers=modifiers, methods=methods)

    @staticmethod
    def GetMethod(name=None, *, modifiers="public"):
        return _FieldAccessorMetadata("get", name, modifiers)

    @staticmethod
    def SetMethod(name=None, *, modifiers="public"):
        return _FieldAccessorMetadata("set", name, modifiers)

    @staticmethod
    def ClassBuilder():
        return _mark("classbuild", ())


joverride = JHelper.Override
joverload = JHelper.Overload
jmethod = JHelper.Method
jfield = JHelper.Field
jconstructor = JHelper.Constructor
jpreconstructor = JHelper.PreConstructor
jgetmethod = JHelper.GetMethod
jsetmethod = JHelper.SetMethod
jmvelmethod = JHelper.MVELMethod
jmveloverride = JHelper.MVELOverride
jMVELmethod = JHelper.MVELMethod
jMVELoverride = JHelper.MVELOverride
jclassbuilder = JHelper.ClassBuilder


def java_subclass(java_class=None, *interfaces, methods=None, constructors=None, custom_name=None, **kwargs):
    def wrap(cls):
        if not issubclass(cls, Base):
            cls = type(cls.__name__, (cls, Base), {})
        return cls.extends(java_class, *interfaces)
    if isinstance(java_class, type) and issubclass(java_class, Base):
        target, java_class = java_class, None
        return wrap(target)
    return wrap


class PyObj(Base):
    def __init__(self, obj=None):
        self.__dict__["obj"] = obj
        super().__init__()

    @classmethod
    def create(cls, obj):
        return cls(obj)


def __getattr__(name):
    if name == "JavaClass":
        import exd_java
        return exd_java.JavaClassInfo
    raise AttributeError(name)
