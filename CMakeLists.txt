cmake_minimum_required(VERSION 3.16)

project(DynamicxEcatTool VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Quick QuickControls2)
find_package(yaml-cpp CONFIG REQUIRED)

if(TARGET yaml-cpp AND NOT TARGET yaml-cpp::yaml-cpp)
    add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
endif()

add_subdirectory(SOEM)
add_subdirectory(SOEM_interface)

set(APP_QML_FILES
    Main.qml
    TestPage.qml
    DebugPage.qml
    ParamsConfigPage.qml
    ErrorPopup.qml
    panels/MITConfigPanel.qml
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
        yaml-cpp::yaml-cpp
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
