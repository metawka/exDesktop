from exd_webview import WebApp

__all__ = ["WebApp", "show_html", "show_url"]


def show_html(html, title="", width=420, height=640, on_message=None, on_close=None):
    return WebApp(title, html=html, width=width, height=height, on_message=on_message, on_close=on_close).show()


def show_url(url, title="", width=420, height=640, on_message=None, on_close=None):
    return WebApp(title, url=url, width=width, height=height, on_message=on_message, on_close=on_close).show()
