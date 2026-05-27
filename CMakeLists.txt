cmake_minimum_required(VERSION 3.16)

project(DynamicxEcatTool VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2)
find_package(Qt6 REQUIRED COMPONENTS Core Concurrent)
find_package(yaml-cpp CONFIG REQUIRED)

add_subdirectory(SOEM)
add_subdirectory(SOEM_interface)

qt_add_executable(DynamicxEcatTool
    main.cpp
    resources.qrc
    icons.rc
)

qt_policy(SET QTP0001 OLD)
qt_add_qml_module(DynamicxEcatTool
    URI DynamicxEcatToolQml
    VERSION 1.0
    QML_FILES
        Main.qml
        SOURCES Backend/EthercatBackend.h Backend/EthercatBackend.cpp
        SOURCES Backend/ConfigurationParserBase.h
        SOURCES Backend/DxSlaveConfigurationParser.h Backend/DxSlaveConfigurationParser.cpp
        SOURCES Backend/ConfigurationParserBase.cpp
        QML_FILES TestPage.qml
        QML_FILES DebugPage.qml
        SOURCES Frontend/Frontend.h Frontend/Frontend.cpp
        SOURCES
        SOURCES
        SOURCES Backend/EcatSlaveConfigurationParser.h Backend/EcatSlaveConfigurationParser.cpp
        SOURCES Backend/EcatSlave/RmEcatSlave.h
        SOURCES Backend/EcatSlave/RmEcatSlave.cpp
        QML_FILES ErrorPopup.qml
        SOURCES Backend/DeviceStatusModel.h
        RESOURCES resources.qrc
        QML_FILES
        QML_FILES
        QML_FILES ParamsConfigPage.qml
        QML_FILES panels/MITConfigPanel.qml
        QML_FILES
        SOURCES Backend/MitMotorCommand.h Backend/MitMotorCommand.cpp
    )

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(DynamicxEcatTool PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.DynamicxEcatTool
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(DynamicxEcatTool
    PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent
)
target_link_libraries(DynamicxEcatTool PRIVATE Qt6::Core)
target_link_libraries(DynamicxEcatTool PRIVATE yaml-cpp)

include(GNUInstallDirs)
install(TARGETS DynamicxEcatTool
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

target_include_directories(DynamicxEcatTool
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/SOEM_interface/include
)

target_link_libraries(DynamicxEcatTool
    PUBLIC
    soem_interface)

add_custom_command(TARGET DynamicxEcatTool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:DynamicxEcatTool>/release

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:soem_interface>
        $<TARGET_FILE_DIR:DynamicxEcatTool>/release
)
