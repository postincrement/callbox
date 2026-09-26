cmake_minimum_required(VERSION 3.20)

project(callbox
  VERSION 0.1.0
  DESCRIPTION "OPAL callbox"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Sibling source trees. Pass -DCALLBOX_PTLIB_DIR= or -DCALLBOX_OPAL_DIR=
# when the checkouts are not next to this repository.
set(CALLBOX_PTLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../opalvoip-ptlib" CACHE PATH
  "PTLib source directory, CMake build directory, or install prefix")
set(CALLBOX_OPAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../opalvoip-opal" CACHE PATH
  "OPAL source directory")

get_filename_component(CALLBOX_PTLIB_DIR "${CALLBOX_PTLIB_DIR}" ABSOLUTE)
get_filename_component(CALLBOX_OPAL_DIR "${CALLBOX_OPAL_DIR}" ABSOLUTE)

if(NOT EXISTS "${CALLBOX_OPAL_DIR}/CMakeLists.txt")
  message(FATAL_ERROR
    "OPAL source was not found at:\n  ${CALLBOX_OPAL_DIR}\n"
    "Set CALLBOX_OPAL_DIR to the OPAL source directory.")
endif()

# PTLib publishes a CMake package from its build directory or install prefix.
# OPAL's configure step reads that package, so it has to exist before OPAL is added.
set(_callbox_ptlib_build "")
set(_callbox_ptlib_prefix "")
if(EXISTS "${CALLBOX_PTLIB_DIR}/PTLibTargets.cmake")
  set(_callbox_ptlib_build "${CALLBOX_PTLIB_DIR}")
elseif(EXISTS "${CALLBOX_PTLIB_DIR}/build/PTLibTargets.cmake")
  set(_callbox_ptlib_build "${CALLBOX_PTLIB_DIR}/build")
elseif(EXISTS "${CALLBOX_PTLIB_DIR}/lib/cmake/PTLib/PTLibConfig.cmake")
  set(_callbox_ptlib_prefix "${CALLBOX_PTLIB_DIR}/lib/cmake/PTLib")
elseif(EXISTS "${CALLBOX_PTLIB_DIR}/CMakeLists.txt")
  set(_callbox_ptlib_build "${CALLBOX_PTLIB_DIR}/build")
  message(STATUS "Configuring PTLib in ${_callbox_ptlib_build}")
  set(_callbox_ptlib_args
    -S "${CALLBOX_PTLIB_DIR}"
    -B "${_callbox_ptlib_build}"
    -DPTLIB_BUILD_SAMPLES=OFF)
  if(CMAKE_BUILD_TYPE)
    list(APPEND _callbox_ptlib_args -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_callbox_ptlib_args}
    RESULT_VARIABLE _callbox_ptlib_rc)
  if(NOT _callbox_ptlib_rc EQUAL 0 OR NOT EXISTS "${_callbox_ptlib_build}/PTLibTargets.cmake")
    message(FATAL_ERROR "Could not configure PTLib in ${_callbox_ptlib_build}")
  endif()
else()
  message(FATAL_ERROR
    "PTLib was not found at:\n  ${CALLBOX_PTLIB_DIR}\n"
    "Set CALLBOX_PTLIB_DIR to the PTLib source directory, a CMake build directory, or an install prefix.")
endif()

if(_callbox_ptlib_prefix)
  set(PTLib_DIR "${_callbox_ptlib_prefix}")
else()
  set(PTLib_DIR "${_callbox_ptlib_build}")
endif()
find_package(PTLib CONFIG REQUIRED)
find_package(SQLite3 REQUIRED)

option(CALLBOX_H323 "Include the H.323 stack" ON)
option(CALLBOX_SIP "Include the SIP stack" ON)

set(OPAL_BUILD_SAMPLES OFF CACHE BOOL "Build OPAL sample programs")
# Callbox owns these. OPAL's option() commands leave an existing cache value
# alone, and SIP is dropped unless SDP is also enabled.
if(CALLBOX_SIP)
  set(OPAL_SDP ON CACHE BOOL "SDP protocol support" FORCE)
endif()
set(OPAL_H323 ${CALLBOX_H323} CACHE BOOL "H.323 protocol support" FORCE)
set(OPAL_SIP ${CALLBOX_SIP} CACHE BOOL "SIP protocol support" FORCE)
if(_callbox_ptlib_prefix)
  set(OPAL_PTLIB_DIR "${CALLBOX_PTLIB_DIR}" CACHE PATH
    "PTLib install prefix, or a PTLib CMake build directory." FORCE)
else()
  set(OPAL_PTLIB_DIR "${_callbox_ptlib_build}" CACHE PATH
    "PTLib install prefix, or a PTLib CMake build directory." FORCE)
endif()
add_subdirectory("${CALLBOX_OPAL_DIR}" "${CMAKE_BINARY_DIR}/opal" EXCLUDE_FROM_ALL)

if(_callbox_ptlib_build AND EXISTS "${_callbox_ptlib_build}/CMakeCache.txt")
  # `cmake --build` does not pass GNU Make's jobserver fds to the nested
  # make, so `make -jN` warns and builds PTLib with -j1. A recipe that
  # runs $(MAKE) is recursive, so Make leaves the jobserver open.
  if(CMAKE_GENERATOR MATCHES "Makefiles")
    add_custom_target(callbox_ptlib
      COMMAND $(MAKE) -C "${_callbox_ptlib_build}"
      USES_TERMINAL
      COMMENT "Building PTLib")
  else()
    add_custom_target(callbox_ptlib
      COMMAND "${CMAKE_COMMAND}" --build "${_callbox_ptlib_build}"
      USES_TERMINAL
      COMMENT "Building PTLib")
  endif()
  add_dependencies(opal callbox_ptlib)
endif()

add_executable(callbox src/main.cxx)
target_link_libraries(callbox PRIVATE opal SQLite::SQLite3)

include(GNUInstallDirs)
install(TARGETS callbox RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
