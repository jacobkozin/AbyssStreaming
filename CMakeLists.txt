cmake_minimum_required(VERSION 3.28)
project(AbyssStream VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ABYSS_BUILD_UI "Build the Qt desktop frontend" ON)
option(ABYSS_WITH_OBS "Build the libobs streaming/capture backend" OFF)
option(ABYSS_BUILD_TESTS "Build core tests" ON)

# One-click Windows builds use the official OBS runtime DLL plus the matching
# OBS source headers. These two cache variables let us create an imported
# libobs target without requiring a complete local OBS build tree.
set(ABYSS_OBS_INCLUDE_DIR "" CACHE PATH "Path containing obs.h (normally OBS source/libobs)")
set(ABYSS_OBS_LIBRARY "" CACHE FILEPATH "Path to the Windows obs import library")

add_library(abyss_stream_core STATIC
    src/core/MultistreamController.cpp
    src/core/MockStreamBackend.cpp
    src/core/MockSceneBackend.cpp
)
target_include_directories(abyss_stream_core PUBLIC src)

if(ABYSS_WITH_OBS)
    if(NOT TARGET OBS::libobs)
        if(ABYSS_OBS_INCLUDE_DIR AND ABYSS_OBS_LIBRARY)
            if(NOT EXISTS "${ABYSS_OBS_INCLUDE_DIR}/obs.h")
                message(FATAL_ERROR "ABYSS_OBS_INCLUDE_DIR does not contain obs.h: ${ABYSS_OBS_INCLUDE_DIR}")
            endif()
            if(NOT EXISTS "${ABYSS_OBS_LIBRARY}")
                message(FATAL_ERROR "ABYSS_OBS_LIBRARY does not exist: ${ABYSS_OBS_LIBRARY}")
            endif()

            add_library(OBS::libobs UNKNOWN IMPORTED GLOBAL)
            set_target_properties(OBS::libobs PROPERTIES
                IMPORTED_LOCATION "${ABYSS_OBS_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${ABYSS_OBS_INCLUDE_DIR}"
            )
        else()
            find_package(libobs CONFIG REQUIRED)
        endif()
    endif()

    target_sources(abyss_stream_core PRIVATE
        src/obs/ObsEngine.cpp
        src/obs/ObsStreamBackend.cpp
        src/obs/ObsSceneBackend.cpp
    )
    target_compile_definitions(abyss_stream_core PUBLIC ABYSS_WITH_OBS=1)
    target_link_libraries(abyss_stream_core PUBLIC OBS::libobs)
endif()

if(ABYSS_BUILD_UI)
    find_package(Qt6 6.5 QUIET COMPONENTS Core Gui Widgets)
    if(Qt6_FOUND)
        add_executable(AbyssStream WIN32
            src/main.cpp
            src/ui/MainWindow.cpp
        )
        if(ABYSS_WITH_OBS)
            target_sources(AbyssStream PRIVATE src/ui/ObsPreviewWidget.cpp)
        endif()
        target_include_directories(AbyssStream PRIVATE src)
        target_link_libraries(AbyssStream PRIVATE abyss_stream_core Qt6::Core Qt6::Gui Qt6::Widgets)
        set_target_properties(AbyssStream PROPERTIES AUTOMOC ON)
        target_compile_definitions(AbyssStream PRIVATE ABYSS_STREAM_VERSION="${PROJECT_VERSION}")
    else()
        message(WARNING "Qt6 was not found. The UI target is skipped; core/tests can still build.")
    endif()
endif()

if(ABYSS_BUILD_TESTS)
    enable_testing()
    add_executable(abyss_stream_core_tests
        tests/test_controller.cpp
        tests/test_scene_backend.cpp
    )
    target_link_libraries(abyss_stream_core_tests PRIVATE abyss_stream_core)
    add_test(NAME abyss_stream_core_tests COMMAND abyss_stream_core_tests)
endif()
