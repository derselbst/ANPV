#[=======================================================================[.rst:
FindMango
-------

Finds the Mango library.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if found:

``Mango::libjxl``
  The Mango library

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``Mango_FOUND``
  True if the system has the Mango library.

#]=======================================================================]


# Find the headers and library
find_path(
  Mango_INCLUDE_DIR
  NAMES "mango/mango.hpp")
  
find_library(
  Mango_LIBRARY
  NAMES "mango" "mango-static")

# Forward the result to CMake
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Mango REQUIRED_VARS "Mango_LIBRARY" "Mango_INCLUDE_DIR")

# Create the target
if(Mango_FOUND AND NOT TARGET mango)
  add_library(mango UNKNOWN IMPORTED)
  set_target_properties(
    mango
    PROPERTIES IMPORTED_LOCATION "${Mango_LIBRARY}"
               INTERFACE_INCLUDE_DIRECTORIES "${Mango_INCLUDE_DIR}")
endif()


mark_as_advanced(Mango_INCLUDE_DIR Mango_LIBRARY)
