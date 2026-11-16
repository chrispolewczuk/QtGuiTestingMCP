"""Unit tests for the dependency-light parts of the MCP adapter.

Copyright (c) 2026 Chris Polewczuk
SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import base64
import unittest
from unittest.mock import patch

from src import qtgui_mcp_server as server


class QtGuiMcpServerTest(unittest.TestCase):
    """Verify parsing, state lookup, and bounded script execution."""

    def test_line_script_parses_action_and_alias_commands(self) -> None:
        """Menu actions have a readable no-quote script syntax."""

        document = server._parse_line_script(
            "activate openAction\n"
            "action saveAction\n"
            "assert visible mainWindow true\n"
        )

        self.assertEqual(
            document["steps"][:2],
            [
                {"action": "activate_action", "target": "openAction"},
                {"action": "activate_action", "target": "saveAction"},
            ],
        )

    def test_line_script_rejects_action_arguments(self) -> None:
        """The simple language rejects ambiguous action selectors."""

        with self.assertRaisesRegex(ValueError, "exactly one QAction object name"):
            server._parse_line_script("activate openAction extra")

    def test_state_lookup_finds_nested_action(self) -> None:
        """Assertions can resolve actions alongside widgets and windows."""

        state = {
            "widgets": [
                {
                    "objectName": "mainWindow",
                    "children": [
                        {
                            "objectName": "fileMenu",
                            "actions": [
                                {
                                    "objectName": "openAction",
                                    "uiType": "action",
                                    "visible": True,
                                }
                            ],
                        }
                    ],
                }
            ],
            "windows": [],
        }

        action = server._find_state_element(state, "openAction")
        self.assertIsNotNone(action)
        self.assertEqual(action["uiType"], "action")

    def test_execute_script_runs_action_and_captures_each_step(self) -> None:
        """The runner executes actions and preserves automatic screenshot metadata."""

        screenshot = base64.b64encode(b"test-png").decode("ascii")

        def fake_send(command: dict) -> dict:
            if command["cmd"] == "tree":
                return {
                    "widgets": [{"objectName": "mainWindow", "visible": True}],
                    "windows": [],
                }
            if command["cmd"] == "popup_errors":
                return {"popups": []}
            if command["cmd"] == "screenshot":
                return {
                    "image_base64": screenshot,
                    "objectName": "mainWindow",
                    "className": "QMainWindow",
                }
            if command["cmd"] == "activate_action":
                return {"status": "activated", "objectName": command["target"]}
            raise AssertionError(f"Unexpected bridge command: {command}")

        document = {
            "name": "action-flow",
            "steps": [
                {"action": "activate_action", "target": "openAction"},
                {"action": "observe"},
            ],
        }
        with patch.object(server, "send_cmd", side_effect=fake_send):
            result = server._execute_script(document)

        self.assertEqual(result["status"], "passed")
        self.assertEqual(len(result["steps"]), 2)
        self.assertTrue(all("screenshot" in step for step in result["steps"]))
        self.assertTrue(result["steps"][0]["screenshot"]["image_captured"])

    def test_require_success_exposes_bridge_errors(self) -> None:
        """Bridge command failures become ordinary MCP-visible exceptions."""

        with self.assertRaisesRegex(RuntimeError, "Action is disabled"):
            server.require_success({"error": "Action is disabled"})


if __name__ == "__main__":
    unittest.main()
