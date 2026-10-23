cmake_minimum_required(VERSION 3.25)

option(WITH_QT_MULTIMEDIA "Enable support for Qt Multimedia player" ON)

option(MICRODICT_STATIC_BUILD "Link everything statically, producing a binary with no shared-library dependencies" OFF)

include(FeatureSummary)

file(READ "VERSION" PROJECT_VERSION_ST)
string(STRIP "${PROJECT_VERSION_ST}" PROJECT_VERSION_ST)
project(microdict
        VERSION ${PROJECT_VERSION_ST}
        LANGUAGES CXX C)

if (APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
endif ()

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MICRODICT "microdict")
if (APPLE)
    set(MICRODICT "MicroDict")
endif ()

set(MD_QT_COMPONENTS Concurrent LinguistTools Multimedia Widgets Xml)

if (LINUX OR BSD)
    list(APPEND MD_QT_COMPONENTS DBus)
endif ()

if (MICRODICT_STATIC_BUILD)
    list(APPEND MD_QT_COMPONENTS Svg WaylandClient)
endif ()

if (MICRODICT_STATIC_BUILD)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -static")

    set(CMAKE_CXX_LINK_EXECUTABLE
            "<CMAKE_CXX_COMPILER> <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> -Wl,--start-group <LINK_LIBRARIES> -Wl,--end-group")
endif ()

find_package(Qt6 6.2 REQUIRED COMPONENTS ${MD_QT_COMPONENTS})

qt_standard_project_setup()
set(CMAKE_AUTORCC ON)

set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/")

file(GLOB_RECURSE ALL_SOURCE_FILES CONFIGURE_DEPENDS src/*.cc src/*.hh src/*.ui src/*.c)

if (APPLE)
    file(GLOB_RECURSE MACOS_SOURCE_FILES CONFIGURE_DEPENDS src/**/*.mm)

    set(MACOS_APP_ICON ${CMAKE_SOURCE_DIR}/icons/macicon.icns)
    set_source_files_properties(${MACOS_APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif ()

set(QSINGLEAPP_SOURCE_FILES
        thirdparty/qtsingleapplication/src/qtlocalpeer.cpp
        thirdparty/qtsingleapplication/src/qtlocalpeer.h
        thirdparty/qtsingleapplication/src/qtsingleapplication.cpp
        thirdparty/qtsingleapplication/src/qtsingleapplication.h
)

qt_add_executable(${MICRODICT})

target_sources(${MICRODICT} PRIVATE
        icons/flags.qrc
        resources.qrc
        src/stylesheets/css.qrc
        ${ALL_SOURCE_FILES}
        ${MACOS_SOURCE_FILES}
        ${MACOS_APP_ICON}
        ${QSINGLEAPP_SOURCE_FILES}
)

add_compile_definitions(PROGRAM_VERSION="${PROJECT_VERSION}")

target_link_libraries(${MICRODICT} PRIVATE
        Qt6::Xml
        Qt6::Concurrent
        Qt6::Multimedia
        Qt6::Widgets
)

if (MICRODICT_STATIC_BUILD)
    set(_md_extra_plugins "")
    set(_md_missing_plugins "")
    set(_md_plugin_list
            Qt6::QWaylandIntegrationPlugin
            Qt6::QOffscreenIntegrationPlugin
            Qt6::QWaylandXdgShellIntegrationPlugin
            Qt6::QWaylandWlShellIntegrationPlugin
            Qt6::QWaylandBradientDecorationPlugin
            Qt6::QWaylandAdwaitaDecorationPlugin
            Qt6::QSvgPlugin
            Qt6::QJpegPlugin
            Qt6::QGifPlugin
            Qt6::QIcoPlugin
            Qt6::QICOPlugin
            Qt6::QSvgIconPlugin
            Qt6::QXdgDesktopPortalThemePlugin
            Qt6::QFFmpegMediaPlugin)
    set(_md_required_plugins
            Qt6::QWaylandIntegrationPlugin
            Qt6::QOffscreenIntegrationPlugin
            Qt6::QSvgPlugin
            Qt6::QXdgDesktopPortalThemePlugin
            Qt6::QFFmpegMediaPlugin)
    foreach (_md_plugin IN ITEMS ${_md_plugin_list})
        if (TARGET ${_md_plugin})
            list(APPEND _md_extra_plugins ${_md_plugin})
        else ()
            list(APPEND _md_missing_plugins ${_md_plugin})
        endif ()
    endforeach ()
    if (TARGET Qt6::QIcoPlugin AND "Qt6::QICOPlugin" IN_LIST _md_missing_plugins)
        list(REMOVE_ITEM _md_missing_plugins "Qt6::QICOPlugin")
    endif ()
    if (TARGET Qt6::QICOPlugin AND "Qt6::QIcoPlugin" IN_LIST _md_missing_plugins)
        list(REMOVE_ITEM _md_missing_plugins "Qt6::QIcoPlugin")
    endif ()
    foreach (_md_plugin IN LISTS _md_required_plugins)
        if (NOT _md_plugin IN_LIST _md_extra_plugins)
            message(FATAL_ERROR
                    "MICRODICT_STATIC_BUILD is on but ${_md_plugin} does not exist in this Qt "
                    "(${Qt6_VERSION}): the static binary would be missing a plugin it "
                    "cannot load at run time. Check the target name against this Qt's "
                    "cmake package.")
        endif ()
    endforeach ()
    if (_md_missing_plugins)
        message(WARNING "Static build: these Qt plugin targets were not found and are "
                        "therefore NOT linked in: ${_md_missing_plugins}")
    endif ()
    qt_import_plugins(${MICRODICT} INCLUDE ${_md_extra_plugins})
endif ()

target_include_directories(${MICRODICT} PRIVATE
        ${PROJECT_SOURCE_DIR}/thirdparty/qtsingleapplication/src
        ${PROJECT_SOURCE_DIR}/src/
        ${PROJECT_SOURCE_DIR}/src/common
        ${PROJECT_SOURCE_DIR}/src/dict
        ${PROJECT_SOURCE_DIR}/src/dict/utils
        ${PROJECT_SOURCE_DIR}/src/ui
)

target_compile_definitions(${MICRODICT} PRIVATE
        QT_DISABLE_DEPRECATED_UP_TO=0x060700
        QT_NO_DEPRECATED_WARNINGS
)

if (Qt6Widgets_VERSION VERSION_GREATER_EQUAL 6.8.2)
    target_compile_definitions(${MICRODICT} PRIVATE
            QT_ENABLE_STRICT_MODE_UP_TO=0x060800
    )
endif ()

target_compile_definitions(${MICRODICT} PUBLIC
        MAKE_CHINESE_CONVERSION_SUPPORT
        $<$<BOOL:${WITH_QT_MULTIMEDIA}>:MAKE_QTMULTIMEDIA_PLAYER>
)

file(GLOB TRANS_FILES "locale/*.ts")

qt_add_translations(${MICRODICT} TS_FILES ${TRANS_FILES}
        QM_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/locale"
        RESOURCE_PREFIX "/"
        LUPDATE_OPTIONS "-no-ui-lines -locations none -no-obsolete")

add_dependencies(${MICRODICT} "release_translations")

if (LINUX OR BSD)
    include(cmake/Deps_Linux.cmake)
else ()
    include(cmake/Deps_macOS.cmake)
    include(cmake/Package_macOS.cmake)
endif ()

feature_summary(WHAT ALL DESCRIPTION "Build configuration:")
