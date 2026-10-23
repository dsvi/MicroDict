cmake_minimum_required(VERSION 3.22)
project( qtprobe LANGUAGES CXX )

set( CMAKE_CXX_STANDARD 23 )
set( CMAKE_AUTOMOC ON )

# This is deliberately a miniature copy of MicroDict's own static-build
# setup (CMakeLists.txt + cmake/Deps_Linux.cmake, MICRODICT_STATIC_BUILD): archives
# only, -static, and a linker group, so that a failure here points at the same
# thing a failure of the real build would.
set( CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES} )
string( APPEND CMAKE_EXE_LINKER_FLAGS " -static" )
set( CMAKE_CXX_LINK_EXECUTABLE
     "<CMAKE_CXX_COMPILER> <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> -Wl,--start-group <LINK_LIBRARIES> -Wl,--end-group" )

find_package( Qt6 REQUIRED COMPONENTS DBus Widgets Svg Multimedia )

qt_add_executable( qtprobe main.cc )

# The Qt modules' interface lists do not carry their private dependencies, and
# a static link needs every one of them: fontconfig wants expat, freetype wants
# brotli/png/zlib, the wayland client wants libffi, Qt Multimedia's ffmpeg
# plugin wants FFmpeg, and libpulse's logging lives in the private
# libpulsecommon archive. Everything the application links is listed here too,
# so the probe exercises the same archive set.
target_link_libraries( qtprobe PRIVATE
        Qt6::DBus
        Qt6::Widgets
        Qt6::Svg
        Qt6::Multimedia
        -lfontconfig
        -lfreetype
        -lpng16
        -lbrotlidec
        -lbrotlicommon
        -lexpat
        -ldbus-1
        -lxkbcommon
        -lwayland-client
        -lwayland-cursor
        -lffi
        -lz
        -logg
        -lvorbis
        -lvorbisfile
        -llzma
        -lxapian
        -lhunspell-1.7
        -lopencc
        -lmarisa
        -llzo2
        -luuid
        -lavformat
        -lavcodec
        -lavutil
        -lswresample
        -lswscale
        m
        pthread )

file( GLOB MD_PULSECOMMON_LIBRARIES /usr/lib/pulseaudio/libpulsecommon-*.a )
if ( MD_PULSECOMMON_LIBRARIES )
    target_link_libraries( qtprobe PRIVATE ${MD_PULSECOMMON_LIBRARIES} )
endif ()

find_library( MD_LIBINTL_LIBRARY NAMES intl )
if ( MD_LIBINTL_LIBRARY )
    target_link_libraries( qtprobe PRIVATE ${MD_LIBINTL_LIBRARY} )
endif ()
