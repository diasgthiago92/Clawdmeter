import asyncio
import datetime
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch, AsyncMock
from zoneinfo import ZoneInfo

from daemon.kiro_routines import KiroRoutines, phone_row


class PhoneRoutineTests(unittest.TestCase):
    def test_local_states_and_daily_reset(self):
        now = datetime.datetime(2026, 9, 22, 14, tzinfo=ZoneInfo('America/Sao_Paulo')).timestamp()
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.assertEqual(phone_row(now, root)[2:5], [3, 0, 0])
            (root / 'logs').mkdir()
            (root / 'logs/20260922-110000.log').touch()
            for status, expected in [('running', 2), ('failed', 0), ('completed', 1)]:
                (root / 'estado.json').write_text(json.dumps(dict(
                    attempt_day='2026-09-22', status=status,
                    started_at='2026-09-22T11:00:00-03:00',
                    finished_at='2026-09-22T11:25:00-03:00')))
                row = phone_row(now, root)
                self.assertEqual(row[2:], [expected, 1, 0, 1])
                self.assertEqual(row[1], '11:00' if expected == 2 else '11:25')
            self.assertEqual(phone_row(now + 86400, root)[2:5], [3, 0, 0])
            (root / 'estado.json').write_text('bad json')
            self.assertEqual(phone_row(now, root)[2], 0)

    def test_slack_failure_does_not_hide_local_routine(self):
        source = KiroRoutines()
        source.payloads = [{'r': [['Backup', '10:00', 1, 1, 1]]}]
        local = ['Preços celulares', '11:00', 2, 1, 0, 1]
        with patch.object(source, '_get_slack', AsyncMock(side_effect=OSError('offline'))), \
                patch('daemon.kiro_routines.phone_row', return_value=local):
            payloads = asyncio.run(source.get(100))
        rows = [row for payload in payloads for row in payload['r']]
        self.assertEqual(rows, [local, ['Backup', '10:00', 1, 1, 1]])

    def test_without_slack_token(self):
        with patch('daemon.kiro_routines.load_token', return_value=None), \
                patch('daemon.kiro_routines.phone_row', return_value=['Preços celulares', '11:00', 3, 0, 0, 1]):
            rows = asyncio.run(KiroRoutines().get(100))
        self.assertEqual(rows[0]['r'][0][0], 'Preços celulares')
