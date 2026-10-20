cmake_minimum_required(VERSION 3.24)

project(EDA VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Prefer WXWIN when it is set, otherwise use the sibling wxWidgets directory:
# F:/VScode/Code/wxWidgets-3.2.2 on this machine.
if(WIN32 AND NOT wxWidgets_ROOT_DIR)
    if(DEFINED ENV{WXWIN} AND EXISTS "$ENV{WXWIN}/include/wx/wx.h")
        set(wxWidgets_ROOT_DIR "$ENV{WXWIN}"
            CACHE PATH "wxWidgets root directory")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../wxWidgets-3.2.2/include/wx/wx.h")
        get_filename_component(_eda_wx_root
            "${CMAKE_CURRENT_SOURCE_DIR}/../wxWidgets-3.2.2" ABSOLUTE)
        set(wxWidgets_ROOT_DIR "${_eda_wx_root}"
            CACHE PATH "wxWidgets root directory")
    endif()
endif()

if(WIN32 AND wxWidgets_ROOT_DIR AND NOT wxWidgets_LIB_DIR)
    if(MINGW)
        set(_eda_wx_lib_dir "${wxWidgets_ROOT_DIR}/lib/gcc_dll")
    elseif(MSVC)
        set(_eda_wx_lib_dir "${wxWidgets_ROOT_DIR}/lib/vc_x64_dll")
    endif()
    if(DEFINED _eda_wx_lib_dir AND EXISTS "${_eda_wx_lib_dir}")
        set(wxWidgets_LIB_DIR "${_eda_wx_lib_dir}"
            CACHE PATH "wxWidgets library directory")
    endif()
endif()

# The local MinGW DLL build contains both Release and Debug Unicode libraries.
if(WIN32 AND wxWidgets_LIB_DIR AND NOT wxWidgets_CONFIGURATION)
    if(EXISTS "${wxWidgets_LIB_DIR}/mswu")
        set(wxWidgets_CONFIGURATION mswu
            CACHE STRING "wxWidgets configuration")
    elseif(EXISTS "${wxWidgets_LIB_DIR}/mswud")
        set(wxWidgets_CONFIGURATION mswud
            CACHE STRING "wxWidgets configuration")
    endif()
endif()

set(wxWidgets_USE_STATIC OFF)
set(wxWidgets_USE_UNICODE ON)

find_package(wxWidgets REQUIRED COMPONENTS core base)

add_executable(EDA WIN32
    EDA.cpp
    Library.cpp
    Canvas.cpp
    MainFrame.cpp
    Main.cpp
    EDA.h
    Main.h
)

target_include_directories(EDA PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
if(TARGET wxWidgets::wxWidgets)
    target_link_libraries(EDA PRIVATE wxWidgets::wxWidgets)
else()
    include(${wxWidgets_USE_FILE})
    target_link_libraries(EDA PRIVATE ${wxWidgets_LIBRARIES})
endif()

if(WIN32 AND NOT wxWidgets_USE_STATIC AND wxWidgets_LIB_DIR)
    file(GLOB _eda_wx_dlls "${wxWidgets_LIB_DIR}/*.dll")
    if(_eda_wx_dlls)
        add_custom_command(TARGET EDA POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_eda_wx_dlls} $<TARGET_FILE_DIR:EDA>
            VERBATIM
        )
    endif()
endif()

set_target_properties(EDA PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

option(EDA_BUILD_TESTS "Build logic and canvas event regression tests" OFF)
if(EDA_BUILD_TESTS)
    enable_testing()
    add_executable(EDA_tests tests/SchematicTests.cpp EDA.cpp Library.cpp Canvas.cpp)
    target_include_directories(EDA_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    if(TARGET wxWidgets::wxWidgets)
        target_link_libraries(EDA_tests PRIVATE wxWidgets::wxWidgets)
    else()
        target_link_libraries(EDA_tests PRIVATE ${wxWidgets_LIBRARIES})
    endif()
    add_dependencies(EDA_tests EDA)
    add_test(NAME schematic_logic COMMAND EDA_tests)
    add_test(NAME canvas_clicks COMMAND EDA_tests --canvas)
endif()

