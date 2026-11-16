# Contributing

## Before opening a change

Please keep changes focused and explain the user-visible or developer-facing
reason for the change. For bridge changes, include or update a test that covers
the JSON command and its resulting Qt behavior.

## Local validation

At minimum, run:

```powershell
python -m pip install -r requirements.txt
python -m py_compile src/qtgui_mcp_server.py
cmake --build build
ctest --test-dir build --output-on-failure
```

When changing the Qt header, also verify that AUTOMOC completes successfully.
The test suite runs with `QT_QPA_PLATFORM=offscreen` and must not require a
desktop display server.

## C++ conventions

- Follow Qt naming, ownership, and signal/slot conventions.
- Keep widget access in the GUI thread.
- Prefer explicit `objectName` values for agent-facing widgets.
- Document public or application-facing contracts with QDoc-style comments.
- Avoid enabling the bridge in release or production configurations.

## Python conventions

- Keep the MCP layer thin; protocol behavior belongs to the bridge contract.
- Convert bridge failures into MCP-visible exceptions.
- Preserve type annotations and actionable tool docstrings.

## Security

The bridge is a debug automation interface. Do not expose it beyond the local
machine or enable it in production without authentication, authorization, and a
review of which UI actions the agent may perform.

## License

Contributions are accepted under the project’s [MIT License](LICENSE). The
copyright holder is `Chris Polewczuk`.
