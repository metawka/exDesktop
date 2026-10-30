import itertools
import json
from typing import Any, Callable, List, Optional

import _extera

_counter = itertools.count(1)
_builders = {}


class AlertDialogBuilder:
    ALERT_TYPE_MESSAGE = 0
    ALERT_TYPE_SPINNER = 1
    ALERT_TYPE_LOADING = 2
    BUTTON_POSITIVE = -1
    BUTTON_NEGATIVE = -2
    BUTTON_NEUTRAL = -3

    def __init__(self, context: Any = None, progress_style: int = 0, *args):
        self._id = next(_counter)
        self._title = ""
        self._message = ""
        self._buttons = {}
        self._items = []
        self._items_listener = None
        self._shown = False

    def set_title(self, title: str):
        self._title = str(title or "")
        return self

    def set_message(self, message: str):
        self._message = str(message or "")
        return self

    def _button(self, which, text, listener):
        self._buttons[which] = (str(text or ""), listener)
        return self

    def set_positive_button(self, text: str, listener: Optional[Callable] = None):
        return self._button(self.BUTTON_POSITIVE, text, listener)

    def set_negative_button(self, text: str, listener: Optional[Callable] = None):
        return self._button(self.BUTTON_NEGATIVE, text, listener)

    def set_neutral_button(self, text: str, listener: Optional[Callable] = None):
        return self._button(self.BUTTON_NEUTRAL, text, listener)

    def set_items(self, items: List[str], listener: Optional[Callable] = None,
                  *args):
        self._items = [str(x) for x in items]
        self._items_listener = listener
        return self

    def show(self):
        _builders[self._id] = self
        payload = {
            "id": self._id,
            "title": self._title,
            "message": self._message,
            "items": self._items,
            "buttons": [
                {"which": which, "text": text}
                for which, (text, _) in sorted(self._buttons.items())
            ],
        }
        _extera.show_alert(json.dumps(payload, ensure_ascii=False))
        self._shown = True
        return self

    def create(self):
        return self

    def dismiss(self):
        _builders.pop(self._id, None)

    def get_dialog(self):
        return self

    def __getattr__(self, item):
        if item.startswith("__"):
            raise AttributeError(item)
        return lambda *args, **kwargs: self


def dispatch(builder_id, which):
    builder = _builders.get(builder_id)
    if builder is None:
        return
    if which >= 0:
        listener = builder._items_listener
    else:
        listener = builder._buttons.get(which, ("", None))[1]
    try:
        if listener is not None:
            try:
                listener(builder, which)
            except TypeError:
                listener()
    finally:
        _builders.pop(builder_id, None)
