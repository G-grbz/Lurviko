"""Regression cases for the quality engine adapted from G-TMCE.

No model downloads or real inference are required by these tests.
"""
import re
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/subtitle-ai"))
from gfile_subtitle_ai import translation_quality as tr


def test_ranking_caches_are_bounded_and_reuse_recent_scores():
    from types import SimpleNamespace
    calls = []

    class Tokenizer:
        def encode(self, text, out_type=str):
            return text.split()

    class Engine:
        def score_batch(self, sources, targets):
            calls.extend(sources)
            return [SimpleNamespace(log_probs=[-0.5]) for _ in sources]

    ranked = tr._FaithfulSubtitleTranslator(Engine(), Tokenizer(), "en")
    for index in range(700):
        assert ranked.reconstruction_score(f"Source {index}.", f"Ziel {index}.") == -0.5
        assert ranked.candidate_score(f"Source {index}.", f"Ziel {index}.", "de") == -1.0
    assert len(ranked.reconstruction_scores) == 512
    assert len(ranked.translation_scores) == 512
    count = len(calls)
    ranked.candidate_score("Source 699.", "Ziel 699.", "de")
    assert len(calls) == count


def test_gpu_oom_splits_batches_and_reduces_beam_without_losing_order():
    calls = []

    class Engine:
        def translate_batch(self, sources, **kwargs):
            calls.append((len(sources), kwargs["beam_size"]))
            if len(sources) > 1 or kwargs["beam_size"] > 1:
                raise RuntimeError("CUDA out of memory")
            assert kwargs["num_hypotheses"] == 1
            return sources

    backend = tr._AdaptiveTranslationBackend(Engine(), Path("unused"), "cuda", lambda _: None, None)
    assert backend.translate_batch([["first"], ["second"]], beam_size=4, num_hypotheses=4) == [["first"], ["second"]]
    assert (2, 4) in calls and (1, 1) in calls
    assert backend.device == "cuda"


def test_gpu_scoring_oom_falls_back_to_cpu_and_preserves_source_target_pairs(monkeypatch):
    from types import SimpleNamespace
    calls = []

    class GPU:
        def score_batch(self, sources, targets):
            raise RuntimeError("CUDA out of memory")

        def unload_model(self):
            calls.append("unload")

    class CPU:
        def __init__(self, path, **kwargs):
            assert kwargs == {"device": "cpu", "compute_type": "int8"}

        def score_batch(self, sources, targets):
            return list(zip(sources, targets))

    monkeypatch.setitem(sys.modules, "ctranslate2", SimpleNamespace(Translator=CPU))
    backend = tr._AdaptiveTranslationBackend(GPU(), Path("unused"), "cuda", lambda _: None, None)
    sources, targets = [["one"], ["two"]], [["eins"], ["zwei"]]
    assert backend.score_batch(sources, targets) == list(zip(sources, targets))
    assert calls == ["unload"] and backend.device == "cpu"


def test_cancelled_scoring_and_retries_do_not_touch_the_backend():
    import threading
    from types import SimpleNamespace
    from gfile_subtitle_ai.core import OperationCancelled
    cancel = threading.Event()
    cancel.set()
    backend = tr._AdaptiveTranslationBackend(object(), Path("unused"), "cuda", lambda _: None, cancel)
    ranked = tr._FaithfulSubtitleTranslator(backend, SimpleNamespace(), "en", cancel)
    with pytest.raises(OperationCancelled):
        ranked.reconstruction_score("Source.", "Ziel.")
    with pytest.raises(OperationCancelled):
        backend.translate_batch([["source"]], beam_size=4)


def test_live_and_checkpoint_layout_match_final_subtitles(monkeypatch):
    from types import SimpleNamespace
    from gfile_subtitle_ai import transcription
    translated = "Dies ist eine sehr lange übersetzte Nachricht mit vielen wichtigen Einzelheiten, die vollständig und gut lesbar bleiben müssen."

    class Tokenizer:
        def encode(self, text, out_type=str):
            return text.split()

        def decode(self, tokens):
            return " ".join(tokens)

    class Engine:
        def translate_batch(self, sources, **kwargs):
            return [SimpleNamespace(hypotheses=[translated.split() + ["</s>"]]) for _ in sources]

    monkeypatch.setattr(tr, "_prepare_translation_model", lambda *a: Path("unused"))
    monkeypatch.setattr(tr, "_load_translation_runtime", lambda *a: (Engine(), Tokenizer(), "cpu"))
    monkeypatch.setattr(tr, "_strict_translation_quality_reason", lambda *a: None)
    source = [tr.SubtitleCue(1, 10, "A long message that should remain readable and complete.")]
    live, checkpoint = [], []
    final = transcription.translate_cues_with_ai(
        source, "en", "de", authored=True,
        partial_callback=lambda pieces, phase: live.extend(pieces),
        unit_callback=lambda done, total, pieces: checkpoint.append((done, total, pieces)),
    )
    assert final == live == checkpoint[0][2]
    assert checkpoint[0][:2] == (1, 1)
    assert len(final) > 1


def test_resume_inside_a_sentence_uses_full_context_and_publishes_only_remainder(monkeypatch):
    from types import SimpleNamespace
    sources = []

    class Tokenizer:
        def encode(self, text, out_type=str):
            return text.split()

        def decode(self, tokens):
            return " ".join(tokens)

    class Engine:
        def translate_batch(self, payloads, **kwargs):
            sources.extend(" ".join(p) for p in payloads)
            return [SimpleNamespace(hypotheses=[["Gemeinsam", "können", "wir", "helfen.", "</s>"]]) for _ in payloads]

    monkeypatch.setattr(tr, "_prepare_translation_model", lambda *a: Path("unused"))
    monkeypatch.setattr(tr, "_load_translation_runtime", lambda *a: (Engine(), Tokenizer(), "cpu"))
    monkeypatch.setattr(tr, "_strict_translation_quality_reason", lambda *a: None)
    cues = [tr.SubtitleCue(1, 2, "We can work,"), tr.SubtitleCue(2, 3, "and help together.")]
    complete = tr.translate_existing_subtitle_cues_with_ai(cues, "en", "de")
    sources.clear()
    published, completed = [], []
    resumed = tr.translate_existing_subtitle_cues_with_ai(
        cues, "en", "de", resume_unit_offset=1, initial_translated=complete[:1],
        partial_callback=lambda pieces, phase: published.extend(pieces),
        unit_callback=lambda done, total, pieces: completed.append(done),
    )
    assert resumed == complete
    assert published == tr.readable_subtitle_cues(complete[1:]) and completed == [2]
    assert any("We can work, and help together." in text for text in sources)


def test_unknown_source_language_skips_reconstruction_but_keeps_eos(monkeypatch):
    from types import SimpleNamespace
    encoded = []

    class Tokenizer:
        def encode(self, text, out_type=str):
            return text.split()

        def decode(self, tokens):
            return " ".join(tokens)

    class Engine:
        def translate_batch(self, sources, **kwargs):
            encoded.extend(sources)
            return [SimpleNamespace(hypotheses=[["Guten", "Abend.", "</s>"]]) for _ in sources]

        def score_batch(self, *args, **kwargs):
            pytest.fail("Unknown source language cannot be used as a reverse model tag")

    monkeypatch.setattr(tr, "_prepare_translation_model", lambda *a: Path("unused"))
    monkeypatch.setattr(tr, "_load_translation_runtime", lambda *a: (Engine(), Tokenizer(), "cpu"))
    result = tr.translate_existing_subtitle_cues_with_ai([tr.SubtitleCue(1, 2, "Good evening.")], "und", "de")
    assert result[0].text == "Guten Abend."
    assert all(tokens[-1] == "</s>" for tokens in encoded)


def test_translation_upgrade_keeps_source_and_other_language_caches():
    import worker
    state = {"source_complete": True, "detected_language": "en", "translations": {
        "tr": {"complete": True, "completed_units": 12},
        "de": {"engine_version": worker.TRANSLATION_ENGINE_VERSION, "completed_units": 3},
    }}
    worker.prepare_translation_state(state, "tr")
    assert state["source_complete"] and state["detected_language"] == "en"
    assert state["translations"]["tr"] == {"engine_version": worker.TRANSLATION_ENGINE_VERSION}
    assert state["translations"]["de"]["completed_units"] == 3
    state["translations"]["tr"]["completed_units"] = 2
    worker.prepare_translation_state(state, "tr")
    assert state["translations"]["tr"]["completed_units"] == 2


def test_cuda_loading_fallback_reports_the_actual_cpu_device(monkeypatch, tmp_path):
    from types import SimpleNamespace
    from gfile_subtitle_ai import transcription
    (tmp_path / "model.bin").touch()
    (tmp_path / "spiece.model").touch()
    calls = []

    def load_model(path, device, compute_type):
        calls.append(device)
        if device == "cuda":
            raise RuntimeError("CUDA driver unavailable")
        return "cpu-model"

    monkeypatch.setitem(sys.modules, "ctranslate2", SimpleNamespace(Translator=load_model))
    monkeypatch.setitem(sys.modules, "sentencepiece", SimpleNamespace(SentencePieceProcessor=lambda **kwargs: "tokenizer"))
    monkeypatch.setenv("LURVIKO_SUBTITLE_AI_TRANSLATION_DEVICE", "cuda")
    monkeypatch.setenv("LURVIKO_SUBTITLE_AI_PRIVATE_CUDA_READY", "1")
    monkeypatch.setattr(transcription, "_best_cuda_compute_type", lambda: "int8")
    assert transcription._load_translation_runtime(tmp_path, lambda _: None) == ("cpu-model", "tokenizer", "cpu")
    assert calls == ["cuda", "cpu"]


def test_authored_short_words_are_preserved_and_only_asr_fragments_are_filtered(monkeypatch):
    from gfile_subtitle_ai import transcription
    seen = []

    def translate(cues, *args, **kwargs):
        seen.append(list(cues))
        return list(cues)

    monkeypatch.setattr(tr, "translate_existing_subtitle_cues_with_ai", translate)
    cues = [tr.SubtitleCue(1, 1.5, "The"), tr.SubtitleCue(2, 3, "Good evening.")]
    transcription.translate_cues_with_ai(cues, "en", "de", authored=True)
    transcription.translate_cues_with_ai(cues, "en", "de", authored=False)
    assert seen[0] == cues
    assert seen[1] == cues[1:]


@pytest.mark.parametrize('text,expected', [('The only way to solve this\nwas to work together.', ['The only way to solve this was to work together.']), ('Uma frase longa\nem duas linhas.', ['Uma frase longa em duas linhas.']), ('这是同一句话\n只是换了行。', ['这是同一句话 只是换了行。']), ('- First speaker\nfinishes here.\n- Second speaker.', ['- First speaker finishes here.', '- Second speaker.'])])
def test_display_newlines_are_not_translation_boundaries(text, expected):
    assert tr._authored_translation_lines(text) == expected


def test_inline_styles_keep_separating_whitespace(monkeypatch):
    monkeypatch.setattr(tr, '_decode_single_translation', lambda *a, **k: 'translated')
    result = tr._translate_existing_cue_text_strict(object(), object(), 'Hello <b>world</b> today.', 'de')
    assert result == 'translated <b>translated</b> translated'


@pytest.mark.parametrize('source,output,expected', [('We knew one another.', 'Birbirimizi tanıyorduk, birbirimizi tanırdık.', 'Birbirimizi tanıyorduk.'), ('A problem was going to happen,', 'Ama kötü bir şey olacaktı, kötü şeyler olacakdı.', 'Ama kötü bir şey olacaktı.'), ('Un message unique.', 'Eine wichtige Nachricht. Eine wichtige Nachricht.', 'Eine wichtige Nachricht.'), ('A single statement.', '这是一条重要消息。这是一条重要消息。', '这是一条重要消息。')])
def test_alternative_duplicate_repair_is_language_agnostic(source, output, expected):
    assert tr._remove_translation_clause_duplicates(source, output) == expected


def test_genuine_source_repetition_is_not_removed():
    source = 'Come on, come on, come on.'
    output = 'Hadi, hadi, hadi.'
    assert tr._remove_translation_clause_duplicates(source, output) == output
    assert tr._strict_translation_quality_reason(source, output) is None


@pytest.mark.parametrize('source,output', [('We used to know each other.', 'Birbirimizi tanıyorduk.'), ('Get out of the vehicle, please.', 'Araçtan çıkın lütfen.'), ('Please come home before it gets dark.', '请在天黑之前回家。'), ('请在天黑之前回家。', 'Please come home before it gets dark.'), ('请在天黑之前回家，因为今晚可能会下雨。', 'Veuillez rentrer chez vous avant la tombée de la nuit, car il peut pleuvoir ce soir.')])
def test_morphology_and_unspaced_scripts_are_not_omissions(source, output):
    assert tr._strict_translation_quality_reason(source, output) is None


def test_unfinished_sentence_groups_respect_styles_gaps_and_speakers():
    cues = [tr.SubtitleCue(1, 2, '<i>The only way</i>'), tr.SubtitleCue(2.001, 3, '<i>was to help.</i>'), tr.SubtitleCue(4, 5, 'Another phrase,'), tr.SubtitleCue(9, 10, 'after a long gap.'), tr.SubtitleCue(10.001, 11, '<i>A different style,</i>'), tr.SubtitleCue(11.001, 12, 'cannot be merged.'), tr.SubtitleCue(13, 14, '- One person,\n- Another person.')]
    assert tr._authored_sentence_groups(cues) == [[0, 1]]


@pytest.mark.parametrize('target', ['Eine längere Nachricht mit mehreren wichtigen Teilen.', '这是一条包含多个重要部分的较长消息。', 'هذه رسالة أطول تحتوي على عدة أجزاء مهمة.'])
def test_sentence_distribution_never_duplicates_or_drops_words(target):
    pieces = tr._distribute_authored_sentence(target, ['A first part,', 'and the ending.'])
    assert len(pieces) == 2
    assert re.sub('\\s+', '', ''.join(pieces)) == re.sub('\\s+', '', target)


def test_missing_sentence_rescue_translates_complete_sentences_not_display_lines(monkeypatch):
    seen = []

    def retry(_translator, _tokenizer, source, _target, **kwargs):
        seen.append(source)
        return {'We should leave!': 'Wir sollten gehen!', 'It is late!': 'Es ist spät!'}[source]
    monkeypatch.setattr(tr, '_strict_retry_translation', retry)
    output = tr._strict_sentence_retry_translation(object(), object(), 'We should leave! It is late!', 'de')
    assert seen == ['We should leave!', 'It is late!']
    assert output == 'Wir sollten gehen! Es ist spät!'


def test_unfinished_sentence_first_pass_is_batched(monkeypatch):
    from types import SimpleNamespace
    batch_sizes = []

    class Tokenizer:

        def encode(self, text, out_type=str):
            return text.split()

        def decode(self, tokens):
            return ' '.join(tokens)

    class Translator:

        def translate_batch(self, payloads, **kwargs):
            batch_sizes.append(len(payloads))
            return [SimpleNamespace(hypotheses=[['Gemeinsam', 'können', 'wir', 'helfen.']]) for _ in payloads]
    monkeypatch.setattr(tr, '_prepare_translation_model', lambda *a, **k: object())
    monkeypatch.setattr(tr, '_load_translation_runtime', lambda *a, **k: (Translator(), Tokenizer(), 'cpu'))
    monkeypatch.setattr(tr, '_strict_translation_quality_reason', lambda *a, **k: None)
    cues = [tr.SubtitleCue(3 * i, 3 * i + 1, 'We can work,' if i % 2 == 0 else 'and help together.') for i in range(6)]
    cues = [tr.SubtitleCue(i // 2 * 5 + i % 2, i // 2 * 5 + i % 2 + 1, cue.text) for i, cue in enumerate(cues)]
    progress = []
    result = tr.translate_existing_subtitle_cues_with_ai(cues, 'en', 'de', progress=lambda n, t: progress.append(n))
    assert batch_sizes == [2, 1]
    assert len(result) == len(cues)
    assert [(c.start, c.end) for c in result] == [(c.start, c.end) for c in cues]
    assert progress == list(range(1, 7))


@pytest.mark.parametrize('tokens,limit,expected', [(['Ein', 'halbes', 'Wor'], 3, ''), (['Ein', 'Wort', '</s>'], 3, 'Ein Wort'), (['完整', '句子', '</s>'], 3, '完整 句子'), (['A', 'complete', 'sentence.'], 10, 'A complete sentence.')])
def test_token_limit_fragments_never_reach_translation_fallback(tokens, limit, expected):
    from types import SimpleNamespace
    tokenizer = SimpleNamespace(decode=lambda tokens: ' '.join(tokens))
    result = SimpleNamespace(hypotheses=[tokens])
    assert tr._decode_translation_result(result, tokenizer, 'de', max_length=limit) == expected


def test_batch_checks_completion_for_every_hypothesis():
    from types import SimpleNamespace

    class Translator:

        def translate_batch(self, payloads, **kwargs):
            assert kwargs['return_end_token'] is True
            limit = kwargs['max_decoding_length']
            return [SimpleNamespace(hypotheses=[['partial'] * limit]), SimpleNamespace(hypotheses=[['Fertig.', '</s>']])]
    tokenizer = SimpleNamespace(encode=lambda text, **kwargs: text.split(), decode=lambda tokens: ' '.join(tokens))
    result = tr._decode_translation_batch(Translator(), tokenizer, ['A sentence.', 'Another sentence.'], 'de')
    assert result == ['', 'Fertig.']


@pytest.mark.parametrize('source,output,expected', [('A package arrived today.', 'Das Paket ist heute angekommen.Das Paket, das ist heute angekommen.', 'Das Paket ist heute angekommen.'), ('Une seule phrase.', '这是一个句子。这是一个句子。', '这是一个句子。'), ('...cats and dogs,', '...Katzen und Hunde, Katzen und Haustiere.', '...Katzen und Hunde.')])
def test_sentence_duplicate_repair_handles_internal_commas_and_missing_spaces(source, output, expected):
    assert tr._remove_translation_clause_duplicates(source, output) == expected


@pytest.mark.parametrize('source,output', [('Feed the animals and then clean their shelter.', 'Füttere die Tiere. Dann reinige ihr Gehege.'), ('The cats and dogs, then cats and birds.', 'Katzen und Hunde, dann Katzen und Vögel.'), ('Go home! Go home!', 'Geh nach Hause! Geh nach Hause!')])
def test_duplicate_repair_keeps_distinct_clauses_and_genuine_repetition(source, output):
    assert tr._remove_translation_clause_duplicates(source, output) == output


@pytest.mark.parametrize('text,expected', [('Eine Antwort.Noch eine.', ['Eine Antwort.', 'Noch eine.']), ('E.V., can you check version 2.5?', ['E.V., can you check version 2.5?']), ('...a continuation.', ['...a continuation.'])])
def test_sentence_boundaries_ignore_initials_decimals_and_leading_ellipsis(text, expected):
    assert tr._translation_sentence_spans(text) == expected


@pytest.mark.parametrize('quality', ['fast', 'balanced', 'maximum'])
def test_all_quality_profiles_reject_duplicate_alternatives(monkeypatch, quality):
    bad = 'Das Paket ist heute angekommen.Das Paket, das ist heute angekommen.'
    monkeypatch.setattr(tr, '_decode_single_translation', lambda *a, **kwargs: bad)
    monkeypatch.setattr(tr, '_strict_retry_translation', lambda *a, **kwargs: bad)
    events = set()
    result = tr._translate_payload_strict(object(), object(), 'A package arrived today.', 'de', qa_events=events, quality_profile=quality)
    assert result == 'Das Paket ist heute angekommen.'
    assert 'deduplicated' in events
    assert 'review' not in events


@pytest.mark.parametrize('language,text', [('de', 'Eine Nachricht.'), ('zh', '这是消息。'), ('ar', 'هذه رسالة.')])
def test_madlad_source_has_exactly_one_terminal_eos(language, text):
    from types import SimpleNamespace
    tokenizer = SimpleNamespace(encode=lambda value, **kwargs: value.split())
    encoded = tr._encode_translation_source(tokenizer, text, language)
    assert encoded[0] == f'<2{language}>'
    assert encoded[-1] == '</s>'
    assert encoded.count('</s>') == 1
    tokenizer = SimpleNamespace(encode=lambda value, **kwargs: value.split() + ['</s>'])
    assert tr._encode_translation_source(tokenizer, text, language).count('</s>') == 1


def test_sentence_segmentation_preserves_short_commands_and_repetition():
    assert tr._authored_translation_segments('A door opened. Run! Run!') == ['A door opened.', 'Run!', 'Run!']
    assert tr._strict_cue_payloads('<i>A door opened. Run! Run!</i>') == ['A door opened.', 'Run!', 'Run!']
    assert tr._authored_translation_segments('他说了这句话。快走！快走！') == ['他说了这句话。', '快走！', '快走！']
    assert tr._authored_translation_segments('E.V., wait...\nthere is more.') == ['E.V., wait... there is more.']


def test_short_sentences_are_translated_separately_without_losing_markup(monkeypatch):
    seen = []

    def fake(_rt, _tok, text, _lang, **kwargs):
        seen.append(text)
        return {'A door opened.': 'Eine Tür öffnete sich.', 'Run!': 'Lauf!'}[text]
    monkeypatch.setattr(tr, '_translate_payload_strict', fake)
    output, _events = tr._translate_existing_cue_text_strict_with_qa(object(), object(), '<i>A door opened. Run! Run!</i>', 'de')
    assert seen == ['A door opened.', 'Run!']
    assert output == '<i>Eine Tür öffnete sich.\nLauf!\nLauf!</i>'


@pytest.mark.parametrize('language,alternatives', [('de', ['Ein falscher Satz.', 'Eine richtige Aussage.']), ('es', ['Una frase incorrecta.', 'Una afirmación correcta.']), ('zh', ['一个错误的句子。', '一个正确的句子。'])])
def test_source_reconstruction_ranks_candidates_without_language_rules(monkeypatch, language, alternatives):
    from types import SimpleNamespace
    monkeypatch.setattr(tr, '_strict_translation_quality_reason', lambda *args: None)
    tokenizer = SimpleNamespace(encode=lambda text, **kwargs: text.split(), decode=lambda tokens: ' '.join(tokens))

    class Translator:
        calls = 0

        def translate_batch(self, source, **kwargs):
            assert kwargs['return_scores'] and kwargs['return_end_token']
            return [SimpleNamespace(hypotheses=[x.split() + ['</s>'] for x in alternatives], scores=[-0.1, -0.12])]

        def score_batch(self, source, target, **kwargs):
            self.calls += 1
            assert target == [['Send', 'the', 'report.', '</s>']] * 2
            return [SimpleNamespace(log_probs=[-0.9] * 4), SimpleNamespace(log_probs=[-0.2] * 4)]
    engine = Translator()
    ranked = tr._FaithfulSubtitleTranslator(engine, tokenizer, 'en')
    result = ranked.translate_batch([tr._encode_translation_source(tokenizer, 'Send the report.', language)], beam_size=2, max_decoding_length=50)
    assert tr._decode_translation_result(result[0], tokenizer, language) == alternatives[1]
    assert ranked.reconstruction_score('Send the report.', alternatives[1]) == -0.2
    assert engine.calls == 1


def test_ambiguous_endings_widen_only_one_item_and_preserve_source_order(monkeypatch):
    from types import SimpleNamespace
    monkeypatch.setattr(tr, '_strict_translation_quality_reason', lambda *args: None)
    tokenizer = SimpleNamespace(encode=lambda text, **kwargs: text.split(), decode=lambda tokens: ' '.join(tokens))
    calls = []

    class Translator:

        def translate_batch(self, sources, **kwargs):
            calls.append((len(sources), kwargs['beam_size']))
            if kwargs['beam_size'] == 8:
                return [SimpleNamespace(hypotheses=[['A', 'B', 'C', 'faithful.', '</s>']], scores=[-0.15])]
            return [SimpleNamespace(hypotheses=[['A', 'B', 'C', 'one.', '</s>'], ['A', 'B', 'C', 'two.', '</s>']], scores=[-0.1, -0.12]), SimpleNamespace(hypotheses=[['Another', 'message.', '</s>']], scores=[-0.2])]

        def score_batch(self, sources, targets, **kwargs):
            return [SimpleNamespace(log_probs=[-0.2]) for source in sources]
    ranked = tr._FaithfulSubtitleTranslator(Translator(), tokenizer, 'en')
    result = ranked.translate_batch([tr._encode_translation_source(tokenizer, x, 'de') for x in ['A message.', 'Another message.']], beam_size=2, max_decoding_length=40)
    assert calls == [(2, 2), (1, 8)]
    assert [tr._decode_translation_result(x, tokenizer, 'de') for x in result] == ['A B C faithful.', 'Another message.']


def test_fidelity_scoring_cancellation_is_not_swallowed(monkeypatch):
    from types import SimpleNamespace
    import threading
    from gfile_subtitle_ai.core import OperationCancelled
    monkeypatch.setattr(tr, '_strict_translation_quality_reason', lambda *args: None)
    cancel = threading.Event()
    tokenizer = SimpleNamespace(encode=lambda text, **kwargs: text.split(), decode=lambda tokens: ' '.join(tokens))

    class Translator:

        def translate_batch(self, sources, **kwargs):
            return [SimpleNamespace(hypotheses=[['Eine', 'Nachricht.', '</s>']], scores=[-0.1])]

        def score_batch(self, sources, targets, **kwargs):
            cancel.set()
            return [SimpleNamespace(log_probs=[-0.2])]
    ranked = tr._FaithfulSubtitleTranslator(Translator(), tokenizer, 'en', cancel)
    with pytest.raises(OperationCancelled):
        ranked.translate_batch([tr._encode_translation_source(tokenizer, 'A message.', 'de')], max_decoding_length=40)


def test_generated_speaker_dash_is_not_duplicated(monkeypatch):
    monkeypatch.setattr(tr, '_decode_single_translation', lambda *args, **kwargs: '- Ja.')
    monkeypatch.setattr(tr, '_strict_translation_quality_reason', lambda *args: None)
    assert tr._translate_payload_strict(object(), object(), '- Yes.', 'de') == '- Ja.'
    assert tr._translate_payload_strict(object(), object(), 'Yes.', 'de') == 'Ja.'


def test_fidelity_pause_checkpoint_observes_cancellation():
    import threading
    from gfile_subtitle_ai.core import OperationCancelled
    paused = threading.Event()
    cancelled = threading.Event()
    paused.set()
    cancelled.set()
    ranked = tr._FaithfulSubtitleTranslator(object(), object(), 'en', cancelled, paused)
    with pytest.raises(OperationCancelled):
        ranked.translate_batch([])


def test_low_fidelity_is_reported_instead_of_claiming_semantic_success(monkeypatch):
    from types import SimpleNamespace
    monkeypatch.setattr(tr, '_strict_translation_quality_reason', lambda *args: None)
    events = set()
    translator = SimpleNamespace(reconstruction_score=lambda source, candidate: -3.0)
    result = tr._translate_payload_strict(translator, object(), 'An uncertain technical statement.', 'de', initial_candidate='Eine unsichere technische Aussage.', qa_events=events)
    assert result == 'Eine unsichere technische Aussage.'
    assert {'review', 'low_fidelity'} <= events


@pytest.mark.parametrize('source,count', [('Wait, wait, wait, wait, wait.', 5), ('Espere, espere, espere.', 3), ('انتظر، انتظر، انتظر.', 3), ('等等，等等，等等。', 3), ('待って、待って、待って。', 3), ('- Stop, stop, stop!', 3)])
def test_comma_separated_repetition_retains_every_utterance(source, count):
    assert len(tr._authored_translation_segments(source)) == count
    assert len(tr._strict_cue_payloads('<i>' + source + '</i>')) == count


@pytest.mark.parametrize('source', ['Wait, we need more information.', 'Cats, dogs, birds and fish.', 'Stop, stop the machine.', '第一句话，后面是不同的话。'])
def test_ordinary_comma_clauses_are_not_split_as_repetition(source):
    assert tr._authored_translation_segments(source) == [source]


def test_same_speaker_sentences_use_local_context(monkeypatch):
    seen = []

    def fake(_tr, _tok, source, _lang, **kwargs):
        seen.append((source, kwargs['previous_context'], kwargs['next_context']))
        return source
    monkeypatch.setattr(tr, '_translate_payload_strict', fake)
    tr._translate_existing_cue_text_strict_with_qa(object(), object(), '<i>A question? A related answer.</i>', 'de', previous_context='Earlier cue.', next_context='Later cue.')
    assert seen == [('A question?', 'Earlier cue.', 'A related answer.'), ('A related answer.', 'A question?', 'Later cue.')]


def test_source_ranking_also_considers_target_fluency(monkeypatch):
    from types import SimpleNamespace
    monkeypatch.setattr(tr, '_strict_translation_quality_reason', lambda *args: None)
    monkeypatch.setattr(tr, '_strict_context_translation', lambda *args, **kwargs: 'A fluent contextual result.')
    engine = SimpleNamespace(reconstruction_score=lambda source, text: -0.2 if text.startswith('An awkward') else -0.3, candidate_score=lambda source, text, target: -1.0 if text.startswith('An awkward') else -0.5)
    result = tr._translate_payload_strict(engine, object(), 'A source sentence.', 'de', initial_candidate='An awkward result.', previous_context='Previous.', quality_profile='maximum')
    assert result == 'A fluent contextual result.'


def test_low_confidence_widening_is_bounded_and_keeps_other_items(monkeypatch):
    from types import SimpleNamespace
    monkeypatch.setattr(tr, '_strict_translation_quality_reason', lambda *args: None)
    tokenizer = SimpleNamespace(encode=lambda text, **kwargs: text.split(), decode=lambda tokens: ' '.join(tokens))
    calls = []

    class Translator:

        def translate_batch(self, sources, **kwargs):
            calls.append((len(sources), kwargs['beam_size']))
            if kwargs['beam_size'] == 8:
                return [SimpleNamespace(hypotheses=[['Wider', 'candidate.', '</s>']], scores=[-0.1])]
            return [SimpleNamespace(hypotheses=[['Uncertain', 'candidate.', '</s>']], scores=[-0.1]), SimpleNamespace(hypotheses=[['Other', 'candidate.', '</s>']], scores=[-0.1])]

        def score_batch(self, sources, targets, **kwargs):
            return [SimpleNamespace(log_probs=[-3.0 if source[1] in {'Uncertain', 'Wider'} else -0.2]) for source in sources]
    ranked = tr._FaithfulSubtitleTranslator(Translator(), tokenizer, 'en')
    result = ranked.translate_batch([tr._encode_translation_source(tokenizer, text, 'de') for text in ['Uncertain source.', 'Other source.']], beam_size=2, max_decoding_length=40)
    assert calls == [(2, 2), (1, 8)]
    assert [tr._decode_translation_result(item, tokenizer, 'de') for item in result] == ['Wider candidate.', 'Other candidate.']


def test_context_candidate_scores_full_target_and_reuses_cache():
    from types import SimpleNamespace
    tokenizer = SimpleNamespace(encode=lambda text, **kwargs: text.split(), decode=lambda tokens: ' '.join(tokens))
    calls = []

    class Translator:

        def score_batch(self, sources, targets, **kwargs):
            calls.append((sources, targets))
            return [SimpleNamespace(log_probs=[-0.5, -0.5])]
    ranked = tr._FaithfulSubtitleTranslator(Translator(), tokenizer, 'en')
    assert ranked.candidate_score('Source sentence.', 'Target sentence.', 'de') == -1.0
    assert ranked.candidate_score('Source sentence.', 'Target sentence.', 'de') == -1.0
    assert calls == [([['<2de>', 'Source', 'sentence.', '</s>']], [['Target', 'sentence.', '</s>']]), ([['<2en>', 'Target', 'sentence.', '</s>']], [['Source', 'sentence.', '</s>']])]


@pytest.mark.parametrize('quality', ['fast', 'balanced', 'maximum'])
def test_repeated_utterances_reuse_words_but_keep_punctuation_and_batch_alignment(monkeypatch, quality):
    seen = []

    def fake(_tr, _tok, source, _lang, **kwargs):
        seen.append((source, kwargs['initial_candidate']))
        return '- Halt,' if len(seen) == 1 else 'Weiter.'
    monkeypatch.setattr(tr, '_translate_payload_strict', fake)
    result, _ = tr._translate_existing_cue_text_strict_with_qa(object(), object(), '<i>- Stop, stop, stop! Proceed.</i>', 'de', initial_candidates=['first', 'second', 'third', 'fourth'], quality_profile=quality)
    assert result == '<i>- Halt,\nHalt,\nHalt!\nWeiter.</i>'
    assert seen == [('- Stop,', 'first'), ('Proceed.', 'fourth')]


def test_repetition_reuse_does_not_cross_explicit_speakers(monkeypatch):
    seen = []

    def fake(_tr, _tok, source, _lang, **kwargs):
        seen.append(source)
        return '- Ja.' if len(seen) == 1 else '- Jawohl.'
    monkeypatch.setattr(tr, '_translate_payload_strict', fake)
    result, _ = tr._translate_existing_cue_text_strict_with_qa(object(), object(), '- Yes.\n- Yes.', 'de')
    assert seen == ['- Yes.', '- Yes.']
    assert result == '- Ja.\n- Jawohl.'


def test_repetition_reuse_keeps_whitespace_before_inline_styles(monkeypatch):
    monkeypatch.setattr(tr, '_translate_payload_strict', lambda *args, **kwargs: 'Allez!')
    result, _ = tr._translate_existing_cue_text_strict_with_qa(object(), object(), 'Go! Go! <i>Now.</i>', 'fr')
    assert 'Allez! <i>' in result


@pytest.mark.parametrize('quality', ['fast', 'balanced', 'maximum'])
def test_incremental_translation_finishes_qa_before_decoding_next_window(monkeypatch, quality):
    from types import SimpleNamespace
    timeline = []
    decoded = 0
    tokenizer = SimpleNamespace(encode=lambda *args, **kwargs: [])
    engine = SimpleNamespace(translate_batch=lambda *args, **kwargs: [])
    monkeypatch.setattr(tr, '_prepare_translation_model', lambda *args, **kwargs: object())
    monkeypatch.setattr(tr, '_load_translation_runtime', lambda *args, **kwargs: (engine, tokenizer, 'cpu'))

    def decode(_engine, _tokenizer, payloads, _language, **kwargs):
        nonlocal decoded
        decoded += len(payloads)
        timeline.append(('decode', decoded))
        return ['Übersetzt.'] * len(payloads)

    def finish(_engine, _tokenizer, source, _language, **kwargs):
        timeline.append(('qa', source))
        assert kwargs['quality_profile'] == quality
        assert kwargs['initial_candidates'] == ['Übersetzt.']
        return ('Übersetzt.', set())
    monkeypatch.setattr(tr, '_decode_translation_batch', decode)
    monkeypatch.setattr(tr, '_translate_existing_cue_text_strict_with_qa', finish)
    cues = [tr.SubtitleCue(i * 2, i * 2 + 1, f'Source {i}.') for i in range(25)]
    counts = []

    def progress(done, total):
        counts.append(done)
        timeline.append(('progress', done))
        assert total == len(cues)
        if done == 1:
            assert decoded == 4
        if done == 4:
            assert decoded == 4
    output = tr.translate_existing_subtitle_cues_with_ai(cues, 'fr', 'de', progress=progress, quality_profile=quality)
    assert counts == list(range(1, 26))
    assert timeline.index(('progress', 4)) < timeline.index(('decode', 12))
    assert [(cue.start, cue.end) for cue in output] == [(cue.start, cue.end) for cue in cues]


def test_incremental_windows_keep_cross_boundary_sentence_groups_intact(monkeypatch):
    from types import SimpleNamespace
    batches = []
    tokenizer = SimpleNamespace(encode=lambda *args, **kwargs: [])
    engine = SimpleNamespace(translate_batch=lambda *args, **kwargs: [])
    monkeypatch.setattr(tr, '_prepare_translation_model', lambda *args, **kwargs: object())
    monkeypatch.setattr(tr, '_load_translation_runtime', lambda *args, **kwargs: (engine, tokenizer, 'cpu'))

    def decode(_engine, _tokenizer, payloads, _language, **kwargs):
        batches.append(payloads)
        return ['Eine zusammenhängende Aussage mit drei Teilen.'] * len(payloads)
    monkeypatch.setattr(tr, '_decode_translation_batch', decode)
    monkeypatch.setattr(tr, '_translate_existing_cue_text_strict_with_qa', lambda *args, **kwargs: ('Übersetzt.', set()))
    monkeypatch.setattr(tr, '_translate_payload_strict', lambda *args, **kwargs: kwargs['initial_candidate'])
    texts = ['First.', 'Second.', 'Third.', '<i>A sentence starts,</i>', '<i>continues here,</i>', '<i>and finishes.</i>', 'Last.']
    cues = [tr.SubtitleCue(i, i + 1, text) for i, text in enumerate(texts)]
    progress = []

    def on_progress(done, total):
        progress.append(done)
        if done == 1:
            assert len(batches) == 1
            assert len(batches[0]) == 4
    output = tr.translate_existing_subtitle_cues_with_ai(cues, 'en', 'de', progress=on_progress)
    assert batches[0][-1] == 'A sentence starts, continues here, and finishes.'
    assert batches[1] == ['Last.']
    assert progress == list(range(1, 8))
    assert all((cue.text.startswith('<i>') and cue.text.endswith('</i>') for cue in output[3:6]))
    assert ' '.join((cue.text.removeprefix('<i>').removesuffix('</i>') for cue in output[3:6])) == 'Eine zusammenhängende Aussage mit drei Teilen.'


def test_incremental_cancellation_does_not_decode_remaining_file(monkeypatch):
    import threading
    from types import SimpleNamespace
    from gfile_subtitle_ai.core import OperationCancelled
    cancel = threading.Event()
    batches = []
    tokenizer = SimpleNamespace(encode=lambda *args, **kwargs: [])
    engine = SimpleNamespace(translate_batch=lambda *args, **kwargs: [])
    monkeypatch.setattr(tr, '_prepare_translation_model', lambda *args, **kwargs: object())
    monkeypatch.setattr(tr, '_load_translation_runtime', lambda *args, **kwargs: (engine, tokenizer, 'cpu'))

    def decode(_engine, _tokenizer, payloads, _language, **kwargs):
        batches.append(payloads)
        return ['Übersetzt.'] * len(payloads)
    monkeypatch.setattr(tr, '_decode_translation_batch', decode)
    monkeypatch.setattr(tr, '_translate_existing_cue_text_strict_with_qa', lambda *args, **kwargs: ('Übersetzt.', set()))
    cues = [tr.SubtitleCue(i, i + 1, 'A source sentence.') for i in range(100)]
    with pytest.raises(OperationCancelled):
        tr.translate_existing_subtitle_cues_with_ai(cues, 'en', 'de', cancel_event=cancel, progress=lambda done, total: cancel.set())
    assert len(batches) == 1 and len(batches[0]) == 4


def test_cancellation_during_first_batch_prevents_qa_and_progress(monkeypatch):
    import threading
    from types import SimpleNamespace
    from unittest.mock import Mock
    from gfile_subtitle_ai.core import OperationCancelled
    cancel = threading.Event()
    tokenizer = SimpleNamespace(encode=lambda *args, **kwargs: [])
    engine = SimpleNamespace(translate_batch=lambda *args, **kwargs: [])
    monkeypatch.setattr(tr, '_prepare_translation_model', lambda *args, **kwargs: object())
    monkeypatch.setattr(tr, '_load_translation_runtime', lambda *args, **kwargs: (engine, tokenizer, 'cpu'))

    def decode(_engine, _tokenizer, payloads, _language, **kwargs):
        cancel.set()
        return ['Übersetzt.'] * len(payloads)
    finish = Mock()
    progress = Mock()
    monkeypatch.setattr(tr, '_decode_translation_batch', decode)
    monkeypatch.setattr(tr, '_translate_existing_cue_text_strict_with_qa', finish)
    with pytest.raises(OperationCancelled):
        tr.translate_existing_subtitle_cues_with_ai([tr.SubtitleCue(0, 1, 'A source sentence.')], 'en', 'de', cancel_event=cancel, progress=progress)
    finish.assert_not_called()
    progress.assert_not_called()
