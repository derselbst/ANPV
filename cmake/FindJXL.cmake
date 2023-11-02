#[=======================================================================[.rst:
FindJXL
-------

Finds the JXL library.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if found:

``JXL::libJXL``
  The JXL library

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``JXL_FOUND``
  True if the system has the JXL library.

#]=======================================================================]

# Use pkg-config if available
find_package(PkgConfig QUIET)
pkg_check_modules(PC_JXL QUIET jxl)

# Find the headers and library
find_path(
  JXL_INCLUDE_DIR
  NAMES "jxl/decode.h"
  HINTS "${PC_JXL_INCLUDEDIR}")

find_library(
  JXL_LIBRARY
  NAMES "jxl"
  HINTS "${PC_JXL_LIBDIR}")

# Handle transitive dependencies
if(PC_JXL_FOUND)
  get_target_properties_from_pkg_config("${JXL_LIBRARY}" "PC_JXL" "_jxl")
else()
  set(_jxl_link_libraries "Threads::Threads")
endif()

# Forward the result to CMake
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(JXL REQUIRED_VARS "JXL_LIBRARY"
                                                     "JXL_INCLUDE_DIR")

# Create the target
if(JXL_FOUND AND NOT TARGET JXL::JXL)
  add_library(JXL::JXL UNKNOWN IMPORTED)
  set_target_properties(
    JXL::JXL
    PROPERTIES IMPORTED_LOCATION "${JXL_LIBRARY}"
               INTERFACE_COMPILE_OPTIONS "${_jxl_compile_options}"
               INTERFACE_INCLUDE_DIRECTORIES "${JXL_INCLUDE_DIR}"
               INTERFACE_LINK_LIBRARIES "${_jxl_link_libraries}"
               INTERFACE_LINK_DIRECTORIES "${_jxl_link_directories}")
endif()

mark_as_advanced(JXL_INCLUDE_DIR JXL_LIBRARY)
