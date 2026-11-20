import json
import time

import _compat
import _extera
import tl_runtime

TLRPC = tl_runtime.TLRPC


def peer_info(dialog_id):
    try:
        return json.loads(_extera.peer_info(int(dialog_id or 0)) or "{}")
    except Exception:
        return {}


def get_input_peer(dialog_id):
    info = peer_info(dialog_id)
    kind = info.get("type")
    if info.get("self"):
        return TLRPC.TL_inputPeerSelf()
    if kind == "user":
        return TLRPC.TL_inputPeerUser(
            user_id=int(info.get("bare_id") or 0),
            access_hash=int(info.get("access_hash") or 0))
    if kind == "channel":
        return TLRPC.TL_inputPeerChannel(
            channel_id=int(info.get("bare_id") or 0),
            access_hash=int(info.get("access_hash") or 0))
    if kind == "chat":
        return TLRPC.TL_inputPeerChat(chat_id=int(info.get("bare_id") or 0))
    return TLRPC.TL_inputPeerEmpty()


def get_input_user(user_id):
    info = peer_info(user_id)
    if info.get("self"):
        return TLRPC.TL_inputUserSelf()
    if info.get("type") != "user":
        return TLRPC.TL_inputUserEmpty()
    return TLRPC.TL_inputUser(
        user_id=int(info.get("bare_id") or 0),
        access_hash=int(info.get("access_hash") or 0))


def get_input_channel(channel_id):
    dialog = -abs(int(channel_id or 0))
    info = peer_info(dialog)
    if info.get("type") != "channel":
        return TLRPC.TL_inputChannelEmpty()
    return TLRPC.TL_inputChannel(
        channel_id=int(info.get("bare_id") or 0),
        access_hash=int(info.get("access_hash") or 0))


def get_peer(dialog_id):
    dialog_id = int(dialog_id or 0)
    info = peer_info(dialog_id)
    kind = info.get("type")
    bare = int(info.get("bare_id") or abs(dialog_id))
    if kind == "channel":
        return TLRPC.TL_peerChannel(channel_id=bare)
    if kind == "chat":
        return TLRPC.TL_peerChat(chat_id=bare)
    return TLRPC.TL_peerUser(user_id=bare)


_pinned_peers = {}


class pin_peers:
    def __init__(self, objects):
        self.objects = objects
        self.previous = {}

    def __enter__(self):
        for key, value in self.objects.items():
            self.previous[key] = _pinned_peers.get(key)
            _pinned_peers[key] = value
        return self

    def __exit__(self, *args):
        for key, value in self.previous.items():
            if value is None:
                _pinned_peers.pop(key, None)
            else:
                _pinned_peers[key] = value


def get_user(user_id):
    pinned = _pinned_peers.get(("user", int(user_id or 0)))
    if pinned is not None:
        return pinned
    info = peer_info(user_id)
    if info.get("type") != "user":
        return None
    name = info.get("name") or ""
    first, _, last = name.partition(" ")
    return TLRPC.TL_user(
        id=int(info.get("bare_id") or user_id),
        access_hash=int(info.get("access_hash") or 0),
        first_name=first,
        last_name=last or None,
        username=info.get("username") or None,
        phone=info.get("phone") or None,
        bot=bool(info.get("bot")),
        premium=bool(info.get("premium")),
        self=bool(info.get("self")))


def get_chat(chat_id):
    pinned = _pinned_peers.get(("chat", abs(int(chat_id or 0))))
    if pinned is not None:
        return pinned
    dialog = -abs(int(chat_id or 0))
    info = peer_info(dialog)
    kind = info.get("type")
    if kind == "channel":
        return TLRPC.TL_channel(
            id=int(info.get("bare_id") or abs(dialog)),
            access_hash=int(info.get("access_hash") or 0),
            title=info.get("name") or "",
            username=info.get("username") or None,
            megagroup=bool(info.get("megagroup")),
            broadcast=not info.get("megagroup"))
    if kind == "chat":
        return TLRPC.TL_chat(
            id=int(info.get("bare_id") or abs(dialog)),
            title=info.get("name") or "")
    return None


class _Instance(metaclass=_compat.LenientMeta):
    def __init__(self, account=0):
        self.currentAccount = account

    def __getattr__(self, item):
        if item.startswith("__"):
            raise AttributeError(item)
        return _compat.Stub(type(self).__name__ + "." + item)


class ConnectionsManager(_Instance):
    _java_name = "org.telegram.tgnet.ConnectionsManager"
    RequestFlagFailOnServerErrors = 2
    RequestFlagCanCompress = 4
    RequestFlagWithoutLogin = 8
    RequestFlagTryDifferentDc = 16
    RequestFlagForceDownload = 32
    RequestFlagInvokeAfter = 64
    RequestFlagNeedQuickAck = 128
    ConnectionTypeGeneric = 1
    DEFAULT_DATACENTER_ID = 2147483647
    _instances = {}

    @classmethod
    def getInstance(cls, account=0):
        account = int(account or 0)
        if account not in cls._instances:
            cls._instances[account] = cls(account)
        return cls._instances[account]

    def sendRequest(self, request, delegate=None, *args, **kwargs):
        import client_utils
        return client_utils.send_request(request, delegate)

    def cancelRequest(self, request_id, notify=False):
        return None

    def getCurrentTime(self):
        return int(time.time())

    def getCurrentTimeMillis(self):
        return int(time.time() * 1000)

    def getConnectionState(self):
        return 3


class MessagesController(_Instance):
    _java_name = "org.telegram.messenger.MessagesController"
    _instances = {}

    def premiumFeaturesBlocked(self):
        return False

    def premiumPurchaseBlocked(self):
        return False

    def isPremiumUser(self, user=None):
        return bool(getattr(user, "premium", False)) if user is not None else UserConfig.getInstance(0).isPremium()

    @classmethod
    def getInstance(cls, account=0):
        account = int(account or 0)
        if account not in cls._instances:
            cls._instances[account] = cls(account)
        return cls._instances[account]

    def getInputPeer(self, dialog_or_peer):
        return get_input_peer(_dialog_of(dialog_or_peer))

    def getInputUser(self, user):
        if isinstance(user, tl_runtime.TLObject):
            user = getattr(user, "id", 0)
        return get_input_user(user)

    def getInputChannel(self, channel):
        if isinstance(channel, tl_runtime.TLObject):
            channel = getattr(channel, "id", 0)
        return get_input_channel(channel)

    def getPeer(self, dialog_id):
        return get_peer(dialog_id)

    def getUser(self, user_id):
        return get_user(user_id)

    def getChat(self, chat_id):
        return get_chat(chat_id)

    def getChatFull(self, chat_id, *args):
        chat = self.getChat(chat_id)
        if chat is not None:
            return getattr(chat, "full_chat", chat)
        import _compat
        return _compat.Stub("ChatFull")

    def blockPeer(self, peer_id, *args):
        return None

    def deleteDialog(self, dialog_id, *args):
        return None

    def putUser(self, *args, **kwargs):
        return True

    def putChat(self, *args, **kwargs):
        return True

    def putUsers(self, *args, **kwargs):
        return None

    def putChats(self, *args, **kwargs):
        return None

    def getUserOrChat(self, username):
        return None

    def _send(self, request):
        import client_utils
        return client_utils.send_request(request, None, self.currentAccount)

    @staticmethod
    def _is_channel(dialog_id):
        try:
            return int(dialog_id) < 0 and peer_info(int(dialog_id)).get("type") == "channel"
        except Exception:
            return False

    def openByUserName(self, username, fragment=None, *args):
        name = str(username or "").lstrip("@").strip()
        if not name:
            return
        _extera.open_url("tg://resolve?domain=" + name)

    def deleteMessages(self, messages=None, randoms=None, encrypted=None, dialog_id=0, *args):
        ids = [int(x) for x in (messages or [])]
        dialog_id = int(dialog_id or 0)
        if not ids:
            return
        for_all = bool(args[0]) if args and isinstance(args[0], bool) else True
        if self._is_channel(dialog_id):
            self._send(TLRPC.TL_channels_deleteMessages(channel=get_input_channel(-dialog_id),
                                                        id=tl_runtime.ArrayList(ids)))
        else:
            self._send(TLRPC.TL_messages_deleteMessages(revoke=for_all, id=tl_runtime.ArrayList(ids)))

    def pinDialog(self, dialog_id, pin=True, *args):
        self._send(TLRPC.TL_messages_toggleDialogPin(
            pinned=bool(pin), peer=TLRPC.TL_inputDialogPeer(peer=get_input_peer(int(dialog_id)))))
        return True

    def markDialogAsRead(self, dialog_id, max_id=0, *args):
        dialog_id = int(dialog_id)
        if self._is_channel(dialog_id):
            self._send(TLRPC.TL_channels_readHistory(channel=get_input_channel(-dialog_id), max_id=int(max_id)))
        else:
            self._send(TLRPC.TL_messages_readHistory(peer=get_input_peer(dialog_id), max_id=int(max_id)))

    def isForum(self, dialog_id, *args):
        try:
            return bool(peer_info(int(dialog_id)).get("forum"))
        except Exception:
            return False

    def isDialogMuted(self, dialog_id, *args):
        try:
            return bool(peer_info(int(dialog_id)).get("muted"))
        except Exception:
            return False

    def getAllDialogs(self):
        return tl_runtime.ArrayList(_dialogs())

    def getDialogs(self, folder_id=0):
        return tl_runtime.ArrayList([d for d in _dialogs() if int(getattr(d, "folder_id", 0)) == int(folder_id)])

    def getDialog(self, dialog_id):
        for dialog in _dialogs():
            if int(dialog.id) == int(dialog_id):
                return dialog
        return None

    def getDialogFilters(self):
        return tl_runtime.ArrayList(_filters())

    def getUserFull(self, user_id):
        return None

    def getChatFull(self, chat_id):
        return None


def _dialogs():
    try:
        data = json.loads(_extera.dialogs_list() or "[]")
    except Exception:
        return []
    result = []
    for entry in data:
        dialog = TLRPC.TL_dialog(
            id=int(entry.get("id") or 0), unread_count=int(entry.get("unread") or 0),
            pinned=bool(entry.get("pinned")), top_message=int(entry.get("top_message") or 0),
            folder_id=int(entry.get("folder_id") or 0))
        dialog.id = int(entry.get("id") or 0)
        dialog.peer = get_peer(dialog.id)
        dialog.last_message_date = int(entry.get("date") or 0)
        dialog.unread_mentions_count = int(entry.get("mentions") or 0)
        result.append(dialog)
    return result


class _DialogFilter:
    _java_name = "org.telegram.messenger.MessagesController$DialogFilter"

    def __init__(self, data):
        self.id = int(data.get("id") or 0)
        self.name = str(data.get("title") or "")
        self.dialogs = tl_runtime.ArrayList(_dialog_subset(data.get("chats") or ()))
        self.alwaysShow = tl_runtime.ArrayList([int(x) for x in data.get("chats") or ()])
        self.neverShow = tl_runtime.ArrayList()
        self.pinnedDialogs = {}
        self.flags = 0
        self.unreadCount = 0

    def isDefault(self):
        return self.id == 0

    def includesDialog(self, account, dialog_id, *args):
        return int(dialog_id) in self.alwaysShow

    def isChatlist(self):
        return False


def _dialog_subset(ids):
    wanted = {int(x) for x in ids}
    return [d for d in _dialogs() if int(d.id) in wanted]


def _filters():
    try:
        data = json.loads(_extera.dialog_filters() or "[]")
    except Exception:
        return []
    return [_DialogFilter(entry) for entry in data]


def _dialog_of(value):
    if isinstance(value, tl_runtime.TLObject):
        for attr, sign in (("user_id", 1), ("channel_id", -1), ("chat_id", -1)):
            bare = getattr(value, attr, None)
            if bare:
                return sign * int(bare)
        bare = getattr(value, "id", 0)
        return int(bare or 0)
    return int(value or 0)


def self_id():
    try:
        return int(_extera.self_id() or 0)
    except Exception:
        return 0


class UserConfig(_Instance):
    _java_name = "org.telegram.messenger.UserConfig"
    selectedAccount = 0
    _instances = {}

    @classmethod
    def getInstance(cls, account=0):
        account = int(account or 0)
        if account not in cls._instances:
            cls._instances[account] = cls(account)
        return cls._instances[account]

    @property
    def clientUserId(self):
        return self_id()

    def getClientUserId(self):
        return self_id()

    def getCurrentUser(self):
        return get_user(self_id())

    def isClientActivated(self):
        return self_id() != 0

    def isPremium(self):
        try:
            return bool(peer_info(self_id()).get("premium"))
        except Exception:
            return False

    def hasPremiumOnAccounts(self):
        return self.isPremium()

    def getNewMessageId(self):
        return 0


class _ClassGetter:
    def __init__(self, name):
        self.name = name

    def __get__(self, obj, owner):
        name = self.name

        def getter(*args):
            import exd_java
            return exd_java.JavaClassInfo(name, owner)
        return getter


def _class_getter(name):
    return _ClassGetter(name)


class ChatActivity(_Instance):
    _java_name = "org.telegram.ui.ChatActivity"
    def __init__(self, dialog_id=0):
        super().__init__(0)
        self._dialog_id = int(dialog_id or 0)

    def getDialogId(self):
        return self._dialog_id

    def getCurrentChat(self):
        return get_chat(self._dialog_id) if self._dialog_id < 0 else None

    def getCurrentUser(self):
        return get_user(self._dialog_id) if self._dialog_id > 0 else None

    def getCurrentEncryptedChat(self):
        return None

    def getCurrentAccount(self):
        return 0

    def getMessagesController(self):
        return MessagesController.getInstance(0)

    def getConnectionsManager(self):
        return ConnectionsManager.getInstance(0)

    def getUserConfig(self):
        return UserConfig.getInstance(0)

    def getTopicId(self):
        return 0

    def getThreadId(self):
        return 0

    def isInScheduleMode(self):
        return False

    def getChatMode(self):
        return 0

    getClass = _class_getter("org.telegram.ui.ChatActivity")

    def getParentActivity(self):
        return _compat._LaunchActivity.instance

    def startActivityForResult(self, intent, request_code=0, *args):
        return _compat._LaunchActivity.instance.startActivityForResult(intent, request_code)

    def getContext(self):
        return _compat.Stub("Context")

    def updateTopPanel(self, *args, **kwargs):
        return None

    def createView(self, *args, **kwargs):
        return None

    def onResume(self, *args, **kwargs):
        return None


class ProfileActivity(ChatActivity):
    _java_name = "org.telegram.ui.ProfileActivity"
    getClass = _class_getter("org.telegram.ui.ProfileActivity")

    def __init__(self, dialog_id=0):
        super().__init__(dialog_id)
        self.chatInfo = None

    def getUserId(self):
        return self._dialog_id if self._dialog_id > 0 else 0

    def getChatId(self):
        return -self._dialog_id if self._dialog_id < 0 else 0

    def getBotVerificationDrawable(self, *args):
        return None

    def updateRowsIds(self, *args):
        return None

    def updateListAnimated(self, *args):
        return None


class DialogsActivity(ChatActivity):
    _java_name = "org.telegram.ui.DialogsActivity"
    getClass = _class_getter("org.telegram.ui.DialogsActivity")

    def getDialogsType(self):
        return 0

    def getFolderId(self):
        return 0


def current_fragment():
    try:
        dialog = int(_extera.current_peer() or 0)
    except Exception:
        dialog = 0
    if not dialog:
        return None
    return ChatActivity(dialog)


class RequestDelegate:
    def __init__(self, fn=None):
        self._fn = fn

    def run(self, response, error):
        if self._fn:
            self._fn(response, error)


def install(shims):
    tgnet = shims.setdefault("org.telegram.tgnet", {})
    tgnet.update({
        "TLRPC": TLRPC,
        "TLObject": tl_runtime.TLObject,
        "ConnectionsManager": ConnectionsManager,
        "RequestDelegate": RequestDelegate,
    })
    tl = shims.setdefault("org.telegram.tgnet.tl", {})
    for name in ("TL_stars", "TL_account", "TL_bots", "TL_stories",
                 "TL_payments", "TL_chatlists", "TL_fragment", "TL_premium",
                 "TL_smsjobs", "TL_phone", "TL_business", "TL_stats",
                 "TL_channels", "TL_messages", "TL_contacts", "TL_users",
                 "TL_photos", "TL_upload", "TL_help", "TL_updates"):
        tl[name] = tl_runtime._Namespace(name)
    messenger = shims.setdefault("org.telegram.messenger", {})
    messenger["MessagesController"] = MessagesController
    messenger["UserConfig"] = UserConfig
    shims.setdefault("org.telegram.ui", {})["ChatActivity"] = ChatActivity
    shims["org.telegram.ui"]["ProfileActivity"] = ProfileActivity
    shims["org.telegram.ui"]["DialogsActivity"] = DialogsActivity
