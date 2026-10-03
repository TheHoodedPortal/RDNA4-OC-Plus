cmake_minimum_required(VERSION 3.24)
project(RDNA4OCPlusNative LANGUAGES CXX RC)
option(RDNA_ENABLE_HOVER_EXPERIMENT "Enable the optional enhanced interaction hover treatment" OFF)

configure_file(src/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/app.rc" APPEND PROPERTY OBJECT_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/app.manifest"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/ocplus.ico")
add_executable(RDNA4OCPlus WIN32 src/main.cpp src/hardware.cpp "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
target_compile_features(RDNA4OCPlus PRIVATE cxx_std_20)
target_compile_definitions(RDNA4OCPlus PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
if(RDNA_ENABLE_HOVER_EXPERIMENT)
  target_compile_definitions(RDNA4OCPlus PRIVATE RDNA_ENABLE_HOVER_EXPERIMENT=1)
endif()
target_link_libraries(RDNA4OCPlus PRIVATE d2d1 dwrite dwmapi ole32 setupapi shell32)

enable_testing()
add_executable(RDNA4CoreTests tests/core_tests.cpp src/hardware.cpp)
target_compile_features(RDNA4CoreTests PRIVATE cxx_std_20)
target_compile_definitions(RDNA4CoreTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_link_libraries(RDNA4CoreTests PRIVATE setupapi)
add_test(NAME RDNA4CoreTests COMMAND RDNA4CoreTests)

# Avoid a separate Visual C++ runtime redistributable for this small native
# branch. Windows graphics and windowing APIs remain OS-provided components.
if(MSVC)
  # The GUI favors a small standalone binary; keep inlining for responsive drawing.
  string(REPLACE "/O2" "/O1" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
  foreach(target RDNA4OCPlus RDNA4CoreTests)
    set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /utf-8)
  endforeach()
  target_compile_options(RDNA4OCPlus PRIVATE $<$<CONFIG:Release>:/Gy> $<$<CONFIG:Release>:/Gw>)
  target_link_options(RDNA4OCPlus PRIVATE $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()
