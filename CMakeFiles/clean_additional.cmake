# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "CMakeFiles\\DynamicxEcatTool_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\DynamicxEcatTool_autogen.dir\\ParseCache.txt"
  "CMakeFiles\\dynamicx_backend_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\dynamicx_backend_autogen.dir\\ParseCache.txt"
  "DynamicxEcatTool_autogen"
  "SOEM\\CMakeFiles\\soem_autogen.dir\\AutogenUsed.txt"
  "SOEM\\CMakeFiles\\soem_autogen.dir\\ParseCache.txt"
  "SOEM\\soem_autogen"
  "SOEM_interface\\CMakeFiles\\soem_interface_autogen.dir\\AutogenUsed.txt"
  "SOEM_interface\\CMakeFiles\\soem_interface_autogen.dir\\ParseCache.txt"
  "SOEM_interface\\soem_interface_autogen"
  "dynamicx_backend_autogen"
  )
endif()
