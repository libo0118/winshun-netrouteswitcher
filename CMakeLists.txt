cmake_minimum_required(VERSION 3.24)
project(NetRouteSwitcher VERSION 0.2.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Qml Quick)
if(NOT Qt6_VERSION VERSION_LESS "6.13.0")
    message(FATAL_ERROR "The host Qt contract is pinned to Qt 6.12.x")
endif()

qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
if(MSVC)
    add_compile_options(/utf-8 /EHsc /W3)
endif()

add_library(netrouteswitcher_core STATIC
    src/core/RouteModels.h
    src/core/RouteLogic.h
    src/core/RouteLogic.cpp
    src/core/ThemeData.h
    src/ui/ThemeObject.h
    src/ui/ThemeObject.cpp
)
target_include_directories(netrouteswitcher_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(netrouteswitcher_core PUBLIC
    Qt6::Core
    Qt6::Gui
)
if(MSVC)
    set_target_properties(netrouteswitcher_core PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
endif()

qt_add_executable(NetRouteSwitcher WIN32
    src/main.cpp
    src/protocol/Protocol.h
    src/protocol/Protocol.cpp
    src/platform/WinNetworkService.h
    src/platform/WinNetworkService.cpp
    src/ui/AdapterListModel.h
    src/ui/AdapterListModel.cpp
    src/ui/WifiRuleModel.h
    src/ui/WifiRuleModel.cpp
    src/ui/AppController.h
    src/ui/AppController.cpp
)
target_include_directories(NetRouteSwitcher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(NetRouteSwitcher PRIVATE
    netrouteswitcher_core
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    iphlpapi
    ws2_32
    advapi32
    shell32
    user32
)
if(MSVC)
    target_link_options(NetRouteSwitcher PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    set_target_properties(NetRouteSwitcher PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
endif()

qt_add_resources(NetRouteSwitcher "nrs_res"
    PREFIX "/nrs"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES
        qml/MainWindow.qml
        qml/MetricDialog.qml
        qml/WifiRulesDialog.qml
        icon.png
)

option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    qt_add_executable(netrouteswitcher_tests
        tests/CoreTests.cpp
    )
    target_include_directories(netrouteswitcher_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(netrouteswitcher_tests PRIVATE
        netrouteswitcher_core
        Qt6::Core
        Qt6::Gui
    )
    if(MSVC)
        set_target_properties(netrouteswitcher_tests PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
        )
    endif()
endif()
