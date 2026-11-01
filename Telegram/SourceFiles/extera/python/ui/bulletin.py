from typing import Any, Callable, Optional

import _extera


class BulletinHelper:
    DURATION_SHORT = 1500
    DURATION_LONG = 2750
    DURATION_PROLONG = 5000

    @staticmethod
    def show_info(message: str, fragment: Any = None, *args, **kwargs):
        _extera.toast(str(message))

    @staticmethod
    def show_error(message: str, fragment: Any = None, *args, **kwargs):
        _extera.toast(str(message))

    @staticmethod
    def show_success(message: str, fragment: Any = None, *args, **kwargs):
        _extera.toast(str(message))

    @staticmethod
    def show_simple(text: str, icon: Any = None, fragment: Any = None,
                    duration: Optional[int] = None, *args, **kwargs):
        _extera.toast(str(text))

    @staticmethod
    def show_two_line(title: str, subtitle: str, icon: Any = None,
                      fragment: Any = None, duration: Optional[int] = None,
                      *args, **kwargs):
        _extera.toast(str(title) + (": " + str(subtitle) if subtitle else ""))

    @staticmethod
    def show_with_button(text: str, icon: Any = None, button_text: str = "",
                         on_click: Optional[Callable] = None, fragment: Any = None,
                         duration: Optional[int] = None, *args, **kwargs):
        _extera.toast(str(text))

    @staticmethod
    def show_undo(text: str, on_undo: Optional[Callable] = None,
                  on_action: Optional[Callable] = None, subtitle: Optional[str] = None,
                  fragment: Any = None, *args, **kwargs):
        _extera.toast(str(text))

    @staticmethod
    def show_copied_to_clipboard(fragment: Any = None, *args, **kwargs):
        _extera.toast("Copied to clipboard")

    @staticmethod
    def show_link_copied(fragment: Any = None, *args, **kwargs):
        _extera.toast("Link copied to clipboard")

    @staticmethod
    def show_file_saved_to_gallery(fragment: Any = None, *args, **kwargs):
        _extera.toast("Saved to gallery")

    @staticmethod
    def show_file_saved_to_downloads(file_type: str = "DOCUMENT", amount: int = 1,
                                     fragment: Any = None, *args, **kwargs):
        _extera.toast("Saved to downloads")
