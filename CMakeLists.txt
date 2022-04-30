cmake_minimum_required(VERSION 3.17.0 FATAL_ERROR) # because of CMAKE_CXX_STANDARD
project (ANPV VERSION 0.2 LANGUAGES CXX)

### setup binary output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)

option ( BUILD_SHARED_LIBS "Build a shared object or DLL" off )

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

add_compile_definitions(_HAS_AUTO_PTR_ETC=1)

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
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -O0 -Wextra -fno-inline-functions -fsanitize=address -funwind-tables -fno-omit-frame-pointer -fno-common") #  -D_GLIBCXX_DEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -fstack-protector-all")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fstack-protector-all")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    if(USE_LIBCXX)
        set(STDCXX "libc++")
    else(USE_LIBCXX)
        set(STDCXX "libstdc++")
    endif(USE_LIBCXX)
    
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic -Wall -stdlib=${STDCXX}")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -O0 -Wextra -Wdocumentation -fno-inline-functions -fsanitize=address -funwind-tables -fno-omit-frame-pointer -fno-common")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -g3 -Rpass=loop-vectorize -Rpass-analysis=loop-vectorize -fstack-protector-all")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fstack-protector-all")
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

find_package(Qt6 COMPONENTS Core Widgets Gui Svg Test REQUIRED)

find_package(JPEG REQUIRED)

find_package(TIFF REQUIRED)

#find_package(KF5KIO REQUIRED)

add_subdirectory(libkexiv2)
include_directories("${CMAKE_BINARY_DIR}/libkexiv2/src")

add_subdirectory(libkdcraw)
include_directories("${CMAKE_BINARY_DIR}/libkdcraw/src")

#add_subdirectory(kitemviews)
#include_directories("${CMAKE_BINARY_DIR}/kitemviews/src")

qt_add_library(anpv-lib STATIC MANUAL_FINALIZATION
src/widgets/AfPointOverlay.cpp
src/widgets/AfPointOverlay.hpp
src/widgets/CancellableProgressDialog.hpp
src/widgets/CancellableProgressWidget.cpp
src/widgets/CancellableProgressWidget.hpp
src/widgets/CancellableProgressWidget.ui
src/widgets/DocumentView.cpp
src/widgets/DocumentView.hpp
src/widgets/ExifOverlay.cpp
src/widgets/ExifOverlay.hpp
src/widgets/FileOperationConfigDialog.cpp
src/widgets/FileOperationConfigDialog.hpp
src/widgets/FileOperationConfigDialog.ui
src/widgets/MessageWidget.cpp
src/widgets/MessageWidget.hpp
src/widgets/MultiDocumentView.cpp
src/widgets/MultiDocumentView.hpp
src/widgets/MainWindow.cpp
src/widgets/MainWindow.hpp
src/widgets/MainWindow.ui
src/widgets/PreviewAllImagesDialog.cpp
src/widgets/PreviewAllImagesDialog.hpp
src/widgets/PreviewAllImagesDialog.ui
src/widgets/ProgressIndicatorHelper.cpp
src/widgets/ProgressIndicatorHelper.hpp
src/widgets/ThumbnailListView.cpp
src/widgets/ThumbnailListView.hpp
src/decoders/DecoderFactory.hpp
src/decoders/DecoderFactory.cpp
src/decoders/DecodingState.hpp
src/decoders/SmartImageDecoder.cpp
src/decoders/SmartImageDecoder.hpp
src/decoders/SmartJpegDecoder.cpp
src/decoders/SmartJpegDecoder.hpp
src/decoders/SmartTiffDecoder.cpp
src/decoders/SmartTiffDecoder.hpp
src/logic/ExifWrapper.cpp
src/logic/ExifWrapper.hpp
src/logic/Formatter.hpp
src/logic/HardLinkFileCommand.cpp
src/logic/HardLinkFileCommand.hpp
src/logic/Image.cpp
src/logic/Image.hpp
src/logic/MoonPhase.cpp
src/logic/MoonPhase.hpp
src/logic/MoveFileCommand.cpp
src/logic/MoveFileCommand.hpp
src/logic/UserCancellation.hpp
src/logic/WaitCursor.hpp
src/logic/xThreadGuard.hpp
src/logic/TraceTimer.cpp
src/logic/TraceTimer.hpp
src/logic/ANPV.cpp
src/logic/ANPV.hpp
src/models/SortedImageModel.cpp
src/models/SortedImageModel.hpp
ANPV.qrc)

target_compile_definitions(anpv-lib PRIVATE ANPV_VERSION="${PROJECT_VERSION}")
target_link_libraries(anpv-lib PRIVATE Qt6::Core Qt6::Widgets Qt6::Svg ${JPEG_LIBRARIES} ${TIFF_LIBRARIES} KExiv2 KDcraw) #KF5::KIOCore KF5::KIOFileWidgets KF5::KIOWidgets
target_include_directories(anpv-lib SYSTEM PRIVATE ${JPEG_INCLUDE_DIRS} ${TIFF_INCLUDE_DIRS})
target_include_directories(anpv-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src/widgets ${CMAKE_CURRENT_SOURCE_DIR}/src/logic ${CMAKE_CURRENT_SOURCE_DIR}/src/decoders ${CMAKE_CURRENT_SOURCE_DIR}/src/models)

qt_add_executable(anpv MANUAL_FINALIZATION "main.cpp" "images/ANPV.rc")
target_link_libraries(anpv PRIVATE Qt6::Widgets anpv-lib)
target_include_directories(anpv PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/logic)

install ( TARGETS anpv
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

set_target_properties(anpv anpv-lib
                      PROPERTIES
                      CXX_STANDARD 20
                      CXX_EXTENSIONS OFF
                      CXX_STANDARD_REQUIRED ON
                      C_STANDARD 99
                      C_EXTENSIONS OFF
                      C_STANDARD_REQUIRED ON)

if(WIN32)
  if(NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL")
    pkg_check_modules ( CMS REQUIRED lcms2 IMPORTED_TARGET )
    target_link_libraries(KDcraw PUBLIC PkgConfig::CMS) # raw.lib(dcraw_fileio.cpp.obj) : error LNK2019: unresolved external symbol cmsOpenProfileFromFile referenced in function "protected: void __cdecl LibRaw::apply_profile(char const *,char const *)"
    
    pkg_check_modules ( JASPER REQUIRED jasper IMPORTED_TARGET )
    target_link_libraries(anpv PRIVATE PkgConfig::JASPER) # raw.lib(dcraw_common.cpp.obj) : error LNK2019: unresolved external symbol jas_init referenced in function "protected: void __cdecl LibRaw::redcine_load_raw(void)"
    
    pkg_check_modules ( HB REQUIRED harfbuzz IMPORTED_TARGET )
    target_link_libraries(anpv PRIVATE PkgConfig::HB) # Qt6Gui.lib(qtextengine.cpp.obj) : error LNK2019: unresolved external symbol hb_language_get_default referenced in function
    
    pkg_check_modules ( LZMA REQUIRED liblzma IMPORTED_TARGET )
    target_link_libraries(anpv PRIVATE PkgConfig::LZMA) # tiff.lib(tif_lzma.c.obj) : error LNK2019: unresolved external symbol lzma_code referenced in function LZMADecode
  endif()
  
  target_link_libraries(anpv-lib PRIVATE "Shlwapi") # StrCmpLogicalW()
  target_link_libraries(KExiv2 PUBLIC "Psapi") # exiv2.lib(version.cpp.obj) : error LNK2019: unresolved external symbol EnumProcessModules referenced in function
  set_target_properties(anpv PROPERTIES WIN32_EXECUTABLE TRUE)
else()
    install(
            FILES contrib/ANPV.desktop
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
        )

    install(
            FILES images/ANPV.svg
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps/
        )
endif()

qt_finalize_target(anpv-lib)
qt_finalize_target(anpv)

add_subdirectory(test)
