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
        self._red = set()
        self._view = None
        self._view_height = -2
        self._context = context

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

    def make_button_red(self, button_type):
        self._red.add(int(button_type))
        return self

    def set_cancelable(self, cancelable):
        self._cancelable = bool(cancelable)
        self._update({"cancelable": self._cancelable})
        return self

    def set_canceled_on_touch_outside(self, cancel):
        self._canceled_on_touch_outside = bool(cancel)
        self._update({"outside": self._canceled_on_touch_outside})
        return self

    def set_progress(self, progress):
        self._progress = max(0, min(100, int(progress)))
        self._update({"progress": self._progress})
        return self

    def _update(self, values):
        if not self._shown or self._id not in _builders:
            return
        data = {"id": self._id}
        data.update(values)
        try:
            _extera.update_alert(json.dumps(data))
        except Exception:
            pass

    def set_view(self, view, height=-2):
        self._view = view
        self._view_height = height
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
        if self._context is not None:
            return self._context
        import _compat
        return _compat.SHIMS.get("org.telegram.messenger", {}).get("ApplicationLoader").applicationContext

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
        payload = self._base_payload()
        if self._view is not None:
            import android_views
            tree = android_views._node(self._view)
            with android_views._lock:
                android_views._ids[0] += 1
                view_id = android_views._ids[0]
            android_views._mark_live(self._view, view_id)
            payload["view"] = tree
            payload["view_id"] = view_id
        return payload

    def _base_payload(self):
        return {
            "id": self._id,
            "title": self._title,
            "message": self._message,
            "alert_type": self._alert_type,
            "items": self._items,
            "cancelable": True if self._cancelable is None else self._cancelable,
            "outside": self._canceled_on_touch_outside if self._canceled_on_touch_outside is not None
            else (True if self._cancelable is None else self._cancelable),
            "progress": -1 if self._progress is None else self._progress,
            "red": sorted(self._red),
            "exd": self._exd_extra,
            "buttons": [
                {"which": which, "text": text}
                for which, (text, _) in sorted(self._buttons.items())
            ],
        }

    def create(self):
        return self

    def show(self):
        if self._view is not None:
            import exd_webview
            web = exd_webview.find_webview(self._view)
            if web is not None:
                web.open(self._title)
                return self
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
        if self._id in _builders:
            self._update({"dismiss": True})
            if self._id in _builders:
                _dispatch_dismiss(self._id, "dismiss")
        return None

    def get_dialog(self):
        return self

    def getContainerView(self):
        if self._view is None:
            import android_views
            self._view = android_views.FrameLayout(None)
        return self._view

    def get_button(self, button_type):
        entry = self._buttons.get(int(button_type))
        if entry is None:
            return None
        return _ButtonView(self, int(button_type))


class _ButtonView:
    def __init__(self, builder, which):
        self._builder = builder
        self._which = which

    def setText(self, text):
        label, listener = self._builder._buttons[self._which]
        self._builder._buttons[self._which] = (str(text), listener)

    def getText(self):
        return self._builder._buttons[self._which][0]

    def performClick(self):
        dispatch(self._builder._id, self._which)
        return True

    def setEnabled(self, enabled):
        return None

    def setTextColor(self, color):
        if isinstance(color, int) and (color & 0xFFFFFF) and ((color >> 16) & 0xFF) > 0xC0                 and ((color >> 8) & 0xFF) < 0x70:
            self._builder._red.add(self._which)


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
