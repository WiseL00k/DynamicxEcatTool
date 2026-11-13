# Apply the reviewed dependency patch to a build-tree copy. The checked-out
# submodule stays pristine, and fresh checkouts/CI compile the same patched code.
if(WIN32)
    set(_port_platform win32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_port_platform linux)
else()
    message(FATAL_ERROR "The concurrent application runtime supports Windows and Linux ports only")
endif()

find_package(Git REQUIRED)
set(_port_relative "oshw/${_port_platform}/nicdrv.c")
set(_port_overlay "${CMAKE_CURRENT_BINARY_DIR}/soem-port-overlay")
set(_port_patch "${CMAKE_CURRENT_LIST_DIR}/patches/soem-${_port_platform}-port-lock.patch")
file(MAKE_DIRECTORY "${_port_overlay}/oshw/${_port_platform}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/SOEM/${_port_relative}"
               "${_port_overlay}/${_port_relative}" COPYONLY)
# Run at the source root: git apply silently skips paths outside its working
# subdirectory when invoked inside an overlay nested in a Git checkout.
file(RELATIVE_PATH _port_overlay_relative "${CMAKE_CURRENT_SOURCE_DIR}" "${_port_overlay}")
execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --unsafe-paths "--directory=${_port_overlay_relative}" "${_port_patch}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE _port_patch_result ERROR_VARIABLE _port_patch_error
)
if(NOT _port_patch_result EQUAL 0)
    message(FATAL_ERROR "SOEM port synchronization patch no longer applies: ${_port_patch_error}")
endif()
file(READ "${_port_overlay}/${_port_relative}" _port_patched_source)
if(NOT _port_patched_source MATCHES "Check and publish RX state under the same lock")
    message(FATAL_ERROR "SOEM synchronization patch was skipped; refusing an unsafe concurrent port")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_port_patch}")
get_target_property(_soem_sources soem SOURCES)
list(REMOVE_ITEM _soem_sources "${_port_relative}" "${CMAKE_CURRENT_SOURCE_DIR}/SOEM/${_port_relative}")
set_property(TARGET soem PROPERTY SOURCES "${_soem_sources}")
target_sources(soem PRIVATE "${_port_overlay}/${_port_relative}")
