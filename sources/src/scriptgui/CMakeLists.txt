cmake_minimum_required(VERSION 3.25)
project(ScriptFrontend VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

qt_standard_project_setup()

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/ScriptViewer.cpp
    src/LogViewer.cpp
    src/StatusLed.cpp
    src/ScriptHighlighterBase.cpp
    src/ScriptHighlighter.cpp
    src/CommScriptHighlighter.cpp
    src/IniHighlighter.cpp
    src/ShellTerminal.cpp
    src/CommDumpView.cpp
    src/CommDumpModel.cpp
)

set(HEADERS
    src/MainWindow.hpp
    src/ScriptViewer.hpp
    src/LogViewer.hpp
    src/StatusLed.hpp
    src/AppStyle.hpp
    src/ScriptHighlighterBase.hpp
    src/ScriptHighlighter.hpp
    src/CommScriptHighlighter.hpp
    src/IniHighlighter.hpp
    src/ShellTerminal.hpp
)

qt_add_executable(ScriptFrontend
    ${SOURCES}
    ${HEADERS}
)

target_link_libraries(ScriptFrontend PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# src/config/inc holds headers shared between this standalone GUI project and
# the uscript core build (uSharedConfig.hpp, uSharedScriptRegex.hpp) — it's a
# plain header directory with no Qt/build-system dependency of its own, so a
# direct include path (rather than a cross-project CMake target_link_libraries)
# is the simplest way to reach it from here.
target_include_directories(ScriptFrontend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../config/inc
)

# Ensure the binary is placed next to the build artefacts
set_target_properties(ScriptFrontend PROPERTIES
    WIN32_EXECUTABLE FALSE   # keep a console window on Windows for stderr
)
