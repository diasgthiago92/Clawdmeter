import json

from ai_actions import (
    NO_ACTIONS,
    build_rows,
    claude_actions,
    claude_action_text,
    clip,
    gemini_step_action,
    kiro_actions,
)


def test_claude_tool_calls_become_actions():
    line = json.dumps({
        "timestamp": "2026-09-15T12:00:00Z",
        "message": {"content": [
            {"type": "text", "text": "ok"},
            {"type": "tool_use", "name": "Bash", "input": {"command": "ls", "description": "Listar arquivos"}},
            {"type": "tool_use", "name": "Edit", "input": {"file_path": "/a/b/ui.cpp"}},
            {"type": "tool_use", "name": "TodoWrite", "input": {}},
        ]},
    }).encode()
    actions = claude_actions([line, b"not json"], since=0)
    assert [text for _, text in actions] == ["Listar arquivos", "Editou ui.cpp"]


def test_claude_mcp_name_is_shortened():
    assert claude_action_text("mcp__slack__send_message", {}) == "send message"


def test_kiro_actions_use_purpose_and_prompt_time():
    lines = [
        json.dumps({"kind": "Prompt", "data": {"content": [], "meta": {"timestamp": 1000}}}).encode(),
        json.dumps({"kind": "AssistantMessage", "data": {"content": [
            {"kind": "toolUse", "data": {"name": "shell", "input": {"__tool_use_purpose": "Criar a issue", "command": "x"}}},
        ]}}).encode(),
        json.dumps({"kind": "ToolResults", "data": {"content": []}}).encode(),
    ]
    assert kiro_actions(lines, since=0) == [(1000.0, "Criar a issue")]
    assert kiro_actions(lines, since=2000) == []


def test_gemini_step_reads_tool_action():
    payload = b"\x12\x08abc\x1a\x08list_dir\x22\x40" + b'{"DirectoryPath":"/x","toolAction":"Listing x"}' + b"\x00\x01"
    assert gemini_step_action(payload) == "Listing x"
    assert gemini_step_action(b"\x0asessionID reasoning") is None


def test_rows_group_by_ai_newest_first_with_placeholder():
    groups = [[(100.0, "velha"), (200.0, "nova")], [], [(50.0, "g")]]
    rows = build_rows(groups)
    assert [r[0] for r in rows] == [0, 0, 1, 2]
    assert [r[2] for r in rows] == ["nova", "velha", NO_ACTIONS, "g"]
    assert rows[2][1] == ""


def test_clip_keeps_one_line():
    assert clip("a\n  b") == "a b"
    assert len(clip("x" * 100)) == 44
