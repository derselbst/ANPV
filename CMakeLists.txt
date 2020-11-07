cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR) # because of CMAKE_CXX_STANDARD
project (ANPV)

### setup binary output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)


### Set a default build type if none was specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "")
  message(STATUS "Setting build type to 'RelWithDebInfo' as none was specified.")
  message(STATUS "")
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Tell CMake to run moc when necessary:
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
# As moc files are generated in the binary dir, tell CMake
# to always look for includes there:
set(CMAKE_INCLUDE_CURRENT_DIR ON)


# Global setting: build everything position independent
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# by default, force c++11
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# in case we have any c code, use the best c standard ever
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# if(${CMAKE_VERSION} VERSION_GREATER "3.6.0")
# find_program(CLANG_TIDY
#   NAMES "clang-tidy"
#   DOC "Path to clang-tidy executable")
#   
#     if(CLANG_TIDY)
#         # whenever clang-tidy is available, use it to automatically add braces after ever "make"
#         set(CMAKE_CXX_CLANG_TIDY "clang-tidy;-checks=-*,readability-braces-around-statements;-fix")
# #         set(CMAKE_CXX_CLANG_TIDY "clang-tidy;-fix")
#     endif(CLANG_TIDY)
# endif(${CMAKE_VERSION} VERSION_GREATER "3.6.0")


### set compile flags
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic -Wall -Warray-bounds -Wstack-protector")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -O0 -Wextra -fstack-check -fstack-protector-all -fno-inline-functions -fsanitize=address") #  -D_GLIBCXX_DEBUG")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    if(USE_LIBCXX)
        set(STDCXX "libc++")
    else(USE_LIBCXX)
        set(STDCXX "libstdc++")
    endif(USE_LIBCXX)
    
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic -Wall -stdlib=${STDCXX}")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -O0 -Wextra -Wdocumentation -fstack-protector-all -fno-inline-functions -fsanitize=address")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -g3 -Rpass=loop-vectorize -Rpass-analysis=loop-vectorize")
endif()

### setup project include dirs
set(PROJECT_LOCAL_INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/cereal/include
        ${CMAKE_SOURCE_DIR}/src/Common
        ${CMAKE_SOURCE_DIR}/src/PlayerLogic
        ${CMAKE_SOURCE_DIR}/src/InputLibraryWrapper
        ${CMAKE_SOURCE_DIR}/src/AudioOutput)


### Search in the `cmake' directory for additional CMake modules.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(FindPkgConfig)
include(GNUInstallDirs)

#############################
### SCAN FOR DEPENDENCIES ###
#############################
find_package (Threads REQUIRED)
set(LD_FLAGS ${LD_FLAGS} ${CMAKE_THREAD_LIBS_INIT})

find_package ( OpenMP QUIET )
if ( OpenMP_FOUND OR OpenMP_C_FOUND )
    message(STATUS "Found OpenMP ${OpenMP_C_SPEC_DATE}")
    # require at least OMP 3.0
    if ( ( NOT OpenMP_C_SPEC_DATE LESS "200805" ) OR NOT ( OpenMP_C_VERSION VERSION_LESS "3.0" ) )
        set ( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}" )
        set ( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}" )
        set ( CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}" )
        set ( LD_FLAGS "${OpenMP_C_LIBRARIES};${LD_FLAGS}" )
    endif()
endif()

find_package(Qt5 COMPONENTS Widgets REQUIRED)
#     
#     HANDLE_FOUND(Qt5Widgets)
#     
#     if(${Qt5Widgets_FOUND} AND ${Qt5DBus_FOUND})
#         set(USE_GUI TRUE)
#         add_definitions(-DUSE_GUI)
#         
#         # We need add -DQT_WIDGETS_LIB when using QtWidgets in Qt 5.
#         add_definitions(${Qt5Widgets_EXECUTABLE_COMPILE_FLAGS})
#         
#         MY_PRINT(QT5CORE)
# 
#     endif(${Qt5Widgets_FOUND} AND ${Qt5DBus_FOUND})
#     
#     summary_add("QT GUI support" USE_GUI)
#     
#     find_package(Qt5OpenGL)
#     HANDLE_FOUND(Qt5OpenGL)
# 
#     # OpenGL & GLEW library
#     find_package(OpenGL)
#     HANDLE_FOUND(OPENGL)
#     
#     if(Qt5OpenGL_FOUND AND OPENGL_FOUND)
#         set(USE_VISUALIZER TRUE)
#         add_definitions(-DUSE_VISUALIZER)
#     endif(Qt5OpenGL_FOUND AND OPENGL_FOUND)


find_package(JPEG REQUIRED)

add_subdirectory(libkexiv2)
include_directories("${CMAKE_BINARY_DIR}/libkexiv2/src")

message("")
### Show a summary of what we got
# summary_show()

set(SRC
main.cpp
DecoderFactory.hpp
DecoderFactory.cpp
DecodingState.hpp
DocumentController.cpp
DocumentController.hpp
DocumentView.cpp
DocumentView.hpp
ExifWrapper.cpp
ExifWrapper.hpp
Formatter.hpp
ImageDecodeTask.cpp
ImageDecodeTask.hpp
SmartImageDecoder.cpp
SmartImageDecoder.hpp
SmartJpegDecoder.cpp
SmartJpegDecoder.hpp
UserCancellation.hpp
)

add_executable(anpv ${SRC})
target_link_libraries(anpv ${Qt5Widgets_LIBRARIES} ${JPEG_LIBRARIES} KF5KExiv2)
target_include_directories(anpv SYSTEM PRIVATE ${Qt5Widgets_INCLUDE_DIRS} ${JPEG_INCLUDE_DIRS})
