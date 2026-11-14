cmake_minimum_required(VERSION 3.10)
project(capture_pipewire LANGUAGES C)
find_package(PkgConfig)
pkg_check_modules(CAPTURE_PIPEWIRE REQUIRED IMPORTED_TARGET
  gio-2.0 gio-unix-2.0 libpipewire-0.3 libspa-0.2 wayland-client)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
set(PROTOCOL_XML ${CMAKE_CURRENT_SOURCE_DIR}/src/zkde-screencast-unstable-v1.xml)
set(PROTOCOL_H ${CMAKE_CURRENT_BINARY_DIR}/zkde-screencast-client.h)
set(PROTOCOL_C ${CMAKE_CURRENT_BINARY_DIR}/zkde-screencast-protocol.c)
add_custom_command(OUTPUT ${PROTOCOL_H} ${PROTOCOL_C}
  COMMAND ${WAYLAND_SCANNER} client-header ${PROTOCOL_XML} ${PROTOCOL_H}
  COMMAND ${WAYLAND_SCANNER} private-code ${PROTOCOL_XML} ${PROTOCOL_C}
  DEPENDS ${PROTOCOL_XML})
add_library(capture_kde STATIC src/kde_capture.c ${PROTOCOL_H} ${PROTOCOL_C})
target_include_directories(capture_kde PUBLIC src ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(capture_kde PkgConfig::CAPTURE_PIPEWIRE lg_common)
add_library(capture_pipewire STATIC src/pipewire.c src/portal.c)
target_link_libraries(capture_pipewire PkgConfig::CAPTURE_PIPEWIRE capture_kde lg_common)
target_include_directories(capture_pipewire PRIVATE src)
add_executable(lg-kde-source src/kde_list.c)
target_link_libraries(lg-kde-source capture_kde)
set_target_properties(lg-kde-source PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
