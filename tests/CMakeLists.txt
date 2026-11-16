add_executable(agent_bridge_test
    agent_bridge_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/AgentBridge.h
)

target_link_libraries(agent_bridge_test PRIVATE
    Qt6::Gui
    Qt6::Network
    Qt6::Test
    Qt6::Widgets
)

add_test(NAME agent_bridge_test COMMAND agent_bridge_test)

# The bridge is designed to work with Qt's offscreen platform plugin, so the
# test must not depend on a desktop display server or a window manager.
# Qt's CMake package knows the selected kit, but the platform plugin and Qt
# runtime DLLs are not necessarily on the caller's PATH. Derive the kit root
# from Qt6_DIR so CTest works from a plain shell as well as a Qt developer
# prompt. ENVIRONMENT_MODIFICATION is available because this project requires
# CMake 3.21 or newer.
get_filename_component(QT6_INSTALL_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
set_tests_properties(agent_bridge_test PROPERTIES
    ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${QT6_INSTALL_PREFIX}/plugins"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${QT6_INSTALL_PREFIX}/bin"
)
