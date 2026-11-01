import _compat

JavaClass = _compat.Stub("JavaClass")


def collect_peers() -> None:
    return None


def release_plugin(plugin_id=None):
    return None


def _decorator(*args, **kwargs):
    if len(args) == 1 and callable(args[0]) and not kwargs \
            and not isinstance(args[0], str):
        return args[0]

    def wrap(fn):
        return fn
    return wrap


class JField:
    __jfield__ = True

    def __init__(self, field_type, *, default=None, modifiers="public", methods=None):
        self.field_type = field_type
        self.default = default
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


class JMvelMethod:
    __jproxy_method__ = True

    def __init__(self, metadata=None):
        self.python_name = None

    def __set_name__(self, owner, name):
        self.python_name = name

    def __get__(self, instance, owner=None):
        return lambda *args, **kwargs: None


class ClassInterceptor:
    def __init__(self, fn=None, pyclass=None, **kwargs):
        self.fn = fn
        self.pyclass = pyclass
        self.kwargs = kwargs

    def run(self, instance, method_name, args):
        if self.fn:
            return self.fn(instance, method_name, args)
        return None


def class_proxy(clazz, *, fn=None, interceptor=None, methods=None,
                constructors=None, custom_name=None, **kwargs):
    return None


class Base:
    __jbaseclass__ = None
    __jinterfaces__ = ()
    __jproxy_class__ = None
    __jowner__ = None
    __jowner_from_caller__ = False

    def __init__(self, *args, **kwargs):
        pass

    @classmethod
    def on_pre_init(cls, *args):
        return None

    def on_post_init(self, *args):
        return None

    @property
    def java(self):
        return self

    @property
    def this(self):
        return self

    def __getattr__(self, item):
        if item.startswith("__"):
            raise AttributeError(item)
        return _compat.Stub(type(self).__name__ + "." + item)

    def jrelease(self):
        self.on_release()

    def on_release(self):
        return None

    @classmethod
    def extends(cls, java_class, *interfaces, methods=None, constructors=None,
                custom_name=None):
        cls.__jbaseclass__ = java_class
        cls.__jinterfaces__ = interfaces
        return cls

    @classmethod
    def bind(cls, java_class, *interfaces, methods=None, constructors=None,
             custom_name=None):
        return cls.extends(java_class, *interfaces)

    @classmethod
    def java_class(cls):
        return cls.__jbaseclass__

    @classmethod
    def from_java(cls, instance, *, init_args=None):
        return instance

    @classmethod
    def new_java_instance(cls, *args):
        return cls.new_instance(*args)

    @classmethod
    def new_instance(cls, *args, init_args=None):
        values = list(init_args) if init_args is not None else list(args)
        try:
            instance = cls(*values)
        except TypeError:
            instance = cls.__new__(cls)
        instance.on_post_init(*values)
        return instance


class JHelper:
    @staticmethod
    def Override(*signature, arg_types=None, modifiers="public", throws=None, name=None):
        return _decorator(*signature)

    @staticmethod
    def Overload(name, arg_types=None, *, modifiers="public", throws=None):
        return _decorator(name)

    @staticmethod
    def Method(*signature, return_type=None, arg_types=None, modifiers="public",
               throws=None, name=None):
        return _decorator(*signature)

    @staticmethod
    def MVELMethod(*, code=None, return_type=None, arguments=None, arg_types=None,
                   modifiers="public", throws=None, name=None, override=False):
        return _decorator("mvel")

    @staticmethod
    def MVELOverride(*signature, code=None, arguments=None, arg_types=None,
                     modifiers="public", throws=None, name=None):
        return _decorator("mvel")

    @staticmethod
    def Constructor(*signature, arg_types=None):
        return _decorator(*signature)

    @staticmethod
    def PreConstructor(*signature, arg_types=None):
        return _decorator(*signature)

    @staticmethod
    def Field(field_type, *, default=None, modifiers="public", methods=None):
        return JField(field_type, default=default, modifiers=modifiers, methods=methods)

    @staticmethod
    def GetMethod(name=None, *, modifiers="public"):
        return _decorator(name)

    @staticmethod
    def SetMethod(name=None, *, modifiers="public"):
        return _decorator(name)

    @staticmethod
    def ClassBuilder():
        return _decorator("builder")


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


def java_subclass(java_class=None, *interfaces, methods=None, constructors=None,
                  custom_name=None, **kwargs):
    def wrap(cls):
        if not issubclass(cls, Base):
            cls = type(cls.__name__, (cls, Base), {})
        cls.__jbaseclass__ = java_class
        cls.__jinterfaces__ = interfaces
        return cls
    if isinstance(java_class, type) and issubclass(java_class, Base):
        target, java_class = java_class, None
        return wrap(target)
    return wrap


class PyObj(Base):
    def __init__(self, obj=None):
        self.obj = obj

    @classmethod
    def create(cls, obj):
        return cls(obj)
