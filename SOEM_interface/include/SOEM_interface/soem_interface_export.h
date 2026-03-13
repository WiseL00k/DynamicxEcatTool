#pragma once

#if defined(_WIN32)
  #if defined(SOEM_INTERFACE_BUILD)
    #define SOEM_INTERFACE_EXPORT __declspec(dllexport)
  #else
    #define SOEM_INTERFACE_EXPORT __declspec(dllimport)
  #endif
#else
  #define SOEM_INTERFACE_EXPORT __attribute__((visibility("default")))
#endif

