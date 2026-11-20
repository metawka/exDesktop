from typing import Any, Callable, Optional

from ui.alert import AlertDialogBuilder


def show_alert(title: Any = "", message: Any = "", button: str = "OK",
               on_click: Optional[Callable] = None, *args, **kwargs):
    builder = AlertDialogBuilder(None)
    builder.set_title(str(title or ""))
    builder.set_message(str(message or ""))
    builder.set_positive_button(str(button or "OK"), (lambda dialog, which: on_click()) if on_click else None)
    builder.show()
    return builder


def show_confirm(title: Any = "", message: Any = "", on_confirm: Optional[Callable] = None,
                 positive: str = "OK", negative: str = "Cancel", *args, **kwargs):
    builder = AlertDialogBuilder(None)
    builder.set_title(str(title or ""))
    builder.set_message(str(message or ""))
    builder.set_positive_button(str(positive), (lambda dialog, which: on_confirm()) if on_confirm else None)
    builder.set_negative_button(str(negative), None)
    builder.show()
    return builder
