import re
from dataclasses import dataclass
from enum import Enum
from html.parser import HTMLParser
from typing import Any, Dict, List, Optional, Tuple


class TLEntityType(Enum):
    CODE = "code"
    PRE = "pre"
    STRIKETHROUGH = "strikethrough"
    TEXT_LINK = "text_link"
    BOLD = "bold"
    ITALIC = "italic"
    UNDERLINE = "underline"
    SPOILER = "spoiler"
    CUSTOM_EMOJI = "custom_emoji"
    BLOCKQUOTE = "blockquote"

    @classmethod
    def from_(cls, entity):
        if isinstance(entity, cls):
            return entity
        if isinstance(entity, str):
            return cls(entity.lower())
        name = type(entity).__name__
        for key, value in _TL_NAMES.items():
            if name == value or name.endswith(value):
                return cls(key)
        kind = entity.get("type") if isinstance(entity, dict) else getattr(entity, "type", None)
        if isinstance(kind, str):
            return cls(kind.lower())
        raise ValueError("unknown entity %r" % (entity,))


_TL_NAMES = {
    "code": "TL_messageEntityCode",
    "pre": "TL_messageEntityPre",
    "strikethrough": "TL_messageEntityStrike",
    "text_link": "TL_messageEntityTextUrl",
    "bold": "TL_messageEntityBold",
    "italic": "TL_messageEntityItalic",
    "underline": "TL_messageEntityUnderline",
    "spoiler": "TL_messageEntitySpoiler",
    "custom_emoji": "TL_messageEntityCustomEmoji",
    "blockquote": "TL_messageEntityBlockquote",
}


@dataclass
class RawEntity:
    type: TLEntityType
    offset: int
    length: int
    language: Optional[str] = None
    url: Optional[str] = None
    document_id: Optional[int] = None
    collapsed: Optional[bool] = False

    @property
    def TLRPC_ENTITIES_MAP(self):
        return dict(_TL_NAMES)

    def to_tlrpc_object(self):
        from org.telegram.tgnet import TLRPC
        entity = getattr(TLRPC, _TL_NAMES[TLEntityType.from_(self.type).value])()
        entity.offset = int(self.offset)
        entity.length = int(self.length)
        kind = TLEntityType.from_(self.type)
        if kind is TLEntityType.PRE:
            entity.language = self.language or ""
        elif kind is TLEntityType.TEXT_LINK:
            entity.url = self.url or ""
        elif kind is TLEntityType.CUSTOM_EMOJI:
            entity.document_id = int(self.document_id or 0)
        elif kind is TLEntityType.BLOCKQUOTE:
            entity.collapsed = bool(self.collapsed)
        return entity


# HTML

_HTML_TAGS = {
    "b": TLEntityType.BOLD,
    "strong": TLEntityType.BOLD,
    "i": TLEntityType.ITALIC,
    "em": TLEntityType.ITALIC,
    "u": TLEntityType.UNDERLINE,
    "s": TLEntityType.STRIKETHROUGH,
    "del": TLEntityType.STRIKETHROUGH,
    "strike": TLEntityType.STRIKETHROUGH,
    "code": TLEntityType.CODE,
    "pre": TLEntityType.PRE,
    "spoiler": TLEntityType.SPOILER,
    "tg-spoiler": TLEntityType.SPOILER,
    "a": TLEntityType.TEXT_LINK,
    "blockquote": TLEntityType.BLOCKQUOTE,
    "emoji": TLEntityType.CUSTOM_EMOJI,
    "tg-emoji": TLEntityType.CUSTOM_EMOJI,
}

_LANGUAGE_CLASS_RE = re.compile(r"language-([\w#+-]+)")


class _EntityHTMLParser(HTMLParser):
    def __init__(self):
        HTMLParser.__init__(self, convert_charrefs=True)
        self._parts: List[str] = []
        self._length = 0
        self._stack: List[list] = []
        self.entities: List[RawEntity] = []

    def _emit(self, data: str):
        if data:
            self._parts.append(data)
            self._length += len(data)

    def handle_starttag(self, tag, attrs):
        self._start(tag.lower(), dict(attrs))

    def handle_startendtag(self, tag, attrs):
        tag = tag.lower()
        if tag == "br":
            self._emit("\n")
        else:
            self._start(tag, dict(attrs))
            self._end(tag)

    def handle_endtag(self, tag):
        self._end(tag.lower())

    def handle_data(self, data):
        self._emit(data)

    def _inside_pre(self) -> bool:
        return bool(self._stack) and self._stack[-1][1] is TLEntityType.PRE

    def _start(self, tag: str, attrs: Dict[str, str]):
        if tag == "br":
            self._emit("\n")
            return
        entity_type = _HTML_TAGS.get(tag)
        if entity_type is None:
            return
        if self._inside_pre():
            if entity_type is TLEntityType.CODE:
                match = _LANGUAGE_CLASS_RE.search(attrs.get("class") or "")
                if match:
                    self._stack[-1][3]["language"] = match.group(1)
            return
        extra: Dict[str, Any] = {}
        if entity_type is TLEntityType.TEXT_LINK:
            extra["url"] = attrs.get("href") or ""
        elif entity_type is TLEntityType.PRE:
            extra["language"] = attrs.get("language") or ""
        elif entity_type is TLEntityType.CUSTOM_EMOJI:
            raw_id = attrs.get("id") or attrs.get("emoji-id") or ""
            extra["document_id"] = int(raw_id) if str(raw_id).isdigit() else None
        elif entity_type is TLEntityType.BLOCKQUOTE:
            extra["collapsed"] = "expandable" in attrs or "collapsed" in attrs
        self._stack.append([tag, entity_type, self._length, extra])

    def _end(self, tag: str):
        for index in range(len(self._stack) - 1, -1, -1):
            if self._stack[index][0] == tag:
                while len(self._stack) > index:
                    self._close_top()
                return

    def _close_top(self):
        _tag, entity_type, start, extra = self._stack.pop()
        length = self._length - start
        if length <= 0:
            return
        if entity_type is TLEntityType.TEXT_LINK and not extra.get("url"):
            return
        if entity_type is TLEntityType.CUSTOM_EMOJI and extra.get("document_id") is None:
            return
        self.entities.append(RawEntity(offset=start, length=length,
                                       type=entity_type, **extra))

    def result(self) -> Tuple[str, List[RawEntity]]:
        while self._stack:
            self._close_top()
        self.entities.sort(key=lambda e: (e.offset, -e.length))
        return "".join(self._parts), self.entities


def _parse_html(text: str) -> Tuple[str, List[RawEntity]]:
    parser = _EntityHTMLParser()
    parser.feed(text)
    parser.close()
    return parser.result()


# Markdown

_MD_RE = re.compile(
    r"(?P<pre>```(?P<prelang>\w+)?[ \t]*\n?(?P<prebody>.*?)```)"
    r"|(?P<quote>^(?:\*\*>|>)[ \t]?[^\n]*(?:\n(?:\*\*>|>)[ \t]?[^\n]*)*)"
    r"|(?P<emoji>!\[(?P<emojialt>[^\]]*)\]\(tg://emoji\?id=(?P<emojiid>\d+)\))"
    r"|(?P<link>\[(?P<linktext>[^\]]+)\]\((?P<linkurl>[^)\s]+)\))"
    r"|(?P<code>`(?P<codebody>[^`\n]+)`)"
    r"|(?P<spoiler>\|\|(?P<spoilerbody>.+?)\|\|)"
    r"|(?P<bold2>\*\*(?P<bold2body>[^*\n]+?)\*\*)"
    r"|(?P<bold>\*(?P<boldbody>[^*\n]+?)\*)"
    r"|(?P<italic2>__(?P<italic2body>[^_\n]+?)__)"
    r"|(?P<underline>--(?P<underlinebody>[^-\n]+?)--)"
    r"|(?P<italic>(?<![\w])_(?P<italicbody>[^_\n]+?)_(?![\w]))"
    r"|(?P<strike2>~~(?P<strike2body>[^~\n]+?)~~)"
    r"|(?P<strike>~(?P<strikebody>[^~\n]+?)~)",
    re.S | re.M,
)

_MD_QUOTE_MARKER_RE = re.compile(r"^(?:\*\*>|>)[ \t]?")

_MD_INLINE_TYPES = {
    "code": ("codebody", TLEntityType.CODE),
    "spoiler": ("spoilerbody", TLEntityType.SPOILER),
    "bold2": ("bold2body", TLEntityType.BOLD),
    "bold": ("boldbody", TLEntityType.BOLD),
    "italic2": ("italic2body", TLEntityType.ITALIC),
    "underline": ("underlinebody", TLEntityType.UNDERLINE),
    "italic": ("italicbody", TLEntityType.ITALIC),
    "strike2": ("strike2body", TLEntityType.STRIKETHROUGH),
    "strike": ("strikebody", TLEntityType.STRIKETHROUGH),
}


def _parse_markdown_plain(text: str) -> Tuple[str, List[RawEntity]]:
    out: List[str] = []
    entities: List[RawEntity] = []
    plain_length = 0
    position = 0
    for match in _MD_RE.finditer(text):
        start, end = match.span()
        chunk = text[position:start]
        out.append(chunk)
        plain_length += len(chunk)
        kind = match.lastgroup
        offset = plain_length
        if kind == "pre":
            body = match.group("prebody")
            if body.endswith("\n"):
                body = body[:-1]
            language = match.group("prelang") or None
            out.append(body)
            plain_length += len(body)
            if body:
                entities.append(RawEntity(TLEntityType.PRE, offset, len(body),
                                          language=language))
        elif kind == "quote":
            raw = match.group("quote")
            collapsed = raw.startswith("**>")
            body = "\n".join(
                _MD_QUOTE_MARKER_RE.sub("", line) for line in raw.split("\n"))
            out.append(body)
            plain_length += len(body)
            if body:
                entities.append(RawEntity(TLEntityType.BLOCKQUOTE, offset, len(body),
                                          collapsed=collapsed))
        elif kind == "emoji":
            alt = match.group("emojialt") or "\U0001F642"
            document_id = int(match.group("emojiid"))
            out.append(alt)
            plain_length += len(alt)
            entities.append(RawEntity(TLEntityType.CUSTOM_EMOJI, offset, len(alt),
                                      document_id=document_id))
        elif kind == "link":
            body = match.group("linktext")
            url = match.group("linkurl")
            out.append(body)
            plain_length += len(body)
            emoji_id = url[len("tg://emoji?id="):] if url.startswith("tg://emoji?id=") else url
            if emoji_id.isdigit():
                entities.append(RawEntity(TLEntityType.CUSTOM_EMOJI, offset, len(body),
                                          document_id=int(emoji_id)))
            else:
                entities.append(RawEntity(TLEntityType.TEXT_LINK, offset, len(body),
                                          url=url))
        else:
            group_name, entity_type = _MD_INLINE_TYPES[kind]
            body = match.group(group_name)
            out.append(body)
            plain_length += len(body)
            entities.append(RawEntity(entity_type, offset, len(body)))
        position = end
    out.append(text[position:])
    return "".join(out), entities


# API

_MD_ESCAPABLE = "\\`*_~|>[]()-"


def _parse_markdown(text: str) -> Tuple[str, List[RawEntity]]:
    kept = []
    out = []
    index = 0
    while index < len(text):
        char = text[index]
        if char == chr(92) and index + 1 < len(text) and text[index + 1] in _MD_ESCAPABLE:
            out.append(chr(0xF0000 + len(kept)))
            kept.append(text[index + 1])
            index += 2
            continue
        out.append(char)
        index += 1
    plain, entities = _parse_markdown_plain("".join(out))
    if kept:
        plain = "".join(kept[ord(c) - 0xF0000] if 0xF0000 <= ord(c) < 0xF0000 + len(kept) else c for c in plain)
    return plain, entities


def _utf16_length(text: str) -> int:
    return len(text.encode("utf-16-le")) // 2


def parse_raw(text: str, parse_mode: Optional[str] = "HTML") -> Tuple[str, List[RawEntity]]:
    if text is None:
        text = ""
    text = str(text)
    mode = (parse_mode or "HTML").lower()
    if mode == "html":
        plain, entities = _parse_html(text)
    elif mode == "markdown":
        plain, entities = _parse_markdown(text)
    else:
        raise ValueError(
            f"unsupported parse_mode {parse_mode!r}: expected 'HTML' or 'Markdown'")
    for entity in entities:
        utf16_offset = _utf16_length(plain[:entity.offset])
        utf16_length = _utf16_length(plain[entity.offset:entity.offset + entity.length])
        entity.offset, entity.length = utf16_offset, utf16_length
    return plain, entities


def entity_to_dict(raw: RawEntity) -> Dict[str, Any]:
    data: Dict[str, Any] = {
        "type": raw.type.value,
        "offset": int(raw.offset),
        "length": int(raw.length),
    }
    if raw.url:
        data["url"] = raw.url
    if raw.language:
        data["language"] = raw.language
    if raw.document_id is not None:
        data["document_id"] = str(int(raw.document_id))
    if raw.collapsed:
        data["collapsed"] = True
    return data


def parse_text(text: str, parse_mode: Optional[str] = "HTML",
               is_caption: bool = False) -> Dict[str, Any]:
    plain, raw_entities = parse_raw(text, parse_mode)
    entities = [entity_to_dict(raw) for raw in raw_entities]
    key = "caption" if is_caption else "message"
    return {key: plain, "entities": entities}
