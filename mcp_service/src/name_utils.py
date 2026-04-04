import re
from difflib import SequenceMatcher
from functools import lru_cache
from typing import Iterable

try:
    from pypinyin import lazy_pinyin
except ImportError:  # pragma: no cover - optional dependency at runtime
    lazy_pinyin = None


_ASCII_TOKEN_RE = re.compile(r"[a-z0-9]+")


@lru_cache(maxsize=512)
def phonetic_key(name: str) -> str:
    text = name.strip()
    if not text:
        return ""

    if lazy_pinyin is None:
        tokens = _ASCII_TOKEN_RE.findall(text.casefold())
        if tokens:
            return "".join(tokens)
        return "".join(ch for ch in text.casefold() if not ch.isspace())

    tokens = []
    for item in lazy_pinyin(text, errors=lambda value: list(value.casefold())):
        token = "".join(_ASCII_TOKEN_RE.findall(str(item).casefold()))
        if token:
            tokens.append(token)
    return "".join(tokens)


def resolve_canonical_name(
    raw_name: str,
    candidates: Iterable[str],
    similarity_threshold: float,
) -> str:
    normalized = raw_name.strip()
    candidate_list = [candidate for candidate in candidates if candidate and candidate.strip()]
    if not normalized or not candidate_list:
        return normalized

    exact_map = {candidate.casefold(): candidate for candidate in candidate_list}
    exact = exact_map.get(normalized.casefold())
    if exact is not None:
        return exact

    normalized_key = phonetic_key(normalized)
    if not normalized_key:
        return normalized

    phonetic_matches = [
        candidate
        for candidate in candidate_list
        if phonetic_key(candidate) == normalized_key
    ]
    if len(phonetic_matches) == 1:
        return phonetic_matches[0]
    if len(phonetic_matches) > 1:
        return sorted(
            phonetic_matches,
            key=lambda candidate: (len(candidate), candidate.casefold()),
        )[0]

    scored: list[tuple[float, str]] = []
    for candidate in candidate_list:
        candidate_key = phonetic_key(candidate)
        if not candidate_key:
            continue
        score = SequenceMatcher(None, normalized_key, candidate_key).ratio()
        scored.append((score, candidate))

    if not scored:
        return normalized

    scored.sort(key=lambda item: (item[0], -len(item[1])), reverse=True)
    best_score, best_candidate = scored[0]
    next_score = scored[1][0] if len(scored) > 1 else 0.0
    if best_score >= similarity_threshold and (best_score - next_score) >= 0.03:
        return best_candidate
    return normalized
