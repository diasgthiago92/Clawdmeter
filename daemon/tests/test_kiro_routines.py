import datetime

from daemon.kiro_routines import build_rows, load_token, routine_of


def test_routine_of_titles_and_status():
    assert routine_of(":bar_chart: *OLX Rejected Rescue* — resumo do dia 12/09/2026") == ("OLX Rejected Rescue", True)
    assert routine_of(":white_check_mark: *Board Financiamento* atualizado e em dia") == ("Board Financiamento", True)
    assert routine_of(":mailbox_with_mail: *Organização de e-mails* — 13/09 09:00") == ("Organização e-mails", True)
    assert routine_of("Seu consumo mensal de Histórico Veicular é de 16501") == ("Histórico Veicular", True)
    assert routine_of(":white_check_mark: Backup local concluído (13/09 10:31)") == ("Backup local", True)
    assert routine_of(":white_check_mark: Backup .kiro → Drive concluído") == ("Backup Drive", True)
    assert routine_of(":x: Falha no backup .kiro → Drive (2026-09-12 10:30): timed out") == ("Backup Drive", False)
    assert routine_of("") is None


def test_build_rows_latest_first_with_counts():
    def ts(h, m):
        return str(datetime.datetime(2026, 9, 13, h, m).timestamp())

    rows = build_rows([
        {"ts": ts(11, 0), "text": ":white_check_mark: *Board Financiamento* atualizado"},
        {"ts": ts(10, 0), "text": ":white_check_mark: *Board Financiamento* atualizado"},
        {"ts": ts(10, 53), "text": ":x: Falha no backup .kiro → Drive: timeout"},
        {"ts": ts(9, 0), "text": ":mailbox_with_mail: *Organização de e-mails* — 13/09"},
        {"ts": ts(8, 0), "text": ""},
    ])
    assert rows == [
        ["Board Financiamento", "11:00", 1, 2, 1],
        ["Backup Drive", "10:53", 0, 1, 1],
        ["Organização e-mails", "09:00", 1, 1, 1],
    ]


def test_load_token(tmp_path):
    env = tmp_path / ".env"
    env.write_text("OTHER=1\nSLACK_BOT_TOKEN='xoxb-test'\n")
    assert load_token(env) == "xoxb-test"
    assert load_token(tmp_path / "missing") is None


def test_rerun_only_known_routines(tmp_path):
    import asyncio

    from daemon.kiro_routines import rerun, rerun_labels

    (tmp_path / "routines.json").write_text('{"Minha Rotina": "com.kiro.minha", "Ruim": "x; rm -rf /"}')
    labels = rerun_labels(tmp_path / "routines.json")
    assert labels["Minha Rotina"] == "com.kiro.minha" and "Ruim" not in labels
    assert asyncio.run(rerun("Nao Existe", labels)) is False
