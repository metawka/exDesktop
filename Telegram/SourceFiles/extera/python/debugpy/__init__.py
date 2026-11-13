__version__ = "0.0"


def listen(*args, **kwargs):
    return ("127.0.0.1", 0)


def connect(*args, **kwargs):
    return None


def wait_for_client(*args, **kwargs):
    return None


def is_client_connected():
    return False


def breakpoint():
    return None


def configure(*args, **kwargs):
    return None


def log_to(*args, **kwargs):
    return None


def trace_this_thread(*args, **kwargs):
    return None
