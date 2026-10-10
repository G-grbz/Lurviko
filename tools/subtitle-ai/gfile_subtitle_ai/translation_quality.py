# SPDX-License-Identifier: GPL-3.0-or-later
"""Local subtitle quality engine, adapted from G-TMCE's newer translation path.

Runtime/model loading, permissions and readable layout remain owned by Lurviko.
"""
from __future__ import annotations
from dataclasses import dataclass
from difflib import SequenceMatcher
import os
import re
import time
from pathlib import Path
from typing import Any, Callable, Iterable
from .core import OperationCancelled, UserVisibleError, ui_text
from .transcription import (
    SubtitleCue, DEFAULT_TRANSLATION_MODEL, TRANSLATION_TARGET_CODES,
    _clean_text, _cue_finishes_sentence, _normalised_translation_text,
    _translation_word_tokens, _translation_untranslated_reason,
    _longest_same_token_run, _sentence_break_count, _wrap_subtitle_text,
    _prepare_translation_model, _load_translation_runtime,
    _translation_decode_profile, _is_cuda_oom_error, normalise_asr_language,
    readable_subtitle_cues,
)

class _AdaptiveTranslationBackend:
    """Keep quality decoding usable on small GPUs without restarting a job."""
    def __init__(self, translator: Any, model_path: Path, device: str, logger: Callable, cancel_event: Any):
        self.translator = translator
        self.model_path = model_path
        self.device = device
        self.logger = logger
        self.cancel_event = cancel_event

    def _call(self, method: str, sources: list, targets: list | None = None, **kwargs: Any) -> list:
        if self.cancel_event is not None and self.cancel_event.is_set():
            raise OperationCancelled()
        try:
            arguments = (sources,) if targets is None else (sources, targets)
            return getattr(self.translator, method)(*arguments, **kwargs)
        except Exception as exc:
            if self.device != "cuda" or not _is_cuda_oom_error(exc):
                raise
            if len(sources) > 1:
                midpoint = len(sources) // 2
                self.logger("Lurviko AI Translation: reducing GPU batch size; completed cues are retained")
                return self._call(method, sources[:midpoint], None if targets is None else targets[:midpoint], **kwargs) + self._call(
                    method, sources[midpoint:], None if targets is None else targets[midpoint:], **kwargs)
            if method == "translate_batch" and int(kwargs.get("beam_size", 1)) > 1:
                smaller = dict(kwargs, beam_size=max(1, int(kwargs["beam_size"]) // 2))
                smaller["num_hypotheses"] = min(int(kwargs.get("num_hypotheses", 1)), smaller["beam_size"])
                return self._call(method, sources, targets, **smaller)
            if self.cancel_event is not None and self.cancel_event.is_set():
                raise OperationCancelled()
            import ctranslate2
            self.logger("Lurviko AI Translation: GPU workspace exhausted; continuing quality checks on CPU/int8")
            self.translator.unload_model()
            self.translator = ctranslate2.Translator(str(self.model_path), device="cpu", compute_type="int8")
            self.device = "cpu"
            return self._call(method, sources, targets, **kwargs)

    def translate_batch(self, sources: list, **kwargs: Any) -> list:
        return self._call("translate_batch", sources, **kwargs)

    def score_batch(self, sources: list, targets: list, **kwargs: Any) -> list:
        return self._call("score_batch", sources, targets, **kwargs)

AI_TRANSLATION_QUALITY_PROFILES: dict[str, dict[str, Any]] = {
    # Throughput first: large first-pass batches, a small beam and only one
    # conservative retry when QA rejects a span.
    "fast": {
        "batch_size": 32, "beam_size": 2, "repetition_penalty": 1.12,
        "no_repeat_ngram_size": 3, "length_factor": 1.70, "length_extra": 6,
        "retry_attempts": 1, "context_mode": "fail",
    },
    # Default: keeps the fast batch-first architecture but gives suspicious
    # spans a broader retry search.
    "balanced": {
        "batch_size": 24, "beam_size": 3, "repetition_penalty": 1.10,
        "no_repeat_ngram_size": 3, "length_factor": 1.75, "length_extra": 6,
        "retry_attempts": 2, "context_mode": "fail",
    },
    # Accuracy first: smaller batches, wider beam and neighbour-context
    # candidate generation for short dialogue even when the first pass looks
    # structurally valid.  This is intentionally slower.
    "maximum": {
        "batch_size": 12, "beam_size": 4, "repetition_penalty": 1.10,
        "no_repeat_ngram_size": 3, "length_factor": 2.00, "length_extra": 8,
        "retry_attempts": 3, "context_mode": "short_or_fail",
    },
}


def _normalise_translation_quality_profile(value: str | None) -> str:
    key = str(value or "balanced").strip().lower()
    aliases = {"medium": "balanced", "max": "maximum", "quality": "maximum"}
    key = aliases.get(key, key)
    return key if key in AI_TRANSLATION_QUALITY_PROFILES else "balanced"


def _translation_invalid_reason(source: str, output: str) -> str | None:
    return (
        _translation_degeneration_reason(source, output)
        or _translation_untranslated_reason(source, output)
    )


def _translation_degeneration_reason(source: str, output: str) -> str | None:
    """Detect decoder loops without rejecting legitimate repeated dialogue.

    MADLAD occasionally gets stuck on a token when the ASR source is a tiny
    fragment (for example ``The`` -> ``The The The...``) or when a sentence
    ends on a filler.  Compare the generated repetition with the source so
    real dialogue such as ``go, go, go`` remains valid.
    """
    source_tokens = _translation_word_tokens(source)
    output_tokens = _translation_word_tokens(output)
    if not output_tokens:
        return "empty"

    source_count = max(1, len(source_tokens))
    output_count = len(output_tokens)
    comparable_words = not (_uses_unspaced_translation_script(source) or _uses_unspaced_translation_script(output))
    source_run = _longest_same_token_run(source_tokens)
    output_run = _longest_same_token_run(output_tokens)

    # Short inputs sometimes become duplicated answers (for example
    # ``I don't know.`` -> ``Bilmiyorum, bilmiyorum.``). A two-token run is
    # suspicious only when the source itself did not repeat that token.
    repeated_source_clauses = _has_adjacent_translation_duplicate(source)
    if output_run >= 2 and source_run < 2 and output_count <= 8 and not repeated_source_clauses:
        return f"short-token-repeat:{output_run}"

    # Tiny ASR fragments are especially prone to semantic hallucinations that
    # are not token loops (for example ``The`` becoming a whole unrelated
    # sentence). Reject implausible expansion before it reaches the subtitle.
    if comparable_words and source_count == 1 and output_count >= 5:
        return f"tiny-input-expansion:{source_count}->{output_count}"
    if comparable_words and source_count == 2 and output_count > 8:
        return f"tiny-input-expansion:{source_count}->{output_count}"
    if comparable_words and source_count <= 4 and output_count > source_count * 4 + 4:
        return f"short-input-expansion:{source_count}->{output_count}"

    # Beam decoding can occasionally emit two alternative translations one
    # after another. For subtitle work that is both semantically dangerous and
    # far too verbose. Retry when one source sentence unexpectedly expands to
    # multiple target sentences with additional material.
    source_sentences = _sentence_break_count(source)
    output_sentences = _sentence_break_count(output)
    if (
        source_sentences <= 1
        and output_sentences >= 2
        and comparable_words and output_count >= source_count + 3
    ):
        return f"sentence-expansion:{source_sentences}->{output_sentences}"

    # The supplied sample exposed runs such as one source token becoming 7-36
    # copies in the translation. Keep genuine source repetition by allowing a
    # little headroom over the longest run already present in the source.
    if output_run >= 4 and output_run > source_run + 2 and not repeated_source_clauses:
        return f"token-loop:{output_run}"

    # A subtitle translation should not explode to many times the source size.
    # Use a generous limit because some language pairs naturally expand.
    if comparable_words and output_count > max(24, source_count * 4 + 10):
        return f"length-explosion:{source_count}->{output_count}"

    # Low lexical diversity is another signature of a loop even when
    # punctuation or a short preamble interrupts the repeated token run.
    if output_count >= 12:
        unique_ratio = len(set(output_tokens)) / output_count
        if comparable_words and unique_ratio < 0.22 and output_count > source_count * 2:
            return f"low-diversity:{unique_ratio:.2f}"

    return None


def _decode_translation_result(
    result: Any, tokenizer: Any, target_language: str, *, max_length: int | None = None,
) -> str:
    hypothesis = list(result.hypotheses[0]) if result.hypotheses else []
    # A length-limited hypothesis can end in the middle of a SentencePiece
    # word. It is not a finished translation, even if its text passes QA.
    # Ask CT2 to retain EOS, and never send these fragments to layout/fallback.
    if max_length is not None and len(hypothesis) >= max_length and "</s>" not in hypothesis:
        return ""
    hypothesis = [
        token for token in hypothesis
        if token not in {"</s>", "<pad>", f"<2{target_language}>"}
    ]
    return _clean_text(tokenizer.decode(hypothesis)) if hypothesis else ""


def _encode_translation_source(tokenizer: Any, text: str, target_language: str) -> list[str]:
    """Build a complete MADLAD/T5 input, not an unfinished text prefix.

    Plain SentencePiece does not add the T5 EOS that Hugging Face tokenizers
    normally append. The converted CT2 model also has add_source_eos=false.
    Missing EOS makes the translator invent continuations of the source.
    """
    tokens = list(tokenizer.encode(f"<2{target_language}> {_clean_text(text)}", out_type=str))
    if not tokens or tokens[-1] != "</s>":
        tokens.append("</s>")
    return tokens


@dataclass
class _RankedTranslationResult:
    hypotheses: list[list[str]]


class _FaithfulSubtitleTranslator:
    """Re-rank a bounded set of translations by source reconstruction score.

    Uses teacher-forced scoring with the same loaded multilingual model, not
    another model or language-specific negation/idiom dictionaries. Scores
    compare the identical source tokens for every candidate. They are a
    preference signal, not a guarantee of semantic equivalence.
    """
    def __init__(self, translator: Any, tokenizer: Any, source_language: str, cancel_event: Any = None, pause_event: Any = None):
        self.translator = translator
        self.tokenizer = tokenizer
        self.source_language = source_language
        self.cancel_event = cancel_event
        self.pause_event = pause_event
        self.reconstruction_scores: dict[tuple[str, str], float] = {}
        self.translation_scores: dict[tuple[str, str], float] = {}

    def _checkpoint(self) -> None:
        while self.pause_event is not None and self.pause_event.is_set():
            if self.cancel_event is not None and self.cancel_event.is_set():
                raise OperationCancelled()
            time.sleep(0.12)
        if self.cancel_event is not None and self.cancel_event.is_set():
            raise OperationCancelled()

    @staticmethod
    def _remember_score(cache: dict, key: tuple[str, str], value: float) -> None:
        cache[key] = value
        if len(cache) > 512:
            del cache[next(iter(cache))]

    def reconstruction_score(self, source: str, candidate: str) -> float:
        key = (_clean_text(source), _clean_text(candidate))
        if key not in self.reconstruction_scores:
            self._checkpoint()
            target = list(self.tokenizer.encode(key[0], out_type=str))
            if not target or target[-1] != "</s>":
                target.append("</s>")
            result = self.translator.score_batch(
                [_encode_translation_source(self.tokenizer, key[1], self.source_language)], [target],
            )[0]
            self._checkpoint()
            self._remember_score(self.reconstruction_scores, key, sum(result.log_probs) / max(1, len(result.log_probs)))
        return self.reconstruction_scores[key]

    def candidate_score(self, source: str, candidate: str, target_language: str) -> float:
        """Compare source fidelity AND target fluency on the same full input."""
        key = (_clean_text(source), _clean_text(candidate))
        if key not in self.translation_scores:
            self._checkpoint()
            target = list(self.tokenizer.encode(key[1], out_type=str))
            if not target or target[-1] != "</s>":
                target.append("</s>")
            result = self.translator.score_batch(
                [_encode_translation_source(self.tokenizer, key[0], target_language)], [target],
            )[0]
            self._checkpoint()
            self._remember_score(self.translation_scores, key, sum(result.log_probs) / max(1, len(result.log_probs)))
        return self.translation_scores[key] + self.reconstruction_score(source, candidate)

    def translate_batch(self, source_tokens: list[list[str]], **kwargs: Any) -> list[Any]:
        self._checkpoint()
        # At least two candidates are needed to resolve a close wrong/correct
        # choice. Bound search/scoring even for Maximum to avoid VRAM spikes.
        kwargs["beam_size"] = max(2, int(kwargs.get("beam_size", 2)))
        kwargs["num_hypotheses"] = min(4, kwargs["beam_size"])
        kwargs["return_scores"] = True
        kwargs["return_end_token"] = True
        results = self.translator.translate_batch(source_tokens, **kwargs)
        expanded: set[int] = set()
        # Very similar endings can carry opposite meanings or different
        # modalities. A small beam can prune the faithful form entirely.
        # Widen only these ambiguous items, individually, keeping memory
        # bounded instead of running a large beam for the whole subtitle.
        if kwargs["beam_size"] < 8:
            for index, result in enumerate(results):
                alternatives = [_translation_word_tokens(self.tokenizer.decode(h)) for h in result.hypotheses]
                if len(alternatives) < 2 or min(map(len, alternatives)) < 4:
                    continue
                prefix = 0
                for words in zip(*alternatives):
                    if len(set(words)) != 1:
                        break
                    prefix += 1
                endings = {tuple(words[prefix:]) for words in alternatives}
                if prefix >= min(map(len, alternatives)) - 2 and len(endings) > 1:
                    self._checkpoint()
                    wider = dict(kwargs, beam_size=8, num_hypotheses=4)
                    results[index] = self.translator.translate_batch([source_tokens[index]], **wider)[0]
                    expanded.add(index)
        reverse_sources: list[list[str]] = []
        reverse_targets: list[list[str]] = []
        refs: list[tuple[int, int, float]] = []
        limit = kwargs.get("max_decoding_length")
        for index, (tokens, result) in enumerate(zip(source_tokens, results)):
            original = [token for token in tokens if not re.fullmatch(r"<2[^>]+>", token)]
            source = self.tokenizer.decode([t for t in original if t != "</s>"])
            for alternative, hypothesis in enumerate(result.hypotheses):
                candidate = _decode_translation_result(_RankedTranslationResult([hypothesis]), self.tokenizer, "", max_length=limit)
                if not candidate or _strict_translation_quality_reason(source, candidate) is not None:
                    continue
                reverse_sources.append(_encode_translation_source(self.tokenizer, candidate, self.source_language))
                target = list(self.tokenizer.encode(source, out_type=str))
                if not target or target[-1] != "</s>":
                    target.append("</s>")
                reverse_targets.append(target)
                refs.append((index, alternative, float(result.scores[alternative])))
        if not refs:
            return results
        self._checkpoint()
        scores = self.translator.score_batch(reverse_sources, reverse_targets, max_batch_size=8)
        choices: dict[int, tuple[float, int]] = {}
        for position, ((index, alternative, forward), reverse) in enumerate(zip(refs, scores)):
            if not reverse.log_probs:
                continue
            reverse_score = sum(reverse.log_probs) / len(reverse.log_probs)
            source = self.tokenizer.decode([t for t in reverse_targets[position] if t != "</s>"])
            candidate = _decode_translation_result(
                _RankedTranslationResult([results[index].hypotheses[alternative]]), self.tokenizer, "",
            )
            self._remember_score(self.reconstruction_scores, (_clean_text(source), _clean_text(candidate)), reverse_score)
            self._remember_score(self.translation_scores, (_clean_text(source), _clean_text(candidate)), forward)
            score = forward + reverse_score
            if index not in choices or score > choices[index][0]:
                choices[index] = (score, alternative)
        self._checkpoint()
        ranked = [_RankedTranslationResult([result.hypotheses[choices[index][1]]]) if index in choices else result
                  for index, result in enumerate(results)]
        # A short low-confidence fragment may require a wider search even if
        # its alternatives do not share a prefix. Retry one item at a time,
        # never recurse from an already-wide decode or widen long paragraphs.
        if kwargs["beam_size"] < 8:
            for index, result in enumerate(ranked):
                original = [t for t in source_tokens[index] if t != "</s>" and not re.fullmatch(r"<2[^>]+>", t)]
                source = self.tokenizer.decode(original)
                candidate = _decode_translation_result(result, self.tokenizer, "", max_length=limit)
                if (index not in expanded and index in choices and candidate and len(source_tokens[index]) <= 48
                        and self.reconstruction_scores.get((_clean_text(source), _clean_text(candidate)), 0) < -2.5):
                    ranked[index] = self.translate_batch([source_tokens[index]], **dict(kwargs, beam_size=8, num_hypotheses=4))[0]
        return ranked


def _decode_single_translation(
    translator: Any,
    tokenizer: Any,
    text: str,
    target_language: str,
    *,
    beam_size: int,
    repetition_penalty: float,
    no_repeat_ngram_size: int,
    length_factor: float = 3.0,
    length_extra: int = 8,
) -> str:
    tokens = _encode_translation_source(tokenizer, text, target_language)
    max_length = min(512, max(12, int(len(tokens) * length_factor) + length_extra))
    result = translator.translate_batch(
        [tokens],
        beam_size=beam_size,
        repetition_penalty=repetition_penalty,
        no_repeat_ngram_size=no_repeat_ngram_size,
        max_decoding_length=max_length,
        return_end_token=True,
    )[0]
    return _decode_translation_result(result, tokenizer, target_language, max_length=max_length)


def _decode_translation_batch(
    translator: Any,
    tokenizer: Any,
    texts: list[str],
    target_language: str,
    *,
    beam_size: int = 2,
    repetition_penalty: float = 1.10,
    no_repeat_ngram_size: int = 3,
    length_factor: float = 1.75,
    length_extra: int = 6,
) -> list[str]:
    """Decode many independent subtitle spans in one CTranslate2 call.

    A single max decoding length is required by CTranslate2, so size batches
    conservatively and derive the limit from the longest member.  Structural
    subtitle markup never reaches this helper.
    """
    if not texts:
        return []
    encoded = [
        _encode_translation_source(tokenizer, text, target_language)
        for text in texts
    ]
    longest = max((len(tokens) for tokens in encoded), default=1)
    max_length = min(512, max(12, int(longest * length_factor) + length_extra))
    results = translator.translate_batch(
        encoded,
        beam_size=beam_size,
        repetition_penalty=repetition_penalty,
        no_repeat_ngram_size=no_repeat_ngram_size,
        max_decoding_length=max_length,
        return_end_token=True,
    )
    return [_decode_translation_result(result, tokenizer, target_language, max_length=max_length) for result in results]


def _strict_translation_parts(text: str) -> list[tuple[str, str]]:
    """Split subtitle text into translatable and literal formatting parts.

    Existing subtitle files are authored/timed assets, not ASR output.  Their
    cue structure and presentation markup therefore must never be rewritten by
    the MT model.  HTML-like tags and ASS override blocks are emitted verbatim;
    SDH brackets are structural delimiters while their visible payload remains
    translatable.
    """
    value = str(text or "")
    token_re = re.compile(r"(<[^>]+>|\{[^{}]*\\[^{}]*\}|\[[^\[\]]*\])")
    parts: list[tuple[str, str]] = []
    cursor = 0
    for match in token_re.finditer(value):
        if match.start() > cursor:
            parts.append(("text", value[cursor:match.start()]))
        token = match.group(0)
        if token.startswith("[") and token.endswith("]"):
            parts.append(("literal", "["))
            if token[1:-1]:
                parts.append(("text", token[1:-1]))
            parts.append(("literal", "]"))
        else:
            parts.append(("literal", token))
        cursor = match.end()
    if cursor < len(value):
        parts.append(("text", value[cursor:]))
    return parts


def _translation_clauses(value: str) -> list[str]:
    # Ignore trailing punctuation and display wrapping: neither is a clause.
    return [part.strip() for part in re.split(r"[.!?;…。,，；！？]+", value) if part.strip()]


def _near_duplicate_translation_clauses(left: str, right: str) -> bool:
    """Compare alternative renderings without language-specific dictionaries."""
    lt = _translation_word_tokens(left)
    rt = _translation_word_tokens(right)
    if not lt or not rt:
        return False
    normal_left = " ".join(lt)
    normal_right = " ".join(rt)
    if SequenceMatcher(None, normal_left, normal_right).ratio() >= 0.78:
        return True
    remaining = list(rt)
    matches = 0
    for token in lt:
        for index, other in enumerate(remaining):
            if token == other or (
                min(len(token), len(other)) >= 3
                and token[:3] == other[:3]
                and SequenceMatcher(None, token, other).ratio() >= 0.58
            ):
                matches += 1
                remaining.pop(index)
                break
    return matches >= 2 and matches / min(len(lt), len(rt)) >= 0.66


def _has_adjacent_translation_duplicate(value: str) -> bool:
    clauses = _translation_clauses(value)
    return any(_near_duplicate_translation_clauses(a, b) for a, b in zip(clauses, clauses[1:]))


def _remove_translation_clause_duplicates(source: str, output: str) -> str:
    """Last-resort repair of demonstrable doubled alternatives, not dialogue.

    Only remove adjacent near-duplicates when the source has fewer clauses and
    does not repeat itself. No movie phrases, target words or translations are
    substituted here. Normal QA still runs on the repaired candidate.
    """
    if _has_adjacent_translation_duplicate(source):
        return output
    # Check complete sentences first. Commas inside a second alternative
    # must not disguise the repeated sentence as several unrelated clauses.
    sentence_spans = _translation_sentence_spans(output)
    if len(_translation_sentence_spans(source)) == 1 and len(sentence_spans) > 1:
        first = sentence_spans[0]
        if all(_near_duplicate_translation_clauses(first, other) for other in sentence_spans[1:]):
            output = first
    source_count = len(_translation_clauses(source))
    spans = list(re.finditer(r"[^.!?;…。,，；！？]+[.!?;…。,，；！？]*", output))
    if len(spans) <= source_count:
        return output
    kept: list[str] = []
    removed = 0
    for span in spans:
        current = span.group().strip()
        if kept and len(spans) - removed > source_count and _near_duplicate_translation_clauses(kept[-1], current):
            # The last duplicate often carries the sentence-ending punctuation.
            # Preserve that ending on the original rendering.
            ending = re.search(r"[.!?…。！？]+$", current)
            if ending:
                kept[-1] = re.sub(r"[.!?…。,;，；！？]+$", "", kept[-1]) + ending.group()
            removed += 1
        else:
            kept.append(current)
    prefix = output[:spans[0].start()] if spans else ""
    return prefix + " ".join(kept) if removed else output


def _translation_sentence_spans(value: str) -> list[str]:
    # MT sometimes omits the space after a full stop. Ignore decimals and
    # single-letter initials rather than treating every dot as a sentence.
    boundaries = []
    for match in re.finditer(r"[.!?…。！？]+[\"'’”»)]*", value):
        end = match.end()
        if end == len(value):
            continue
        if not re.search(r"[^\W\d_]", value[:match.start()], flags=re.UNICODE):
            continue
        if value[end].isdigit():
            continue
        if match.group() == "." and re.search(r"(?:^|[^\w])\w$", value[:match.start()]):
            continue
        boundaries.append(end)
    pieces = []
    start = 0
    for end in boundaries + [len(value)]:
        piece = value[start:end].strip()
        if piece:
            pieces.append(piece)
        start = end
    return pieces


def _uses_unspaced_translation_script(value: str) -> bool:
    return bool(re.search(r"[\u0e00-\u0eff\u1000-\u109f\u1780-\u17ff\u3040-\u30ff\u3400-\u9fff]", value))


def _strict_translation_quality_reason(source: str, output: str) -> str | None:
    """Conservative language-agnostic QA for authored subtitle translation.

    The check deliberately relies on structure and relative size instead of a
    source/target-language word list.  That keeps the same safety policy for
    every supported language pair.
    """
    base = _translation_invalid_reason(source, output)
    if base is not None:
        return base

    src = _clean_text(source)
    dst = _clean_text(output)
    if not src or not dst:
        return "empty"

    src_words = _translation_word_tokens(src)
    dst_words = _translation_word_tokens(dst)
    sw = len(src_words)
    dw = len(dst_words)
    comparable_words = not (_uses_unspaced_translation_script(src) or _uses_unspaced_translation_script(dst))
    src_chars = len(re.sub(r"[\W_]", "", src, flags=re.UNICODE))
    dst_chars = len(re.sub(r"[\W_]", "", dst, flags=re.UNICODE))

    # Exact source echoes are suspicious for real dialogue of useful length,
    # regardless of which language is the source.  Very short names/labels are
    # exempt because they often should survive translation unchanged.
    if _normalised_translation_text(src) == _normalised_translation_text(dst):
        if sw >= 3 or len(src) >= 18 or re.search(r"[.!?…]$", src):
            return "source-echo-generic"

    # Catch dropped clauses / hallucinated elaborations.  Limits are generous
    # enough for naturally expanding language pairs but strict enough to catch
    # the failures seen in authored subtitle translation.
    # Agglutinative languages can express several source words in one target
    # word. A low word count alone is not evidence of missing dialogue.
    if comparable_words and sw >= 4 and dw <= max(1, int(sw * 0.55)) and dst_chars < src_chars * 0.55:
        return f"probable-omission:{sw}->{dw}"
    if comparable_words and sw >= 5 and dw > max(sw + 4, int(sw * 1.60)):
        return f"probable-expansion:{sw}->{dw}"
    # Tiny labels/names are where MT models most often emit both the source and
    # a transliterated/translated duplicate (for example "V-Max. V-Maks.").
    # Allow one extra word, but reject a doubled short span.
    if comparable_words and 1 <= sw <= 3 and dw >= max(sw + 2, sw * 2):
        return f"probable-short-duplication:{sw}->{dw}"

    # Catch duplicated/paraphrased output fragments inside a single translated
    # span. MT decoders sometimes emit two near-equivalent Turkish renderings
    # for one source clause (for example "... düşünüyordum. Aynı şeyi düşündüm"
    # or "... zorundadır, ... yapmalıdır"). Compare adjacent output clauses
    # conservatively and only reject them when the source does not contain a
    # matching amount of clause structure. This stays language-pair agnostic.
    src_clause_count = len(_translation_clauses(src))
    dst_clause_count = len(_translation_clauses(dst))
    if (
        dst_clause_count > src_clause_count
        and _has_adjacent_translation_duplicate(dst)
        and not _has_adjacent_translation_duplicate(src)
    ):
        return f"probable-output-duplication:{src_clause_count}->{dst_clause_count}"
    if comparable_words and sw >= 4 and dst_clause_count > src_clause_count and dw >= sw + 3:
        return f"probable-clause-expansion:{src_clause_count}->{dst_clause_count}"
    if comparable_words and sw >= 4 and dst_clause_count >= src_clause_count + 2 and dst_chars > src_chars * 1.5:
        return f"probable-clause-expansion:{src_clause_count}->{dst_clause_count}"

    # Named entities should normally survive translation, but the model must
    # not invent extra repetitions of them.  This catches outputs such as
    # "... Peter Parker ... Peter Parker" when the source mentions the name
    # once, without assuming any specific source or target language.
    proper_tokens = re.findall(r"(?<![.!?]\s)\b[A-ZÀ-ÖØ-Þ][\w’'-]{2,}\b", src, flags=re.UNICODE)
    for token in set(proper_tokens):
        src_count = len(re.findall(rf"\b{re.escape(token)}\b", src, flags=re.IGNORECASE | re.UNICODE))
        dst_count = len(re.findall(rf"\b{re.escape(token)}\b", dst, flags=re.IGNORECASE | re.UNICODE))
        if src_count >= 1 and dst_count > src_count:
            return f"proper-name-duplication:{token}:{src_count}->{dst_count}"

    # Losing a whole clause is a common subtitle-MT failure even when the raw
    # word ratio still looks plausible (for example "Hey ... Whoa ..." ->
    # only "Hey ...").  Clause punctuation is language-agnostic enough to
    # use as a conservative coverage signal.
    src_clauses = len(re.findall(r"[,;:!?…]+", src))
    dst_clauses = len(re.findall(r"[,;:!?…]+", dst))
    if sw >= 5 and src_clauses >= 2 and dst_clauses == 0:
        return f"probable-clause-loss:{src_clauses}->{dst_clauses}"

    # Character ratios help for languages where whitespace token counts are not
    # meaningful (CJK is the important case).  Ignore punctuation and spaces.
    src_chars = len(re.sub(r"[\W_]", "", src, flags=re.UNICODE))
    dst_chars = len(re.sub(r"[\W_]", "", dst, flags=re.UNICODE))
    if src_chars >= 16:
        cross_script = _uses_unspaced_translation_script(src) != _uses_unspaced_translation_script(dst)
        # A Han character can carry the information of several Latin letters.
        # Raw character ratios are not interchangeable between writing systems.
        minimum_ratio = 0.10 if cross_script else 0.24
        maximum_ratio = 6.0 if cross_script else 3.4
        if dst_chars < max(3, int(src_chars * minimum_ratio)):
            return f"probable-char-omission:{src_chars}->{dst_chars}"
        if dst_chars > max(src_chars + 28, int(src_chars * maximum_ratio)):
            return f"probable-char-expansion:{src_chars}->{dst_chars}"

    # A translation must not silently delete one side of a two-speaker cue.
    # Lines are translated independently, but this guards direct helper use too.
    src_speakers = len(re.findall(r"(?m)^\s*[-–—]\s*\S", str(source or "")))
    dst_speakers = len(re.findall(r"(?m)^\s*[-–—]\s*\S", str(output or "")))
    if src_speakers and src_speakers != dst_speakers:
        return f"speaker-count:{src_speakers}->{dst_speakers}"

    return None


def _strict_candidate_penalty(source: str, output: str) -> float:
    """Rank suspicious candidates when no decode passes the hard QA gate."""
    if not output:
        return 1_000_000.0
    reason = _strict_translation_quality_reason(source, output)
    src_words = max(1, len(_translation_word_tokens(source)))
    dst_words = max(1, len(_translation_word_tokens(output)))
    src_chars = max(1, len(re.sub(r"[\W_]", "", source, flags=re.UNICODE)))
    dst_chars = max(1, len(re.sub(r"[\W_]", "", output, flags=re.UNICODE)))
    word_weight = 0.0 if _uses_unspaced_translation_script(source) or _uses_unspaced_translation_script(output) else 12.0
    penalty = abs(dst_words / src_words - 1.0) * word_weight + abs(dst_chars / src_chars - 1.0) * 4.0
    if reason:
        penalty += 25.0
        if reason.startswith("source-echo"):
            penalty += 18.0
        elif "omission" in reason or "clause-loss" in reason:
            penalty += 14.0
        elif "expansion" in reason:
            penalty += 12.0
    return penalty


def _strip_authored_markup_for_context(text: str) -> str:
    """Return only visible text for neighbouring-cue translation context."""
    visible = "".join(value for kind, value in _strict_translation_parts(text) if kind == "text")
    visible = re.sub(r"(?m)^\s*[-–—]\s*", "", visible)
    return _clean_text(visible)


def _strict_context_translation(
    translator: Any,
    tokenizer: Any,
    source: str,
    target_language: str,
    previous_context: str = "",
    next_context: str = "",
) -> str:
    """Translate the current span together with neighbours, returning only it.

    MADLAD is a translation model rather than an instruction-following chat
    model.  Stable separators are therefore more reliable than natural-language
    prompts for supplying context without letting context leak into the result.
    """
    current = _clean_text(source)
    previous = _clean_text(previous_context)
    following = _clean_text(next_context)
    if not current or not (previous or following):
        return ""

    sep = " ||| "
    segments: list[str] = []
    current_index = 0
    if previous:
        segments.append(previous)
        current_index += 1
    segments.append(current)
    if following:
        segments.append(following)
    combined = sep.join(segments)
    translated = _decode_single_translation(
        translator,
        tokenizer,
        combined,
        target_language,
        beam_size=3,
        repetition_penalty=1.12,
        no_repeat_ngram_size=3,
        length_factor=2.15,
        length_extra=10,
    )
    parts = [part.strip() for part in re.split(r"\s*\|\s*\|\s*\|\s*", translated)]
    if len(parts) != len(segments):
        return ""
    candidate = _clean_text(parts[current_index])
    return candidate if candidate else ""


def _strict_retry_translation(
    translator: Any,
    tokenizer: Any,
    source: str,
    target_language: str,
    *,
    retry_attempts: int = 2,
) -> str:
    """Retry authored text with conservative decodes and return best candidate."""
    source = _clean_text(source)
    attempts = (
        # Do not prohibit recurring subword ngrams on rescue decodes: normal
        # inflection and legitimate repeated words also share SentencePieces.
        # Give cross-script translations room to finish, then check coverage.
        dict(beam_size=1, repetition_penalty=1.20, no_repeat_ngram_size=0, length_factor=3.0, length_extra=12),
        dict(beam_size=2, repetition_penalty=1.14, no_repeat_ngram_size=0, length_factor=3.0, length_extra=12),
        dict(beam_size=4, repetition_penalty=1.10, no_repeat_ngram_size=0, length_factor=3.5, length_extra=12),
    )
    best = ""
    best_penalty = 10**9
    for settings in attempts[:max(1, min(len(attempts), int(retry_attempts)))]:
        candidate = _decode_single_translation(
            translator, tokenizer, source, target_language, **settings
        )
        if not candidate:
            continue
        reason = _strict_translation_quality_reason(source, candidate)
        if reason is None:
            return candidate
        # Keep the least size-distorted fallback in case every decoder profile
        # is suspicious.  It is only used after all retries are exhausted.
        penalty = _strict_candidate_penalty(source, candidate)
        if penalty < best_penalty:
            best = candidate
            best_penalty = penalty
    return best


def _strict_sentence_retry_translation(
    translator: Any, tokenizer: Any, source: str, target_language: str,
) -> str:
    """Rescue omitted complete sentences without any language/phrase rewrites."""
    parts = [p.strip() for p in re.split(r"(?<=[!?…。！？])\s+|(?<=\.)\s+(?=[\w\"'“‘])", source) if p.strip()]
    if not 2 <= len(parts) <= 4:
        return ""
    translated: list[str] = []
    for part in parts:
        candidate = _strict_retry_translation(
            translator, tokenizer, part, target_language, retry_attempts=2,
        )
        if not candidate or _strict_translation_quality_reason(part, candidate) is not None:
            return ""
        translated.append(candidate)
    result = " ".join(translated)
    return result if _strict_translation_quality_reason(source, result) is None else ""


def _translate_payload_strict(
    translator: Any,
    tokenizer: Any,
    source: str,
    target_language: str,
    *,
    previous_context: str = "",
    next_context: str = "",
    qa_events: set[str] | None = None,
    initial_candidate: str | None = None,
    quality_profile: str = "balanced",
) -> str:
    """Translate one visible subtitle span with QA, context and retry."""
    raw = str(source or "")
    leading = raw[: len(raw) - len(raw.lstrip())]
    trailing = raw[len(raw.rstrip()):] if raw.rstrip() != raw else ""
    body = raw.strip()

    speaker_prefix = ""
    speaker_match = re.match(r"^([-–—]\s*)", body)
    if speaker_match:
        speaker_prefix = speaker_match.group(1)
        body = body[speaker_match.end():].lstrip()

    payload = _clean_text(body)
    if not payload:
        return raw
    if not re.search(r"[^\W\d_]", payload, flags=re.UNICODE):
        return raw

    profile_key = _normalise_translation_quality_profile(quality_profile)
    profile = AI_TRANSLATION_QUALITY_PROFILES[profile_key]
    candidates: list[str] = []
    initial = _clean_text(initial_candidate or "")
    if not initial:
        initial = _decode_single_translation(
            translator, tokenizer, payload, target_language,
            beam_size=int(profile["beam_size"]),
            repetition_penalty=float(profile["repetition_penalty"]),
            no_repeat_ngram_size=int(profile["no_repeat_ngram_size"]),
            length_factor=float(profile["length_factor"]),
            length_extra=int(profile["length_extra"]),
        )
    if initial:
        candidates.append(initial)

    initial_reason = _strict_translation_quality_reason(payload, initial)
    short_dialogue = len(_translation_word_tokens(payload)) <= 9
    fidelity = getattr(translator, "reconstruction_score", None)
    uncertain = callable(fidelity) and initial and fidelity(payload, initial) < -2.5
    # Structural QA alone cannot detect changed polarity or a wrong sense.
    # Short dialogue can use neighbours, but candidates must compete against
    # the original source instead of automatically accepting the last decode.
    use_context = initial_reason is not None or uncertain or (
        profile.get("context_mode") == "short_or_fail" and short_dialogue
    )
    if use_context and (previous_context or next_context):
        contextual = _strict_context_translation(
            translator, tokenizer, payload, target_language,
            previous_context=previous_context, next_context=next_context,
        )
        if contextual and contextual not in candidates:
            candidates.append(contextual)
            if qa_events is not None:
                qa_events.add("context")

    valid = [c for c in candidates if _strict_translation_quality_reason(payload, c) is None]
    if uncertain and not _cue_finishes_sentence(payload):
        # An authored fragment can be mistaken for a different sense. A
        # punctuation-only decode variant gives it a complete-input boundary;
        # it never supplies a replacement phrase or a target-language word.
        variant = _decode_single_translation(
            translator, tokenizer, payload.rstrip(",;:") + ".", target_language,
            beam_size=4, repetition_penalty=1.0, no_repeat_ngram_size=0,
            length_factor=3.0, length_extra=12,
        )
        if variant and _strict_translation_quality_reason(payload, variant) is None:
            valid.append(variant)
            if qa_events is not None:
                qa_events.add("retry")
    if valid and callable(fidelity):
        rank = getattr(translator, "candidate_score", None)
        candidate = max(valid, key=lambda c: rank(payload, c, target_language) if callable(rank) else fidelity(payload, c))
    elif len(candidates) > 1 and _strict_translation_quality_reason(payload, candidates[-1]) is None:
        candidate = candidates[-1]
    elif valid:
        candidate = min(valid, key=lambda c: _strict_candidate_penalty(payload, c))
    else:
        candidate = ""

    if not candidate:
        if qa_events is not None:
            qa_events.add("retry")
        retry = _strict_retry_translation(
            translator, tokenizer, payload, target_language,
            retry_attempts=int(profile.get("retry_attempts", 2)),
        )
        if retry:
            candidates.append(retry)
            if _strict_translation_quality_reason(payload, retry) is None:
                candidate = retry

    if not candidate:
        rescue = _strict_sentence_retry_translation(translator, tokenizer, payload, target_language)
        if rescue:
            candidates.append(rescue)
            if _strict_translation_quality_reason(payload, rescue) is None:
                candidate = rescue

    if not candidate and (previous_context or next_context):
        rescue_context = _strict_context_translation(
            translator, tokenizer, payload, target_language,
            previous_context=previous_context, next_context=next_context,
        )
        if rescue_context:
            candidates.append(rescue_context)
            if _strict_translation_quality_reason(payload, rescue_context) is None:
                candidate = rescue_context
                if qa_events is not None:
                    qa_events.add("context")

    if not candidate:
        # Retry first; only if decoding cannot resolve an obvious duplicated
        # alternative do we consider a conservative structural repair.
        repaired = [_remove_translation_clause_duplicates(payload, c) for c in candidates]
        repaired = [c for c in repaired if _strict_translation_quality_reason(payload, c) is None]
        if repaired:
            candidate = min(repaired, key=lambda c: _strict_candidate_penalty(payload, c))
            if qa_events is not None:
                qa_events.add("deduplicated")

    if not candidate:
        # Do not silently replace a suspicious translation with the source.  Keep
        # the least-risk candidate and explicitly mark this cue for review.
        usable = [c for c in candidates if c]
        candidate = min(usable, key=lambda c: _strict_candidate_penalty(payload, c)) if usable else payload
        if qa_events is not None:
            qa_events.add("review")

    candidate = _clean_text(candidate)
    if callable(fidelity) and len(_translation_word_tokens(payload)) >= 3 and fidelity(payload, candidate) < -2.5:
        if qa_events is not None:
            qa_events.add("review")
            qa_events.add("low_fidelity")
    # The model sometimes adds a dialogue dash even when translating only
    # the text after the authored dash. Speaker markers belong to the source
    # layout; restoring its single prefix must not produce "- - Yes".
    candidate = re.sub(r"^[-–—]\s+", "", candidate)
    if not _uses_unspaced_translation_script(candidate):
        # Repair missing spaces at genuine sentence boundaries, but leave
        # initials, decimals and unspaced writing systems untouched.
        candidate = " ".join(_translation_sentence_spans(candidate))
    return f"{leading}{speaker_prefix}{candidate}{trailing}"


def _authored_translation_lines(value: str) -> list[str]:
    """Join visual wrapping, retaining only explicit speaker boundaries.

    A newline in an authored subtitle is usually typesetting, not the end of a
    sentence. Sending each half to MT independently invents endings and loses
    meaning. An explicit dialogue dash still starts a separate speaker span.
    """
    groups: list[str] = []
    for line in str(value).splitlines():
        content = line.strip()
        if not content:
            continue
        if groups and not re.match(r"^[-–—]\s*\S", content):
            groups[-1] += " " + content
        else:
            groups.append(content)
    if groups:
        # Spaces around inline style/SDH boundaries still separate words.
        if value[:1].isspace():
            leading = value[:len(value) - len(value.lstrip())]
            groups[0] = ("\n" if "\n" in leading else " ") + groups[0]
        if value[-1:].isspace():
            trailing = value[len(value.rstrip()):]
            groups[-1] += "\n" if "\n" in trailing else " "
    return groups or [value]


def _authored_translation_segments(value: str) -> list[str]:
    """Keep each completed sentence, including short repeated commands.

    Translating several sentences at once can omit the final short ones even
    when the overall output looks plausible. This also preserves intentional
    repetitions without asking a repetition-penalized decoder to recreate them.
    """
    segments: list[str] = []
    for line in _authored_translation_lines(value):
        leading = line[:len(line) - len(line.lstrip())]
        trailing = line[len(line.rstrip()):]
        pieces: list[str] = []
        for piece in _translation_sentence_spans(line.strip()):
            if pieces and re.search(r"(?:\.{2,}|…)[\"'’”»)]*$", pieces[-1]):
                pieces[-1] += " " + piece
            else:
                pieces.append(piece)
        # A comma-separated repeated command is authored repetition too.
        # Sending the whole run to MT often contracts five repetitions into
        # two or three. Only split an exact repeated utterance, never ordinary
        # lists or clauses which merely share a few words.
        repeated: list[str] = []
        for piece in pieces:
            clauses = [m.group().strip() for m in re.finditer(r"[^,，،、]+[,，،、]?", piece) if m.group().strip()]
            keys = [_normalised_translation_text(c) for c in clauses]
            if len(keys) >= 2 and keys[0] and len(set(keys)) == 1:
                repeated.extend(clauses)
            else:
                repeated.append(piece)
        pieces = repeated
        if pieces:
            pieces[0] = leading + pieces[0]
            pieces[-1] += trailing
            segments.extend(pieces)
    return segments or [value]


def _translate_existing_cue_text_strict_with_qa(
    translator: Any,
    tokenizer: Any,
    text: str,
    target_language: str,
    *,
    previous_context: str = "",
    next_context: str = "",
    initial_candidates: list[str] | None = None,
    quality_profile: str = "balanced",
) -> tuple[str, set[str]]:
    """Translate one authored cue and return its QA events."""
    parts = _strict_translation_parts(text)
    initial_iter = iter(initial_candidates or [])
    translated: list[str] = []
    qa_events: set[str] = set()
    has_literal_markup = any(kind == "literal" for kind, _value in parts)
    for kind, value in parts:
        if kind == "literal":
            translated.append(value)
        else:
            line_parts = _authored_translation_segments(value)
            single_speaker = len(_authored_translation_lines(value)) == 1
            previous_utterance = ""
            previous_translation = ""
            for line_index, content in enumerate(line_parts):
                if line_index:
                    translated.append("\n")
                initial_candidate = next(initial_iter, None)
                # Adjacent identical utterances from one speaker should not
                # become unrelated synonyms solely because their punctuation
                # or neighbour context differs. Reuse words, not punctuation
                # or the previous utterance's speaker dash. Never cross styles
                # or explicit speaker boundaries.
                body = re.sub(r"^[-–—]\s*", "", content.strip())
                utterance = re.sub(r"[,，،、.!?。！？]+$", "", body).strip().casefold()
                if (single_speaker and utterance and utterance == previous_utterance
                        and re.search(r"[,，،、.!?。！？]+$", previous_translation.strip())):
                    words = re.sub(r"^[-–—]\s*", "", previous_translation.strip())
                    words = re.sub(r"[,，،、.!?。！？]+$", "", words).rstrip()
                    ending = re.search(r"[,，،、.!?。！？]+$", body)
                    prefix = re.match(r"^([-–—]\s*)", content.strip())
                    leading = content[:len(content) - len(content.lstrip())]
                    trailing = content[len(content.rstrip()):]
                    translated.append(leading + (prefix.group(1) if prefix else "") + words
                                      + (ending.group() if ending else "") + trailing)
                    continue
                try:
                    translated_piece = _translate_payload_strict(
                        translator, tokenizer, content, target_language,
                        previous_context=line_parts[line_index - 1] if single_speaker and line_index > 0 else previous_context,
                        next_context=line_parts[line_index + 1] if single_speaker and line_index + 1 < len(line_parts) else next_context,
                        qa_events=qa_events,
                        initial_candidate=initial_candidate,
                        quality_profile=quality_profile,
                    )
                except TypeError as exc:
                    # Preserve compatibility with integrations/tests that replace
                    # the legacy four-argument helper with a simple callable.
                    if "unexpected keyword argument" not in str(exc):
                        raise
                    translated_piece = _translate_payload_strict(
                        translator, tokenizer, content, target_language
                    )
                translated.append(translated_piece)
                previous_utterance = utterance
                previous_translation = translated_piece
    result = "".join(translated).strip()
    if result and not has_literal_markup:
        if "\n" in result:
            result = "\n".join(_wrap_subtitle_text(line) for line in result.splitlines())
        else:
            result = _wrap_subtitle_text(result)
    return result, qa_events


def _translate_existing_cue_text_strict(
    translator: Any,
    tokenizer: Any,
    text: str,
    target_language: str,
) -> str:
    result, _events = _translate_existing_cue_text_strict_with_qa(
        translator, tokenizer, text, target_language
    )
    return result


def _strict_cue_payloads(text: str) -> list[str]:
    """Return payloads in the exact order consumed by strict cue translation."""
    payloads: list[str] = []
    for kind, value in _strict_translation_parts(text):
        if kind != "text":
            continue
        for content in _authored_translation_segments(value):
            body = content.strip()
            speaker_match = re.match(r"^([-–—]\s*)", body)
            if speaker_match:
                body = body[speaker_match.end():].lstrip()
            payload = _clean_text(body)
            if payload and re.search(r"[^\W\d_]", payload, flags=re.UNICODE):
                payloads.append(payload)
            else:
                payloads.append("")
    return payloads


def _authored_sentence_payload(text: str) -> tuple[str, str, str] | None:
    """Detach whole-cue styles for safe sentence context across timed cues.

    Inline styling, SDH descriptions and multi-speaker cues stay on the
    span-preserving path. Never merge different presentation/positioning.
    """
    parts = _strict_translation_parts(text)
    indices = [i for i, (kind, value) in enumerate(parts) if kind == "text" and value.strip()]
    if len(indices) != 1 or any(value in {"[", "]"} for kind, value in parts if kind == "literal"):
        return None
    index = indices[0]
    value = parts[index][1]
    if len(_authored_translation_segments(value)) != 1 or re.match(r"^\s*[-–—]", value):
        return None
    prefix = "".join(v for _kind, v in parts[:index])
    suffix = "".join(v for _kind, v in parts[index + 1:])
    return _clean_text(value), prefix, suffix


def _authored_sentence_groups(items: list[SubtitleCue]) -> list[list[int]]:
    """Group only nearby continuations of an unfinished source sentence."""
    groups: list[list[int]] = []
    index = 0
    while index < len(items):
        current = _authored_sentence_payload(items[index].text)
        group = [index]
        length = len(current[0]) if current else 0
        while current and index + 1 < len(items) and len(group) < 4:
            following = _authored_sentence_payload(items[index + 1].text)
            gap = items[index + 1].start - items[index].end
            if (not following or _cue_finishes_sentence(current[0])
                    or current[1:] != following[1:] or not 0 <= gap <= 1.25
                    or length + 1 + len(following[0]) > 320):
                break
            index += 1
            group.append(index)
            length += 1 + len(following[0])
            current = following
        if len(group) > 1:
            groups.append(group)
        index += 1
    return groups


def _distribute_authored_sentence(text: str, sources: list[str]) -> list[str]:
    """Allocate contextual MT text to original windows without word duplication."""
    text = _clean_text(text)
    # Whitespace boundaries for spaced scripts; character boundaries for
    # scripts where words are not separated. Never assume target word counts.
    if _uses_unspaced_translation_script(text):
        boundaries = list(range(1, len(text)))
    else:
        boundaries = [match.start() for match in re.finditer(r"\s+", text)]
    if len(boundaries) < len(sources) - 1:
        return []
    weights = [max(1, len(_clean_text(source))) for source in sources]
    total = sum(weights)
    offset = 0
    cumulative = 0
    pieces: list[str] = []
    for index, weight in enumerate(weights[:-1]):
        cumulative += weight
        ideal = len(text) * cumulative / total
        available = [boundary for boundary in boundaries if boundary > offset]
        remaining = len(sources) - index - 2
        if remaining:
            available = available[:-remaining]
        if not available:
            return []
        boundary = min(available, key=lambda b: abs(b - ideal) - (1.5 if text[b - 1] in ",;.!?…。，；！？" else 0))
        pieces.append(text[offset:boundary].strip())
        offset = boundary
    pieces.append(text[offset:].strip())
    return pieces if all(pieces) else []


def translate_existing_subtitle_cues_with_ai(
    cues: Iterable[SubtitleCue],
    source_language: str,
    target_language: str,
    *,
    model_name: str | None = None,
    cancel_event: Any | None = None,
    pause_event: Any | None = None,
    progress: Callable[[int, int], None] | None = None,
    log: Callable[[str], None] | None = None,
    qa_report: dict[str, Any] | None = None,
    quality_profile: str = "balanced",
    partial_callback: Callable[[list[SubtitleCue], str], None] | None = None,
    unit_callback: Callable[[int, int, list[SubtitleCue]], None] | None = None,
    resume_unit_offset: int = 0,
    initial_translated: Iterable[SubtitleCue] | None = None,
) -> list[SubtitleCue]:
    """Translate an authored subtitle with cue count/timestamps hard-locked.

    This path is language-pair agnostic. Unfinished sentences can share MT
    context, but their text is distributed back to the original cue windows.
    The model cannot rewrite subtitle formatting. Readability splitting is a
    separate deterministic pass after source-structure validation.
    """
    source_language = "und" if str(source_language or "und").lower() in {"und", "auto", ""} else normalise_asr_language(source_language)
    target_language = normalise_asr_language(target_language)
    if target_language not in TRANSLATION_TARGET_CODES:
        raise UserVisibleError(
            ui_text("error_translation_target_unsupported", language=target_language)
        )
    if target_language == source_language:
        raise UserVisibleError(
            ui_text("error_translation_same_language", language=target_language)
        )
    items = [cue for cue in cues if cue.text.strip() and cue.end > cue.start]
    if not items:
        return []

    logger = log or (lambda _message: None)
    profile_key = _normalise_translation_quality_profile(quality_profile)
    profile = AI_TRANSLATION_QUALITY_PROFILES[profile_key]

    def wait_if_paused() -> None:
        while pause_event is not None and pause_event.is_set():
            if cancel_event is not None and cancel_event.is_set():
                raise OperationCancelled()
            time.sleep(0.12)

    model_name = (
        model_name or os.environ.get("LURVIKO_SUBTITLE_AI_TRANSLATION_MODEL", DEFAULT_TRANSLATION_MODEL)
    ).strip() or DEFAULT_TRANSLATION_MODEL
    model_path = _prepare_translation_model(model_name, logger, cancel_event)
    translator, tokenizer, device = _load_translation_runtime(model_path, logger)
    ranking_supported = callable(getattr(translator, "score_batch", None))
    batching_supported = callable(getattr(translator, "translate_batch", None))
    device_batch, device_beam = _translation_decode_profile(device, logger)
    if batching_supported:
        translator = _AdaptiveTranslationBackend(translator, model_path, device, logger, cancel_event)
    profile = dict(profile, beam_size=min(int(profile["beam_size"]), max(2, device_beam)))
    if source_language in TRANSLATION_TARGET_CODES and ranking_supported:
        translator = _FaithfulSubtitleTranslator(translator, tokenizer, source_language, cancel_event, pause_event)
        logger("Lurviko AI Translation: source-reconstruction ranking enabled (same local model, bounded alternatives)")
    total = len(items)
    logger(
        "Lurviko AI Translation: strict authored-subtitle mode; "
        f"{total} cue(s), {source_language}->{target_language}; timings/markup locked; "
        f"quality={profile_key}"
    )
    output: list[SubtitleCue] = list(initial_translated or [])
    retried_cues = 0
    context_cues = 0
    review_items: list[dict[str, Any]] = []
    contextual_by_cue: dict[int, tuple[str, set[str]]] = {}
    sentence_groups = _authored_sentence_groups(items)
    group_payloads = [
        " ".join(_authored_sentence_payload(items[i].text)[0] for i in group)
        for group in sentence_groups
    ]
    env_batch = os.environ.get("LURVIKO_SUBTITLE_AI_TRANSLATION_BATCH_SIZE", "").strip()
    batch_size = max(1, min(64, device_batch, int(env_batch or profile["batch_size"])))
    can_batch = batching_supported and callable(getattr(tokenizer, "encode", None))
    initial_groups = ["" for _ in sentence_groups]
    group_by_first_cue = {group[0]: i for i, group in enumerate(sentence_groups)}
    grouped_indices = {i for group in sentence_groups for i in group}
    group_end_by_cue = {i: group[-1] + 1 for group in sentence_groups for i in group}
    if sentence_groups:
        logger(f"Lurviko AI Translation: {len(sentence_groups)} unfinished sentence group(s) share translation context; original cue windows retained")

    # Prepare metadata only. Decode and finish QA for a small chronological
    # window before advancing, rather than translating the entire file while
    # progress remains at zero. Sentence groups must never cross a window.
    per_cue_payloads = [_strict_cue_payloads(cue.text) for cue in items]
    initial_by_cue: list[list[str]] = [["" for _ in payloads] for payloads in per_cue_payloads]
    prepared_until = 0
    if can_batch:
        logger(f"Lurviko AI Translation: incremental translation + QA (up to {batch_size} spans/batch)")
    else:
        logger("Lurviko AI Translation: batch first pass unavailable; using compatibility path")

    resume_unit_offset = max(0, min(int(resume_unit_offset or 0), total))
    group_by_cue = {cue_idx: group_idx for group_idx, group in enumerate(sentence_groups) for cue_idx in group}
    for index, cue in enumerate(items[resume_unit_offset:], start=resume_unit_offset + 1):
        wait_if_paused()
        if cancel_event is not None and cancel_event.is_set():
            raise OperationCancelled()
        if index - 1 >= prepared_until:
            window_start = index - 1
            # A small warm-up window gets real completed-cue progress out
            # quickly; later windows retain useful batching throughput.
            window_size = min(batch_size, 4 if window_start == 0 else 8)
            window_end = min(total, window_start + window_size)
            window_end = max(window_end, group_end_by_cue.get(window_end - 1, window_end))
            logger(f"Lurviko AI Translation: translating/checking cues {index}-{window_end}/{total}")
            if can_batch:
                refs: list[tuple[str, int, int, str]] = []
                # A checkpoint can stop midway through an unfinished sentence.
                # Decode its complete context again, then publish only the remainder.
                resumed_group = group_by_cue.get(window_start)
                if resumed_group is not None and sentence_groups[resumed_group][0] < window_start:
                    refs.append(("group", resumed_group, 0, group_payloads[resumed_group]))
                for cue_idx in range(window_start, window_end):
                    if cue_idx in group_by_first_cue:
                        group_index = group_by_first_cue[cue_idx]
                        refs.append(("group", group_index, 0, group_payloads[group_index]))
                    elif cue_idx not in grouped_indices:
                        refs.extend(("cue", cue_idx, payload_idx, payload)
                                    for payload_idx, payload in enumerate(per_cue_payloads[cue_idx]) if payload)
                for offset in range(0, len(refs), batch_size):
                    wait_if_paused()
                    if cancel_event is not None and cancel_event.is_set():
                        raise OperationCancelled()
                    chunk = refs[offset:offset + batch_size]
                    decoded = _decode_translation_batch(
                        translator, tokenizer, [entry[3] for entry in chunk], target_language,
                        beam_size=int(profile["beam_size"]),
                        repetition_penalty=float(profile["repetition_penalty"]),
                        no_repeat_ngram_size=int(profile["no_repeat_ngram_size"]),
                        length_factor=float(profile["length_factor"]),
                        length_extra=int(profile["length_extra"]),
                    )
                    wait_if_paused()
                    if cancel_event is not None and cancel_event.is_set():
                        raise OperationCancelled()
                    for (kind, ref_index, payload_idx, _payload), candidate in zip(chunk, decoded):
                        if kind == "group":
                            initial_groups[ref_index] = candidate
                        else:
                            initial_by_cue[ref_index][payload_idx] = candidate
            prepared_until = window_end
        previous_context = _strip_authored_markup_for_context(items[index - 2].text) if index > 1 and 0 <= cue.start - items[index - 2].end <= 2.0 else ""
        next_context = _strip_authored_markup_for_context(items[index].text) if index < total and 0 <= items[index].start - cue.end <= 2.0 else ""
        if index - 1 in group_by_cue and index - 1 not in contextual_by_cue:
            group_index = group_by_cue[index - 1]
            group = sentence_groups[group_index]
            events: set[str] = {"sentence_context"}
            translated_sentence = _translate_payload_strict(
                translator, tokenizer, group_payloads[group_index], target_language,
                qa_events=events, quality_profile=profile_key,
                initial_candidate=initial_groups[group_index],
            )
            payloads = [_authored_sentence_payload(items[i].text) for i in group]
            pieces = _distribute_authored_sentence(translated_sentence, [p[0] for p in payloads])
            if pieces:
                for i, piece, (_source, prefix, suffix) in zip(group, pieces, payloads):
                    contextual_by_cue[i] = (prefix + piece + suffix, set(events))
        if index - 1 in contextual_by_cue:
            translated_text, events = contextual_by_cue[index - 1]
        else:
            try:
                translated_text, events = _translate_existing_cue_text_strict_with_qa(
                    translator, tokenizer, cue.text, target_language,
                    previous_context=previous_context, next_context=next_context,
                    initial_candidates=initial_by_cue[index - 1],
                    quality_profile=profile_key,
                )
            except TypeError as exc:
                if "unexpected keyword argument" not in str(exc):
                    raise
                translated_text, events = _translate_existing_cue_text_strict_with_qa(
                    translator, tokenizer, cue.text, target_language,
                    previous_context=previous_context, next_context=next_context,
                )
        if not translated_text:
            translated_text = cue.text
            events.add("review")
        if "retry" in events:
            retried_cues += 1
        if "context" in events or "sentence_context" in events:
            context_cues += 1
        if "review" in events:
            review_items.append({
                "cue": index,
                "start": cue.start,
                "end": cue.end,
                "source": cue.text,
                "output": translated_text,
                "reasons": sorted(events),
            })
        finished = SubtitleCue(cue.start, cue.end, translated_text)
        output.append(finished)
        pieces = readable_subtitle_cues([finished])
        if partial_callback is not None and pieces:
            partial_callback(list(pieces), "translated")
        if unit_callback is not None:
            unit_callback(index, total, list(pieces))
        if progress is not None:
            progress(index, total)

    summary = {
        "total": total,
        "successful": total - len(review_items),
        "retried": retried_cues,
        "context_used": context_cues,
        "review": len(review_items),
        "review_items": review_items,
        "quality_profile": profile_key,
        "sentence_groups": len(sentence_groups),
    }
    if qa_report is not None:
        qa_report.clear()
        qa_report.update(summary)
    logger(
        "Lurviko AI Translation QA: "
        f"{total} cue / {summary['successful']} passed / {retried_cues} retried / "
        f"{len(review_items)} review required / {context_cues} context-assisted"
    )
    for item in review_items[:20]:
        logger(f"QA review cue {item['cue']} ({', '.join(item['reasons'])}): {item['source']!r} -> {item['output']!r}")
    return output
