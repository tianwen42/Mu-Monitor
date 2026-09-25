find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Sql Test)

function(mu_add_test target)
    qt_add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
    )
    mu_enable_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 60)
endfunction()

mu_add_test(ProtocolCodecTest
    ProtocolCodecTest.cpp
)
target_link_libraries(ProtocolCodecTest PRIVATE
    Qt::Core
    Qt::Test
    mu_network
)

mu_add_test(TcpDeviceDataSourceTest
    TcpDeviceDataSourceTest.cpp
)
target_link_libraries(TcpDeviceDataSourceTest PRIVATE
    Qt::Core
    Qt::Network
    Qt::Test
    mu_network
)

mu_add_test(DeviceSimulatorServerTest
    DeviceSimulatorServerTest.cpp
    ../tools/DeviceSimulator/DeviceSimulatorServer.cpp
    ../tools/DeviceSimulator/DeviceSimulatorServer.h
    ../tools/DeviceSimulator/FrameEncoder.cpp
    ../tools/DeviceSimulator/FrameEncoder.h
)
target_include_directories(DeviceSimulatorServerTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tools/DeviceSimulator
)
target_link_libraries(DeviceSimulatorServerTest PRIVATE
    Qt::Core
    Qt::Network
    Qt::Test
    mu_network
)

mu_add_test(TcpProtocolIntegrationTest
    TcpProtocolIntegrationTest.cpp
    ../tools/DeviceSimulator/DeviceSimulatorServer.cpp
    ../tools/DeviceSimulator/DeviceSimulatorServer.h
    ../tools/DeviceSimulator/FrameEncoder.cpp
    ../tools/DeviceSimulator/FrameEncoder.h
)
target_include_directories(TcpProtocolIntegrationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tools/DeviceSimulator
)
target_link_libraries(TcpProtocolIntegrationTest PRIVATE
    Qt::Core
    Qt::Network
    Qt::Test
    mu_network
)

mu_add_test(TimeUtilsTest
    TimeUtilsTest.cpp
    ../src/utils/TimeUtils.cpp
    ../src/utils/TimeUtils.h
)
target_link_libraries(TimeUtilsTest PRIVATE
    Qt::Core
    Qt::Test
)

mu_add_test(TelemetryTableModelTest
    TelemetryTableModelTest.cpp
    ../src/core/BusinessStates.cpp
    ../src/core/BusinessStates.h
    ../src/ui/TelemetryTableModel.cpp
    ../src/ui/TelemetryTableModel.h
    ../src/utils/TimeUtils.cpp
    ../src/utils/TimeUtils.h
)
target_link_libraries(TelemetryTableModelTest PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Test
)

mu_add_test(DataDirectoryTest
    DataDirectoryTest.cpp
    ../src/database/DataDirectory.cpp
    ../src/database/DataDirectory.h
)
target_link_libraries(DataDirectoryTest PRIVATE
    Qt::Core
    Qt::Test
)

mu_add_test(DatabaseManagerTest
    DatabaseManagerTest.cpp
    ../src/core/BusinessStates.cpp
    ../src/core/BusinessStates.h
    ../src/database/DataDirectory.cpp
    ../src/database/DataDirectory.h
    ../src/database/DatabaseManager.cpp
    ../src/database/DatabaseManager.h
    ../src/utils/TimeUtils.cpp
    ../src/utils/TimeUtils.h
)
target_link_libraries(DatabaseManagerTest PRIVATE
    Qt::Core
    Qt::Sql
    Qt::Test
)

mu_add_test(ExcelExporterTest
    ExcelExporterTest.cpp
    ../src/core/BusinessStates.cpp
    ../src/core/BusinessStates.h
    ../src/utils/ExcelExporter.cpp
    ../src/utils/ExcelExporter.h
    ../src/utils/TimeUtils.cpp
    ../src/utils/TimeUtils.h
)
target_include_directories(ExcelExporterTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/QXlsx/header
)
target_link_libraries(ExcelExporterTest PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Test
    QXlsx::QXlsx
)
mu_add_test(CoreModelsTest
    CoreModelsTest.cpp
    ../src/core/BusinessStates.cpp
    ../src/core/BusinessStates.h
    ../src/core/Device.cpp
    ../src/core/Device.h
    ../src/core/TelemetrySample.cpp
    ../src/core/TelemetrySample.h
)
target_link_libraries(CoreModelsTest PRIVATE
    Qt::Core
    Qt::Test
)


mu_add_test(MainWindowResponsiveTest
    MainWindowResponsiveTest.cpp
    ../src/core/BusinessStates.cpp
    ../src/core/BusinessStates.h
    ../src/core/Device.cpp
    ../src/core/Device.h
    ../src/core/TelemetryRecord.h
    ../src/core/TelemetrySample.cpp
    ../src/core/TelemetrySample.h
    ../src/core/HeartbeatRecord.h
    ../src/database/DataDirectory.cpp
    ../src/database/DataDirectory.h
    ../src/database/DatabaseManager.cpp
    ../src/database/DatabaseManager.h
    ../src/app/AppController.cpp
    ../src/app/AppController.h
    ../src/app/MonitoringService.cpp
    ../src/app/MonitoringService.h
    ../src/network/IDeviceDataSource.h
    ../src/network/SimulationDataSource.cpp
    ../src/network/SimulationDataSource.h
    ../src/ui/mainwindow.cpp
    ../src/ui/mainwindow.h
    ../src/ui/mainwindow.ui
    ../src/ui/TelemetryTableModel.cpp
    ../src/ui/TelemetryTableModel.h
    ../src/ui/TrendChartWidget.cpp
    ../src/ui/TrendChartWidget.h
    ../src/ui/SettingsDialog.cpp
    ../src/ui/SettingsDialog.h
    ../src/ui/AboutDialog.cpp
    ../src/ui/AboutDialog.h
    ../src/ui/UserManagementDialog.cpp
    ../src/ui/UserManagementDialog.h
    ../src/utils/ExcelExporter.cpp
    ../src/utils/ExcelExporter.h
    ../src/utils/TimeUtils.cpp
    ../src/utils/TimeUtils.h
    ../resources/resources.qrc
)
target_include_directories(MainWindowResponsiveTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/QXlsx/header
)
target_link_libraries(MainWindowResponsiveTest PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Widgets
    Qt::Sql
    mu_auth
    Qt::Test
    QXlsx::QXlsx
)
set_tests_properties(MainWindowResponsiveTest PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

mu_add_test(SimulationDataSourceTest
    SimulationDataSourceTest.cpp
    ../src/core/Device.cpp
    ../src/core/Device.h
    ../src/core/TelemetrySample.cpp
    ../src/core/TelemetrySample.h
    ../src/network/IDeviceDataSource.h
    ../src/network/SimulationDataSource.cpp
    ../src/network/SimulationDataSource.h
)
target_link_libraries(SimulationDataSourceTest PRIVATE
    Qt::Core
    Qt::Test
)

mu_add_test(ApplicationLayerTest
    ApplicationLayerTest.cpp
    ../src/app/AppController.cpp
    ../src/app/AppController.h
    ../src/app/MonitoringService.cpp
    ../src/app/MonitoringService.h
    ../src/core/BusinessStates.cpp
    ../src/core/BusinessStates.h
    ../src/core/Device.cpp
    ../src/core/Device.h
    ../src/core/TelemetryRecord.h
    ../src/core/TelemetrySample.cpp
    ../src/core/TelemetrySample.h
    ../src/database/DataDirectory.cpp
    ../src/database/DataDirectory.h
    ../src/database/DatabaseManager.cpp
    ../src/database/DatabaseManager.h
    ../src/network/IDeviceDataSource.h
    ../src/network/SimulationDataSource.cpp
    ../src/network/SimulationDataSource.h
    ../src/utils/TimeUtils.cpp
    ../src/utils/TimeUtils.h
)
target_link_libraries(ApplicationLayerTest PRIVATE
    Qt::Core
    Qt::Sql
    Qt::Test
)
set_tests_properties(ApplicationLayerTest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

mu_add_test(ProtocolPayloadMapperTest
    ProtocolPayloadMapperTest.cpp
    ../src/core/Device.cpp
    ../src/core/Device.h
    ../src/core/TelemetrySample.cpp
    ../src/core/TelemetrySample.h
    ../src/network/ProtocolPayloadMapper.cpp
    ../src/network/ProtocolPayloadMapper.h
)
target_link_libraries(ProtocolPayloadMapperTest PRIVATE
    Qt::Core
    Qt::Test
)

mu_add_test(UserManagementTest
    UserManagementTest.cpp
    ../src/database/DataDirectory.cpp
    ../src/database/DataDirectory.h
    ../src/core/BusinessStates.cpp
    ../src/core/BusinessStates.h
    ../src/database/DatabaseManager.cpp
    ../src/database/DatabaseManager.h
    ../src/utils/TimeUtils.cpp
    ../src/utils/TimeUtils.h
)
target_link_libraries(UserManagementTest PRIVATE
    Qt::Core
    Qt::Sql
    Qt::Test
    mu_auth
)