import threading
import time
from typing import Any, Callable, Optional

import _compat
import _extera

PLUGINS_QUEUE = "pluginsQueue"
GLOBAL_QUEUE = "globalQueue"
STAGE_QUEUE = "stageQueue"
EXTERNAL_NETWORK_QUEUE = "externalNetworkQueue"


# Queues

def run_on_queue(fn: Callable, queue_name: str = PLUGINS_QUEUE, delay: int = 0):
    def runner():
        if delay:
            time.sleep(delay / 1000)
        try:
            fn()
        except Exception as e:
            _extera.log(f"run_on_queue task failed: {e}")
    threading.Thread(target=runner, daemon=True).start()


def get_queue_by_name(queue_name: str):
    return _compat.Stub(queue_name)


# Sending

def _reply_id(value):
    if value is None:
        return 0
    if isinstance(value, int):
        return value
    return int(getattr(value, "id", 0) or 0)


def send_message(params: Any, text: Optional[str] = None,
                 parse_mode: Optional[str] = None,
                 reply_to: Optional[Any] = None):
    if isinstance(params, (int, str)) and text is not None:
        peer = params
        msg_text = str(text or "")
        reply = reply_to
    elif isinstance(params, dict):
        peer = params.get("peer")
        msg_text = params.get("message") or params.get("caption") or ""
        reply = params.get("replyToMsg")
    else:
        peer = getattr(params, "peer", None)
        msg_text = getattr(params, "message", "") or getattr(params, "caption", "") or ""
        reply = getattr(params, "replyToMsg", None)
    entities = getattr(params, "entities", None) if not isinstance(params, (int, str, dict)) else None
    _compat.send(peer, msg_text, parse_mode, _reply_id(reply), entities=entities)


def send_text(peer_id: int, text: str, parse_mode: Optional[str] = None,
              replyToMsg: Any = None, **kwargs):
    _compat.send(peer_id, text, parse_mode, _reply_id(replyToMsg), entities=kwargs.get("entities"))


def send_photo(peer_id, path, caption=None, **kwargs):
    _extera.log("send_photo is not available on desktop yet")


def send_document(peer_id, path, caption=None, **kwargs):
    _extera.log("send_document is not available on desktop yet")


def send_video(peer_id, path, caption=None, **kwargs):
    _extera.log("send_video is not available on desktop yet")


def send_audio(peer_id, path, caption=None, **kwargs):
    _extera.log("send_audio is not available on desktop yet")


def edit_message(message_object, text, parse_mode=None, **kwargs):
    _extera.log("edit_message is not available on desktop yet")


def send_request(request: Any, callback: Optional[Callable] = None):
    _extera.log("send_request is not available on desktop yet")
    return 0


# Controllers

def get_last_fragment():
    return _compat._Context("fragment")


def get_account_instance():
    return _compat.Stub("AccountInstance")


def get_selected_account() -> int:
    return 0


def get_messages_controller():
    return _compat.Stub("MessagesController")


def get_contacts_controller():
    return _compat.Stub("ContactsController")


def get_media_data_controller():
    return _compat.Stub("MediaDataController")


def get_notification_center():
    return _compat.Stub("NotificationCenter")


def get_locale_controller():
    return _compat.Stub("LocaleController")


def get_user_config():
    return _compat.Stub("UserConfig")


def get_connections_manager():
    return _compat.Stub("ConnectionsManager")


class _SendMessagesHelper:
    def sendMessage(self, params, *args, **kwargs):
        send_message(params)

def get_send_messages_helper(*args, **kwargs):
    return _SendMessagesHelper()


def get_file_loader():
    return _compat.Stub("FileLoader")


def get_messages_storage():
    return _compat.Stub("MessagesStorage")


def get_current_dialog_id() -> int:
    try:
        return int(_extera.current_peer() or 0)
    except Exception:
        return 0


class NotificationCenterDelegate:
    def didReceivedNotification(self, id, account, args):
        pass
