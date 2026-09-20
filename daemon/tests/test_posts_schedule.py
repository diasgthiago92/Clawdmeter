import datetime

from daemon.posts_schedule import ENVIADO, NAO_ENVIADO, AGENDADO, INSTAGRAM, TIKTOK, build_rows

TODAY = datetime.date(2026, 9, 20)


def item(dia, hora, slot, modelo, status, tema="Tema"):
    return {"dia": dia, "hora": hora, "slot": slot, "modelo": modelo, "status": status, "tema": tema}


def test_instagram_states_and_stories_left_out(tmp_path):
    rows = build_rows([
        item("2026-09-19", 19, "noite", "tweet-problema-repo", "publicado"),
        item("2026-09-18", 19, "noite", "tweet-problema-repo", "pendente"),
        item("2026-09-20", 12, "almoco", "imagem-problema-repo", "agendado"),
        item("2026-09-20", 13, "story-solucao", "story", "agendado"),
    ], [], tmp_path, TODAY)
    assert [(r[0], r[1], r[3]) for r in rows] == [
        (INSTAGRAM, "18/09 19h", NAO_ENVIADO),      # ordenado por data
        (INSTAGRAM, "19/09 19h", ENVIADO),
        (INSTAGRAM, "20/09 12h", AGENDADO),
    ]


def test_reel_goes_to_tiktok_only_when_logged(tmp_path):
    (tmp_path / "logs").mkdir()
    (tmp_path / "logs" / "2026-09-19-extra.log").write_text("TikTok: rascunho enviado (videos/x)\n")
    rows = build_rows([
        item("2026-09-19", 10, "extra", "reel-pronto", "publicado", "Com log"),
        item("2026-09-20", 10, "extra", "reel-pronto", "publicado", "Sem log"),
    ], [], tmp_path, TODAY)
    tiktok = {r[2]: r[3] for r in rows if r[0] == TIKTOK}
    assert tiktok == {"Com log": ENVIADO, "Sem log": NAO_ENVIADO}


def test_queued_resend_replaces_the_derived_tiktok_row(tmp_path):
    fila = [{"quando": "2026-09-21 10:00", "video": "v.mp4", "titulo": "Reenvio",
             "post": "content/published/2026-09-18-extra/post.json"}]
    rows = build_rows([item("2026-09-18", 10, "extra", "reel-pronto", "publicado", "Reel")], fila, tmp_path, TODAY)
    assert [(r[0], r[1], r[3]) for r in rows if r[0] == TIKTOK] == [(TIKTOK, "21/09 10h", AGENDADO)]
    fila[0]["enviado_em"] = "2026-09-21 10:05"
    rows = build_rows([item("2026-09-18", 10, "extra", "reel-pronto", "publicado", "Reel")], fila, tmp_path, TODAY)
    assert [r[3] for r in rows if r[0] == TIKTOK] == [ENVIADO]


def test_window_and_title_folding(tmp_path):
    rows = build_rows([
        item("2026-09-10", 8, "manha", "curiosidade-ia", "publicado"),
        item("2026-09-30", 8, "manha", "curiosidade-ia", "agendado"),
        item("2026-09-21", 8, "manha", "curiosidade-ia", "agendado", "Título — muito longo, com travessão e mais texto"),
    ], [], tmp_path, TODAY)
    assert len(rows) == 1 and rows[0][2].endswith("...") and "—" not in rows[0][2] and len(rows[0][2]) <= 24
