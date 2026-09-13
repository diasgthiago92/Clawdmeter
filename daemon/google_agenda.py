"""Today's Google Calendar meetings for the Agenda screen.

Uses the read-only token written by google_calendar_login.py and refreshes its
short-lived access token itself (it is this daemon's own token). Sent as

    {"g": [[start "HH:MM" | "Dia", end "HH:MM" | "", title, state], ...],
     "o": offset, "n": total}

state: 0 = already over, 1 = happening now, 2 = still to come. Declined
meetings and working-location entries are skipped. Before the one-time login
the screen gets {"g": [], "o": 0, "n": 0, "login": 1}.
"""

import datetime
import json
from pathlib import Path

import httpx

from market_quotes import chunk_table

TOKEN_FILE = Path.home() / ".config" / "claude-usage-monitor" / "google_calendar_token.json"
EVENTS_URL = "https://www.googleapis.com/calendar/v3/calendars/primary/events"
AGENDA_MAX = 24   # the device scrolls and opens on the current meeting
AGENDA_REFRESH_S = 60
TITLE_MAX = 30
_SKIP_TYPES = {"workingLocation", "focusTime"}


def _local(value: str) -> datetime.datetime:
    return datetime.datetime.fromisoformat(value.replace("Z", "+00:00")).astimezone()


def build_rows(events: list[dict], now: datetime.datetime) -> list[list]:
    rows = []
    for ev in events:
        if ev.get("status") == "cancelled" or ev.get("eventType") in _SKIP_TYPES:
            continue
        me = next((a for a in ev.get("attendees") or [] if a.get("self")), None)
        if me and me.get("responseStatus") == "declined":
            continue
        title = (ev.get("summary") or "(sem título)").strip()[:TITLE_MAX]
        start, end = ev.get("start") or {}, ev.get("end") or {}
        if "dateTime" in start and "dateTime" in end:
            s, e = _local(start["dateTime"]), _local(end["dateTime"])
            state = 0 if e <= now else 1 if s <= now else 2
            rows.append((0, s, [s.strftime("%H:%M"), e.strftime("%H:%M"), title, state]))
        elif "date" in start:
            rows.append((-1, None, ["Dia", "", title, 1]))
    rows.sort(key=lambda r: (r[0], r[1] or now))
    return [r[2] for r in rows][:AGENDA_MAX]


class GoogleAgenda:
    def __init__(self, token_file: Path = TOKEN_FILE) -> None:
        self.token_file = token_file
        self.access_token: str | None = None
        self.access_expiry = 0.0
        self.payloads: list[dict] = []
        self.fetched_at = 0.0

    async def _access(self, http: httpx.AsyncClient, now: float) -> str | None:
        if self.access_token and now < self.access_expiry - 60:
            return self.access_token
        try:
            creds = json.loads(self.token_file.read_text())
        except (OSError, ValueError):
            return None
        resp = await http.post(creds["token_uri"], data={
            "grant_type": "refresh_token",
            "refresh_token": creds["refresh_token"],
            "client_id": creds["client_id"],
            "client_secret": creds["client_secret"],
        })
        resp.raise_for_status()
        body = resp.json()
        self.access_token = body["access_token"]
        self.access_expiry = now + float(body.get("expires_in", 3600))
        return self.access_token

    async def get(self, now: float) -> list[dict]:
        if now - self.fetched_at < AGENDA_REFRESH_S:
            return self.payloads
        self.fetched_at = now
        local_now = datetime.datetime.fromtimestamp(now).astimezone()
        midnight = local_now.replace(hour=0, minute=0, second=0, microsecond=0)
        async with httpx.AsyncClient(timeout=10.0) as http:
            token = await self._access(http, now)
            if not token:
                self.payloads = [{"g": [], "o": 0, "n": 0, "login": 1}]
                return self.payloads
            resp = await http.get(EVENTS_URL, headers={"Authorization": f"Bearer {token}"}, params={
                "timeMin": midnight.isoformat(),
                "timeMax": (midnight + datetime.timedelta(days=1)).isoformat(),
                "singleEvents": "true",
                "orderBy": "startTime",
                "maxResults": 50,
            })
            resp.raise_for_status()
        rows = build_rows(resp.json().get("items") or [], local_now)
        self.payloads = chunk_table("g", rows) if rows else [{"g": [], "o": 0, "n": 0}]
        return self.payloads
