cmake_minimum_required(VERSION 3.16)

project(DynamicxEcatTool VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_TESTING OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Quick QuickControls2)
find_package(yaml-cpp CONFIG REQUIRED)

include(CTest)
if(TARGET yaml-cpp AND NOT TARGET yaml-cpp::yaml-cpp)
    add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
endif()

add_subdirectory(SOEM)
add_subdirectory(SOEM_interface)

set(APP_QML_FILES
    Main.qml
    DesignTokens.qml
    AppButton.qml
    PanelCard.qml
    StatusBadge.qml
    ConfirmDialog.qml
    SideNavButton.qml
    NicSelector.qml
    ThemeModeControl.qml
    LogDock.qml
    SessionUiAdapter.qml
    LogUiAdapter.qml
    TestPage.qml
    DebugPage.qml
    ParamsConfigPage.qml
    ErrorPopup.qml
    BusConfigurationPage.qml
    panels/MITConfigPanel.qml
)

set(APP_QML_RESOURCES
    icons/logo-mark.svg
    icons/test.svg
    icons/debug.svg
    icons/motor.svg
    icons/bus.svg
    icons/terminal.svg
    icons/chevron-left.svg
    icons/chevron-right.svg
)

set(DYNAMICX_BACKEND_SOURCES
    Backend/Commands/MitMotorCommand.cpp
    Backend/Commands/MitMotorCommand.h
    Backend/Config/ConfigurationParserBase.cpp
    Backend/Config/ConfigurationParserBase.h
    Backend/Config/DxSlaveConfigurationParser.cpp
    Backend/Config/DxSlaveConfigurationParser.h
    Backend/Config/EcatSlaveConfigurationParser.cpp
    Backend/Config/EcatSlaveConfigurationParser.h
    Backend/Ethercat/BusSessionCoordinator.cpp
    Backend/Ethercat/BusSessionCoordinator.h
    Backend/Ethercat/EthercatBackend.cpp
    Backend/Ethercat/EthercatBackend.h
    Backend/Ethercat/EthercatErrorMapper.cpp
    Backend/Ethercat/EthercatErrorMapper.h
    Backend/Ethercat/EthercatMasterController.cpp
    Backend/Ethercat/EthercatMasterController.h
    Backend/Ethercat/EthercatSlaveLoader.cpp
    Backend/Ethercat/EthercatSlaveLoader.h
    Backend/Ethercat/SlaveTypes.cpp
    Backend/Ethercat/SlaveTypes.h
    Backend/Ethercat/MitSlaveController.cpp
    Backend/Ethercat/MitSlaveController.h
    Backend/Ethercat/SdoConfigMapper.cpp
    Backend/Ethercat/SdoConfigMapper.h
    Backend/Explorer/EsiParser.cpp
    Backend/Explorer/EsiParser.h
    Backend/Explorer/EsiRepository.cpp
    Backend/Explorer/EsiRepository.h
    Backend/Explorer/EthercatExplorerController.cpp
    Backend/Explorer/EthercatExplorerController.h
    Backend/Explorer/ExplorerModels.cpp
    Backend/Explorer/ExplorerModels.h
    Backend/Explorer/ExplorerTypes.cpp
    Backend/Explorer/ExplorerTypes.h
    Backend/Explorer/PdoValueCodec.cpp
    Backend/Explorer/PdoValueCodec.h
    Backend/Ethercat/Slaves/EcatSlaveFactory.cpp
    Backend/Ethercat/Slaves/EcatSlaveFactory.h
    Backend/Ethercat/Slaves/MitEcatSlave.cpp
    Backend/Ethercat/Slaves/MitEcatSlave.h
    Backend/Ethercat/Slaves/RmEcatSlave.cpp
    Backend/Ethercat/Slaves/RmEcatSlave.h
    Backend/Flash/FlashService.cpp
    Backend/Flash/FlashService.h
    Backend/Models/DeviceStatusModel.h
    Backend/Monitor/EthercatMonitorController.cpp
    Backend/Monitor/EthercatMonitorController.h
    Backend/Monitor/EthercatMonitorWorker.cpp
    Backend/Monitor/EthercatMonitorWorker.h
    Backend/Network/NetworkAdapterService.cpp
    Backend/Network/NetworkAdapterService.h
)

add_library(dynamicx_backend STATIC
    ${DYNAMICX_BACKEND_SOURCES}
)

target_include_directories(dynamicx_backend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/SOEM_interface/include
)

target_link_libraries(dynamicx_backend
    PUBLIC
        Qt6::Core
        yaml-cpp
        soem_interface
)

qt_add_executable(DynamicxEcatTool
    main.cpp
    App/ApplicationBootstrap.cpp
    App/ApplicationBootstrap.h
    resources.qrc
    icons.rc
)

qt_policy(SET QTP0001 OLD)
qt_add_qml_module(DynamicxEcatTool
    URI DynamicxEcatToolQml
    VERSION 1.0
    QML_FILES ${APP_QML_FILES}
    RESOURCES ${APP_QML_RESOURCES}
)

set_target_properties(DynamicxEcatTool PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_include_directories(DynamicxEcatTool
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(DynamicxEcatTool
    PRIVATE
        Qt6::Core
        Qt6::Quick
        Qt6::QuickControls2
        dynamicx_backend
)

include(GNUInstallDirs)
install(TARGETS DynamicxEcatTool
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

add_custom_command(TARGET DynamicxEcatTool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:DynamicxEcatTool>/release
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:soem_interface>
        $<TARGET_FILE_DIR:DynamicxEcatTool>/release
)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(DynamicxExplorerTests
        tests/ExplorerTests.cpp
    )

    target_include_directories(DynamicxExplorerTests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_definitions(DynamicxExplorerTests
        PRIVATE
            DYNAMICX_TEST_ESI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/esi"
    )

    target_link_libraries(DynamicxExplorerTests
        PRIVATE Qt6::Test dynamicx_backend
    )
    add_test(NAME DynamicxExplorerTests COMMAND DynamicxExplorerTests)
    set_tests_properties(DynamicxExplorerTests PROPERTIES
        ENVIRONMENT
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>\;$<TARGET_FILE_DIR:yaml-cpp::yaml-cpp>\;$<TARGET_FILE_DIR:soem_interface>\;C:/Windows/System32"
    )


endif()
