function(configure_dc_test target)
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR})
    if(WIN32)
        set_property(TARGET ${target} APPEND PROPERTY LINK_OPTIONS /STACK:8388608)
    endif()
    add_test(NAME ${target} COMMAND ${target})
    if(WIN32)
        set_tests_properties(${target} PROPERTIES ENVIRONMENT
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>\;$<TARGET_FILE_DIR:yaml-cpp::yaml-cpp>\;$<TARGET_FILE_DIR:soem_interface>\;$ENV{SystemRoot}/System32\;$ENV{SystemRoot}/System32/Npcap")
    endif()
    set_tests_properties(${target} PROPERTIES TIMEOUT 120)
endfunction()

add_executable(DynamicxDcRuntimeTests DcRuntimeTests.cpp)
target_include_directories(DynamicxDcRuntimeTests PRIVATE ${PROJECT_SOURCE_DIR}/SOEM_interface/src)
target_link_libraries(DynamicxDcRuntimeTests PRIVATE Qt6::Test soem_interface)
configure_dc_test(DynamicxDcRuntimeTests)

add_executable(DynamicxDcControllerTests DcControllerTests.cpp)
target_link_libraries(DynamicxDcControllerTests PRIVATE Qt6::Test dynamicx_backend)
configure_dc_test(DynamicxDcControllerTests)

add_executable(DynamicxDcQuickTests QuickTestMain.cpp)
target_compile_definitions(DynamicxDcQuickTests PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
    DC_PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
target_link_libraries(DynamicxDcQuickTests PRIVATE
    Qt6::QuickTest Qt6::Quick Qt6::QuickControls2
)
configure_dc_test(DynamicxDcQuickTests)
set_property(TEST DynamicxDcQuickTests APPEND PROPERTY ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen" "QT_QUICK_BACKEND=software" "QT_QUICK_CONTROLS_STYLE=Fusion" "QT_SCALE_FACTOR=1")

get_property(DC_QUICK_TEST_ENVIRONMENT TEST DynamicxDcQuickTests PROPERTY ENVIRONMENT)
foreach(scale IN ITEMS 1.25 1.5 2)
    string(REPLACE "." "_" scale_name "${scale}")
    set(test_name "DynamicxDcQuickTests_scale_${scale_name}")
    add_test(NAME ${test_name} COMMAND DynamicxDcQuickTests)
    set_tests_properties(${test_name} PROPERTIES
        TIMEOUT 120
        ENVIRONMENT "${DC_QUICK_TEST_ENVIRONMENT};QT_SCALE_FACTOR=${scale}")
endforeach()
