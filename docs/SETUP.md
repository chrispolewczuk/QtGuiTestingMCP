# MCP client setup

`scripts/setup_mcp.py` registers the QtGuiTestingMCP stdio server in the local
configuration format used by common MCP clients. It is intentionally a
dependency-free Python script, so it can be used before installing the MCP
SDK itself.

## Quick start

From the QtGuiTestingMCP checkout, preview the configuration first:

```powershell
python scripts/setup_mcp.py --client claude
```

Apply it after reviewing the preview:

```powershell
python scripts/setup_mcp.py --client claude --write
```

The client-specific aliases make the common cases shorter:

```powershell
python scripts/setup_mcp.py --claude --write
python scripts/setup_mcp.py --codex --write
python scripts/setup_mcp.py --opencode --write
python scripts/setup_mcp.py --cursor --write
```

The command uses the Python interpreter that launched the setup script and an
absolute path to `src/qtgui_mcp_server.py`. This is important when the MCP client
starts the server with a different working directory.

## Configure several clients

To prepare Claude Desktop, Codex, OpenCode, and Cursor in one operation:

```powershell
python scripts/setup_mcp.py --all --write
```

The `all` form uses project configuration for Codex, OpenCode, and Cursor;
Claude Desktop is always configured at its user-level configuration path.
Use `--scope global` when configuring one of the clients with a user-level
configuration:

```powershell
python scripts/setup_mcp.py --client codex --scope global --write
python scripts/setup_mcp.py --client opencode --scope global --write
python scripts/setup_mcp.py --client cursor --scope global --write
```

## Options

| Option | Meaning |
|---|---|
| `--client` | `claude`, `codex`, `opencode`, `cursor`, `generic`, or `all` |
| `--claude`, `--codex`, `--opencode`, `--cursor` | Short form for selecting one client |
| `--all` | Configure Claude, Codex, OpenCode, and Cursor |
| `--scope` | `project` (default) or `global` where supported |
| `--name` | Server key, default `qt-gui-testing` |
| `--path` | Explicit config path; useful for non-standard installations |
| `--python` | Executable used to launch the Python MCP server |
| `--server` | Explicit path to `qtgui_mcp_server.py` |
| `--write` | Apply the preview; without it no files are changed |
| `--force` | Replace a different existing entry with the same server name |
| `--no-backup` | Do not save a timestamped backup before replacement |

For example, a virtual-environment interpreter and a custom server name can
be selected explicitly:

```powershell
python scripts/setup_mcp.py `
  --client cursor `
  --name qt-gui-testing `
  --python .venv/Scripts/python.exe `
  --write
```

## Client formats and locations

The utility merges one server entry and preserves unrelated configuration.
Existing files are backed up as `filename.bak.YYYYMMDD-HHMMSS` before they are
changed. A file that contains comments or trailing commas (JSONC) is not
rewritten automatically; pass `--path` to a strict JSON file or edit that
file using the client’s own CLI.

- Claude Desktop: `claude_desktop_config.json` with a top-level `mcpServers`
  object.
- Codex: `.codex/config.toml` or `~/.codex/config.toml` with an
  `[mcp_servers.<name>]` table.
- OpenCode: `opencode.json` with `mcp.servers.<name>` and a local command
  array. If an existing `opencode.jsonc` is found, it is selected but must be
  strict JSON for this utility to edit it.
- Cursor: `.cursor/mcp.json` or `~/.cursor/mcp.json` with a top-level
  `mcpServers` object.
- Generic: `mcp-config.json` in the repository, using the conventional
  `mcpServers` JSON shape.

Client configuration is separate from the Qt application. Start the Debug
Qt application with the offscreen platform before asking the MCP client to
use the tools:

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
./build/qtgui-testing-mcp.exe
```

Restart or reload the MCP client after changing its configuration. The setup
utility does not install `mcp`, launch the Qt process, or change the bridge's
port.
