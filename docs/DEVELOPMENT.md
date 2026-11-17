# Development guide

## Repository layout

```text
src/
  AgentBridge.h       Qt-side localhost bridge
  main.cpp            Minimal demonstration application
  qtgui_mcp_server.py FastMCP stdio adapter
scripts/
  setup_mcp.py        Cross-platform MCP client configuration utility
  package_release.py   Release archive assembly utility
tests/
  CMakeLists.txt
  agent_bridge_test.cpp
  test_setup_mcp.py   Python setup utility tests
  test_qtgui_mcp_server.py  Python adapter and script-language tests
```

The Qt bridge is intentionally kept in the application process. This preserves
Qt's GUI-thread requirement: QWidget and QWindow inspection and mutation happen
on the same thread as the application's event loop.

## Build

Configure a Debug build with Qt 6 and enable the offscreen platform when
running the application:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/msvc2022_64 `
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
$env:QT_QPA_PLATFORM = "offscreen"
./build/qtgui-testing-mcp.exe
```

On Windows, the Qt kit's `bin` directory must be on `PATH` and its `plugins`
directory must be available through `QT_PLUGIN_PATH`. CTest configures these
paths automatically from the Qt package selected by CMake.

The bridge is enabled only when the `QT_GUI_TESTING_MCP_ENABLED` configuration
macro is defined, which the project sets for Debug builds. Release packaging
sets `QT_GUI_TESTING_MCP_ENABLE_BRIDGE=ON` explicitly so the optimized demo
binary remains usable as a downloadable testing fixture.

## Tests

CTest builds and runs the QtTest integration test when `BUILD_TESTING` is on:

```powershell
ctest --test-dir build --output-on-failure
```

The test uses its own TCP port and an offscreen Qt platform. It exercises the
same JSON/TCP path used by the MCP adapter rather than calling private bridge
helpers directly.

The setup utility tests run separately from CTest because they do not require
Qt:

```powershell
python -m unittest tests/test_setup_mcp.py -v
```

The adapter tests can be run together with the setup tests:

```powershell
python -m unittest tests/test_setup_mcp.py tests/test_qtgui_mcp_server.py -v
```

Local validation is currently performed on Windows. CI is configured to run the
same build and test suite on Windows, Ubuntu, and macOS, and can be started by
pushes, pull requests, or manual workflow dispatch.

For WSL, a source checkout on `/mnt/<drive>` can be used, but CMake should
write its build directory on the Linux filesystem. For example:

```bash
cd /mnt/e/Dev/QWidgetsMCP
cmake -S . -B /tmp/qtgui-testing-mcp-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build /tmp/qtgui-testing-mcp-build
ctest --test-dir /tmp/qtgui-testing-mcp-build --output-on-failure
```

This avoids DrvFS permission and metadata limitations that can affect CMake
build directories located directly under `/mnt/<drive>`.

Linux release packaging also requires `patchelf` so Qt can rewrite deployed
plugin RPATHs:

```bash
sudo apt install patchelf
```

## MCP adapter

Install the Python dependency and run the stdio server from the repository root:

```powershell
python -m pip install -r requirements.txt
python src/qtgui_mcp_server.py
```

Interactive widgets, standalone QWindow objects, and menu/toolbar QActions
should have unique, stable `objectName` values. Those names are the selectors
exposed to the agent. QAction entries are serialized under their owning
widget's `actions` array and activated through the semantic action path.

The bridge links Qt's `Gui`, `Network`, and `Widgets` modules. QWidget roots are
returned under `widgets`; standalone QWindow roots are returned under `windows`.
On the `offscreen` platform, Qt may not expose a composited native surface for
generic QWindow screenshots. In that case the bridge returns a clear capture
error while still supporting inspection and event delivery.

The setup command previews client configuration by default. Use
`python scripts/setup_mcp.py --client all --write` to configure the supported
clients, or see [SETUP.md](SETUP.md) for the supported paths and formats.

The repository version is stored in `VERSION`. Keep it in `MAJOR.MINOR.PATCH`
form and use a matching `vMAJOR.MINOR.PATCH` Git tag for releases. See
[RELEASING.md](RELEASING.md).
