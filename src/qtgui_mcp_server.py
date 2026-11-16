# Copyright (c) 2026 Chris Polewczuk
# SPDX-License-Identifier: MIT

"""FastMCP adapter for the localhost Qt GUI diagnostic bridge.

The C++ side owns the Qt event loop and exposes one JSON request per TCP
connection. This module deliberately stays thin: it translates MCP tool calls
into that local protocol and turns the returned PNG bytes into an MCP Image.

The bridge supports both Qt Widgets and Qt GUI ``QWindow`` applications. It is
intended for a debug/offscreen application launched separately from this
process. It is not an application supervisor and does not attempt to start or
stop the Qt executable.
"""

import base64
import json
import socket
import time
from pathlib import Path
from typing import Any

# MCP SDK 2.x renamed FastMCP to MCPServer and moved the import path. Keep a
# compatibility fallback so the adapter also remains usable with the 1.x
# maintenance line when a project has not migrated its Python environment yet.
try:
    from mcp.server.mcpserver import Image, MCPServer
except ModuleNotFoundError:  # MCP SDK 1.x
    from mcp.server.fastmcp import FastMCP, Image

    MCPServer = FastMCP


def _read_project_version() -> str:
    """Read the repository version shared by CMake, releases, and Python."""
    version_file = Path(__file__).resolve().parents[1] / "VERSION"
    return version_file.read_text(encoding="utf-8").strip()


__version__ = _read_project_version()


mcp = MCPServer("QtGuiTestingMCP")


UI_TEST_COMMANDS = """UI test commands

Lifecycle:
  start ui test
  end ui test

Inspection:
  tree
  show ui test commands
  status
  wait idle [MILLISECONDS]
  logs
  clear logs
  screenshot [windowName]
  save screenshot to PATH

Interaction:
  click OBJECT_NAME
  activate ACTION_NAME
  type OBJECT_NAME TEXT...
  key OBJECT_NAME KEY [Ctrl+Alt+Shift+Meta]
  mouse click OBJECT_NAME
  mouse double-click OBJECT_NAME
  mouse move OBJECT_NAME
  wait MILLISECONDS
  wait-text OBJECT_NAME EXPECTED TEXT...

Assertions:
  assert visible OBJECT_NAME [true|false]
  assert enabled OBJECT_NAME [true|false]
  assert text OBJECT_NAME EXPECTED TEXT...
  assert contains OBJECT_NAME TEXT...
  assert popup [critical|warning|information] [TEXT...]
  assert no-errors

Notes:
  - Use tree first to discover exact objectName selectors. Standalone QWindow
    roots are returned in the ``windows`` array; QWidget roots are in
    ``widgets``.
  - Text consumes the rest of the line; quotes are not required.
  - A screenshot is captured automatically after every command.
  - Scripts are limited to 100 commands and never execute arbitrary code.
"""

def send_cmd(payload: dict) -> dict:
    """Send one command to the bridge and decode its JSON response.

    A new connection is used for every command because the C++ protocol is a
    single-request exchange. ``create_connection`` applies the five-second
    timeout to both connection establishment and subsequent socket operations.
    """
    with socket.create_connection(("127.0.0.1", 9999), timeout=5) as sock:
        # JSON is sent as UTF-8. The C++ server waits until it can parse a
        # complete object, so packet boundaries do not need special handling
        # in this client.
        sock.sendall(json.dumps(payload).encode("utf-8"))
        chunks = []
        while data := sock.recv(65536):
            chunks.append(data)

    if not chunks:
        raise RuntimeError("The Qt bridge closed the connection without a response")

    try:
        response = json.loads(b"".join(chunks).decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise RuntimeError("The Qt bridge returned an invalid JSON response") from error

    if not isinstance(response, dict):
        raise RuntimeError("The Qt bridge returned a non-object JSON response")
    return response


def require_success(response: dict) -> dict:
    """Raise an MCP-visible error when the bridge rejected a command."""
    if "error" in response:
        raise RuntimeError(str(response["error"]))
    return response


@mcp.tool()
def show_ui_test_commands() -> str:
    """Show the complete line-oriented UI test command reference."""
    return UI_TEST_COMMANDS


@mcp.tool()
def get_app_status() -> str:
    """Return Qt bridge health, window counts, active window, and log count."""
    return json.dumps(require_success(send_cmd({"cmd": "status"})), indent=2)


@mcp.tool()
def wait_for_idle(milliseconds: int = 250) -> str:
    """Process queued Qt events for a bounded interval and return bridge status."""
    duration = max(0, min(int(milliseconds), 5000))
    return json.dumps(
        require_success(send_cmd({"cmd": "idle", "milliseconds": duration})),
        indent=2,
    )


@mcp.tool()
def get_recent_logs() -> str:
    """Return recent captured Qt debug, info, warning, and critical messages."""
    return json.dumps(require_success(send_cmd({"cmd": "logs"})), indent=2)


@mcp.tool()
def clear_recent_logs() -> str:
    """Clear the bridge's bounded in-memory Qt log buffer."""
    return json.dumps(require_success(send_cmd({"cmd": "clear_logs"})))


@mcp.tool()
def press_key(
    key: str,
    object_name: str | None = None,
    modifiers: list[str] | None = None,
) -> str:
    """Send a real key press/release pair to a QWidget or QWindow.

    If ``object_name`` is omitted, the bridge targets the focused Qt object.
    """
    payload = {"cmd": "key", "key": key, "modifiers": modifiers or []}
    if object_name:
        payload["target"] = object_name
    return json.dumps(require_success(send_cmd(payload)))


@mcp.tool()
def mouse_event(
    object_name: str,
    action: str = "click",
    button: str = "left",
    x: int | None = None,
    y: int | None = None,
    modifiers: list[str] | None = None,
) -> str:
    """Send a mouse move, click, or double-click event to a QWidget or QWindow."""
    payload = {
        "cmd": "mouse",
        "target": object_name,
        "action": action,
        "button": button,
        "modifiers": modifiers or [],
    }
    if x is not None:
        payload["x"] = x
    if y is not None:
        payload["y"] = y
    return json.dumps(require_success(send_cmd(payload)))


def _find_ui_element(
    elements: list[dict[str, Any]], object_name: str
) -> dict[str, Any] | None:
    """Find a QWidget or QWindow recursively in a serialized UI tree."""
    for element in elements:
        if element.get("objectName") == object_name:
            return element
        child = _find_ui_element(element.get("children", []), object_name)
        if child is not None:
            return child
    return None


def _find_ui_action(
    elements: list[dict[str, Any]], object_name: str
) -> dict[str, Any] | None:
    """Find a QAction attached to a serialized widget hierarchy."""
    for element in elements:
        for action in element.get("actions", []):
            if action.get("objectName") == object_name:
                return action
        action = _find_ui_action(element.get("children", []), object_name)
        if action is not None:
            return action
    return None


def _observe_state() -> dict[str, Any]:
    """Collect the state used by script assertions and the final report."""
    tree = require_success(send_cmd({"cmd": "tree"}))
    popups = require_success(send_cmd({"cmd": "popup_errors"}))
    return {
        "widgets": tree.get("widgets", []),
        "windows": tree.get("windows", []),
        "popup_errors": popups.get("popups", []),
    }


def _find_state_element(state: dict[str, Any], object_name: str) -> dict[str, Any] | None:
    """Find an element across both QWidget and standalone QWindow roots."""
    elements = [*state.get("widgets", []), *state.get("windows", [])]
    return _find_ui_element(elements, object_name) or _find_ui_action(elements, object_name)


def _safe_response(response: dict[str, Any]) -> dict[str, Any]:
    """Remove binary screenshot data before embedding a response in a report."""
    result = dict(response)
    if "image_base64" in result:
        result["image_captured"] = True
        result["image_base64_length"] = len(result.pop("image_base64"))
    return result


def _capture_screenshot(target: str | None = None) -> tuple[dict[str, Any], bytes]:
    """Capture a PNG from the bridge and return both metadata and bytes."""
    payload = {"cmd": "screenshot"}
    if target:
        payload["target"] = target
    response = require_success(send_cmd(payload))
    try:
        image_bytes = base64.b64decode(response["image_base64"], validate=True)
    except (KeyError, ValueError) as error:
        raise RuntimeError("The Qt bridge returned invalid screenshot data") from error
    return response, image_bytes


def _run_script_step(step: dict[str, Any]) -> dict[str, Any]:
    """Execute one validated script step and return its structured result."""
    action = step.get("action")

    if action == "click":
        return require_success(send_cmd({"cmd": "click", "target": step["target"]}))

    if action == "activate_action":
        return require_success(send_cmd({
            "cmd": "activate_action",
            "target": step["target"],
        }))

    if action == "type":
        return require_success(send_cmd({
            "cmd": "type",
            "target": step["target"],
            "text": step.get("text", ""),
        }))

    if action == "key":
        payload = {
            "cmd": "key",
            "key": step["key"],
            "modifiers": step.get("modifiers", []),
        }
        if step.get("target"):
            payload["target"] = step["target"]
        return require_success(send_cmd(payload))

    if action == "mouse":
        payload = {
            "cmd": "mouse",
            "target": step["target"],
            "action": step.get("mouse_action", "click"),
            "button": step.get("button", "left"),
            "modifiers": step.get("modifiers", []),
        }
        if step.get("x") is not None:
            payload["x"] = step["x"]
        if step.get("y") is not None:
            payload["y"] = step["y"]
        return require_success(send_cmd(payload))

    if action == "wait":
        milliseconds = max(0, min(int(step.get("milliseconds", 0)), 30000))
        time.sleep(milliseconds / 1000)
        return {"waited_milliseconds": milliseconds}

    if action == "observe":
        return _observe_state()

    if action == "help":
        return {"commands": UI_TEST_COMMANDS}

    if action == "status":
        return require_success(send_cmd({"cmd": "status"}))

    if action == "wait_idle":
        milliseconds = max(0, min(int(step.get("milliseconds", 250)), 5000))
        return require_success(send_cmd({"cmd": "idle", "milliseconds": milliseconds}))

    if action == "logs":
        return require_success(send_cmd({"cmd": "logs"}))

    if action == "clear_logs":
        return require_success(send_cmd({"cmd": "clear_logs"}))

    if action == "screenshot":
        response, _ = _capture_screenshot(step.get("target"))
        return _safe_response(response)

    if action == "assert_no_popup_errors":
        state = _observe_state()
        if state["popup_errors"]:
            raise AssertionError(f"Unexpected popup errors: {state['popup_errors']}")
        return {"popup_errors": []}

    if action in {"assert_visible", "assert_enabled", "assert_text"}:
        state = _observe_state()
        element = _find_state_element(state, step["target"])
        if element is None:
            raise AssertionError(f"Qt UI element not found: {step['target']}")

        if action == "assert_visible":
            expected = bool(step.get("value", True))
            actual = bool(element.get("visible", False))
            if actual != expected:
                raise AssertionError(
                    f"UI element {step['target']} visible={actual}, expected {expected}"
                )
            return {"objectName": step["target"], "visible": actual}

        if action == "assert_enabled":
            expected = bool(step.get("value", True))
            if "enabled" not in element:
                raise AssertionError(
                    f"UI element {step['target']} is a QWindow; QWindow has no enabled state"
                )
            actual = bool(element["enabled"])
            if actual != expected:
                raise AssertionError(
                    f"UI element {step['target']} enabled={actual}, expected {expected}"
                )
            return {"objectName": step["target"], "enabled": actual}

        expected_text = str(step.get("text", ""))
        actual_text = str(element.get("text", ""))
        if step.get("match", "equals") == "contains":
            matched = expected_text in actual_text
        else:
            matched = actual_text == expected_text
        if not matched:
            raise AssertionError(
                f"Widget {step['target']} text={actual_text!r}, expected {expected_text!r}"
            )
        return {"objectName": step["target"], "text": actual_text}

    if action == "assert_popup":
        state = _observe_state()
        for popup in state["popup_errors"]:
            if step.get("target") and popup.get("objectName") != step["target"]:
                continue
            if step.get("severity") and popup.get("severity") != step["severity"]:
                continue
            if step.get("text_contains") and step["text_contains"] not in popup.get("text", ""):
                continue
            return {"popup": popup}
        raise AssertionError(f"Expected popup was not found: {step}")

    if action == "wait_for_text":
        deadline = time.monotonic() + min(int(step.get("timeout_ms", 5000)), 30000) / 1000
        interval = max(10, min(int(step.get("poll_ms", 100)), 1000)) / 1000
        while time.monotonic() < deadline:
            state = _observe_state()
            element = _find_state_element(state, step["target"])
            if element is not None:
                actual_text = str(element.get("text", ""))
                expected_text = str(step.get("text", ""))
                if (expected_text in actual_text if step.get("match") == "contains"
                        else actual_text == expected_text):
                    return {"objectName": step["target"], "text": actual_text}
            time.sleep(interval)
        raise AssertionError(f"Timed out waiting for text on {step['target']}")

    raise ValueError(f"Unknown script action: {action!r}")


def _execute_script(document: dict[str, Any] | list[dict[str, Any]]) -> dict[str, Any]:
    """Execute a JSON script document and retain a result for every step."""
    if isinstance(document, list):
        name = "anonymous"
        steps = document
        stop_on_error = True
    elif isinstance(document, dict):
        name = str(document.get("name", "anonymous"))
        steps = document.get("steps", [])
        stop_on_error = bool(document.get("stop_on_error", True))
    else:
        raise ValueError("A script must be a JSON object or an array of steps")

    if not isinstance(steps, list) or not steps:
        raise ValueError("A script must contain a non-empty 'steps' array")
    if len(steps) > 100:
        raise ValueError("A script may contain at most 100 steps")

    started = time.monotonic()
    results = []
    last_screenshot: bytes | None = None
    for index, step in enumerate(steps):
        if not isinstance(step, dict):
            raise ValueError(f"Step {index} must be a JSON object")

        result: dict[str, Any] = {
            "index": index,
            "action": step.get("action"),
        }
        failed = False
        try:
            if step.get("action") == "save_screenshot":
                # The automatic capture from the preceding command is the
                # current buffer. On the first command, capture one now so the
                # save operation still has deterministic behavior.
                if last_screenshot is None:
                    _, last_screenshot = _capture_screenshot()
                output_path = Path(step["path"]).expanduser()
                output_path.write_bytes(last_screenshot)
                result["status"] = "passed"
                result["result"] = {
                    "saved_to": str(output_path.resolve()),
                    "bytes": len(last_screenshot),
                }
            else:
                result["status"] = "passed"
                result["result"] = _run_script_step(step)
        except Exception as error:  # Report the failed step instead of hiding it.
            failed = True
            result["status"] = "failed"
            result["error"] = str(error)

        # Every command leaves a fresh screenshot in the buffer and reports
        # capture metadata. The image bytes stay local unless the script asks
        # to save them, avoiding huge base64 payloads in the test report.
        try:
            screenshot_response, last_screenshot = _capture_screenshot()
            result["screenshot"] = _safe_response(screenshot_response)
        except Exception as error:
            result["screenshot"] = {"captured": False, "error": str(error)}

        results.append(result)
        if failed and stop_on_error:
            break

    try:
        final_state = _observe_state()
    except Exception as error:
        final_state = {"error": str(error)}

    passed = all(result["status"] == "passed" for result in results)
    return {
        "name": name,
        "status": "passed" if passed else "failed",
        "duration_ms": round((time.monotonic() - started) * 1000),
        "steps": results,
        "final_state": final_state,
    }


def _parse_line_script(script: str) -> dict[str, Any]:
    """Translate the human-friendly line syntax into internal script steps.

    Text arguments intentionally consume the remainder of their line. This
    means an agent can write ``type nameEdit Ada Lovelace`` without quoting the
    value or constructing a nested JSON object.
    """
    steps: list[dict[str, Any]] = []

    for line_number, raw_line in enumerate(script.splitlines(), start=1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue

        # These phrases make scripts read like a test procedure. Both markers
        # collect the current widget/popup state; the runner also collects the
        # final state automatically, so ``end ui test`` is optional.
        normalized_line = " ".join(line.lower().split())

        def fail(message: str) -> None:
            raise ValueError(f"Line {line_number}: {message}")

        if normalized_line in {"start ui test", "begin ui test", "end ui test", "finish ui test"}:
            steps.append({"action": "observe"})
            continue

        if normalized_line in {"show ui test commands", "ui test help", "help ui test"}:
            steps.append({"action": "help"})
            continue

        if normalized_line == "status":
            steps.append({"action": "status"})
            continue

        if normalized_line in {"logs", "show logs", "show recent logs"}:
            steps.append({"action": "logs"})
            continue

        if normalized_line in {"clear logs", "clear recent logs"}:
            steps.append({"action": "clear_logs"})
            continue

        if normalized_line in {"show ui test commands", "ui test help", "help ui test"}:
            steps.append({"action": "help"})
            continue

        if normalized_line.startswith("save screenshot to "):
            path = line[len("save screenshot to "):].strip()
            if not path:
                fail("save screenshot to requires a filename")
            steps.append({"action": "save_screenshot", "path": path})
            continue

        command, separator, remainder = line.partition(" ")
        if not separator:
            remainder = ""
        command = command.lower()
        remainder = remainder.strip()

        if command == "click":
            if not remainder or " " in remainder:
                fail("click requires exactly one object name")
            steps.append({"action": "click", "target": remainder})
            continue

        if command in {"activate", "action", "activate-action"}:
            if not remainder or " " in remainder:
                fail("activate requires exactly one QAction object name")
            steps.append({"action": "activate_action", "target": remainder})
            continue

        if command == "type":
            target, separator, text = remainder.partition(" ")
            if not separator or not text:
                fail("type requires an object name followed by text")
            steps.append({"action": "type", "target": target, "text": text})
            continue

        if command in {"key", "press-key", "press_key"}:
            parts = remainder.split()
            if len(parts) != 2:
                fail("key requires an object name and key, for example: key editor Ctrl+S")
            steps.append({"action": "key", "target": parts[0], "key": parts[1]})
            continue

        if command == "mouse":
            parts = remainder.split()
            if len(parts) != 2:
                fail("mouse requires an action and object name, for example: mouse click submitButton")
            action, target = parts
            if action not in {"click", "double-click", "doubleclick", "move"}:
                fail("mouse action must be click, double-click, or move")
            steps.append({"action": "mouse", "mouse_action": action, "target": target})
            continue

        if command == "wait":
            if remainder.lower().startswith("idle"):
                idle_duration = remainder[4:].strip() or "250"
                try:
                    milliseconds = int(idle_duration)
                except ValueError:
                    fail("wait idle accepts optional milliseconds, for example: wait idle 250")
                steps.append({"action": "wait_idle", "milliseconds": milliseconds})
                continue
            try:
                milliseconds = int(remainder)
            except ValueError:
                fail("wait requires milliseconds, for example: wait 250")
            steps.append({"action": "wait", "milliseconds": milliseconds})
            continue

        if command in {"tree", "observe"}:
            if remainder:
                fail(f"{command} does not accept arguments")
            steps.append({"action": "observe"})
            continue

        if command in {"popup", "popups", "errors", "popup_errors"}:
            if remainder:
                fail(f"{command} does not accept arguments")
            steps.append({"action": "observe"})
            continue

        if command == "screenshot":
            if remainder and " " in remainder:
                fail("screenshot accepts at most one object name")
            step = {"action": "screenshot"}
            if remainder:
                step["target"] = remainder
            steps.append(step)
            continue

        if command == "assert":
            assertion, separator, arguments = remainder.partition(" ")
            if not assertion:
                fail("assert requires a condition")
            assertion = assertion.lower()
            arguments = arguments.strip()

            if assertion in {"no-errors", "no_errors", "no-popups", "no_popups"}:
                if arguments:
                    fail(f"assert {assertion} does not accept arguments")
                steps.append({"action": "assert_no_popup_errors"})
                continue

            target, separator, expected = arguments.partition(" ")
            if assertion in {"visible", "enabled"}:
                value = expected.strip().lower() if separator else "true"
                if value not in {"true", "false"}:
                    fail(f"assert {assertion} expects true or false")
                steps.append({
                    "action": f"assert_{assertion}",
                    "target": target,
                    "value": value == "true",
                })
                continue

            if assertion in {"text", "contains"}:
                if not separator or not expected:
                    fail(f"assert {assertion} requires an object name and text")
                steps.append({
                    "action": "assert_text",
                    "target": target,
                    "text": expected,
                    "match": "contains" if assertion == "contains" else "equals",
                })
                continue

            if assertion == "popup":
                severity = target if target else None
                text_contains = expected if separator and expected else None
                step = {"action": "assert_popup"}
                if severity:
                    step["severity"] = severity
                if text_contains:
                    step["text_contains"] = text_contains
                steps.append(step)
                continue

            fail(f"unknown assertion: {assertion}")

        if command == "wait-text":
            target, separator, expected = remainder.partition(" ")
            if not separator or not expected:
                fail("wait-text requires an object name and expected text")
            steps.append({
                "action": "wait_for_text",
                "target": target,
                "text": expected,
                "match": "equals",
            })
            continue

        fail(f"unknown command: {command}")

    if not steps:
        raise ValueError("The script contains no commands")
    return {"name": "line-script", "steps": steps}


@mcp.tool()
def get_widget_tree() -> str:
    """Return visible Qt Widgets and standalone Qt GUI windows.

    Call this before interacting with the application. Use the returned
    ``objectName`` values as selectors for the interaction tools; do not guess
    names. QWidget roots and descendants are returned in ``widgets``. Standalone
    QWindow roots are returned in ``windows`` with ``uiType: "window"``.
    Menu and toolbar actions are nested under their owning widget's ``actions``
    array with ``uiType: "action"``.
    """
    return json.dumps(require_success(send_cmd({"cmd": "tree"})), indent=2)


@mcp.tool()
def click_widget(object_name: str) -> str:
    """Activate a QAbstractButton by its exact object name.

    Use an object name returned by ``get_widget_tree``. This performs semantic
    Qt button activation and emits the normal button signals; it does not
    simulate mouse movement, hover, or platform-specific pointer events.
    """
    return json.dumps(
        require_success(send_cmd({"cmd": "click", "target": object_name}))
    )


@mcp.tool()
def activate_action(action_name: str) -> str:
    """Trigger a QAction from a QMenu, QMenuBar, or QToolBar.

    Use an action ``objectName`` returned in the ``actions`` collection of
    ``get_widget_tree``. This invokes QAction's semantic trigger path and works
    whether the action is presented in a menu, toolbar, or both.
    """
    return json.dumps(
        require_success(send_cmd({"cmd": "activate_action", "target": action_name}))
    )


@mcp.tool()
def type_text(object_name: str, text: str) -> str:
    """Set text on a QLineEdit or QTextEdit by its exact object name.

    Use an object name returned by ``get_widget_tree``. This deterministically
    assigns widget state with ``setText`` or ``setPlainText``; it does not
    simulate individual keyboard events.
    """
    return json.dumps(
        require_success(send_cmd({"cmd": "type", "target": object_name, "text": text}))
    )


@mcp.tool()
def get_popup_errors() -> str:
    """Return visible QMessageBox error, warning, and information popups.

    Call this after an interaction that may show an error dialog. The result
    includes each popup's object name, title, severity, main text, informative
    text, and detailed text. An empty ``popups`` list means no QMessageBox is
    currently visible.
    """
    return json.dumps(require_success(send_cmd({"cmd": "popup_errors"})), indent=2)


@mcp.tool()
def run_test_script(script: str) -> str:
    """Run a simple line-oriented UI-test script and return its state report.

    The normal syntax uses one command per line, without quotes or JSON:

    ``start ui test``
    ``type nameEdit Ada Lovelace``
    ``click helloButton``
    ``activate openAction``
    ``assert text messageEdit Hello, Ada Lovelace!``
    ``assert no-errors``
    ``screenshot mainWindow``
    ``save screenshot to artifacts\\after-login.png``

    ``start ui test`` and ``end ui test`` are readable aliases for capturing
    the current UI state. A PNG screenshot is captured automatically after
    every command. ``save screenshot to PATH`` writes the current screenshot
    buffer to the requested path.

    Supported commands also include ``status``, ``wait idle``, ``logs``,
    ``clear logs``, and ``show ui test commands``. Lines beginning with ``#``
    are comments. Text consumes the rest of its line, so spaces do not require
    quoting.

    JSON scripts from the previous interface remain accepted for programmatic
    callers. No form evaluates Python, shell commands, or arbitrary code. The
    runner is bounded to 100 steps, stops at the first failure by default, and
    always reports final QWidget, QWindow, and popup state.
    """
    stripped = script.lstrip()
    if stripped.startswith(("{", "[")):
        try:
            document = json.loads(script)
        except json.JSONDecodeError as error:
            raise ValueError(f"Script is not valid JSON: {error.msg}") from error
    else:
        document = _parse_line_script(script)
    return json.dumps(_execute_script(document), indent=2)


@mcp.prompt()
def create_ui_test_script(request: str) -> str:
    """Guide an agent in translating a natural-language request into a script."""
    return f"""Create a Qt GUI UI test script for this request:

{request}

Use only the line-oriented commands below. First call get_widget_tree when
object names are unknown. Prefer assertions after state-changing actions,
check for popup errors, and save screenshots when visual evidence is requested.
Return the proposed script before running it.

{UI_TEST_COMMANDS}
"""


@mcp.tool()
def take_screenshot(window_name: str | None = None) -> Image:
    """Capture a visible Qt window or popup as a PNG image.

    Pass a window or popup ``objectName`` from ``get_widget_tree`` or
    ``get_popup_errors`` to target it exactly. When omitted, a visible
    QMessageBox is preferred; otherwise the first visible top-level QWidget or
    standalone QWindow is captured.
    """
    _, image_bytes = _capture_screenshot(window_name)
    return Image(data=image_bytes, format="png")


if __name__ == "__main__":
    mcp.run()
