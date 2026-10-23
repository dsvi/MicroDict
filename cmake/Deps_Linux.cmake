find_package(PkgConfig REQUIRED)

set(Optional_Pkgs "")
if (WITH_QT_MULTIMEDIA)
    list(APPEND Optional_Pkgs
            "libavcodec;libavformat;libavutil;libswresample")
endif ()

pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET
        hunspell
        liblzma
        lzo2
        opencc
        tomlplusplus
        vorbis
        vorbisfile
        xapian-core
        zlib
        ${Optional_Pkgs}
)

target_link_libraries(${MICRODICT} PRIVATE PkgConfig::DEPS)

if (BSD STREQUAL "FreeBSD")
    target_compile_definitions(${MICRODICT} PUBLIC LIBICONV_PLUG)
else ()
    find_package(Iconv REQUIRED)
    target_link_libraries(${MICRODICT} PRIVATE Iconv::Iconv)
endif ()

target_link_libraries(${MICRODICT} PRIVATE Qt6::DBus)

if (MICRODICT_STATIC_BUILD)
    target_link_options(${MICRODICT} PRIVATE
            -Wl,--undefined=pthread_mutexattr_destroy
            -Wl,--undefined=thrd_exit
            -Wl,--undefined=__cxa_pure_virtual)
    target_link_libraries(${MICRODICT} PRIVATE
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
    )

    if (WITH_QT_MULTIMEDIA)
        target_link_libraries(${MICRODICT} PRIVATE -lswscale)
    endif ()

    file(GLOB MD_PULSECOMMON_LIBRARIES /usr/lib/pulseaudio/libpulsecommon-*.a)
    if (MD_PULSECOMMON_LIBRARIES)
        target_link_libraries(${MICRODICT} PRIVATE ${MD_PULSECOMMON_LIBRARIES})
    endif ()

    target_link_libraries(${MICRODICT} PRIVATE m pthread)

    find_library(MD_LIBINTL_LIBRARY NAMES intl)
    if (MD_LIBINTL_LIBRARY)
        target_link_libraries(${MICRODICT} PRIVATE ${MD_LIBINTL_LIBRARY})
    endif ()

    if (NOT Iconv_IS_BUILT_IN)
        message(STATUS "Iconv: using the external (GNU) libiconv: ${Iconv_LIBRARY}")
    endif ()
endif ()
