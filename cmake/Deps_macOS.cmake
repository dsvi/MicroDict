set(ENV{PATH} "$ENV{PATH}:/usr/local/bin/:/opt/homebrew/bin")
target_include_directories(${MICRODICT} PRIVATE /usr/local/include /opt/homebrew/include)

find_library(CARBON_LIBRARY Carbon REQUIRED)
find_library(APPKIT_LIBRARY AppKit REQUIRED)
target_link_libraries(${MICRODICT} PRIVATE ${CARBON_LIBRARY} ${APPKIT_LIBRARY})

find_package(PkgConfig REQUIRED)

set(Optional_Pkgs "")
if (WITH_QT_MULTIMEDIA)
    list(APPEND Optional_Pkgs "libavcodec;libavformat;libavutil;libswresample")
    list(APPEND Optional_Pkgs "libsharpyuv;libwebp;libjxl_cms")
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

find_package(Iconv REQUIRED)
target_link_libraries(${MICRODICT} PRIVATE PkgConfig::DEPS Iconv::Iconv)
