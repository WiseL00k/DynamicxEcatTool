cmake_minimum_required(VERSION 3.15)

project(soem_interface LANGUAGES CXX)

add_library(soem_interface SHARED
    src/SOEM_interface/EcatSlaveBase.cpp
    src/SOEM_interface/SoemUtils.cpp
    include/SOEM_interface/EcatMasterBus.h include/SOEM_interface/EcatSlaveBase.h  include/SOEM_interface/soem_interface_export.h include/SOEM_interface/SoemUtils.h
    src/SOEM_interface/EcatMasterBus.cpp

)

target_compile_features(soem_interface PUBLIC cxx_std_17)

target_compile_options(soem_interface
    PRIVATE
        -Wall
        # -Werror
)

target_compile_definitions(soem_interface
    PRIVATE
    SOEM_INTERFACE_BUILD
)

target_include_directories(soem_interface
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/common
)

target_link_libraries(soem_interface
    PUBLIC
        soem
)

target_link_options(soem_interface
    PRIVATE
        "-Wl,--exclude-libs,ALL"
)

set_target_properties(soem_interface PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
