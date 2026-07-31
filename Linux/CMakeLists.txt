cmake_minimum_required(VERSION 3.24)

project(opendisplay-linux VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core DBus)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(AVAHI REQUIRED IMPORTED_TARGET avahi-client)
pkg_check_modules(USBMUXD REQUIRED IMPORTED_TARGET libusbmuxd-2.0)

add_executable(opendisplay-linux
    include/opendisplay/kde_portal.hpp
    src/main.cpp
    src/discovery.cpp
    src/ffmpeg_encoder.cpp
    src/kde_portal.cpp
    src/pipewire_capture.cpp
    src/pipewire_format.cpp
    src/session.cpp
    src/socket.cpp
    src/wire.cpp
)

target_include_directories(opendisplay-linux PRIVATE include)
target_link_libraries(opendisplay-linux PRIVATE
    Qt6::Core
    Qt6::DBus
    PkgConfig::PIPEWIRE
    PkgConfig::AVAHI
    PkgConfig::USBMUXD
    Threads::Threads
)

target_compile_options(opendisplay-linux PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
)

include(CTest)
if(BUILD_TESTING)
    add_executable(opendisplay-wire-tests
        tests/wire_tests.cpp
        src/wire.cpp
    )
    target_include_directories(opendisplay-wire-tests PRIVATE include)
    target_link_libraries(opendisplay-wire-tests PRIVATE Qt6::Core)
    target_compile_options(opendisplay-wire-tests PRIVATE -UNDEBUG)
    add_test(NAME wire COMMAND opendisplay-wire-tests)

    add_executable(opendisplay-encoder-tests
        tests/encoder_tests.cpp
        src/ffmpeg_encoder.cpp
        src/wire.cpp
    )
    target_include_directories(opendisplay-encoder-tests PRIVATE include)
    target_link_libraries(opendisplay-encoder-tests PRIVATE Qt6::Core Threads::Threads)
    target_compile_options(opendisplay-encoder-tests PRIVATE -UNDEBUG)
    add_test(NAME encoder COMMAND opendisplay-encoder-tests)
    set_tests_properties(encoder PROPERTIES TIMEOUT 15)

    add_executable(opendisplay-pipewire-format-tests
        tests/pipewire_format_tests.cpp
        src/pipewire_format.cpp
    )
    target_include_directories(opendisplay-pipewire-format-tests PRIVATE include)
    target_link_libraries(opendisplay-pipewire-format-tests PRIVATE PkgConfig::PIPEWIRE)
    target_compile_options(opendisplay-pipewire-format-tests PRIVATE -UNDEBUG)
    add_test(NAME pipewire-format COMMAND opendisplay-pipewire-format-tests)

    add_executable(opendisplay-socket-tests
        tests/socket_tests.cpp
        src/socket.cpp
    )
    target_include_directories(opendisplay-socket-tests PRIVATE include)
    target_link_libraries(opendisplay-socket-tests PRIVATE PkgConfig::USBMUXD Threads::Threads)
    target_compile_options(opendisplay-socket-tests PRIVATE -UNDEBUG)
    add_test(NAME socket COMMAND opendisplay-socket-tests)
    set_tests_properties(socket PROPERTIES TIMEOUT 5)
endif()

install(TARGETS opendisplay-linux RUNTIME DESTINATION bin)
