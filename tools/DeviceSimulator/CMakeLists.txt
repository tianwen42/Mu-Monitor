find_package(Qt6 REQUIRED COMPONENTS Network Widgets)

qt_add_executable(DeviceSimulator
    WIN32
    main.cpp
    DeviceSimulatorServer.cpp
    DeviceSimulatorServer.h
    DeviceSimulatorWindow.cpp
    DeviceSimulatorWindow.h
)

mu_enable_warnings(DeviceSimulator)

target_link_libraries(DeviceSimulator
    PRIVATE
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::Network
)
