cmake_minimum_required(VERSION 3.22)
project(HanLin LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>
  $<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/permissive->)

add_library(hanlin_transport STATIC Transport.cpp)
target_link_libraries(hanlin_transport PUBLIC advapi32 user32)

option(HANLIN_BUILD_HOST "Build the x64 engine host" ON)
if(HANLIN_BUILD_HOST)
  set(HANLIN_RENDERER_LIBRARY "" CACHE FILEPATH "Static WeaselUI renderer library")
  set(BOOST_ROOT "" CACHE PATH "Boost header directory used by the renderer")
  add_executable(HanLinHost WIN32 EngineHost.cpp HanLinVersion.rc)
  target_compile_definitions(HanLinHost PRIVATE HANLIN_HOST_RESOURCE)
  target_include_directories(HanLinHost PRIVATE
    ../include ../../reference/librime-reference/src
    ../../reference/librime-reference/include "${BOOST_ROOT}")
  target_link_libraries(HanLinHost PRIVATE hanlin_transport
    "${HANLIN_RENDERER_LIBRARY}" shlwapi shell32 ole32 uuid gdiplus d2d1 dwrite
    dwmapi comctl32 gdi32 imm32 usp10 bcrypt)
endif()

option(HANLIN_BUILD_TIP "Build the TSF text service" ON)
if(HANLIN_BUILD_TIP)
  add_library(HanLinTip SHARED TextService.cpp TsfObjects.cpp Registration.cpp HanLinVersion.rc)
  target_include_directories(HanLinTip PRIVATE ../../reference/librime-reference/include)
  target_link_libraries(HanLinTip PRIVATE hanlin_transport ole32 oleaut32 uuid shlwapi gdi32)
  target_link_options(HanLinTip PRIVATE /DEF:${CMAKE_CURRENT_SOURCE_DIR}/HanLinTip.def)
endif()
