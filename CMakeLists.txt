cmake_minimum_required(VERSION 3.20)
# C und C++: miniz liegt als C-Quelle unter vendor/.
project(TFU2Import LANGUAGES C CXX)

# ------------------------------------------------------------
#  Aufbau wie beim SWBF2 Import: EIN Leser, zwei Ziele.
#
#    tfudump.exe     braucht KEIN Max-SDK  -> Gegenprobe, Bestandspruefung
#    TFU2Import.dlu  braucht das Max-SDK   -> das Plugin
# ------------------------------------------------------------

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MAX_VERSION "0" CACHE STRING "3ds Max Jahrgang, z.B. 2026")
set(3DSMAX_SDK_DIR "" CACHE PATH "3ds Max SDK (maxsdk-Ordner)")
option(TFU2_BUILD_TOOL   "tfudump.exe bauen" ON)
option(TFU2_BUILD_PLUGIN "Max-Plugin bauen (braucht das Max SDK)" OFF)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "32-Bit-Bau. Es braucht x64:  cmake -S . -B build -A x64")
endif()

# /MD in jeder Konfiguration - Max und Plugin muessen denselben Heap benutzen.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

if(MSVC)
    add_compile_options(/guard:cf)
    add_link_options(/guard:cf)
    add_compile_definitions(_MSVC_STL_HARDENING=1 _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
endif()

set(TFU2_LESER
    src/tfu_pak.h src/tfu_pak.cpp
    src/tfu_gto.h src/tfu_gto.cpp
    src/tfu_model.h src/tfu_model.cpp
    src/tfu_anim.h src/tfu_anim.cpp
    src/tfu_katalog.h src/tfu_katalog.cpp
    vendor/miniz/miniz.c vendor/miniz/miniz_tinfl.c
    vendor/miniz/miniz_tdef.c vendor/miniz/miniz_zip.c)

add_library(tfuleser STATIC ${TFU2_LESER})
target_include_directories(tfuleser PUBLIC src vendor/miniz vendor/bcdec)
target_compile_definitions(tfuleser PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
if(MSVC)
    target_compile_options(tfuleser PRIVATE /EHsc /utf-8)
    set_source_files_properties(vendor/miniz/miniz.c vendor/miniz/miniz_tinfl.c
        vendor/miniz/miniz_tdef.c vendor/miniz/miniz_zip.c PROPERTIES COMPILE_FLAGS "/W0")
    set_source_files_properties(src/tfu_pak.cpp src/tfu_gto.cpp src/tfu_model.cpp src/tfu_anim.cpp
        PROPERTIES COMPILE_FLAGS "/W4")
endif()

if(TFU2_BUILD_TOOL)
    add_executable(tfudump tools/tfudump.cpp)
    target_link_libraries(tfudump PRIVATE tfuleser)
    target_compile_definitions(tfudump PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(tfudump PRIVATE /EHsc /utf-8 /W4)
    endif()
    set_target_properties(tfudump PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/bin")
endif()

if(TFU2_BUILD_PLUGIN)
    if(NOT EXISTS "${3DSMAX_SDK_DIR}/include/max.h")
        message(FATAL_ERROR "3ds Max SDK nicht gefunden unter: ${3DSMAX_SDK_DIR}")
    endif()
    add_library(TFU2Import SHARED
        src/tfu2import.h
        src/tfu2import_dll.cpp
        src/tfu2import_max.cpp
        src/tfu2import_fenster.cpp
        src/tfu2import_animfenster.cpp
        src/tfu2_ui.h
        src/tfu2import_res.h
        src/tfu2import.rc
        tfu2import.def)
    set_target_properties(TFU2Import PROPERTIES
        SUFFIX ".dlu" PREFIX "" OUTPUT_NAME "TFU2Import"
        RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/bin")
    target_include_directories(TFU2Import PRIVATE src)
    target_include_directories(TFU2Import SYSTEM PRIVATE "${3DSMAX_SDK_DIR}/include")
    target_link_directories(TFU2Import PRIVATE "${3DSMAX_SDK_DIR}/lib/x64/Release")
    target_link_libraries(TFU2Import PRIVATE tfuleser
        core.lib maxutil.lib bmm.lib Geom.lib mesh.lib
        Maxscrpt.lib MNMath.lib Paramblk2.lib comctl32.lib ole32.lib uuid.lib shell32.lib dwmapi.lib uxtheme.lib advapi32.lib)
    target_compile_definitions(TFU2Import PRIVATE
        _UNICODE UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
        MAX_JAHRGANG=${MAX_VERSION})
    if(MSVC)
        # /Zp8 ist beim Max-SDK Pflicht (Struct-Packing der SDK-Strukturen).
        target_compile_options(TFU2Import PRIVATE /EHsc /Zp8 /bigobj /utf-8 /Zc:__cplusplus /external:W0 /W4 /wd4100 /wd4458)
        target_link_options(TFU2Import PRIVATE /DEBUG /OPT:REF /OPT:ICF)
    endif()
endif()

message(STATUS "TFU2Import: Max ${MAX_VERSION}  Plugin=${TFU2_BUILD_PLUGIN}  Werkzeug=${TFU2_BUILD_TOOL}")
