from dataclasses import dataclass
from typing import Any, Tuple

from text_formatting import RawEntity, TLEntityType, entity_to_dict, parse_raw

__all__ = ["parse_markdown"]


@dataclass
class ParsedMessage:
    text: str
    entities: Tuple[RawEntity, ...]

    def __iter__(self):
        return iter((self.text, self.entities))

    def __len__(self) -> int:
        return len(self.text)

    def __str__(self) -> str:
        return self.text

    def to_dict(self):
        return {"message": self.text, "entities": [entity_to_dict(e) for e in self.entities]}


MarkdownEntity = RawEntity
ParsedText = ParsedMessage


def _parse(text: Any, parse_mode: str) -> ParsedMessage:
    plain, raw_entities = parse_raw("" if text is None else str(text), parse_mode)
    return ParsedMessage(plain, tuple(raw_entities))


def parse_markdown(markdown: Any) -> ParsedMessage:
    return _parse(markdown, "Markdown")


def parse_html(text: Any) -> ParsedMessage:
    return _parse(text, "HTML")
