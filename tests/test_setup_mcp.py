"""Tests for the dependency-free MCP client setup command.

Copyright (c) 2026 Chris Polewczuk
SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from scripts import setup_mcp


class SetupMcpTest(unittest.TestCase):
    """Verify previewing, merging, and rendering the supported formats."""

    def test_preview_does_not_create_a_file(self) -> None:
        """The default preview mode must not mutate the target path."""

        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "mcp.json"
            result = setup_mcp.main(["--client", "generic", "--path", str(path)])
            self.assertEqual(result, 0)
            self.assertFalse(path.exists())

    def test_json_merge_preserves_unrelated_servers(self) -> None:
        """A JSON setup keeps existing MCP entries intact."""

        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "mcp.json"
            path.write_text(
                json.dumps({"mcpServers": {"existing": {"command": "tool"}}}),
                encoding="utf-8",
            )
            result = setup_mcp.main(
                ["--client", "generic", "--path", str(path), "--write"]
            )
            self.assertEqual(result, 0)
            config = json.loads(path.read_text(encoding="utf-8"))
            self.assertIn("existing", config["mcpServers"])
            self.assertEqual(
                config["mcpServers"]["qt-gui-testing"]["args"],
                [str(setup_mcp.repository_root() / "src" / "qtgui_mcp_server.py")],
            )
            self.assertEqual(len(list(Path(temporary_directory).glob("mcp.json.bak.*"))), 1)

    def test_opencode_uses_v2_server_layout(self) -> None:
        """OpenCode receives a local server below ``mcp.servers``."""

        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "opencode.json"
            setup_mcp.main(["--client", "opencode", "--path", str(path), "--write"])
            config = json.loads(path.read_text(encoding="utf-8"))
            server = config["mcp"]["servers"]["qt-gui-testing"]
            self.assertEqual(server["type"], "local")
            self.assertEqual(server["command"][1], str(setup_mcp.repository_root() / "src" / "qtgui_mcp_server.py"))

    def test_codex_writes_a_valid_toml_table(self) -> None:
        """Codex output contains the expected ``mcp_servers`` table."""

        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "config.toml"
            path.write_text("model = \"test\"\n", encoding="utf-8")
            setup_mcp.main(["--client", "codex", "--path", str(path), "--write"])
            import tomllib

            config = tomllib.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(
                config["mcp_servers"]["qt-gui-testing"]["args"],
                [str(setup_mcp.repository_root() / "src" / "qtgui_mcp_server.py")],
            )


if __name__ == "__main__":
    unittest.main()
