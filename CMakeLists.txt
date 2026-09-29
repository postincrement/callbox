# Build PTLib and OPAL in their own trees first. Callbox links those builds
# and does not configure or rebuild them.
#
#   cmake -S ../opalvoip-ptlib -B ../opalvoip-ptlib/build
#   cmake --build ../opalvoip-ptlib/build
#
#   cmake -S ../opalvoip-opal -B ../opalvoip-opal/build \
#     -DOPAL_PTLIB_DIR=../opalvoip-ptlib/build
#   cmake --build ../opalvoip-opal/build
#
#   cmake -S . -B build
#   cmake --build build
#
# Debug callbox links the sibling debug builds:
#   cmake --build ../opalvoip-ptlib/build --target debug
#   cmake --build ../opalvoip-opal/build --target debug
#   cmake --build build --target debug
cmake_minimum_required(VERSION 3.20)

project(callbox
  VERSION 0.1.0
  DESCRIPTION "OPAL callbox"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# A source checkout, a CMake build directory, or an install prefix.
set(CALLBOX_PTLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../opalvoip-ptlib" CACHE PATH
  "PTLib source directory, CMake build directory, or install prefix")
set(CALLBOX_OPAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../opalvoip-opal" CACHE PATH
  "OPAL source directory, CMake build directory, or install prefix")

get_filename_component(CALLBOX_PTLIB_DIR "${CALLBOX_PTLIB_DIR}" ABSOLUTE)
get_filename_component(CALLBOX_OPAL_DIR "${CALLBOX_OPAL_DIR}" ABSOLUTE)

# Resolve hint to a CMake package directory. out_kind is "build" or "prefix".
function(callbox_resolve_package package config hint build_help out_dir out_kind)
  if(EXISTS "${hint}/${config}")
    set(${out_dir} "${hint}" PARENT_SCOPE)
    set(${out_kind} "build" PARENT_SCOPE)
    return()
  endif()
  if(EXISTS "${hint}/build/${config}")
    set(${out_dir} "${hint}/build" PARENT_SCOPE)
    set(${out_kind} "build" PARENT_SCOPE)
    return()
  endif()
  if(EXISTS "${hint}/lib/cmake/${package}/${config}")
    set(${out_dir} "${hint}/lib/cmake/${package}" PARENT_SCOPE)
    set(${out_kind} "prefix" PARENT_SCOPE)
    return()
  endif()

  if(hint MATCHES "/debug$")
    get_filename_component(_parent "${hint}" DIRECTORY)
    set(_extra "\nBuild it with:\n  cmake --build \"${_parent}\" --target debug")
  elseif(EXISTS "${hint}/CMakeLists.txt")
    set(_extra "\n${build_help}")
  else()
    set(_extra "")
  endif()
  message(FATAL_ERROR
    "${package} was not found at:\n  ${hint}${_extra}")
endfunction()

callbox_resolve_package(PTLib PTLibConfig.cmake "${CALLBOX_PTLIB_DIR}"
  "Build PTLib in its own tree, without installing:\n  cmake -S \"${CALLBOX_PTLIB_DIR}\" -B \"${CALLBOX_PTLIB_DIR}/build\"\n  cmake --build \"${CALLBOX_PTLIB_DIR}/build\""
  _callbox_ptlib_dir _callbox_ptlib_kind)
set(PTLib_DIR "${_callbox_ptlib_dir}")

callbox_resolve_package(OPAL OPALConfig.cmake "${CALLBOX_OPAL_DIR}"
  "Build OPAL in its own tree against that PTLib, without installing:\n  cmake -S \"${CALLBOX_OPAL_DIR}\" -B \"${CALLBOX_OPAL_DIR}/build\" -DOPAL_PTLIB_DIR=\"${_callbox_ptlib_dir}\"\n  cmake --build \"${CALLBOX_OPAL_DIR}/build\""
  _callbox_opal_dir _callbox_opal_kind)
set(OPAL_DIR "${_callbox_opal_dir}")

find_package(PTLib CONFIG REQUIRED)
find_package(OPAL CONFIG REQUIRED)
find_package(SQLite3 REQUIRED)
if(NOT TARGET SQLite3::SQLite3)
  add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
endif()

add_executable(callbox
  src/main.cxx
  src/process.cxx
  src/calls.cxx
  src/db/database.cxx
  src/db/sqlite_database.cxx)
target_link_libraries(callbox PRIVATE OPAL::opal PTLib::ptlib SQLite3::SQLite3)

function(callbox_imported_libdir target out_var)
  string(TOUPPER "${CMAKE_BUILD_TYPE}" _cfg)
  get_target_property(_loc "${target}" "IMPORTED_LOCATION_${_cfg}")
  if(NOT _loc)
    get_target_property(_loc "${target}" IMPORTED_LOCATION_RELEASE)
  endif()
  if(NOT _loc)
    get_target_property(_loc "${target}" IMPORTED_LOCATION_DEBUG)
  endif()
  if(NOT _loc)
    get_target_property(_loc "${target}" IMPORTED_LOCATION)
  endif()
  if(NOT _loc)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  get_filename_component(_dir "${_loc}" DIRECTORY)
  set(${out_var} "${_dir}" PARENT_SCOPE)
endfunction()

callbox_imported_libdir(OPAL::opal _callbox_opal_libdir)
callbox_imported_libdir(PTLib::ptlib _callbox_ptlib_libdir)

include(GNUInstallDirs)
set_target_properties(callbox PROPERTIES
  BUILD_RPATH "${_callbox_opal_libdir};${_callbox_ptlib_libdir}"
  INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS callbox RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

message(STATUS "callbox PTLib: ${_callbox_ptlib_dir}")
message(STATUS "callbox OPAL: ${_callbox_opal_dir}")

# "debug" is a second callbox tree linked to the sibling Debug builds.
# Those builds are produced by the PTLib and OPAL debug targets.
if(CALLBOX_SECONDARY_BUILD OR CMAKE_BUILD_TYPE STREQUAL "Debug")
  return()
endif()
if(NOT _callbox_ptlib_kind STREQUAL "build" OR NOT _callbox_opal_kind STREQUAL "build")
  message(STATUS "callbox: debug target needs PTLib and OPAL CMake build trees")
  return()
endif()

set(_callbox_debug_dir "${CMAKE_BINARY_DIR}/debug")
add_custom_target(debug
  COMMAND "${CMAKE_COMMAND}"
    -G "${CMAKE_GENERATOR}"
    -S "${CMAKE_SOURCE_DIR}"
    -B "${_callbox_debug_dir}"
    -DCALLBOX_SECONDARY_BUILD=ON
    -DCMAKE_BUILD_TYPE=Debug
    "-DCALLBOX_PTLIB_DIR=${_callbox_ptlib_dir}/debug"
    "-DCALLBOX_OPAL_DIR=${_callbox_opal_dir}/debug"
  COMMAND "${CMAKE_COMMAND}" --build "${_callbox_debug_dir}"
  USES_TERMINAL
  VERBATIM
  COMMENT "Linking Debug callbox against ${_callbox_ptlib_dir}/debug and ${_callbox_opal_dir}/debug")
add_custom_target(both
  DEPENDS callbox debug
  COMMENT "Building callbox and Debug callbox")
set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${_callbox_debug_dir}")
message(STATUS "callbox: also available: debug, both")
