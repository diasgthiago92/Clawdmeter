#!/usr/bin/env python3
"""One-time Google login for the Agenda screen (read-only calendar access).

Reuses the OAuth desktop client the Kiro agents already use
(~/.kiro/google_credentials.json) but asks only for calendar.readonly and
stores the result in its own file, so the Kiro Gmail token is never touched.

    ~/.kiro/venv/bin/python ~/Clawdmeter/daemon/google_calendar_login.py
"""

import json
from pathlib import Path

from google_auth_oauthlib.flow import InstalledAppFlow

CLIENT_FILE = Path.home() / ".kiro" / "google_credentials.json"
TOKEN_FILE = Path.home() / ".config" / "claude-usage-monitor" / "google_calendar_token.json"
SCOPES = ["https://www.googleapis.com/auth/calendar.readonly"]


def main() -> None:
    flow = InstalledAppFlow.from_client_secrets_file(str(CLIENT_FILE), SCOPES)
    creds = flow.run_local_server(port=0, prompt="consent")
    TOKEN_FILE.parent.mkdir(parents=True, exist_ok=True)
    TOKEN_FILE.write_text(json.dumps({
        "refresh_token": creds.refresh_token,
        "client_id": creds.client_id,
        "client_secret": creds.client_secret,
        "token_uri": creds.token_uri,
        "scopes": SCOPES,
    }))
    TOKEN_FILE.chmod(0o600)
    print(f"Pronto: acesso de leitura à agenda salvo em {TOKEN_FILE}")


if __name__ == "__main__":
    main()
