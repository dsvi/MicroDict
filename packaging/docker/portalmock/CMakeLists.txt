cmake_minimum_required(VERSION 3.22)
project( portalmock LANGUAGES CXX )

set( CMAKE_CXX_STANDARD 23 )
set( CMAKE_AUTOMOC ON )

# ON for the Alpine build image, whose Qt at /opt/qt is a set of archives with
# nothing shared to fall back on; OFF for a distro image with shared Qt, where
# the same sources simply build normally. portalmock.sh picks it.
option( MICRODICT_STATIC_QT "Link against the static Qt of the Alpine image" ON )

if ( MICRODICT_STATIC_QT )
    # Same static setup as the application and the qtprobe next door: archives
    # only, -static, and a linker group, so this links the very Qt the
    # application links.
    set( CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES} )
    string( APPEND CMAKE_EXE_LINKER_FLAGS " -static" )
    set( CMAKE_CXX_LINK_EXECUTABLE
         "<CMAKE_CXX_COMPILER> <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> -Wl,--start-group <LINK_LIBRARIES> -Wl,--end-group" )
endif ()

find_package( Qt6 REQUIRED COMPONENTS DBus Gui Widgets )

# The code under test, compiled straight in rather than reached through the
# application's own build: the portal client from src/hotkey, the colour-scheme
# client from src/lite and the file-chooser probe from src/lite, and nothing else
# of the application.
set( MD_HOTKEY_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../src/hotkey )
set( MD_LITE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../src/lite )

qt_add_executable( portalmock
        main.cc
        ${MD_HOTKEY_DIR}/portalshortcuts.cc
        ${MD_LITE_DIR}/lite_theme.cc
        ${MD_LITE_DIR}/portal_filechooser.cc )

target_include_directories( portalmock PRIVATE ${MD_HOTKEY_DIR} ${MD_LITE_DIR} )
target_include_directories( portalmock PRIVATE ${MD_HOTKEY_DIR} )

target_link_libraries( portalmock PRIVATE Qt6::DBus Qt6::Gui Qt6::Widgets )

# --files opens a real QFileDialog, and that only reaches the portal through Qt's
# xdgdesktopportal platform theme -- a plugin, which a static Qt cannot dlopen. It is
# normally pulled in on its own because Qt marks platform themes "default", but
# naming it here is what keeps the test honest if a Qt bump changes that.
qt_import_plugins( portalmock INCLUDE Qt6::QXdgDesktopPortalThemePlugin )

if ( MICRODICT_STATIC_QT )
    # QtDBus is a wrapper around libdbus and QtGui's own dependencies are not in
    # its interface list - the same private-dependency problem the application
    # has, only with fewer libraries because this program draws nothing. The four
    # Wayland archives are here because this Qt was built with Wayland as its only
    # QPA platform: QtGui's static interface drags in libwayland-client, whose
    # closure marshalling needs libffi, and the cursor/xkb helpers come with it.
    target_link_libraries( portalmock PRIVATE
            -ldbus-1
            -lfontconfig
            -lfreetype
            -lpng16
            -lbrotlidec
            -lbrotlicommon
            -lexpat
            -lxkbcommon
            -lwayland-client
            -lwayland-cursor
            -lffi
            -lz
            m
            pthread )
endif ()
