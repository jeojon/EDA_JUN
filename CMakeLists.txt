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

# Prefer WXWIN when it is set, otherwise use the wxWidgets installation on this
# machine. The directory name is intentionally the actual local spelling.
if(WIN32 AND NOT wxWidgets_ROOT_DIR)
    if(DEFINED ENV{WXWIN} AND EXISTS "$ENV{WXWIN}/include/wx/wx.h")
        set(wxWidgets_ROOT_DIR "$ENV{WXWIN}"
            CACHE PATH "wxWidgets root directory")
    elseif(EXISTS "D:/wxWeigets/wxWidgets-3.3.3/include/wx/wx.h")
        set(wxWidgets_ROOT_DIR "D:/wxWeigets/wxWidgets-3.3.3"
            CACHE PATH "wxWidgets root directory")
    endif()
endif()

if(WIN32 AND MSVC AND wxWidgets_ROOT_DIR AND NOT wxWidgets_LIB_DIR)
    set(_eda_wx_lib_dir "${wxWidgets_ROOT_DIR}/lib/vc_x64_lib")
    if(EXISTS "${_eda_wx_lib_dir}")
        set(wxWidgets_LIB_DIR "${_eda_wx_lib_dir}"
            CACHE PATH "wxWidgets library directory")
    endif()
endif()

# This wxWidgets build contains Debug Unicode static libraries only.
if(WIN32 AND wxWidgets_LIB_DIR AND NOT wxWidgets_CONFIGURATION)
    if(EXISTS "${wxWidgets_LIB_DIR}/mswud")
        set(wxWidgets_CONFIGURATION mswud
            CACHE STRING "wxWidgets configuration")
    elseif(EXISTS "${wxWidgets_LIB_DIR}/mswu")
        set(wxWidgets_CONFIGURATION mswu
            CACHE STRING "wxWidgets configuration")
    endif()
endif()

set(wxWidgets_USE_STATIC ON)
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
target_link_libraries(EDA PRIVATE wxWidgets::wxWidgets)

set_target_properties(EDA PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

