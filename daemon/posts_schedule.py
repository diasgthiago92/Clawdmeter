"""Cronograma de posts da @eaiproduto (Instagram e TikTok) para a tela do aparelho.

Lê o projeto ~/.claude/projetos/eaiproduto (somente leitura) e monta uma linha por
lançamento, dos últimos ``DAYS_BACK`` dias até ``DAYS_AHEAD`` dias à frente:

    {"p": [[rede, "DD/MM HHh", título, estado], ...], "o": offset, "n": total}

rede: 0 Instagram | 1 TikTok. estado: 0 agendado | 1 enviado | 2 não enviado.

Instagram: mesma conta do cronograma do dashboard (``dashboard.cronograma``): posts dos
slots e reels; os stories automáticos ficam de fora. TikTok: os reels vão para os
rascunhos (``run-slot.sh`` no dia da publicação, ``content/tiktok-fila.json`` nos
reenvios); "enviado" vem do log do slot ou do ``enviado_em`` da fila.
"""
from __future__ import annotations

import datetime
import importlib.util
import json
from pathlib import Path

from market_quotes import chunk_table

PROJECT = Path.home() / ".claude" / "projetos" / "eaiproduto"
DAYS_BACK = 0             # só hoje e amanhã
DAYS_AHEAD = 1
POSTS_MAX = 48            # a lista do aparelho rola; o firmware guarda até isto
TITLE_MAX = 24
POSTS_REFRESH_S = 60

# A fonte do firmware traz Latin-1, mas não pontuação tipográfica (vira quadradinho).
_PUNCT = {"—": "-", "–": "-", "―": "-", "…": "...", "“": '"', "”": '"', "‘": "'", "’": "'", "→": "->", "•": "-", "\u00a0": " "}

AGENDADO, ENVIADO, NAO_ENVIADO = 0, 1, 2
INSTAGRAM, TIKTOK = 0, 1


def _load_dashboard(project: Path):
    spec = importlib.util.spec_from_file_location("eai_dashboard", project / "scripts" / "dashboard.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _fold(text: str) -> str:
    for bad, good in _PUNCT.items():
        text = text.replace(bad, good)
    return "".join(c if ord(c) < 256 else "?" for c in text)


def _short(text: str) -> str:
    text = _fold(" ".join((text or "").split()))
    return text if len(text) <= TITLE_MAX else text[: TITLE_MAX - 3].rstrip() + "..."


def _stamp(day: str, hour: int, minute: int = 0) -> str:
    return f"{day[8:10]}/{day[5:7]} {hour:02d}h" if not minute else f"{day[8:10]}/{day[5:7]} {hour:02d}:{minute:02d}"


def _tiktok_logged(project: Path, day: str, slot: str) -> bool:
    try:
        return "TikTok: rascunho enviado" in (project / "logs" / f"{day}-{slot}.log").read_text(errors="replace")
    except OSError:
        return False


def build_rows(items: list[dict], fila: list[dict], project: Path, today: datetime.date,
               labels: dict[str, str] | None = None) -> list[list]:
    """Linhas ordenadas por data. `items` = dashboard.cronograma; `fila` = tiktok-fila.json;
    `labels` = nome curto de cada modelo (usado quando o tema ainda não foi escolhido)."""
    labels = labels or {}
    first = (today - datetime.timedelta(days=DAYS_BACK)).isoformat()
    last = (today + datetime.timedelta(days=DAYS_AHEAD)).isoformat()
    events: list[tuple[str, int, int, list]] = []   # (dia, minutos, rede, linha)

    def add(net: int, day: str, minutes: int, title: str, state: int) -> None:
        if first <= day <= last:
            events.append((day, minutes, net, [net, _stamp(day, minutes // 60, minutes % 60), _short(title), state]))

    for it in items:
        if it["slot"].startswith("story-") or it["dia"] > last:
            continue
        title = it.get("tema") or labels.get(it["modelo"], it["modelo"])
        published = it["status"] == "publicado"
        add(INSTAGRAM, it["dia"], it["hora"] * 60, title,
            ENVIADO if published else NAO_ENVIADO if it["status"] == "pendente" else AGENDADO)
        if not it["modelo"].startswith("reel"):
            continue
        key = f'{it["dia"]}-{it["slot"]}'
        queued = next((i for i, f in enumerate(fila) if key in f.get("post", "")), None)
        if queued is not None:                       # reenvio marcado na fila do TikTok
            continue
        if _tiktok_logged(project, it["dia"], it["slot"]):
            state = ENVIADO
        elif published:
            state = NAO_ENVIADO                      # o Instagram saiu e o rascunho não foi
        else:
            state = NAO_ENVIADO if it["status"] == "pendente" else AGENDADO
        add(TIKTOK, it["dia"], it["hora"] * 60, title, state)

    now = datetime.datetime.now()
    for i, f in enumerate(fila):
        try:
            when = datetime.datetime.strptime(f["quando"], "%Y-%m-%d %H:%M")
        except (KeyError, ValueError):
            continue
        state = ENVIADO if f.get("enviado_em") else NAO_ENVIADO if when <= now else AGENDADO
        add(TIKTOK, when.date().isoformat(), when.hour * 60 + when.minute, f.get("titulo", ""), state)

    events.sort(key=lambda e: (e[0], e[1], e[2]))
    return [e[3] for e in events][:POSTS_MAX]


class PostsSchedule:
    def __init__(self, project: Path = PROJECT) -> None:
        self.project = project
        self.payloads: list[dict] = []
        self.fetched_at = 0.0
        self._dashboard = None

    def _read_fila(self) -> list[dict]:
        try:
            return list(json.loads((self.project / "content" / "tiktok-fila.json").read_text()).get("itens", []))
        except (OSError, ValueError, AttributeError):
            return []

    async def get(self, now: float) -> list[dict]:
        if now - self.fetched_at < POSTS_REFRESH_S:
            return self.payloads
        self.fetched_at = now
        if not (self.project / "scripts" / "dashboard.py").exists():
            return self.payloads
        try:
            if self._dashboard is None:
                self._dashboard = _load_dashboard(self.project)
            when = datetime.datetime.fromtimestamp(now)
            items = self._dashboard.cronograma(when, DAYS_BACK)
        except Exception as e:                       # o projeto muda por fora; nunca derruba o daemon
            raise ValueError(f"cronograma: {e}") from e
        labels = {k: v[0] for k, v in getattr(self._dashboard, "FORMATOS", {}).items()}
        rows = build_rows(items, self._read_fila(), self.project, when.date(), labels)
        self.payloads = chunk_table("p", rows) if rows else [{"p": [], "o": 0, "n": 0}]
        return self.payloads
