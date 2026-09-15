"""Today's Google Calendar meetings for the Agenda screen.

Uses the read-only token written by google_calendar_login.py and refreshes its
short-lived access token itself (it is this daemon's own token). Sent as

    {"g": [[start "HH:MM" | "Dia", end "HH:MM" | "", title, state], ...],
     "o": offset, "n": total}

state: 0 = already over, 1 = happening now, 2 = still to come. Declined
meetings and working-location entries are skipped. Before the one-time login
the screen gets {"g": [], "o": 0, "n": 0, "login": 1}.

Meeting alert: while a meeting starts within ALERT_LEAD_S the device gets
{"mt": [title, "HH:MM", seconds to start, where, joinable]} (where = room or
video link; joinable = 1 when the meeting has a video link the Mac can open)
and holds an alert screen with a countdown; {"mt": 0} once none is imminent.
Its "Começar" button sends {"mg": 1}: the daemon opens that meeting's link
(meeting_link, never a URL from the device) and answers {"mgk": 1 | 0}.
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
ALERT_LEAD_S = 5 * 60
ALERT_GRACE_S = 60          # keep the alert up for a minute after the start
ALERT_TITLE_MAX = 60
WHERE_MAX = 48
ALERT_BYTES = 170
_SKIP_TYPES = {"workingLocation", "focusTime"}


def _local(value: str) -> datetime.datetime:
    return datetime.datetime.fromisoformat(value.replace("Z", "+00:00")).astimezone()


def _where(ev: dict) -> str:
    """Room or video link, shortened for the device ("meet.google.com/abc-defg-hij")."""
    where = (ev.get("location") or "").strip()
    if not where:
        where = ev.get("hangoutLink") or ""
        for entry in (ev.get("conferenceData") or {}).get("entryPoints") or []:
            if not where and entry.get("entryPointType") == "video":
                where = entry.get("uri") or ""
    for prefix in ("https://", "http://", "www."):
        if where.startswith(prefix):
            where = where[len(prefix):]
    return where[:WHERE_MAX]


# Only these hosts are opened from the device's "Começar" button.
JOIN_HOSTS = ("meet.google.com", "zoom.us", "teams.microsoft.com", "teams.live.com")


def meeting_link(ev: dict) -> str | None:
    """Full https video link of an event (Meet, Zoom or Teams), or None."""
    candidates = [ev.get("hangoutLink") or ""]
    candidates += [e.get("uri") or "" for e in (ev.get("conferenceData") or {}).get("entryPoints") or []
                   if e.get("entryPointType") == "video"]
    candidates += (ev.get("location") or "").split()
    for url in candidates:
        if not url.startswith("https://"):
            continue
        host = url[len("https://"):].split("/", 1)[0].split("?", 1)[0].lower()
        if any(host == h or host.endswith("." + h) for h in JOIN_HOSTS):
            return url
    return None


def _attending(ev: dict) -> bool:
    if ev.get("status") == "cancelled" or ev.get("eventType") in _SKIP_TYPES:
        return False
    me = next((a for a in ev.get("attendees") or [] if a.get("self")), None)
    return not (me and me.get("responseStatus") == "declined")


def alert_event(events: list[dict], now: datetime.datetime):
    """(start, event, seconds to start) of the meeting the alert is about, or None."""
    best = None
    for ev in events:
        start = (ev.get("start") or {}).get("dateTime")
        if not start or not _attending(ev):
            continue
        s = _local(start)
        secs = (s - now).total_seconds()
        if -ALERT_GRACE_S <= secs <= ALERT_LEAD_S and (best is None or s < best[0]):
            best = (s, ev, secs)
    return best


def build_alert(events: list[dict], now: datetime.datetime) -> dict:
    """{"mt": [...]} for the next timed meeting starting within ALERT_LEAD_S, else {"mt": 0}."""
    best = alert_event(events, now)
    if not best:
        return {"mt": 0}
    s, ev, secs = best
    title = (ev.get("summary") or "(sem título)").strip()[:ALERT_TITLE_MAX]
    payload = {"mt": [title, s.strftime("%H:%M"), max(0, int(secs)), _where(ev), int(meeting_link(ev) is not None)]}
    # One BLE write: trim the title until the escaped JSON fits.
    while len(json.dumps(payload, separators=(",", ":"))) > ALERT_BYTES and payload["mt"][0]:
        payload["mt"][0] = payload["mt"][0][:-2].rstrip()
    return payload


def build_rows(events: list[dict], now: datetime.datetime) -> list[list]:
    rows = []
    for ev in events:
        if not _attending(ev):
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
        self.events: list[dict] = []

    def alert(self, now: float) -> dict:
        """Meeting alert from the last fetch, recomputed at `now` (exact countdown)."""
        return build_alert(self.events, datetime.datetime.fromtimestamp(now).astimezone())

    def alert_link(self, now: float) -> str | None:
        """Video link of the meeting on the alert screen right now, if it has one."""
        best = alert_event(self.events, datetime.datetime.fromtimestamp(now).astimezone())
        return meeting_link(best[1]) if best else None

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
        self.events = resp.json().get("items") or []
        rows = build_rows(self.events, local_now)
        self.payloads = chunk_table("g", rows) if rows else [{"g": [], "o": 0, "n": 0}]
        return self.payloads
