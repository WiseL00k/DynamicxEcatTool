# Preserve peer FoE errors and require an ACK after BUSY before completing.
# As with the port patch, only a build-tree copy of the dependency is changed.
set(_foe_overlay "${CMAKE_CURRENT_BINARY_DIR}/soem-foe-overlay")
set(_foe_patch "${CMAKE_CURRENT_LIST_DIR}/patches/soem-foe-diagnostics.patch")
file(MAKE_DIRECTORY "${_foe_overlay}/src")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/SOEM/src/ec_foe.c"
               "${_foe_overlay}/src/ec_foe.c" COPYONLY)
file(RELATIVE_PATH _foe_overlay_relative "${CMAKE_CURRENT_SOURCE_DIR}" "${_foe_overlay}")
execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --unsafe-paths "--directory=${_foe_overlay_relative}" "${_foe_patch}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE _foe_patch_result ERROR_VARIABLE _foe_patch_error)
if(NOT _foe_patch_result EQUAL 0)
    message(FATAL_ERROR "SOEM FoE diagnostics patch no longer applies: ${_foe_patch_error}")
endif()
file(READ "${_foe_overlay}/src/ec_foe.c" _foe_patched_source)
string(FIND "${_foe_patched_source}" "ecx_FOEerror(context, slave," _foe_error_hook)
string(FIND "${_foe_patched_source}" "memcpy(MbxOut, lastRequest, sizeof(lastRequest))" _foe_busy_retry)
if(_foe_error_hook LESS 0 OR _foe_busy_retry LESS 0)
    message(FATAL_ERROR "SOEM FoE diagnostics patch was skipped")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_foe_patch}")
get_target_property(_foe_sources soem SOURCES)
list(REMOVE_ITEM _foe_sources "src/ec_foe.c" "${CMAKE_CURRENT_SOURCE_DIR}/SOEM/src/ec_foe.c")
set_property(TARGET soem PROPERTY SOURCES "${_foe_sources}")
set(SOEM_FOE_SOURCE "${_foe_overlay}/src/ec_foe.c")
target_sources(soem PRIVATE "${SOEM_FOE_SOURCE}")
