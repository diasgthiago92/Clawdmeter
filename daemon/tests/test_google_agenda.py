import datetime

from daemon.google_agenda import build_rows


def _ev(title, start, end, **extra):
    return {"summary": title, "start": {"dateTime": start}, "end": {"dateTime": end}, **extra}


def test_build_rows_states_and_filters():
    tz = datetime.timezone(datetime.timedelta(hours=-3))
    now = datetime.datetime(2026, 9, 14, 10, 15, tzinfo=tz)
    events = [
        _ev("Daily", "2026-09-14T09:00:00-03:00", "2026-09-14T09:15:00-03:00"),
        _ev("Planejamento", "2026-09-14T10:00:00-03:00", "2026-09-14T11:00:00-03:00"),
        _ev("1:1", "2026-09-14T14:00:00-03:00", "2026-09-14T14:30:00-03:00"),
        _ev("Recusada", "2026-09-14T15:00:00-03:00", "2026-09-14T16:00:00-03:00",
            attendees=[{"self": True, "responseStatus": "declined"}]),
        _ev("Escritório", "2026-09-14T08:00:00-03:00", "2026-09-14T18:00:00-03:00", eventType="workingLocation"),
        {"summary": "Feriado", "start": {"date": "2026-09-14"}, "end": {"date": "2026-09-15"}},
    ]
    rows = build_rows(events, now)
    assert [r[2] for r in rows] == ["Feriado", "Daily", "Planejamento", "1:1"]
    assert [r[3] for r in rows] == [1, 0, 1, 2]
    assert rows[0][:2] == ["Dia", ""]
    assert rows[2][:2] == [
        datetime.datetime(2026, 9, 14, 10, 0, tzinfo=tz).astimezone().strftime("%H:%M"),
        datetime.datetime(2026, 9, 14, 11, 0, tzinfo=tz).astimezone().strftime("%H:%M"),
    ]


def test_build_rows_keeps_the_whole_day():
    tz = datetime.timezone.utc
    now = datetime.datetime(2026, 9, 14, 20, 0, tzinfo=tz)
    events = [_ev(f"R{h}", f"2026-09-14T{h:02d}:00:00Z", f"2026-09-14T{h:02d}:30:00Z") for h in range(8, 22)]
    rows = build_rows(events, now)
    assert [r[2] for r in rows] == [f"R{h}" for h in range(8, 22)]   # nothing dropped; the device scrolls
    assert [r[3] for r in rows][-2:] == [1, 2]
