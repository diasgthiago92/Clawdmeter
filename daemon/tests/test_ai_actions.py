import datetime
import json

from ai_actions import (
    AI_CLAUDE,
    NO_COMMITS,
    build_rows,
    claude_calls,
    is_commit_command,
    kiro_calls,
    match_commits,
    merge_commits,
    output_commits,
    repo_dir,
    when_label,
)


def test_commit_command_detection():
    assert is_commit_command('git add -A && git commit -q -m "x"')
    assert is_commit_command("/opt/homebrew/bin/rtk git commit -m x")
    assert is_commit_command("git -C /repo commit -m x")
    assert not is_commit_command("git log --oneline")
    assert not is_commit_command("git commit-tree abc")


def test_repo_dir_follows_cd_and_dash_c():
    assert repo_dir("/Users/me", "cd ~/Clawdmeter && git commit -m x").endswith("/Clawdmeter")
    assert repo_dir("/Users/me", "cd proj; git add . && git commit -m x") == "/Users/me/proj"
    assert repo_dir("/Users/me", "git -C /tmp/repo commit -m x") == "/tmp/repo"
    assert repo_dir("/work", "git commit -m x") == "/work"


def test_claude_transcript_calls():
    line = json.dumps({
        "timestamp": "2026-09-15T12:00:00Z", "cwd": "/Users/me",
        "message": {"content": [
            {"type": "tool_use", "name": "Bash", "input": {"command": "cd repo && git commit -q -m 'x'"}},
            {"type": "tool_use", "name": "Bash", "input": {"command": "git status"}},
        ]},
    }).encode()
    calls, printed = claude_calls([line])
    assert [(c[1], c[2]) for c in calls] == [("/Users/me", "cd repo && git commit -q -m 'x'")]
    assert printed == []


def test_output_commit_needs_gits_own_summary():
    real = b'{"stdout": "rsync ok\\n[main b7f1b97] backup: 2026-09-11 14:43\\n 7 files changed, 28 insertions(+)\\n"}'
    assert output_commits(real, 1.0) == [(1.0, "b7f1b97", "backup: 2026-09-11 14:43")]
    accent = b'"[main (root-commit) 265976e] Hist\\u00f3ria nova\\n 1 file changed"'
    assert output_commits(accent, 2.0) == [(2.0, "265976e", "História nova")]
    quoted_twice = b'"text": "[main b7f1b97] backup\\\\n 7 files changed"'
    grep_only = b'"[main b7f1b97] backup: 2026-09-11 14:43"'
    assert output_commits(quoted_twice, 1.0) == [] and output_commits(grep_only, 1.0) == []


def test_kiro_calls_use_turn_time_and_tool_output():
    lines = [
        json.dumps({"kind": "Prompt", "data": {"content": [], "meta": {"timestamp": 1000}}}).encode(),
        json.dumps({"kind": "AssistantMessage", "data": {"content": [
            {"kind": "toolUse", "data": {"name": "shell", "input": {"command": "bash backup.sh"}}},
        ]}}).encode(),
        b'{"version": "v1", "kind": "ToolResults", "data": {"stdout": "[main abc1234] backup\\n 2 files changed"}}',
    ]
    calls, printed = kiro_calls(lines, "/Users/me")
    assert calls == [] and printed == [(1000.0, "abc1234", "backup")]


class FakeGit:
    def __init__(self, rows):
        self.rows = rows

    def commits(self, repo, now):
        return self.rows.get(repo, [])


def test_match_picks_first_commit_after_call_once():
    git = FakeGit({"/r": [(1010.0, "a" * 40, "segundo"), (1002.0, "b" * 40, "primeiro"), (500.0, "c" * 40, "velho")]})
    calls = [(1000.0, "/r", "git commit -m 1"), (1001.0, "/r", "git commit -m 2")]
    assert match_commits(AI_CLAUDE, calls, git, 2000.0) == [(1002.0, "bbbbbbb", "primeiro"), (1010.0, "aaaaaaa", "segundo")]


def test_merge_dedupes_by_hash_and_rows_group_by_ai():
    merged = merge_commits([(20.0, "abc1234", "x")], [(10.0, "abc1234", "x"), (5.0, "def5678", "y")])
    assert sorted(merged) == [(5.0, "def5678", "y"), (20.0, "abc1234", "x")]
    now = datetime.datetime(2026, 9, 15, 12, 0).timestamp()
    rows = build_rows([merged, [], []], now)
    assert [r[0] for r in rows] == [0, 0, 1, 2]
    assert rows[2] == [1, "", NO_COMMITS]


def test_when_label_today_vs_older():
    now = datetime.datetime(2026, 9, 15, 12, 0).timestamp()
    assert when_label(datetime.datetime(2026, 9, 15, 9, 5).timestamp(), now) == "09:05"
    assert when_label(datetime.datetime(2026, 9, 11, 16, 22).timestamp(), now) == "11/09"
