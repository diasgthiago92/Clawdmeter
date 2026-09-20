"""Fila de avisos vindos de outros processos do Mac.

Qualquer script (ex.: o autopost da @eaiproduto) acrescenta uma linha JSON em
``~/.config/claude-usage-monitor/notices.jsonl``; o daemon drena a fila a cada
tick e manda o texto para o firmware, que mostra um balão por 10 s.

Avisos mais velhos que ``MAX_AGE_S`` são descartados: ao reconectar depois de
horas fora, o aparelho não deve piscar notícias de ontem.
"""
from __future__ import annotations

import json
import os
import time
from pathlib import Path

NOTICES_FILE = Path.home() / ".config" / "claude-usage-monitor" / "notices.jsonl"
MAX_AGE_S = 300      # aviso guardado por no máximo 5 min sem aparelho conectado
MAX_TEXT = 80        # o balão quebra em poucas linhas; texto longo vira reticências
MAX_DRAIN = 5        # numa enxurrada, mostra só os últimos

# A fonte do firmware traz Latin-1 (acentos ok), mas não pontuação tipográfica:
# um travessão vira um quadradinho vazio na tela. Trocar antes de mandar.
_PUNCT = {"—": "-", "–": "-", "―": "-", "…": "...", "“": '"', "”": '"',
          "‘": "'", "’": "'", "→": "->", "•": "-", "\u00a0": " "}


def _fold(text: str) -> str:
    for bad, good in _PUNCT.items():
        text = text.replace(bad, good)
    return "".join(c if ord(c) < 256 else "?" for c in text)


def post(text: str, ok: bool = True, path: Path = NOTICES_FILE) -> bool:
    """Enfileira um aviso. Devolve False se não deu para escrever (nunca levanta)."""
    text = _fold(" ".join(str(text).split()))
    if len(text) > MAX_TEXT:
        text = text[: MAX_TEXT - 3] + "..."
    line = json.dumps({"t": text, "ok": bool(ok), "ts": time.time()}, ensure_ascii=False)
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("a", encoding="utf-8") as fh:
            fh.write(line + "\n")      # append de uma linha curta: atômico o bastante
        return True
    except OSError:
        return False


def drain(path: Path = NOTICES_FILE, now: float | None = None) -> list[dict]:
    """Tira todos os avisos da fila e devolve os payloads BLE ``{"nt": [texto, ok]}``.

    O arquivo é renomeado antes de ser lido, então um ``post`` concorrente
    escreve no arquivo novo em vez de ter a linha descartada aqui.
    """
    now = time.time() if now is None else now
    tmp = path.with_name(path.name + ".draining")
    try:
        os.replace(path, tmp)
    except OSError:
        return []
    try:
        raw = tmp.read_text(encoding="utf-8")
    except OSError:
        raw = ""
    finally:
        try:
            tmp.unlink()
        except OSError:
            pass

    out = []
    for line in raw.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            item = json.loads(line)
            text = str(item["t"])
        except (ValueError, KeyError, TypeError):
            continue
        if now - float(item.get("ts", now)) > MAX_AGE_S:
            continue
        out.append({"nt": [_fold(text)[:MAX_TEXT], int(bool(item.get("ok", True)))]})
    return out[-MAX_DRAIN:]
