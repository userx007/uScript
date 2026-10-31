cmake_minimum_required(VERSION 3.25)
project(scriptgui_regression CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test)
set(S ${CMAKE_CURRENT_LIST_DIR}/../../src)
add_executable(audit regression_tests.cpp
 ${S}/MainWindow.cpp ${S}/ScriptViewer.cpp ${S}/LogViewer.cpp ${S}/StatusLed.cpp ${S}/ScriptHighlighterBase.cpp
 ${S}/ScriptHighlighter.cpp ${S}/CommScriptHighlighter.cpp ${S}/IniHighlighter.cpp ${S}/ShellTerminal.cpp
 ${S}/SetupRunner.cpp ${S}/CommDumpView.cpp ${S}/CommDumpModel.cpp)
target_include_directories(audit PRIVATE ${S} ${CMAKE_CURRENT_LIST_DIR}/../../../config/inc)
target_link_libraries(audit Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
