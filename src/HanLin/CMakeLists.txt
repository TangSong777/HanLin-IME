cmake_minimum_required(VERSION 3.22)
project(HanLin LANGUAGES CXX RC)

set(installer "${CMAKE_CURRENT_SOURCE_DIR}/../installer/HanLin.iss")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${installer}")
file(STRINGS "${installer}" version_line REGEX "^#define MyAppVersion ")
if(NOT version_line MATCHES "^#define MyAppVersion \"([0-9]+\\.[0-9]+\\.[0-9]+)\"$")
  message(FATAL_ERROR "Could not read MyAppVersion from HanLin.iss")
endif()
set(HANLIN_VERSION "${CMAKE_MATCH_1}")
string(REPLACE "." "," HANLIN_VERSION_NUMBERS "${HANLIN_VERSION}.0")
configure_file(HanLinVersionDefs.h.in HanLinVersionDefs.h @ONLY)
include_directories("${CMAKE_CURRENT_BINARY_DIR}")
set_property(SOURCE HanLinVersion.rc APPEND PROPERTY OBJECT_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/icons/hanlin.ico"
  "${CMAKE_CURRENT_SOURCE_DIR}/icons/mode-chinese.ico"
  "${CMAKE_CURRENT_SOURCE_DIR}/icons/mode-english.ico")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(UNICODE _UNICODE)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>)

function(hanlin_native target)
  target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/permissive->)
endfunction()

add_library(hanlin_transport STATIC Transport.cpp)
target_link_libraries(hanlin_transport PUBLIC advapi32 user32)
hanlin_native(hanlin_transport)

option(HANLIN_BUILD_HOST "Build the x64 engine host" ON)
if(HANLIN_BUILD_HOST)
  set(BOOST_ROOT "" CACHE PATH "Boost header directory used by the renderer")
  set(panel ../WeaselUI)
  add_library(hanlin_panel STATIC
    ${panel}/DirectWriteResources.cpp ${panel}/FullScreenLayout.cpp
    ${panel}/GdiplusBlur.cpp ${panel}/HorizontalLayout.cpp
    ${panel}/Layout.cpp ${panel}/StandardLayout.cpp
    ${panel}/VerticalLayout.cpp ${panel}/VHorizontalLayout.cpp
    ${panel}/WeaselPanel.cpp ${panel}/WeaselUI.cpp
    ${panel}/ssf/SsfImage.cpp ${panel}/ssf/SsfImageLoader.cpp
    ${panel}/ssf/SsfIniParser.cpp ${panel}/ssf/SsfLayout.cpp
    ${panel}/ssf/SsfLayoutAdapter.cpp ${panel}/ssf/SsfRenderer.cpp
    ${panel}/ssf/SsfSkin.cpp)
  target_include_directories(hanlin_panel PRIVATE ../include "${BOOST_ROOT}")
  target_compile_definitions(hanlin_panel PRIVATE WIN32 _LIB HANLIN_PANEL)
  target_compile_options(hanlin_panel PRIVATE /bigobj /wd4996)
  add_executable(HanLinHost WIN32 EngineHost.cpp TrayIcon.cpp HanLinVersion.rc)
  hanlin_native(HanLinHost)
  target_compile_definitions(HanLinHost PRIVATE HANLIN_HOST_RESOURCE)
  target_include_directories(HanLinHost PRIVATE
    ../include ../../reference/librime-reference/src
    ../../reference/librime-reference/include "${BOOST_ROOT}")
  target_link_libraries(HanLinHost PRIVATE hanlin_transport hanlin_panel
    shlwapi shell32 ole32 uuid gdiplus d2d1 dwrite
    dwmapi comctl32 gdi32 imm32 usp10 bcrypt)

  find_program(HANLIN_CSC csc.exe PATHS "$ENV{SystemRoot}/Microsoft.NET/Framework64/v4.0.30319"
    NO_DEFAULT_PATH REQUIRED)
  set(tools "${CMAKE_CURRENT_SOURCE_DIR}/Tools")
  configure_file("${tools}/AssemblyInfo.cs.in" HanLinToolsAssemblyInfo.cs @ONLY)
  set(tools_sources "${tools}/HostActions.cs" "${tools}/Program.cs" "${tools}/Theme.cs"
    "${tools}/TypingStats.cs" "${tools}/Windows.cs" "${tools}/WordStore.cs")
  set(tools_exe "${CMAKE_CURRENT_BINARY_DIR}/HanLinTools.exe")
  set(tools_version "${CMAKE_CURRENT_BINARY_DIR}/HanLinToolsAssemblyInfo.cs")
  set(tools_references /reference:System.dll /reference:System.Core.dll
    /reference:System.Windows.Forms.dll /reference:System.Drawing.dll)
  set(tools_icon "${CMAKE_CURRENT_SOURCE_DIR}/icons/hanlin.ico")
  set(tools_native_sources)
  foreach(file IN LISTS tools_sources tools_version)
    cmake_path(NATIVE_PATH file native)
    list(APPEND tools_native_sources "${native}")
  endforeach()
  add_custom_command(OUTPUT "${tools_exe}" "${tools_exe}.config"
    COMMAND "${HANLIN_CSC}" /nologo /codepage:65001 /optimize+ /target:winexe /platform:x64
      "${tools_references}" "/win32icon:${tools_icon}" "/win32manifest:${tools}/HanLinTools.manifest"
      "/out:${tools_exe}" ${tools_native_sources}
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${tools}/HanLinTools.exe.config" "${tools_exe}.config"
    DEPENDS ${tools_sources} "${tools_version}" "${tools_icon}"
      "${tools}/HanLinTools.manifest" "${tools}/HanLinTools.exe.config"
    COMMAND_EXPAND_LISTS VERBATIM)
  add_custom_target(hanlin_desktop ALL DEPENDS "${tools_exe}" "${tools_exe}.config")
endif()

option(HANLIN_BUILD_TIP "Build the TSF text service" ON)
if(HANLIN_BUILD_TIP)
  add_library(HanLinTip SHARED TextService.cpp TsfObjects.cpp Registration.cpp HanLinVersion.rc)
  hanlin_native(HanLinTip)
  target_include_directories(HanLinTip PRIVATE ../../reference/librime-reference/include)
  target_link_libraries(HanLinTip PRIVATE hanlin_transport ole32 oleaut32 uuid shlwapi gdi32)
  target_link_options(HanLinTip PRIVATE /DEF:${CMAKE_CURRENT_SOURCE_DIR}/HanLinTip.def)
endif()

option(HANLIN_BUILD_REGRESSION "Build the isolated TSF and transport probe" OFF)
if(HANLIN_BUILD_REGRESSION)
  add_executable(HanLinFrontendRegression ../test/TestHanLin/HanLinFrontendRegression.cpp)
  hanlin_native(HanLinFrontendRegression)
  target_include_directories(HanLinFrontendRegression PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(HanLinFrontendRegression PRIVATE hanlin_transport ole32 oleaut32 uuid)
endif()
