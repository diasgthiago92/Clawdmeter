import json
import time

import notices


def test_post_and_drain_roundtrip(tmp_path):
    fila = tmp_path / "notices.jsonl"
    assert notices.post("Post publicado", True, path=fila)
    assert notices.post("Falhou", False, path=fila)
    assert notices.drain(fila) == [{"nt": ["Post publicado", 1]}, {"nt": ["Falhou", 0]}]
    assert notices.drain(fila) == []        # a fila esvazia ao ser drenada
    assert not fila.exists()


def test_drain_without_file(tmp_path):
    assert notices.drain(tmp_path / "nao-existe.jsonl") == []


def test_old_notices_are_dropped(tmp_path):
    fila = tmp_path / "notices.jsonl"
    now = time.time()
    fila.write_text(
        json.dumps({"t": "velho", "ok": True, "ts": now - notices.MAX_AGE_S - 1}) + "\n"
        + json.dumps({"t": "novo", "ok": True, "ts": now}) + "\n"
    )
    assert notices.drain(fila, now=now) == [{"nt": ["novo", 1]}]


def test_long_text_is_truncated(tmp_path):
    fila = tmp_path / "notices.jsonl"
    notices.post("x" * 200, path=fila)
    (payload,) = notices.drain(fila)
    assert len(payload["nt"][0]) == notices.MAX_TEXT
    assert payload["nt"][0].endswith("...")


def test_typographic_punctuation_is_folded(tmp_path):
    # A fonte do firmware não tem travessão nem reticências: viram quadradinho.
    fila = tmp_path / "notices.jsonl"
    notices.post("post publicado — ok… “aspas”", path=fila)
    (payload,) = notices.drain(fila)
    assert payload["nt"][0] == 'post publicado - ok... "aspas"'


def test_accents_survive(tmp_path):
    fila = tmp_path / "notices.jsonl"
    notices.post("sessão expirou, atenção", False, path=fila)
    assert notices.drain(fila) == [{"nt": ["sessão expirou, atenção", 0]}]


def test_garbage_lines_are_skipped(tmp_path):
    fila = tmp_path / "notices.jsonl"
    fila.write_text("nao e json\n\n" + json.dumps({"sem_texto": 1}) + "\n"
                    + json.dumps({"t": "ok", "ts": time.time()}) + "\n")
    assert notices.drain(fila) == [{"nt": ["ok", 1]}]


def test_flood_keeps_only_the_last_ones(tmp_path):
    fila = tmp_path / "notices.jsonl"
    for i in range(20):
        notices.post(f"aviso {i}", path=fila)
    out = notices.drain(fila)
    assert len(out) == notices.MAX_DRAIN
    assert out[-1] == {"nt": ["aviso 19", 1]}
