#!/usr/bin/env python3
"""Install QtGuiTestingMCP into common MCP client configuration files.

This is deliberately a small, dependency-free setup utility.  It does not
install Python packages, start the Qt application, or edit files until the
caller supplies ``--write``.  A preview is therefore safe to run from a
repository checkout or from an automated setup script.

Copyright (c) 2026 Chris Polewczuk
SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any, Callable


CLIENTS = ("claude", "codex", "opencode", "cursor", "generic")
SCOPES = ("project", "global")
CODEX_MARKER_START = "# >>> QtGuiTestingMCP managed server >>>"
CODEX_MARKER_END = "# <<< QtGuiTestingMCP managed server <<<"


class SetupError(RuntimeError):
    """A user-correctable setup error with a concise CLI message."""


@dataclass(frozen=True)
class ServerCommand:
    """The stdio command that an MCP client launches for QtGuiTestingMCP."""

    command: str
    server_script: str

    def as_json_config(self) -> dict[str, Any]:
        """Return the conventional MCP JSON server entry."""

        return {"command": self.command, "args": [self.server_script]}

    def as_opencode_config(self) -> dict[str, Any]:
        """Return the OpenCode v2 local-server entry."""

        return {
            "type": "local",
            "command": [self.command, self.server_script],
        }


@dataclass(frozen=True)
class PlannedFile:
    """A path and rendered configuration produced by one setup plan."""

    client: str
    path: Path
    content: str
    format_name: str


def repository_root() -> Path:
    """Return the checkout containing this script."""

    return Path(__file__).resolve().parents[1]


def project_version() -> str:
    """Return the version shared by the repository and release tags."""

    return (repository_root() / "VERSION").read_text(encoding="utf-8").strip()


def expand_path(value: str | Path) -> Path:
    """Expand environment variables and ``~`` in a user-supplied path."""

    return Path(os.path.expandvars(os.path.expanduser(str(value)))).resolve()


def executable_value(value: str) -> str:
    """Make an explicitly path-like executable absolute, keeping PATH names intact."""

    expanded = os.path.expandvars(os.path.expanduser(value))
    if os.path.isabs(expanded) or "/" in expanded or "\\" in expanded:
        return str(Path(expanded).resolve())
    return expanded


def home_path(*parts: str) -> Path:
    """Build a path below the current user's home directory."""

    return Path.home().joinpath(*parts)


def app_data_path() -> Path:
    """Return the roaming application-data directory on Windows."""

    if os.name == "nt":
        return expand_path(os.environ.get("APPDATA", home_path("AppData", "Roaming")))
    return home_path(".config")


def codex_config_path(scope: str) -> Path:
    """Return Codex's project or global TOML configuration path."""

    if scope == "project":
        return repository_root() / ".codex" / "config.toml"
    codex_home = os.environ.get("CODEX_HOME")
    return expand_path(codex_home) / "config.toml" if codex_home else home_path(".codex", "config.toml")


def opencode_config_path(scope: str) -> Path:
    """Return OpenCode's project or global JSON/JSONC configuration path."""

    if scope == "project":
        project_jsonc = repository_root() / "opencode.jsonc"
        project_json = repository_root() / "opencode.json"
        return project_jsonc if project_jsonc.exists() else project_json

    if os.name == "nt":
        config_dir = app_data_path() / "opencode"
    elif sys.platform == "darwin":
        config_dir = home_path("Library", "Application Support", "opencode")
    else:
        config_dir = home_path(".config", "opencode")
    jsonc = config_dir / "opencode.jsonc"
    return jsonc if jsonc.exists() else config_dir / "opencode.json"


def cursor_config_path(scope: str) -> Path:
    """Return Cursor's project or global MCP JSON path."""

    if scope == "project":
        return repository_root() / ".cursor" / "mcp.json"
    return home_path(".cursor", "mcp.json")


def claude_config_path() -> Path:
    """Return Claude Desktop's platform-specific JSON configuration path."""

    if os.name == "nt":
        return app_data_path() / "Claude" / "claude_desktop_config.json"
    if sys.platform == "darwin":
        return home_path("Library", "Application Support", "Claude", "claude_desktop_config.json")
    return home_path(".config", "Claude", "claude_desktop_config.json")


def default_path(client: str, scope: str) -> Path:
    """Resolve the documented default path for one client."""

    if client == "claude":
        return claude_config_path()
    if client == "codex":
        return codex_config_path(scope)
    if client == "opencode":
        return opencode_config_path(scope)
    if client == "cursor":
        return cursor_config_path(scope)
    return repository_root() / "mcp-config.json"


def read_json_object(path: Path) -> dict[str, Any]:
    """Read a strict JSON object, reporting JSONC files without rewriting them."""

    if not path.exists():
        return {}
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise SetupError(
            f"{path} is not strict JSON ({error.msg} at line {error.lineno}). "
            "It may be JSONC; use --path with a strict JSON file or update it manually."
        ) from error
    if not isinstance(value, dict):
        raise SetupError(f"{path} must contain a JSON object at the top level.")
    return value


def render_json_config(
    path: Path, client: str, name: str, command: ServerCommand, force: bool
) -> str:
    """Merge one server into a JSON MCP configuration."""

    config = read_json_object(path)
    if client == "opencode":
        mcp = config.setdefault("mcp", {})
        if not isinstance(mcp, dict):
            raise SetupError(f"{path} has a non-object 'mcp' value.")
        servers = mcp.setdefault("servers", {})
        if not isinstance(servers, dict):
            raise SetupError(f"{path} has a non-object 'mcp.servers' value.")
        existing = servers.get(name)
        desired = command.as_opencode_config()
        if existing is not None and existing != desired and not force:
            raise SetupError(
                f"{path} already defines OpenCode server '{name}'. "
                "Use --force to replace it."
            )
        servers[name] = desired
    else:
        servers = config.setdefault("mcpServers", {})
        if not isinstance(servers, dict):
            raise SetupError(f"{path} has a non-object 'mcpServers' value.")
        existing = servers.get(name)
        desired = command.as_json_config()
        if existing is not None and existing != desired and not force:
            raise SetupError(
                f"{path} already defines MCP server '{name}'. Use --force to replace it."
            )
        servers[name] = desired
    return json.dumps(config, indent=2, ensure_ascii=False) + "\n"


def toml_string(value: str) -> str:
    """Render a TOML basic string using JSON's compatible escaping."""

    return json.dumps(value, ensure_ascii=False)


def codex_block(name: str, command: ServerCommand) -> str:
    """Render the managed Codex server table."""

    return "\n".join(
        (
            CODEX_MARKER_START,
            f"[mcp_servers.{name}]",
            f"command = {toml_string(command.command)}",
            f"args = [{toml_string(command.server_script)}]",
            CODEX_MARKER_END,
            "",
        )
    )


def render_codex_config(path: Path, name: str, command: ServerCommand, force: bool) -> str:
    """Append or replace one Codex TOML table without disturbing other tables."""

    existing = path.read_text(encoding="utf-8") if path.exists() else ""
    managed_pattern = re.compile(
        rf"{re.escape(CODEX_MARKER_START)}\n.*?{re.escape(CODEX_MARKER_END)}\n?",
        flags=re.DOTALL,
    )
    block = codex_block(name, command)
    if managed_pattern.search(existing):
        return managed_pattern.sub(block, existing, count=1)

    # A parse catches malformed TOML before this utility appends to it.  The
    # stdlib parser is intentionally read-only; we preserve the user's layout
    # rather than reserializing their entire Codex configuration.
    if existing:
        try:
            import tomllib
        except ModuleNotFoundError as error:
            raise SetupError(
                "Codex setup requires Python 3.11+ (or the standard tomllib module)."
            ) from error
        try:
            parsed = tomllib.loads(existing)
        except tomllib.TOMLDecodeError as error:
            raise SetupError(
                f"{path} is not valid TOML. Fix it before running setup."
            ) from error
        if name in parsed.get("mcp_servers", {}):
            if not force:
                raise SetupError(
                    f"{path} already defines Codex server '{name}'. Use --force to replace it."
                )
            section_pattern = re.compile(
                rf"^\[mcp_servers\.{re.escape(name)}\]\n.*?(?=^\[|\Z)",
                flags=re.MULTILINE | re.DOTALL,
            )
            if not section_pattern.search(existing):
                raise SetupError(
                    f"Codex server '{name}' exists in {path}, but its table could not be located."
                )
            return section_pattern.sub(block, existing, count=1)

    separator = "" if not existing or existing.endswith("\n") else "\n"
    return existing + separator + block


def plan_file(
    client: str,
    path: Path,
    name: str,
    command: ServerCommand,
    force: bool,
) -> PlannedFile:
    """Build one client-specific file plan."""

    if client == "codex":
        if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
            raise SetupError(
                "Codex server names must contain only letters, numbers, '_' or '-'."
            )
        return PlannedFile(client, path, render_codex_config(path, name, command, force), "TOML")
    return PlannedFile(
        client, path, render_json_config(path, client, name, command, force), "JSON"
    )


def backup_path(path: Path) -> Path:
    """Return a non-colliding timestamped backup path."""

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    candidate = path.with_name(f"{path.name}.bak.{stamp}")
    suffix = 1
    while candidate.exists():
        candidate = path.with_name(f"{path.name}.bak.{stamp}.{suffix}")
        suffix += 1
    return candidate


def write_plan(plan: PlannedFile, make_backup: bool) -> str:
    """Write a plan atomically enough for a configuration-sized text file."""

    current = plan.path.read_text(encoding="utf-8") if plan.path.exists() else None
    if current == plan.content:
        return f"unchanged {plan.path}"

    plan.path.parent.mkdir(parents=True, exist_ok=True)
    backup = ""
    if current is not None and make_backup:
        destination = backup_path(plan.path)
        shutil.copy2(plan.path, destination)
        backup = f" (backup: {destination})"
    plan.path.write_text(plan.content, encoding="utf-8", newline="\n")
    return f"updated {plan.path}{backup}"


def build_parser() -> argparse.ArgumentParser:
    """Create the public command-line interface."""

    parser = argparse.ArgumentParser(
        description="Configure QtGuiTestingMCP for an MCP client without installing dependencies."
    )
    parser.add_argument(
        "--version",
        action="version",
        version=f"QtGuiTestingMCP {project_version()}",
    )
    client_group = parser.add_mutually_exclusive_group(required=True)
    client_group.add_argument(
        "--client",
        choices=(*CLIENTS, "all"),
        help="client to configure; use 'all' for Claude, Codex, OpenCode, and Cursor",
    )
    client_group.add_argument(
        "--all",
        dest="client",
        action="store_const",
        const="all",
        help="configure Claude, Codex, OpenCode, and Cursor",
    )
    for flag, client in (
        ("--claude", "claude"),
        ("--codex", "codex"),
        ("--opencode", "opencode"),
        ("--cursor", "cursor"),
    ):
        client_group.add_argument(
            flag,
            dest="client",
            action="store_const",
            const=client,
            help=f"configure {client.title()}",
        )
    parser.add_argument(
        "--scope",
        choices=SCOPES,
        default="project",
        help="project (default) or global config for clients that support both",
    )
    parser.add_argument(
        "--name",
        default="qt-gui-testing",
        help="MCP server name (default: qt-gui-testing)",
    )
    parser.add_argument(
        "--path",
        help="override the config path; valid when configuring one client, not --client all",
    )
    parser.add_argument(
        "--python",
        default=sys.executable,
        help="Python executable launched by the client (default: this interpreter)",
    )
    parser.add_argument(
        "--server",
        help="path to qtgui_mcp_server.py (default: this checkout's src/qtgui_mcp_server.py)",
    )
    parser.add_argument(
        "--write",
        action="store_true",
        help="write the configuration; without this flag only a preview is printed",
    )
    parser.add_argument(
        "--no-backup",
        action="store_true",
        help="do not create a .bak timestamped copy when replacing an existing file",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="replace an existing server entry with the same name",
    )
    return parser


def selected_clients(client: str) -> tuple[str, ...]:
    """Expand the special all-client selection."""

    return ("claude", "codex", "opencode", "cursor") if client == "all" else (client,)


def main(argv: list[str] | None = None) -> int:
    """Run the setup command and return a process exit code."""

    parser = build_parser()
    args = parser.parse_args(argv)
    clients = selected_clients(args.client)
    if args.path and args.client == "all":
        parser.error("--path can only be used when --client names one client")
    if args.client == "claude" and args.scope == "project":
        print("note: Claude Desktop uses a user-level config; --scope is ignored for Claude.")

    server = expand_path(args.server) if args.server else repository_root() / "src" / "qtgui_mcp_server.py"
    if not server.is_file():
        parser.error(f"MCP server script does not exist: {server}")
    command = ServerCommand(executable_value(args.python), str(server))

    plans: list[PlannedFile] = []
    try:
        for client in clients:
            path = expand_path(args.path) if args.path else default_path(client, args.scope)
            plans.append(plan_file(client, path, args.name, command, args.force))
    except SetupError as error:
        parser.error(str(error))

    for plan in plans:
        print(f"[{plan.client}] {plan.path} ({plan.format_name})")
        if args.write:
            print(f"  {write_plan(plan, make_backup=not args.no_backup)}")
        else:
            print("  preview only; rerun with --write to apply")
            print(plan.content)
    if not args.write:
        print("No files were changed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
