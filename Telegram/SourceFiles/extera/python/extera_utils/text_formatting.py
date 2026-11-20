import html as _html
import re
import struct
from html.parser import HTMLParser

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


def _raw(entity):
    if isinstance(entity, RawEntity):
        return entity
    if isinstance(entity, dict):
        return RawEntity(TLEntityType.from_(entity.get("type")), int(entity.get("offset") or 0),
                         int(entity.get("length") or 0), entity.get("language"), entity.get("url"),
                         int(entity["document_id"]) if entity.get("document_id") is not None else None,
                         bool(entity.get("collapsed")))
    kind = TLEntityType.from_(entity)
    return RawEntity(kind, int(getattr(entity, "offset", 0) or 0), int(getattr(entity, "length", 0) or 0),
                     getattr(entity, "language", None), getattr(entity, "url", None),
                     getattr(entity, "document_id", None), bool(getattr(entity, "collapsed", False)))


def _wrap(text, entities, opener, closer, escape=None):
    escape = escape or (lambda chunk: chunk)
    units = add_surrogates(text)
    inserts = []
    for index, entity in enumerate(_raw(e) for e in entities or ()):
        start, end = entity.offset, entity.offset + entity.length
        inserts.append((start, 1, -end, index, opener(entity)))
        inserts.append((end, 0, -start, -index, closer(entity)))
    inserts.sort(key=lambda item: (item[0], item[1], item[2], item[3]))
    out = []
    position = 0
    for offset, _, _, _, markup in inserts:
        out.append(escape(remove_surrogates(units[position:offset])))
        out.append(markup)
        position = offset
    out.append(escape(remove_surrogates(units[position:])))
    return "".join(out)


_HTML_TAG = {
    TLEntityType.BOLD: "b", TLEntityType.ITALIC: "i", TLEntityType.UNDERLINE: "u",
    TLEntityType.STRIKETHROUGH: "s", TLEntityType.SPOILER: "spoiler", TLEntityType.CODE: "code",
}


def _html_open(entity):
    kind = entity.type
    if kind in _HTML_TAG:
        return OPENING_TAG.format(_HTML_TAG[kind])
    if kind is TLEntityType.PRE:
        return '<pre language="%s">' % _html.escape(entity.language, quote=True) if entity.language else "<pre>"
    if kind is TLEntityType.TEXT_LINK:
        return '<a href="%s">' % _html.escape(entity.url or "", quote=True)
    if kind is TLEntityType.CUSTOM_EMOJI:
        return "<emoji id=%s>" % int(entity.document_id or 0)
    if kind is TLEntityType.BLOCKQUOTE:
        return "<blockquote expandable>" if entity.collapsed else "<blockquote>"
    return ""


def _html_close(entity):
    kind = entity.type
    if kind in _HTML_TAG:
        return CLOSING_TAG.format(_HTML_TAG[kind])
    return {TLEntityType.PRE: "</pre>", TLEntityType.TEXT_LINK: "</a>", TLEntityType.CUSTOM_EMOJI: "</emoji>",
            TLEntityType.BLOCKQUOTE: "</blockquote>"}.get(kind, "")


class Parser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.text = ""
        self.entities = []
        self.tag_entities = {}
        self._inner = _base._EntityHTMLParser()

    def handle_starttag(self, tag, attrs):
        self._inner.handle_starttag(tag, attrs)

    def handle_data(self, data):
        self._inner.handle_data(data)

    def handle_endtag(self, tag):
        self._inner.handle_endtag(tag)

    def close(self):
        super().close()
        self.text, self.entities = self._inner.result()


class HTML:
    def __init__(self, client=None):
        self.client = client

    @staticmethod
    def parse(text: str) -> dict:
        return parse_text(text, "HTML")

    @staticmethod
    def unparse(text: str, entities: list) -> str:
        return _wrap(text, entities, _html_open, _html_close, lambda chunk: _html.escape(chunk, quote=False))


_MD_DELIM = {
    TLEntityType.BOLD: BOLD_DELIM, TLEntityType.ITALIC: ITALIC_DELIM, TLEntityType.UNDERLINE: UNDERLINE_DELIM,
    TLEntityType.STRIKETHROUGH: STRIKE_DELIM, TLEntityType.SPOILER: SPOILER_DELIM, TLEntityType.CODE: CODE_DELIM,
}


def _md_open(entity):
    kind = entity.type
    if kind in _MD_DELIM:
        return _MD_DELIM[kind]
    if kind is TLEntityType.PRE:
        return PRE_DELIM + (entity.language or "") + "\n"
    if kind in (TLEntityType.TEXT_LINK, TLEntityType.CUSTOM_EMOJI):
        return "!["[1 if kind is TLEntityType.TEXT_LINK else 0:]
    if kind is TLEntityType.BLOCKQUOTE:
        return BLOCKQUOTE_EXPANDABLE_DELIM if entity.collapsed else BLOCKQUOTE_DELIM
    return ""


def _md_close(entity):
    kind = entity.type
    if kind in _MD_DELIM:
        return _MD_DELIM[kind]
    if kind is TLEntityType.PRE:
        return "\n" + PRE_DELIM
    if kind is TLEntityType.TEXT_LINK:
        return "](%s)" % (entity.url or "")
    if kind is TLEntityType.CUSTOM_EMOJI:
        return "](tg://emoji?id=%s)" % int(entity.document_id or 0)
    if kind is TLEntityType.BLOCKQUOTE and entity.collapsed:
        return BLOCKQUOTE_EXPANDABLE_END_DELIM
    return ""


class Markdown:
    def __init__(self, client=None):
        self.client = client
        self.html = HTML(client)

    @staticmethod
    def protect_escaped_chars(text: str):
        escaped = {}

        def keep(match):
            key = "\x00%d\x00" % len(escaped)
            escaped[key] = match.group(1)
            return key

        return ESCAPED_MARKDOWN_RE.sub(keep, text), escaped

    @staticmethod
    def restore_escaped_chars(text: str, escaped_chars: dict) -> str:
        for key, value in escaped_chars.items():
            text = text.replace(key, value)
        return text

    @staticmethod
    def escape_and_create_quotes(text: str, strict: bool):
        lines = text.split("\n")
        out = []
        in_quote = False
        for line in lines:
            quoted = line.startswith(BLOCKQUOTE_EXPANDABLE_DELIM) or line.startswith(BLOCKQUOTE_DELIM)
            body = line
            if line.startswith(BLOCKQUOTE_EXPANDABLE_DELIM):
                body = line[len(BLOCKQUOTE_EXPANDABLE_DELIM):].lstrip()
            elif line.startswith(BLOCKQUOTE_DELIM):
                body = line[len(BLOCKQUOTE_DELIM):].lstrip()
            if strict:
                body = _html.escape(body, quote=False)
            if quoted and not in_quote:
                out.append(OPENING_TAG.format("blockquote") + body)
                in_quote = True
            elif quoted:
                out.append(body)
            else:
                if in_quote:
                    out[-1] = out[-1] + CLOSING_TAG.format("blockquote")
                    in_quote = False
                out.append(body)
        if in_quote and out:
            out[-1] = out[-1] + CLOSING_TAG.format("blockquote")
        return "\n".join(out)

    @classmethod
    def parse(cls, text: str, strict: bool = False):
        return parse_text(text, "Markdown")

    @staticmethod
    def unparse(text: str, entities: list):
        raws = [_raw(e) for e in entities or ()]
        units = add_surrogates(text)
        verbatim = [False] * (len(units) + 1)
        for entity in raws:
            if entity.type in (TLEntityType.CODE, TLEntityType.PRE):
                for index in range(entity.offset, min(entity.offset + entity.length, len(units))):
                    verbatim[index] = True
        shifted = []
        extra = [0] * (len(units) + 1)
        count = 0
        for index, unit in enumerate(units):
            extra[index] = count
            if not verbatim[index] and unit in MARKDOWN_ESCAPABLE_CHARS:
                shifted.append(chr(92) + unit)
                count += 1
            else:
                shifted.append(unit)
        extra[len(units)] = count
        size = len(units)
        moved = [RawEntity(e.type, e.offset + extra[min(e.offset, size)],
                           e.length + extra[min(e.offset + e.length, size)] - extra[min(e.offset, size)],
                           e.language, e.url, e.document_id, e.collapsed) for e in raws]
        return _wrap(remove_surrogates("".join(shifted)), moved, _md_open, _md_close)
