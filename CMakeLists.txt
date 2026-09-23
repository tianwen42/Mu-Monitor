cmake_minimum_required(VERSION 3.19)
project(Mu-Monitor VERSION 0.1.0 LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Sql Network LinguistTools)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

function(mu_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

if(WIN32)
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I${CMAKE_CURRENT_SOURCE_DIR}/resources")
endif()

add_subdirectory(third_party/QXlsx)

qt_standard_project_setup()
set(CMAKE_AUTORCC ON)

add_library(mu_protocol STATIC
    src/protocol/CRC16.cpp
    src/protocol/CRC16.h
    src/protocol/Frame.h
    src/protocol/FrameCodec.cpp
    src/protocol/FrameCodec.h
    src/protocol/Protocol.cpp
    src/protocol/Protocol.h
)
target_include_directories(mu_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(mu_protocol PUBLIC Qt::Core)
mu_enable_warnings(mu_protocol)

add_library(mu_network STATIC
    src/network/FrameDecoder.cpp
    src/network/FrameDecoder.h
    src/network/TcpConnectionWorker.cpp
    src/network/TcpConnectionWorker.h
    src/network/TcpDeviceDataSource.cpp
    src/network/TcpDeviceDataSource.h
)
target_include_directories(mu_network PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(mu_network PUBLIC
    Qt::Core
    Qt::Network
    mu_protocol
)
mu_enable_warnings(mu_network)
include(CTest)

option(BUILD_DEVICE_SIMULATOR "Build the standalone TCP device simulator" ON)
if(BUILD_DEVICE_SIMULATOR)
    add_subdirectory(tools/DeviceSimulator)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

qt_add_executable(Mu-Monitor
    WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/app/AppController.cpp
    src/app/AppController.h
    src/app/MonitoringService.cpp
    src/app/MonitoringService.h
    src/network/IDeviceDataSource.h
    src/network/SimulationDataSource.cpp
    src/network/SimulationDataSource.h
    src/network/ProtocolPayloadMapper.cpp
    src/network/ProtocolPayloadMapper.h
    src/core/BusinessStates.cpp
    src/core/BusinessStates.h
    src/core/Device.cpp
    src/core/Device.h
    src/core/TelemetryRecord.h
    src/core/TelemetrySample.cpp
    src/core/TelemetrySample.h
    src/core/DeviceInfo.h
    src/database/DataDirectory.cpp
    src/database/DataDirectory.h
    src/database/DatabaseManager.cpp
    src/database/DatabaseManager.h
    src/ui/LoginDialog.cpp
    src/ui/LoginDialog.h
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
    src/utils/ExcelExporter.cpp
    src/utils/ExcelExporter.h
    src/utils/TimeUtils.cpp
    src/utils/TimeUtils.h
    resources/resources.qrc
)

if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc")
    target_sources(Mu-Monitor PRIVATE resources/app.rc)
endif()

mu_enable_warnings(Mu-Monitor)

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
        Qt::Sql
        mu_network
        QXlsx::QXlsx
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
