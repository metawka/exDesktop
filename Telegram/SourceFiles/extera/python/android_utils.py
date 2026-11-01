import sys
from typing import Any, Callable

import _compat
import _extera


# Log

def log(data):
    if isinstance(data, (str, int, float, bool)) or data is None:
        message = str(data)
    else:
        try:
            message = repr(data)
        except Exception:
            message = object.__repr__(data)
    try:
        _extera.log(message)
    except Exception:
        print(f"[extera:sdk] {message}", file=sys.stderr)


def safe_call(fn, *args, **kwargs):
    if fn is None:
        return None
    try:
        return fn(*args, **kwargs)
    except Exception:
        import traceback
        log("callback failed:\n" + traceback.format_exc())
        return None


# Runnables

class _Runnable:
    def __init__(self, fn):
        self._fn = fn

    def run(self):
        safe_call(self._fn)


def R(fn):
    return _Runnable(fn)


def run_on_ui_thread(func, delay=0):
    if func is None:
        return
    _extera.run_on_main(lambda: safe_call(func), int(delay or 0))


# Listeners

class OnClickListener:
    def __init__(self, fn):
        self.fn = fn

    def onClick(self, view=None):
        return safe_call(self.fn, view)


class OnLongClickListener:
    def __init__(self, fn):
        self.fn = fn

    def onLongClick(self, view=None):
        return bool(safe_call(self.fn, view))


class OnTouchListener:
    def __init__(self, fn):
        self.fn = fn

    def onTouch(self, view=None, event=None):
        return bool(safe_call(self.fn, view, event))


class OnKeyListener:
    def __init__(self, fn):
        self.fn = fn

    def onKey(self, view=None, key_code=0, event=None):
        return bool(safe_call(self.fn, view, key_code, event))


def OnSeekBarChangeListener(on_progress_changed, on_start=None, on_stop=None):
    class _Listener:
        def onProgressChanged(self, seek_bar, progress, from_user):
            safe_call(on_progress_changed, seek_bar, progress, from_user)

        def onStartTrackingTouch(self, seek_bar):
            safe_call(on_start, seek_bar)

        def onStopTrackingTouch(self, seek_bar):
            safe_call(on_stop, seek_bar)
    return _Listener()


# Host

def get_context():
    return _compat._Context("applicationContext")


def get_activity():
    return _compat._Context("LaunchActivity")


def copy_to_clipboard(text):
    _extera.copy(str(text))
    try:
        from ui.bulletin import BulletinHelper
        BulletinHelper.show_copied_to_clipboard()
    except Exception:
        pass
    return True


def dp(value):
    return int(value)


def __getattr__(name: str) -> Any:
    return _compat.Stub("android_utils." + name)
