import sys
import itertools
import json

from exd_java import ArrayList, JavaObject

_counter = itertools.count(1)
_pending = {}


def _bundle_value(args, key, default=None):
    if args is None:
        return default
    for getter in ("get", "getBoolean", "getInt", "getLong", "getString"):
        method = getattr(args, getter, None)
        if callable(method):
            try:
                value = method(key)
            except Exception:
                continue
            if value is not None:
                return value
    if isinstance(args, dict):
        return args.get(key, default)
    return default


def _owner():
    try:
        import extera_host
        return extera_host.current_plugin_id() or extera_host.owner_now()
    except Exception:
        return None


def _open(picker, payload):
    _extera = sys.modules["_extera"]
    picker_id = next(_counter)
    _pending[picker_id] = (picker, _owner())
    payload["id"] = picker_id
    _extera.pick_peers(json.dumps(payload))


def dispatch(args):
    entry = _pending.pop(int(args.get("id") or 0), None)
    if entry is None:
        return False
    picker, owner = entry
    import extera_host
    peers = [int(p) for p in args.get("peers") or ()]
    with extera_host.owner_scope(owner):
        try:
            if not args.get("cancelled"):
                picker._exd_done(peers)
        finally:
            picker.onFragmentDestroy()
    return True


class _Fragment(JavaObject):
    def __init__(self, args=None):
        self.arguments = args
        self.delegate = None

    def getArguments(self):
        return self.arguments

    def setDelegate(self, delegate):
        self.delegate = delegate

    def onFragmentCreate(self):
        return True

    def onFragmentDestroy(self):
        return None

    def finishFragment(self, *args):
        return None

    def removeSelfFromStack(self, *args):
        return None

    def getParentActivity(self):
        import _compat
        return _compat._LaunchActivity.instance


class GroupCreateActivityDelegate(JavaObject):
    _java_name = "org.telegram.ui.GroupCreateActivity$GroupCreateActivityDelegate"

    def didSelectUsers(self, *args):
        pass


class GroupCreateAdapter(JavaObject):
    _java_name = "org.telegram.ui.GroupCreateActivity$GroupCreateAdapter"

    def __init__(self, *args):
        self.contacts = ArrayList()


class GroupCreateActivity(_Fragment):
    _java_name = "org.telegram.ui.GroupCreateActivity"
    GroupCreateActivityDelegate = GroupCreateActivityDelegate
    GroupCreateAdapter = GroupCreateAdapter

    def __init__(self, args=None):
        _Fragment.__init__(self, args)
        self.selected = []

    def select(self, ids, *args):
        self.selected = [int(i) for i in (ids.toArray() if hasattr(ids, "toArray") else ids or ())]

    def _exd_present(self):
        _open(self, {"multiple": True, "users": True, "bots": True, "groups": True, "channels": True,
                     "selected": list(self.selected)})

    def _exd_done(self, peers):
        delegate = self.delegate
        method = getattr(delegate, "didSelectUsers", None) if delegate is not None else None
        if not callable(method):
            return
        values = ArrayList(peers)
        try:
            method(True, len(peers), values)
        except TypeError:
            method(values)


class DialogsActivityDelegate(JavaObject):
    _java_name = "org.telegram.ui.DialogsActivity$DialogsActivityDelegate"

    def didSelectDialogs(self, *args):
        return True


class TopicKey(JavaObject):
    _java_name = "org.telegram.messenger.MessagesStorage$TopicKey"

    def __init__(self, dialog_id=0, topic_id=0):
        self.dialogId = int(dialog_id)
        self.topicId = int(topic_id)

    @staticmethod
    def of(dialog_id, topic_id=0):
        return TopicKey(dialog_id, topic_id)


class DialogsPicker(_Fragment):
    _java_name = "org.telegram.ui.DialogsActivity"
    DialogsActivityDelegate = DialogsActivityDelegate

    def _exd_present(self):
        allow_users = bool(_bundle_value(self.arguments, "allowUsers", True))
        _open(self, {"multiple": False,
                     "users": allow_users,
                     "bots": bool(_bundle_value(self.arguments, "allowBots", allow_users)),
                     "groups": bool(_bundle_value(self.arguments, "allowGroups", True)),
                     "channels": bool(_bundle_value(self.arguments, "allowChannels", True)),
                     "title": str(_bundle_value(self.arguments, "selectAlertString", "") or "")})

    def _exd_done(self, peers):
        delegate = self.delegate
        method = getattr(delegate, "didSelectDialogs", None) if delegate is not None else None
        if not callable(method) or not peers:
            return
        dids = ArrayList([TopicKey(peers[0], 0)])
        try:
            method(self, dids, None, False, True, 0, 0, None)
        except TypeError:
            method(self, dids, None, False)

    def getDialogsType(self):
        return int(_bundle_value(self.arguments, "dialogsType", 0) or 0)


def present(fragment):
    show = getattr(type(fragment), "_exd_present", None)
    if callable(show):
        fragment.onFragmentCreate()
        show(fragment)
        return True
    return False


def install(shims):
    import exd_java
    import tgnet_bridge
    entries = {"org.telegram.ui": {"GroupCreateActivity": GroupCreateActivity}}
    for module, values in entries.items():
        shims.setdefault(module, {}).update(values)
    for cls in (GroupCreateActivity, GroupCreateActivityDelegate, GroupCreateAdapter, DialogsActivityDelegate,
                TopicKey):
        exd_java._CLASSES[cls._java_name] = cls
    original_new = tgnet_bridge.DialogsActivity.__new__

    def dialogs_new(cls, *args, **kwargs):
        if args and not isinstance(args[0], (int, float)) and args[0] is not None:
            picker = DialogsPicker(args[0])
            return picker
        return original_new(cls) if original_new is object.__new__ else original_new(cls, *args, **kwargs)

    tgnet_bridge.DialogsActivity.__new__ = staticmethod(dialogs_new)
    tgnet_bridge.DialogsActivity.DialogsActivityDelegate = DialogsActivityDelegate
    for name in ("DIALOGS_TYPE_DEFAULT", "DIALOGS_TYPE_ADD_USERS_TO", "DIALOGS_TYPE_FORWARD"):
        setattr(tgnet_bridge.DialogsActivity, name, {"DIALOGS_TYPE_DEFAULT": 0, "DIALOGS_TYPE_ADD_USERS_TO": 2,
                                                     "DIALOGS_TYPE_FORWARD": 3}[name])
    tgnet_bridge.ChatActivity.presentFragment = lambda self, fragment, *args: present(fragment)
    original_current = tgnet_bridge.current_fragment

    def current_fragment():
        fragment = original_current()
        return fragment if fragment is not None else tgnet_bridge.DialogsActivity(0)

    tgnet_bridge.current_fragment = current_fragment
    import _compat
    _compat._Context.presentFragment = lambda self, fragment, *args: present(fragment)
