import datetime

from daemon.team_fixtures import (
    build_live,
    build_rows,
    competition_name,
    format_kickoff,
    schedule_events,
    watch_candidates,
)


def _event(date, home_id, away_id, away_name, state="pre", slug="bra.1", time_valid=True):
    return {
        "date": date,
        "timeValid": time_valid,
        "league": {"slug": slug, "shortName": "Some League"},
        "competitions": [{
            "status": {"type": {"state": state}},
            "competitors": [
                {"homeAway": "home", "team": {"id": home_id, "shortDisplayName": "Vasco" if home_id == "3454" else away_name}},
                {"homeAway": "away", "team": {"id": away_id, "shortDisplayName": away_name if home_id == "3454" else "Vasco"}},
            ],
        }],
    }


def test_competition_name_maps_known_slugs():
    assert competition_name({"slug": "bra.1"}) == "Brasileirão"
    assert competition_name({"slug": "conmebol.sudamericana"}) == "Sul-Americana"
    assert competition_name({"slug": "xyz", "shortName": "Club Friendly Cup"}) == "Club Friendly C"


def test_format_kickoff_local_time_and_tbd():
    iso = "2026-09-19T23:30Z"
    local = datetime.datetime.fromisoformat("2026-09-19T23:30+00:00").astimezone()
    assert format_kickoff(iso, True).endswith(local.strftime("%d/%m %H:%M"))
    assert format_kickoff(iso, False).endswith(local.strftime("%d/%m"))


def test_build_rows_skips_past_and_played_games():
    now = datetime.datetime(2026, 9, 13, tzinfo=datetime.timezone.utc)
    schedule = {"events": [
        _event("2026-09-10T22:00Z", "3454", "1", "Old Rival"),
        _event("2026-09-15T22:00Z", "3454", "2", "Coritiba", state="post"),
        _event("2026-09-19T23:30Z", "3454", "3", "Coritiba"),
        _event("2026-10-07T23:30Z", "4", "3454", "Botafogo", slug="conmebol.libertadores"),
    ]}
    rows = build_rows(schedule, "3454", now)
    assert [r[0] for r in rows] == ["Coritiba", "Botafogo"]
    assert [r[1] for r in rows] == ["C", "F"]
    assert rows[1][3] == "Libertadores"


def _live_scoreboard(state="in", name="STATUS_SECOND_HALF", clock="67'"):
    return {"events": [{
        "id": "401913959",
        "competitions": [{
            "status": {"displayClock": clock, "type": {"state": state, "name": name}},
            "competitors": [
                {"homeAway": "home", "score": "2", "team": {"id": "3454", "shortDisplayName": "Vasco"}},
                {"homeAway": "away", "score": "1", "team": {"id": "1", "shortDisplayName": "Santa Fe"}},
            ],
        }],
    }]}


def test_build_live_in_progress():
    state, row = build_live(_live_scoreboard(), "401913959", {"slug": "conmebol.sudamericana"})
    assert state == "in"
    assert row == ["Vasco", "Santa Fe", 2, 1, "67'", "2º tempo", "Sul-Americana"]


def test_build_live_halftime_hides_clock_and_other_states():
    _, row = build_live(_live_scoreboard(name="STATUS_HALFTIME", clock="45'"), "401913959")
    assert row[4] == "" and row[5] == "Intervalo"
    assert build_live(_live_scoreboard(state="post"), "401913959") == ("post", None)
    assert build_live(_live_scoreboard(), "999") == ("missing", None)


def test_watch_candidates_window():
    now = datetime.datetime(2026, 9, 15, 23, 0, tzinfo=datetime.timezone.utc)
    events = schedule_events({"events": [
        {"id": 1, "date": "2026-09-15T22:00Z", "league": {"slug": "bra.1"}},   # 1h in
        {"id": 2, "date": "2026-09-15T23:05Z", "league": {"slug": "bra.1"}},   # 5 min to go
        {"id": 3, "date": "2026-09-16T01:00Z", "league": {"slug": "bra.1"}},   # 2h away
        {"id": 4, "date": "2026-09-15T18:00Z", "league": {"slug": "bra.1"}},   # 5h ago
    ]})
    assert [c[0] for c in watch_candidates(events, now)] == ["1", "2"]


def test_is_match_day_uses_local_calendar_day():
    from daemon.team_fixtures import is_match_day

    tz = datetime.timezone(datetime.timedelta(hours=-3))
    kickoff = datetime.datetime(2026, 9, 15, 19, 0, tzinfo=tz)
    events = {"1": ("conmebol.sudamericana", kickoff)}
    assert is_match_day(events, datetime.datetime(2026, 9, 15, 8, 0, tzinfo=tz))
    assert is_match_day(events, datetime.datetime(2026, 9, 15, 23, 50, tzinfo=tz))
    assert not is_match_day(events, datetime.datetime(2026, 9, 14, 12, 0, tzinfo=tz))
    assert not is_match_day({}, datetime.datetime(2026, 9, 15, 12, 0, tzinfo=tz))


def test_almirante_window_starts_two_hours_before_kickoff():
    from daemon.team_fixtures import almirante_window

    tz = datetime.timezone(datetime.timedelta(hours=-3))
    events = {"1": ("conmebol.sudamericana", datetime.datetime(2026, 9, 15, 19, 0, tzinfo=tz))}
    assert not almirante_window(events, datetime.datetime(2026, 9, 15, 16, 59, tzinfo=tz))
    assert almirante_window(events, datetime.datetime(2026, 9, 15, 17, 0, tzinfo=tz))
    assert almirante_window(events, datetime.datetime(2026, 9, 15, 23, 50, tzinfo=tz))
    assert not almirante_window(events, datetime.datetime(2026, 9, 16, 0, 10, tzinfo=tz))
    late = {"2": ("bra.1", datetime.datetime(2026, 9, 15, 23, 30, tzinfo=tz))}
    assert almirante_window(late, datetime.datetime(2026, 9, 16, 1, 0, tzinfo=tz))   # game past midnight
    assert not almirante_window({}, datetime.datetime(2026, 9, 15, 20, 0, tzinfo=tz))
