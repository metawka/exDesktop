import itertools
import json
from typing import Any, Callable, List, Optional

import _extera

_counter = itertools.count(1)
_builders = {}


class AlertDialogBuilder:
    ALERT_TYPE_MESSAGE = 0
    ALERT_TYPE_LOADING = 2
    ALERT_TYPE_SPINNER = 3
    BUTTON_POSITIVE = -1
    BUTTON_NEGATIVE = -2
    BUTTON_NEUTRAL = -3

    def __init__(self, context: Any = None, alert_type: int = ALERT_TYPE_MESSAGE,
                 resources_provider: Any = None, *, progress_style: Optional[int] = None):
        if progress_style is not None:
            if alert_type != self.ALERT_TYPE_MESSAGE and alert_type != progress_style:
                raise TypeError("Conflicting alert_type and progress_style")
            alert_type = progress_style
        self._id = next(_counter)
        self._alert_type = int(alert_type)
        self._title = ""
        self._message = ""
        self._buttons = {}
        self._items = []
        self._items_listener = None
        self._on_dismiss = None
        self._on_cancel = None
        self._on_back = None
        self._cancelable = None
        self._canceled_on_touch_outside = None
        self._progress = None
        self._shown = False
        self._exd_extra = None

    def set_title(self, text):
        self._title = str(text or "")
        return self

    def set_message(self, text):
        self._message = str(text or "")
        return self

    def set_items(self, items: List[str], listener: Optional[Callable] = None,
                  icons=None, *args):
        self._items = [str(x) for x in items]
        self._items_listener = listener
        return self

    def set_positive_button(self, text: str, listener: Optional[Callable] = None):
        self._buttons[self.BUTTON_POSITIVE] = (str(text or ""), listener)
        return self

    def set_negative_button(self, text: str, listener: Optional[Callable] = None):
        self._buttons[self.BUTTON_NEGATIVE] = (str(text or ""), listener)
        return self

    def set_neutral_button(self, text: str, listener: Optional[Callable] = None):
        self._buttons[self.BUTTON_NEUTRAL] = (str(text or ""), listener)
        return self

    def make_button_red(self, which):
        return self

    def set_cancelable(self, cancelable):
        self._cancelable = bool(cancelable)
        return self

    def set_canceled_on_touch_outside(self, cancel):
        self._canceled_on_touch_outside = bool(cancel)
        return self

    def set_progress(self, progress):
        self._progress = int(progress)
        return self

    def set_view(self, view, height=-2):
        return self

    def set_top_animation(self, res_id, size, auto_repeat, background_color):
        return self

    def set_top_animation_is_new(self, is_new):
        return self

    def set_top_image(self, res_id, background_color):
        return self

    def set_top_drawable(self, drawable, background_color):
        return self

    def set_dialog_button_color_key(self, theme_key):
        return self

    def set_message_text_view_clickable(self, clickable):
        return self

    def set_blurred_background(self, blur, blur_behind_if_possible=True):
        return self

    def set_dim_enabled(self, enabled):
        return self

    def get_context(self):
        return None

    def set_on_dismiss_listener(self, listener):
        self._on_dismiss = listener
        return self

    def set_on_cancel_listener(self, listener):
        self._on_cancel = listener
        return self

    def set_on_back_button_listener(self, listener):
        self._on_back = listener
        return self

    def _payload(self):
        return {
            "id": self._id,
            "title": self._title,
            "message": self._message,
            "alert_type": self._alert_type,
            "items": self._items,
            "cancelable": self._cancelable,
            "exd": self._exd_extra,
            "buttons": [
                {"which": which, "text": text}
                for which, (text, _) in sorted(self._buttons.items())
            ],
        }

    def create(self):
        return self

    def show(self):
        _builders[self._id] = self
        try:
            import extera_host
            _owners[self._id] = extera_host.owner_now()
        except Exception:
            _owners[self._id] = "<unknown>"
        _extera.show_alert(json.dumps(self._payload(), ensure_ascii=False))
        self._shown = True
        return self

    def dismiss(self):
        if _builders.pop(self._id, None) is not None:
            _dispatch_dismiss(self._id, "dismiss")
        return self

    def get_dialog(self):
        return self

    def get_button(self, which):
        return None

    def __getattr__(self, item):
        if item.startswith("__"):
            raise AttributeError(item)
        return lambda *args, **kwargs: self


def _scope(builder_id):
    import extera_host
    return extera_host.owner_scope(_owners.get(builder_id))


_owners = {}


def _dispatch_dismiss(builder_id, kind):
    builder = _builders.pop(builder_id, None)
    if builder is None:
        return
    listener = {"dismiss": builder._on_dismiss,
                "cancel": builder._on_cancel,
                "back": builder._on_back}.get(kind)
    if listener is not None:
        from android_utils import safe_call
        with _scope(builder_id):
            safe_call(listener, builder)
    _owners.pop(builder_id, None)


def dispatch(builder_id, which):
    builder = _builders.get(builder_id)
    if builder is None:
        return
    if which == "dismiss" or which == "cancel" or which == "back":
        _dispatch_dismiss(builder_id, which)
        return
    if which >= 0:
        listener = builder._items_listener
    else:
        listener = builder._buttons.get(which, ("", None))[1]
    if listener is not None:
        from android_utils import safe_call
        with _scope(builder_id):
            safe_call(listener, builder, which)
    if which < 0:
        _builders.pop(builder_id, None)
        _owners.pop(builder_id, None)
