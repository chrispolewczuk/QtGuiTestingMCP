cmake_minimum_required(VERSION 3.21)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" QT_GUI_TESTING_MCP_VERSION LIMIT_COUNT 1)
string(STRIP "${QT_GUI_TESTING_MCP_VERSION}" QT_GUI_TESTING_MCP_VERSION)
if(NOT QT_GUI_TESTING_MCP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain a Semantic Version such as 0.1.0")
endif()

project(QtGuiTestingMCP VERSION ${QT_GUI_TESTING_MCP_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

option(QT_GUI_TESTING_MCP_ENABLE_BRIDGE
       "Enable the bridge in non-Debug configurations"
       OFF)

find_package(Qt6 REQUIRED COMPONENTS Gui Network Widgets)

include(CTest)

add_executable(qtgui-testing-mcp
    src/AgentBridge.h
    src/main.cpp
)

# A macOS bundle gives macdeployqt a standard application layout. The property
# is ignored by the other desktop platforms.
set_target_properties(qtgui-testing-mcp PROPERTIES
    MACOSX_BUNDLE TRUE
)

target_link_libraries(qtgui-testing-mcp PRIVATE Qt6::Gui Qt6::Network Qt6::Widgets)

# Do not define Qt's own QT_DEBUG/QT_NO_DEBUG configuration macros here. The
# bridge has an application-owned feature macro so its availability cannot be
# confused with the build type selected by Qt or by the compiler toolchain.
# The bridge is intentionally available only in Debug builds.
target_compile_definitions(qtgui-testing-mcp PRIVATE
    $<$<CONFIG:Debug>:QT_GUI_TESTING_MCP_ENABLED>
    $<$<BOOL:${QT_GUI_TESTING_MCP_ENABLE_BRIDGE}>:QT_GUI_TESTING_MCP_ENABLED>
)

# Install the demo and deploy its Qt runtime with Qt's supported CMake
# deployment API. This is used by the release workflow to create self-contained
# Windows, Linux, and macOS archives. Builds that only need the header bridge do
# not need to install the demo.
include(GNUInstallDirs)
install(TARGETS qtgui-testing-mcp
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(COMMAND qt_generate_deploy_app_script)
    qt_generate_deploy_app_script(
        TARGET qtgui-testing-mcp
        OUTPUT_SCRIPT qtgui_testing_mcp_deploy_script
        NO_TRANSLATIONS
    )
    install(SCRIPT ${qtgui_testing_mcp_deploy_script})
endif()

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
