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


def get_user(user_id):
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
        self=bool(info.get("self")))


def get_chat(chat_id):
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
    _instances = {}

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

    def putUser(self, *args, **kwargs):
        return True

    def putChat(self, *args, **kwargs):
        return True

    def putUsers(self, *args, **kwargs):
        return None

    def putChats(self, *args, **kwargs):
        return None


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
        return False

    def getNewMessageId(self):
        return 0


class ChatActivity(_Instance):
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

    def getClass(self):
        return tl_runtime._ClassInfo("ChatActivity")

    def getParentActivity(self):
        return _compat._LaunchActivity.instance

    def startActivityForResult(self, intent, request_code=0, *args):
        return _compat._LaunchActivity.instance.startActivityForResult(intent, request_code)

    def getContext(self):
        return _compat.Stub("Context")


class ProfileActivity(ChatActivity):
    def getClass(self):
        return tl_runtime._ClassInfo("ProfileActivity")

    def getUserId(self):
        return self._dialog_id if self._dialog_id > 0 else 0

    def getChatId(self):
        return -self._dialog_id if self._dialog_id < 0 else 0


class DialogsActivity(ChatActivity):
    def getClass(self):
        return tl_runtime._ClassInfo("DialogsActivity")

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
