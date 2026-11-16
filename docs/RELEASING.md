# Releasing QtGuiTestingMCP

QtGuiTestingMCP uses Semantic Versioning and a single version source:

```text
VERSION       0.1.0
Git tag       v0.1.0
GitHub        QtGuiTestingMCP 0.1.0
```

The GitHub Actions release workflow runs the reusable CI workflow first. It
publishes a GitHub release only when the tag version exactly matches `VERSION`.

## Prepare a release

From a clean working tree:

1. Update `VERSION` to the next `MAJOR.MINOR.PATCH` value.
2. Add release notes to `CHANGELOG.md`.
3. Run the local validation commands:

   ```powershell
   python -m py_compile src/qtgui_mcp_server.py scripts/setup_mcp.py scripts/package_release.py tests/test_setup_mcp.py tests/test_qtgui_mcp_server.py
   python -m unittest tests/test_setup_mcp.py tests/test_qtgui_mcp_server.py -v
   cmake --build build
   ctest --test-dir build --output-on-failure
   ```

4. Commit the version and changelog update.
5. Create an annotated tag with the same version:

   ```powershell
   git tag -a v0.1.0 -m "QtGuiTestingMCP v0.1.0"
   git push origin main --follow-tags
   ```

The tag workflow verifies the version, runs CI on Windows, Ubuntu, and macOS,
builds self-contained platform archives with the deployed Qt runtime, and
attaches those archives to the GitHub release with generated notes. Do not
manually create the release before the workflow completes.

The package workflow uses Qt's CMake deployment support to collect the runtime
libraries and plugins for the host platform, then adds the Python adapter and
project documentation. See the [Qt deployment documentation](https://doc.qt.io/qt-6/cmake-deployment.html).

Local release validation has currently been performed on Windows. The CI
matrix provides Ubuntu and macOS validation for release candidates; inspect
all three jobs before publishing a release.

## Versioning policy

- Patch: bug fixes and documentation-only corrections.
- Minor: backward-compatible tools, commands, or capabilities.
- Major: breaking protocol, configuration, or public API changes.

Pre-release versions may use tags such as `v0.2.0-rc.1` after the workflow is
extended to accept prerelease values in `VERSION`.
