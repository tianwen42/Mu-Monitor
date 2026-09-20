cmake_minimum_required(VERSION 3.19)
project(Mu-Monitor VERSION 0.1.0 LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets LinguistTools)

qt_standard_project_setup()

qt_add_executable(Mu-Monitor
    WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/core/TelemetryRecord.h
    src/ui/mainwindow.cpp
    src/ui/mainwindow.h
    src/ui/mainwindow.ui
    src/ui/TelemetryTableModel.cpp
    src/ui/TelemetryTableModel.h
    src/ui/TrendChartWidget.cpp
    src/ui/TrendChartWidget.h
    src/ui/SettingsDialog.cpp
    src/ui/SettingsDialog.h
    src/ui/AboutDialog.cpp
    src/ui/AboutDialog.h
    resources/resources.qrc
)

target_compile_definitions(Mu-Monitor PRIVATE
    MU_MONITOR_VERSION="${PROJECT_VERSION}"
)

target_include_directories(Mu-Monitor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

qt_add_translations(
    TARGETS Mu-Monitor
    TS_FILES resources/Mu-Monitor_zh_CN.ts
)

target_link_libraries(Mu-Monitor
    PRIVATE
        Qt::Core
        Qt::Widgets
)

install(TARGETS Mu-Monitor
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET Mu-Monitor
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
