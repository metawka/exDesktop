from typing import Any, List

from text_formatting import RawEntity, entity_to_dict, parse_raw


class MarkdownEntity:
    __slots__ = ("offset", "length", "type", "url", "language",
                 "document_id", "collapsed", "_raw")

    def __init__(self, raw: RawEntity):
        self._raw = raw
        self.offset = raw.offset
        self.length = raw.length
        self.type = raw.type
        self.url = raw.url
        self.language = raw.language
        self.document_id = raw.document_id
        self.collapsed = raw.collapsed

    def to_tlrpc_object(self) -> Any:
        raw = RawEntity(
            offset=int(self.offset),
            length=int(self.length),
            type=self.type,
            url=self.url,
            language=self.language,
            document_id=self.document_id,
            collapsed=bool(self.collapsed),
        )
        return entity_to_dict(raw)

    def __getitem__(self, index: int) -> Any:
        return (self.type, self.offset, self.length, self.url)[index]

    def __iter__(self):
        return iter((self.type, self.offset, self.length, self.url))

    def __len__(self) -> int:
        return 4

    def __repr__(self) -> str:
        return (f"MarkdownEntity({self.type}, offset={self.offset}, "
                f"length={self.length})")


class ParsedText:
    __slots__ = ("text", "entities")

    def __init__(self, text: str, entities: List[MarkdownEntity]):
        self.text = text
        self.entities = entities

    def __iter__(self):
        return iter((self.text, self.entities))

    def __len__(self) -> int:
        return len(self.text)

    def __str__(self) -> str:
        return self.text

    def __repr__(self) -> str:
        return f"ParsedText({self.text!r}, {len(self.entities)} entities)"


def _parse(text: Any, parse_mode: str) -> ParsedText:
    plain, raw_entities = parse_raw("" if text is None else str(text), parse_mode)
    return ParsedText(plain, [MarkdownEntity(raw) for raw in raw_entities])


def parse_markdown(text: Any) -> ParsedText:
    return _parse(text, "Markdown")


def parse_html(text: Any) -> ParsedText:
    return _parse(text, "HTML")
