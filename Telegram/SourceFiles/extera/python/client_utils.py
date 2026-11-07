import json
import threading
import time
from contextlib import contextmanager
from typing import Any, Callable, Optional

import _compat
import _extera


# Queues

STAGE_QUEUE = "stageQueue"
GLOBAL_QUEUE = "globalQueue"
CACHE_CLEAR_QUEUE = "cacheClearQueue"
SEARCH_QUEUE = "searchQueue"
PHONE_BOOK_QUEUE = "phoneBookQueue"
THEME_QUEUE = "themeQueue"
EXTERNAL_NETWORK_QUEUE = "externalNetworkQueue"
PLUGINS_QUEUE = "pluginsQueue"


def _log(message):
    try:
        from android_utils import log
        log(message)
    except Exception:
        print(f"[extera:client_utils] {message}")


def _native(name):
    return getattr(_extera, name, None)


def _not_available(what):
    _log(f"{what} is not available on desktop yet")


# Hook account scope

_hook_state = threading.local()
_MISSING = object()


def get_hook_account():
    return getattr(_hook_state, "account", None)


def get_selected_account() -> int:
    native = _native("get_selected_account")
    if native is None:
        return 0
    try:
        return int(native())
    except Exception:
        return 0


def _enter_hook_account(account):
    previous = getattr(_hook_state, "account", _MISSING)
    _hook_state.account = account
    return previous


def _exit_hook_account(previous):
    if previous is _MISSING:
        try:
            del _hook_state.account
        except AttributeError:
            pass
    else:
        _hook_state.account = previous


@contextmanager
def hook_scope(account):
    previous = getattr(_hook_state, "account", _MISSING)
    _hook_state.account = account
    try:
        yield account
    finally:
        if previous is _MISSING:
            try:
                del _hook_state.account
            except AttributeError:
                pass
        else:
            _hook_state.account = previous


def _resolve_account(account, helper_name: str = None) -> int:
    if account is not None:
        return int(account)
    return get_selected_account()


def _resolve_scoped_account(account) -> int:
    if account is not None:
        return int(account)
    hook_account = get_hook_account()
    if hook_account is not None:
        return int(hook_account)
    return get_selected_account()


# Dispatch queues

def get_queue_by_name(name: str):
    return _compat.Stub(str(name))


def show_error_bulletin(message, fragment=None):
    from ui.bulletin import BulletinHelper
    BulletinHelper.show_error(message, fragment)


def show_info_bulletin(message, fragment=None):
    from ui.bulletin import BulletinHelper
    BulletinHelper.show_info(message, fragment)


def run_on_queue(fn, queue: str = PLUGINS_QUEUE, delay: int = 0, delay_ms: int = None):
    from android_utils import safe_call
    if delay_ms is not None:
        delay = delay_ms
    account = get_hook_account()

    def runner():
        try:
            if delay:
                time.sleep(delay / 1000)
        except Exception:
            pass
        if account is None:
            safe_call(fn)
        else:
            with hook_scope(account):
                safe_call(fn)

    threading.Thread(target=runner, daemon=True).start()
    return get_queue_by_name(queue)


def run_on_queue_named(fn, queue_name: str = PLUGINS_QUEUE, delay: int = 0):
    return run_on_queue(fn, queue_name, delay)


# Requests

class RequestCallback:
    def __init__(self, fn, account=None):
        self._fn = fn
        self._account = _resolve_account(account, "RequestCallback")
        self._extera_request_delegate = True

    def run(self, response, error):
        from android_utils import safe_call
        with hook_scope(self._account):
            safe_call(self._fn, response, error)


def _is_request_delegate(fn) -> bool:
    if getattr(fn, "_extera_request_delegate", False):
        return True
    return not callable(fn) and hasattr(fn, "run")


def _request_to_json(request) -> str:
    if isinstance(request, dict):
        return json.dumps(request, ensure_ascii=False, default=str)
    if hasattr(request, "to_dict"):
        try:
            return json.dumps(request.to_dict(), ensure_ascii=False, default=str)
        except Exception:
            pass
    if isinstance(request, str):
        return json.dumps({"request": request}, ensure_ascii=False)
    return json.dumps({"request": str(request)}, ensure_ascii=False)


def _send_raw(request, delegate) -> int:
    import base64
    import tl_runtime
    native = _native("send_raw_request")
    if native is None:
        _not_available("send_raw_request")
        return 0
    try:
        data = tl_runtime.serialize(request)
    except Exception as e:
        _log(f"send_request serialize failed: {e}")
        return 0
    callback_id = extera_host_register_callback(delegate)
    _raw_types[callback_id] = tl_runtime.result_type(request)
    _raw_names[callback_id] = type(request).__name__
    request_id = int(native(base64.b64encode(data).decode("ascii"), callback_id))
    _log(f"request {type(request).__name__} sent ({len(data)} bytes, id {request_id})")
    return request_id


_raw_types = {}
_raw_names = {}


def send_request(request, fn=None, account=None) -> int:
    import tl_runtime
    if tl_runtime.is_request(request):
        resolved = _resolve_account(account, "send_request")
        if fn is None:
            delegate = RequestCallback(lambda *args: None, account=resolved)
        elif _is_request_delegate(fn):
            delegate = fn
        else:
            delegate = RequestCallback(fn, account=resolved)
        return _send_raw(request, delegate)
    native = _native("send_request")
    if native is None:
        _not_available("send_request")
        return 0
    resolved = _resolve_account(account, "send_request")
    delegate = fn if _is_request_delegate(fn) else RequestCallback(fn, account=resolved)
    callback_id = extera_host_register_callback(delegate)
    try:
        return int(native(_request_to_json(request), callback_id))
    except Exception as e:
        _log(f"send_request failed: {e}")
        return 0


_callback_counter = [0]
_callback_lock = threading.Lock()
_callbacks = {}


_callback_owners = {}


def extera_host_register_callback(delegate) -> str:
    import extera_host
    owner = extera_host.owner_now()
    with _callback_lock:
        _callback_counter[0] += 1
        callback_id = f"cb{_callback_counter[0]}"
        _callbacks[callback_id] = delegate
        _callback_owners[callback_id] = owner
    return callback_id


def extera_host_dispatch_callback(callback_id, response, error, raw=None, code=0):
    import extera_host
    owner = _callback_owners.pop(callback_id, None)
    with extera_host.owner_scope(owner):
        return _dispatch_callback(callback_id, response, error, raw, code)


def _dispatch_callback(callback_id, response, error, raw=None, code=0):
    delegate = _callbacks.pop(callback_id, None)
    result_type = _raw_types.pop(callback_id, None)
    name = _raw_names.pop(callback_id, "request")
    if delegate is None:
        return False
    if raw is not None or (result_type and error):
        import base64
        import tl_runtime
        if error:
            _log(f"request {name} failed: {code} {error}")
            delegate.run(None, tl_runtime.TL_error(int(code or 0), str(error)))
            return True
        try:
            response = tl_runtime.deserialize(base64.b64decode(raw), result_type)
        except Exception as e:
            _log(f"request {name} response parse failed: {e}")
            delegate.run(None, tl_runtime.TL_error(-1, f"PARSE_FAILED: {e}"))
            return True
        _log(f"request {name} done: {type(response).__name__}")
        delegate.run(response, None)
        return True
    delegate.run(response, error)
    return True


# Controllers

def get_account_instance(account=None):
    return _compat.Stub("AccountInstance")


def get_last_fragment():
    import tgnet_bridge
    return tgnet_bridge.current_fragment()


def get_current_fragment():
    return get_last_fragment()


def _stub_controller(name):
    def getter(account=None):
        return _compat.Stub(name)
    return getter


def get_messages_controller(account=None):
    import tgnet_bridge
    return tgnet_bridge.MessagesController.getInstance(account or 0)
get_contacts_controller = _stub_controller("ContactsController")
get_media_data_controller = _stub_controller("MediaDataController")
def get_connections_manager(account=None):
    import tgnet_bridge
    return tgnet_bridge.ConnectionsManager.getInstance(account or 0)
get_location_controller = _stub_controller("LocationController")
get_notifications_controller = _stub_controller("NotificationsController")
get_messages_storage = _stub_controller("MessagesStorage")
get_send_messages_helper = _stub_controller("SendMessagesHelper")
get_file_loader = _stub_controller("FileLoader")
get_secret_chat_helper = _stub_controller("SecretChatHelper")
get_download_controller = _stub_controller("DownloadController")
get_notifications_settings = _stub_controller("SharedPrefs")
get_notification_center = _stub_controller("NotificationCenter")
get_media_controller = _stub_controller("MediaController")
def get_user_config(account=None):
    import tgnet_bridge
    return tgnet_bridge.UserConfig.getInstance(account or 0)
get_locale_controller = _stub_controller("LocaleController")


def get_current_dialog_id() -> int:
    try:
        return int(_extera.current_peer())
    except Exception:
        return 0


# Sending

def _reply_id(value):
    if value is None:
        return 0
    if isinstance(value, (int, float)):
        return int(value)
    if isinstance(value, dict):
        return int(value.get("id", 0) or 0)
    return int(getattr(value, "id", 0) or 0)


def _peer_id(peer) -> int:
    if peer is None:
        return 0
    if isinstance(peer, (int, float)):
        return int(peer)
    if isinstance(peer, dict):
        return int(peer.get("dialog_id", 0) or peer.get("peer_id", 0) or 0)
    for attr in ("dialog_id", "user_id", "chat_id", "channel_id"):
        value = getattr(peer, attr, None)
        if value:
            try:
                return int(value)
            except Exception:
                pass
    return 0


def _parse_entities(text, parse_mode, is_caption=False):
    if not parse_mode or text is None:
        return str(text or ""), []
    from text_formatting import parse_text
    parsed = parse_text(str(text), parse_mode, is_caption=is_caption)
    key = "caption" if is_caption else "message"
    return parsed.get(key, str(text)), parsed.get("entities") or []


def send_text(peer_id, text, replyToMsg=None, parse_mode=None, account=None):
    resolved = _resolve_account(account, "send_text")
    plain, entities = _parse_entities(text, parse_mode)
    native = _native("send_message")
    if native is None:
        _not_available("send_text")
        return
    native(_peer_id(peer_id), plain, json.dumps(entities), _reply_id(replyToMsg))


def send_message(params: dict, parse_mode=None, account=None):
    if not isinstance(params, dict):
        params = {"peer": getattr(params, "peer", None),
                  "message": getattr(params, "message", ""),
                  "replyToMsg": getattr(params, "replyToMsg", None)}
    params = dict(params or {})
    peer = params.pop("peer", params.pop("peer_id", 0))
    message = params.pop("message", None)
    caption = params.pop("caption", None)
    reply = params.pop("replyToMsg", None)
    path = params.pop("path", None)
    photo = params.pop("photo", None)
    document = params.pop("document", None)
    if photo is not None or document is not None or path is not None:
        kind = "photo" if photo is not None else "document"
        media_path = path if path is not None else (photo if photo is not None else document)
        send_media(_peer_id(peer), kind, media_path, caption or message,
                   parse_mode=parse_mode, replyToMsg=reply, account=account)
        return
    text = message if message is not None else (caption or "")
    plain, entities = _parse_entities(text, parse_mode, is_caption=(message is None))
    native = _native("send_message")
    if native is None:
        _not_available("send_message")
        return
    native(_peer_id(peer), plain, json.dumps(entities), _reply_id(reply))


def send_media(peer_id, kind, path, caption=None, parse_mode=None,
               replyToMsg=None, high_quality=False, account=None):
    _resolve_account(account, "send_media")
    plain, entities = _parse_entities(caption, parse_mode, is_caption=True)
    native = _native("send_media")
    if native is None:
        _not_available(f"send_{kind}")
        return
    native(_peer_id(peer_id), str(kind), str(path), plain,
           json.dumps(entities), _reply_id(replyToMsg), bool(high_quality))


def send_photo(peer_id, path, caption=None, high_quality=False, parse_mode=None,
               replyToMsg=None, account=None):
    send_media(peer_id, "photo", path, caption, parse_mode=parse_mode,
               replyToMsg=replyToMsg, high_quality=high_quality, account=account)


def send_video(peer_id, path, caption=None, parse_mode=None,
               replyToMsg=None, account=None):
    send_media(peer_id, "video", path, caption, parse_mode=parse_mode,
               replyToMsg=replyToMsg, account=account)


def send_document(peer_id, path, caption=None, parse_mode=None,
                  replyToMsg=None, account=None, replyToTopMsg=None):
    send_media(peer_id, "document", path, caption, parse_mode=parse_mode,
               replyToMsg=replyToMsg, account=account)


def send_audio(peer_id, path, caption=None, parse_mode=None,
               replyToMsg=None, account=None):
    send_media(peer_id, "audio", path, caption, parse_mode=parse_mode,
               replyToMsg=replyToMsg, account=account)


def edit_message(message_obj, text=None, file_path=None, with_spoiler=False,
                 parse_mode=None, account=None):
    resolved = _resolve_account(account, "edit_message")
    msg_id = _reply_id(message_obj)
    if isinstance(message_obj, dict):
        dialog = int(message_obj.get("dialog_id", 0) or 0)
    else:
        dialog = int(getattr(message_obj, "dialog_id", 0) or 0)
    if not dialog:
        dialog = get_current_dialog_id()
    if file_path is not None:
        _not_available("edit_message media")
        return None
    if text is None:
        raise ValueError("edit_message requires text= or file_path=")
    plain, entities = _parse_entities(text, parse_mode)
    native = _native("edit_message")
    if native is None:
        _not_available("edit_message")
        return None
    try:
        return int(native(dialog, msg_id, plain, json.dumps(entities)))
    except Exception as e:
        _log(f"edit_message failed: {e}")
        return None


# AccountClient

class AccountClient:
    def __init__(self, account=None):
        self._account = None if account is None else int(account)

    def __call__(self, account=None) -> "AccountClient":
        return AccountClient(account)

    @property
    def account(self) -> int:
        return _resolve_scoped_account(self._account)

    def get_account_instance(self):
        return get_account_instance(self.account)

    def get_messages_controller(self):
        return get_messages_controller(self.account)

    def get_contacts_controller(self):
        return get_contacts_controller(self.account)

    def get_media_data_controller(self):
        return get_media_data_controller(self.account)

    def get_connections_manager(self):
        return get_connections_manager(self.account)

    def get_location_controller(self):
        return get_location_controller(self.account)

    def get_notifications_controller(self):
        return get_notifications_controller(self.account)

    def get_messages_storage(self):
        return get_messages_storage(self.account)

    def get_send_messages_helper(self):
        return get_send_messages_helper(self.account)

    def get_file_loader(self):
        return get_file_loader(self.account)

    def get_secret_chat_helper(self):
        return get_secret_chat_helper(self.account)

    def get_download_controller(self):
        return get_download_controller(self.account)

    def get_notifications_settings(self):
        return get_notifications_settings(self.account)

    def get_notification_center(self):
        return get_notification_center(self.account)

    def get_user_config(self):
        return get_user_config(self.account)

    def get_media_controller(self):
        return get_media_controller()

    def get_last_fragment(self):
        return get_last_fragment()

    def send_request(self, request, fn) -> int:
        return send_request(request, fn, account=self.account)

    def run_on_queue(self, fn, queue: str = PLUGINS_QUEUE, delay: int = 0,
                     delay_ms: int = None):
        return run_on_queue(fn, queue=queue, delay=delay, delay_ms=delay_ms)

    def send_text(self, peer_id, text, replyToMsg=None, parse_mode=None):
        return send_text(peer_id, text, replyToMsg=replyToMsg,
                         parse_mode=parse_mode, account=self.account)

    def send_message(self, params: dict, parse_mode=None):
        return send_message(params, parse_mode=parse_mode, account=self.account)

    def send_photo(self, peer_id, path, caption=None, high_quality=False,
                   parse_mode=None, replyToMsg=None):
        return send_photo(peer_id, path, caption=caption, high_quality=high_quality,
                          parse_mode=parse_mode, replyToMsg=replyToMsg,
                          account=self.account)

    def send_video(self, peer_id, path, caption=None, parse_mode=None, replyToMsg=None):
        return send_video(peer_id, path, caption=caption, parse_mode=parse_mode,
                          replyToMsg=replyToMsg, account=self.account)

    def send_document(self, peer_id, path, caption=None, parse_mode=None, replyToMsg=None):
        return send_document(peer_id, path, caption=caption, parse_mode=parse_mode,
                             replyToMsg=replyToMsg, account=self.account)

    def send_audio(self, peer_id, path, caption=None, parse_mode=None, replyToMsg=None):
        return send_audio(peer_id, path, caption=caption, parse_mode=parse_mode,
                          replyToMsg=replyToMsg, account=self.account)

    def edit_message(self, message_obj, text=None, file_path=None,
                     with_spoiler=False, parse_mode=None):
        return edit_message(message_obj, text=text, file_path=file_path,
                            with_spoiler=with_spoiler, parse_mode=parse_mode,
                            account=self.account)


def get_client(account=None) -> AccountClient:
    return AccountClient(account)


# NotificationCenter

class NotificationCenterDelegate:
    def didReceivedNotification(self, notification_id, account, args):
        pass

    @property
    def java(self):
        return self

    def start_observing(self, notification_id: int, account=None):
        import extera_host
        extera_host.add_observer(self, int(notification_id))
        return self

    def stop_observing(self, notification_id: int, account=None):
        import extera_host
        extera_host.remove_observer(self, int(notification_id))
        return self


from android_utils import log, run_on_ui_thread  # noqa: E402,F401
