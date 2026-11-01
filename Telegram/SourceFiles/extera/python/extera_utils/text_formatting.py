import re
import struct

import text_formatting as _base
from text_formatting import RawEntity, TLEntityType, parse_text

BOLD_DELIM = "**"
ITALIC_DELIM = "__"
UNDERLINE_DELIM = "--"
STRIKE_DELIM = "~~"
SPOILER_DELIM = "||"
CODE_DELIM = "`"
PRE_DELIM = "```"
BLOCKQUOTE_DELIM = ">"
BLOCKQUOTE_EXPANDABLE_DELIM = "**>"
BLOCKQUOTE_EXPANDABLE_END_DELIM = "||"
OPENING_TAG = "<{}>"
CLOSING_TAG = "</{}>"
URL_MARKUP = '<a href="{}">{}</a>'
EMOJI_MARKUP = '<emoji id={}>{}</emoji>'
FIXED_WIDTH_DELIMS = [CODE_DELIM, PRE_DELIM]
MARKDOWN_ESCAPABLE_CHARS = r"\`*_~|>[]()"
MARKDOWN_RE = re.compile(
    r"({d})|(!?)\[(.+?)\]\((.+?)\)".format(d="|".join(
        re.escape(d) for d in [
            PRE_DELIM, CODE_DELIM, STRIKE_DELIM, UNDERLINE_DELIM,
            ITALIC_DELIM, BOLD_DELIM, SPOILER_DELIM])))
ESCAPED_MARKDOWN_RE = re.compile(r"\\([\\`*_~|>\[\]()])")


def add_surrogates(text: str) -> str:
    return re.sub(
        r"[\U00010000-\U0010FFFF]",
        lambda m: "".join(
            chr(c) for c in struct.unpack("<HH", m.group().encode("utf-16le"))),
        text)


def remove_surrogates(text: str) -> str:
    return text.encode("utf-16", "surrogatepass").decode("utf-16")


def replace_once(source: str, old: str, new: str, start: int):
    return source[:start] + source[start:].replace(old, new, 1)


class HTML:
    def __init__(self, client=None):
        self.client = client

    def parse(self, text: str):
        return parse_text(text, "HTML")

    @staticmethod
    def unparse(text: str, entities: list):
        return text


class Markdown:
    def __init__(self, client=None):
        self.client = client
        self.html = HTML(client)

    def parse(self, text: str, strict: bool = False):
        return parse_text(text, "Markdown")

    @staticmethod
    def unparse(text: str, entities: list):
        return text


Parser = getattr(_base, "_EntityHTMLParser", None)
