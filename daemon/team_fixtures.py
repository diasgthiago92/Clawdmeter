"""Upcoming matches for one football team (ESPN's public site API).

Sent as {"v": [[opponent, "C"|"F", "Sáb 19/09 20:30", competition], ...],
"o": offset, "n": total} — "C" = casa (home), "F" = fora (away). Times are
converted to the host's local timezone.

While a match is on, LiveMatch sends {"l": [home, away, home goals, away goals,
clock, phase, competition]} every LIVE_REFRESH_S, and {"l": 0} once it ends —
the device holds its live screen (no rotation) between the two.
"""

import datetime

import httpx

from market_quotes import _Refreshing, chunk_table

TEAM_ESPN_ID = "3454"  # Vasco da Gama
SCHEDULE_URL = "https://site.api.espn.com/apis/site/v2/sports/soccer/all/teams/{team}/schedule"
GAMES_MAX = 10   # the device scrolls when they don't fit
FIXTURES_REFRESH_S = 30 * 60
SCOREBOARD_URL = "https://site.api.espn.com/apis/site/v2/sports/soccer/{league}/scoreboard"
LIVE_REFRESH_S = 30
LIVE_LEAD_S = 10 * 60          # start watching a little before kickoff
LIVE_MAX_S = 4 * 60 * 60       # give up on an event this long after kickoff

_WEEKDAYS = ("Seg", "Ter", "Qua", "Qui", "Sex", "Sáb", "Dom")
_COMPETITIONS = {
    "bra.1": "Brasileirão",
    "bra.2": "Série B",
    "bra.copa_do_brazil": "Copa do Brasil",
    "bra.camp.carioca": "Carioca",
    "conmebol.libertadores": "Libertadores",
    "conmebol.sudamericana": "Sul-Americana",
}


def competition_name(league: dict) -> str:
    return _COMPETITIONS.get(league.get("slug", ""), (league.get("shortName") or "")[:15])


def format_kickoff(iso_utc: str, time_valid: bool) -> str:
    """"2026-09-19T23:30Z" -> "Sáb 19/09 20:30" in local time (date only if TBD)."""
    when = datetime.datetime.fromisoformat(iso_utc.replace("Z", "+00:00")).astimezone()
    text = f"{_WEEKDAYS[when.weekday()]} {when:%d/%m}"
    return f"{text} {when:%H:%M}" if time_valid else text


_PHASES = {
    "STATUS_FIRST_HALF": "1º tempo",
    "STATUS_HALFTIME": "Intervalo",
    "STATUS_SECOND_HALF": "2º tempo",
    "STATUS_END_OF_REGULATION": "Fim do tempo normal",
    "STATUS_OVERTIME": "Prorrogação",
    "STATUS_FIRST_HALF_EXTRA_TIME": "Prorrogação",
    "STATUS_HALFTIME_ET": "Intervalo da prorrogação",
    "STATUS_SECOND_HALF_EXTRA_TIME": "Prorrogação",
    "STATUS_END_OF_EXTRATIME": "Fim da prorrogação",
    "STATUS_SHOOTOUT": "Pênaltis",
}


def schedule_events(schedule: dict) -> dict[str, tuple[str, datetime.datetime]]:
    """{event id: (league slug, kickoff)} for every event in a schedule response."""
    out = {}
    for event in schedule.get("events") or []:
        try:
            start = datetime.datetime.fromisoformat(event["date"].replace("Z", "+00:00"))
            out[str(event["id"])] = (event["league"]["slug"], start)
        except (KeyError, TypeError, ValueError):
            continue
    return out


def watch_candidates(events: dict[str, tuple[str, datetime.datetime]],
                     now: datetime.datetime) -> list[tuple[str, str, datetime.datetime]]:
    """(event id, league slug, kickoff) for events inside the live watch window."""
    return [
        (event_id, slug, start)
        for event_id, (slug, start) in events.items()
        if -LIVE_LEAD_S <= (now - start).total_seconds() <= LIVE_MAX_S
    ]


def _team_name(team: dict) -> str:
    return (team.get("shortDisplayName") or team.get("displayName") or "?")[:15]


def build_live(scoreboard: dict, event_id: str, league: dict | None = None) -> tuple[str, list | None]:
    """(state, payload row) for one event: state is "pre", "in", "post" or "missing"."""
    for event in scoreboard.get("events") or []:
        if str(event.get("id")) != event_id:
            continue
        comp = (event.get("competitions") or [{}])[0]
        status = comp.get("status") or {}
        kind = status.get("type") or {}
        state = kind.get("state", "missing")
        if state != "in":
            return state, None
        sides = {c.get("homeAway"): c for c in comp.get("competitors", [])}
        home, away = sides.get("home"), sides.get("away")
        if not home or not away:
            return "missing", None
        phase = _PHASES.get(kind.get("name", ""), kind.get("description") or "Ao vivo")
        clock = "" if kind.get("name") in ("STATUS_HALFTIME", "STATUS_HALFTIME_ET") else status.get("displayClock", "")
        return state, [
            _team_name(home.get("team") or {}),
            _team_name(away.get("team") or {}),
            int(home.get("score") or 0),
            int(away.get("score") or 0),
            clock,
            phase[:24],
            competition_name(league or event.get("league") or {}),
        ]
    return "missing", None


def build_rows(schedule: dict, team_id: str, now: datetime.datetime) -> list:
    rows = []
    for event in schedule.get("events") or []:
        comp = (event.get("competitions") or [{}])[0]
        if (comp.get("status") or {}).get("type", {}).get("state") != "pre":
            continue
        start = datetime.datetime.fromisoformat(event["date"].replace("Z", "+00:00"))
        if start < now:
            continue
        us = next((c for c in comp.get("competitors", []) if c["team"]["id"] == team_id), None)
        them = next((c for c in comp.get("competitors", []) if c["team"]["id"] != team_id), None)
        if not us or not them:
            continue
        rows.append([
            them["team"].get("shortDisplayName") or them["team"].get("displayName", "?"),
            "C" if us.get("homeAway") == "home" else "F",
            format_kickoff(event["date"], event.get("timeValid", True)),
            competition_name(event.get("league") or {}),
        ])
        if len(rows) == GAMES_MAX:
            break
    return rows


class TeamFixtures(_Refreshing):
    refresh_s = FIXTURES_REFRESH_S  # ESPN 403s browser-like UAs; keep httpx's default

    def __init__(self) -> None:
        super().__init__()
        # Accumulated across refreshes: the fixture list may drop a match once it kicks off.
        self.events: dict[str, tuple[str, datetime.datetime]] = {}

    async def _fetch(self, http: httpx.AsyncClient) -> list[dict]:
        resp = await http.get(SCHEDULE_URL.format(team=TEAM_ESPN_ID), params={"fixture": "true"})
        resp.raise_for_status()
        schedule = resp.json()
        now = datetime.datetime.now(datetime.timezone.utc)
        today = now.astimezone().date()
        self.events = {   # keep today's matches all day (match-day shirts), others until they're over
            event_id: (slug, start)
            for event_id, (slug, start) in {**self.events, **schedule_events(schedule)}.items()
            if (now - start).total_seconds() <= LIVE_MAX_S or start.astimezone().date() == today
        }
        rows = build_rows(schedule, TEAM_ESPN_ID, now)
        return chunk_table("v", rows) if rows else [{"v": [], "o": 0, "n": 0}]


class LiveMatch:
    """Polls the scoreboard for a match in progress, using the fixtures' cached schedule."""

    def __init__(self, fixtures: TeamFixtures) -> None:
        self.fixtures = fixtures
        self.checked_at = 0.0
        self.live = False          # the device was last told a match is on

    async def poll(self, now: float) -> dict | None:
        """Payload to send now, or None when there is nothing new to say."""
        if now - self.checked_at < LIVE_REFRESH_S:
            return None
        utc_now = datetime.datetime.fromtimestamp(now, datetime.timezone.utc)
        candidates = watch_candidates(self.fixtures.events, utc_now)
        if not candidates:
            return self._ended()
        self.checked_at = now
        row = None
        async with httpx.AsyncClient(timeout=10.0) as http:
            for event_id, slug, start in candidates:
                resp = await http.get(SCOREBOARD_URL.format(league=slug),
                                      params={"dates": start.strftime("%Y%m%d")})
                resp.raise_for_status()
                state, row = build_live(resp.json(), event_id, {"slug": slug})
                if row:
                    break
        if row is None:
            return self._ended()
        self.live = True
        return {"l": row}

    def _ended(self) -> dict | None:
        if not self.live:
            return None
        self.live = False
        return {"l": 0}


ALMIRANTE_LEAD_S = 3 * 3600


def almirante_window(events: dict[str, tuple[str, datetime.datetime]], now: datetime.datetime) -> bool:
    """True from 3 hours before a kickoff until the end of that day (or 3 hours after
    kickoff, for a late game running past midnight): when the Almirante mascot shows."""
    for _, start in events.values():
        local = start.astimezone()
        day_end = datetime.datetime.combine(local.date() + datetime.timedelta(days=1),
                                            datetime.time(), tzinfo=local.tzinfo)
        until = max(day_end, local + datetime.timedelta(seconds=ALMIRANTE_LEAD_S))
        if local - datetime.timedelta(seconds=ALMIRANTE_LEAD_S) <= now < until:
            return True
    return False


def is_match_day(events: dict[str, tuple[str, datetime.datetime]], now: datetime.datetime) -> bool:
    """True when the team plays on the local calendar day of `now`."""
    today = now.astimezone().date()
    return any(start.astimezone().date() == today for _, start in events.values())
