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


def test_build_alert_next_meeting_within_five_minutes():
    from daemon.google_agenda import build_alert

    tz = datetime.timezone(datetime.timedelta(hours=-3))
    now = datetime.datetime(2026, 9, 14, 9, 26, 30, tzinfo=tz)
    events = [
        _ev("Daily", "2026-09-14T09:00:00-03:00", "2026-09-14T09:15:00-03:00"),
        _ev("Planejamento", "2026-09-14T09:30:00-03:00", "2026-09-14T10:00:00-03:00",
            hangoutLink="https://meet.google.com/abc-defg-hij"),
        _ev("Review", "2026-09-14T09:31:00-03:00", "2026-09-14T10:00:00-03:00", location="Sala Rio 3"),
    ]
    alert = build_alert(events, now)
    assert alert["mt"][0] == "Planejamento" and alert["mt"][2] == 210
    assert alert["mt"][3] == "meet.google.com/abc-defg-hij"
    later = datetime.datetime(2026, 9, 14, 9, 30, 30, tzinfo=tz)
    assert build_alert(events, later)["mt"][0] == "Planejamento"   # grace minute after the start
    assert build_alert(events, datetime.datetime(2026, 9, 14, 11, 0, tzinfo=tz)) == {"mt": 0}


def test_alert_marks_joinable_meetings():
    from daemon.google_agenda import build_alert

    tz = datetime.timezone(datetime.timedelta(hours=-3))
    now = datetime.datetime(2026, 9, 14, 9, 27, tzinfo=tz)
    meet = [_ev("Daily", "2026-09-14T09:30:00-03:00", "2026-09-14T10:00:00-03:00",
                hangoutLink="https://meet.google.com/abc-defg-hij")]
    room = [_ev("Review", "2026-09-14T09:30:00-03:00", "2026-09-14T10:00:00-03:00", location="Sala Rio 3")]
    assert build_alert(meet, now)["mt"][4] == 1
    assert build_alert(room, now)["mt"][4] == 0


def test_meeting_link_only_trusts_known_https_hosts():
    from daemon.google_agenda import meeting_link

    assert meeting_link({"hangoutLink": "https://meet.google.com/abc"}) == "https://meet.google.com/abc"
    assert meeting_link({"conferenceData": {"entryPoints": [
        {"entryPointType": "phone", "uri": "tel:+551100"},
        {"entryPointType": "video", "uri": "https://olx.zoom.us/j/123"}]}}) == "https://olx.zoom.us/j/123"
    assert meeting_link({"location": "Sala 3 https://teams.microsoft.com/l/meetup"}) == "https://teams.microsoft.com/l/meetup"
    assert meeting_link({"location": "https://evil.example/meet.google.com"}) is None
    assert meeting_link({"location": "http://meet.google.com/abc"}) is None
    assert meeting_link({"location": "Sala Rio 3"}) is None
