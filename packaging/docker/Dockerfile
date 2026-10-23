FROM alpine:3.24 AS deps-base

ARG QT_VERSION=v6.12.0

RUN apk add --no-cache \
        build-base cmake ninja meson pkgconf python3 py3-packaging git curl \
        ca-certificates file patch perl gperf nasm util-linux squashfs-tools \
        linux-headers autoconf automake libtool \
        zlib-dev zlib-static \
        libpng-dev libpng-static \
        expat-dev expat-static \
        dbus-dev \
        pcre2-dev pcre2-static \
        freetype-dev freetype-static \
        fontconfig-dev fontconfig-static \
        libxkbcommon-dev libxkbcommon-static \
        brotli-dev brotli-static \
        libffi-dev \
        libogg-dev libogg-static \
        libvorbis-dev libvorbis-static \
        xz-dev xz-static \
        libcap-dev \
        pulseaudio-dev \
        util-linux-dev util-linux-static \
        lzo-dev \
        tomlplusplus-dev \
        fontconfig font-dejavu

RUN cat > /md-fetch.sh <<'EOF'
#!/bin/sh
set -eu
url=$1
want=$2
f=$(basename "$url")
mkdir -p /tmp/dl
curl -fsSL --retry 3 -o "/tmp/dl/$f" "$url"
echo "$want  /tmp/dl/$f" | sha256sum -c -
EOF
RUN chmod +x /md-fetch.sh
RUN cp /usr/include/freetype2/freetype/config/ftoption.h /tmp/ftoption-alpine.h && \
    /md-fetch.sh https://distfiles.alpinelinux.org/distfiles/v3.24/freetype-2.14.3.tar.xz \
      36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f && \
    tar -xf /tmp/dl/freetype-2.14.3.tar.xz -C /tmp && \
    sed -i -e 's|/\* #define FT_CONFIG_OPTION_SUBPIXEL_RENDERING \*/|#define FT_CONFIG_OPTION_SUBPIXEL_RENDERING|' \
           -e 's|/\* #define PCF_CONFIG_OPTION_LONG_FAMILY_NAMES \*/|#define PCF_CONFIG_OPTION_LONG_FAMILY_NAMES|' \
           /tmp/freetype-2.14.3/include/freetype/config/ftoption.h && \
    meson setup /tmp/freetype-build /tmp/freetype-2.14.3 \
      --buildtype=plain --prefix=/usr -Ddefault_library=static \
      -Dbzip2=disabled -Dc_args=-Os -Dcpp_args=-Os && \
    meson compile -C /tmp/freetype-build && \
    meson install -C /tmp/freetype-build && \
    grep -q '^#define FT_CONFIG_OPTION_USE_BZIP2$' /tmp/ftoption-alpine.h && \
    grep -q '^/\* #define FT_CONFIG_OPTION_USE_BZIP2 \*/$' \
      /usr/include/freetype2/freetype/config/ftoption.h && \
    test "$(diff /tmp/ftoption-alpine.h \
      /usr/include/freetype2/freetype/config/ftoption.h | grep -c '^[<>]')" = 2 && \
    ! nm -u /usr/lib/libfreetype.a | grep -q BZ2_ && \
    echo "FREETYPE OK: Alpine's configuration, bzip2 off, nothing left to link" && \
    rm -rf /tmp/freetype-2.14.3 /tmp/freetype-build /tmp/ftoption-alpine.h

RUN /md-fetch.sh https://ftp.gnu.org/gnu/libiconv/libiconv-1.19.tar.gz \
        88dd96a8c0464eca144fc791ae60cd31cd8ee78321e67397e25fc095c4a19aa6 \
 && tar xz -f /tmp/dl/libiconv-1.19.tar.gz -C /tmp \
 && cd /tmp/libiconv-1.19 \
 && ./configure --prefix=/opt/gnu-iconv --disable-shared --enable-static \
        --disable-nls \
 && make -j"$(nproc)" \
 && make install \
 && cd /opt/gnu-iconv/lib \
 && mkdir -p cs && cd cs && ar x ../libcharset.a && ar r ../libiconv.a *.o \
 && cd .. && rm -rf cs

RUN /md-fetch.sh https://gitlab.freedesktop.org/wayland/wayland/-/archive/1.26.0/wayland-1.26.0.tar.gz \
        56b3a985cbfe17a926e3bcf037e4d374f44eaa33f3a4b364549dddf0d324cebc \
 && tar xzf /tmp/dl/wayland-1.26.0.tar.gz -C /tmp \
 && meson setup /tmp/wayland-build /tmp/wayland-1.26.0 \
        --prefix=/usr --libdir=lib \
        --default-library=static -Ddocumentation=false -Dtests=false \
 && meson compile -C /tmp/wayland-build \
 && meson install -C /tmp/wayland-build

RUN /md-fetch.sh https://github.com/libffi/libffi/releases/download/v3.8.0/libffi-3.8.0.tar.gz \
        7da3e2d9a171eb0a038f592ecad3ff2bb2550f3496d87b3b29ad0cf4430c0db4 \
 && tar xzf /tmp/dl/libffi-3.8.0.tar.gz -C /tmp \
 && cd /tmp/libffi-3.8.0 \
 && ./configure --prefix=/usr --disable-shared --enable-static --disable-multilib \
 && make -j"$(nproc)" \
 && make install

RUN /md-fetch.sh https://github.com/hunspell/hunspell/releases/download/v1.7.5/hunspell-1.7.5.tar.gz \
        2e559f0c2a592ba48e421477f58e9bf7e01062277a4a7821afb52fc9401010ae \
 && tar xzf /tmp/dl/hunspell-1.7.5.tar.gz -C /tmp \
 && cd /tmp/hunspell-1.7.5 \
 && ./configure --prefix=/usr --disable-shared --enable-static --disable-tools \
        --disable-dependency-tracking \
 && make -j"$(nproc)" \
 && make install

RUN /md-fetch.sh https://oligarchy.co.uk/xapian/2.1.0/xapian-core-2.1.0.tar.xz \
        8e1259586d342e3d12b5e1f772e9185a10f2ba16e541566b5c3c239f71b8aacc \
 && tar xJf /tmp/dl/xapian-core-2.1.0.tar.xz -C /tmp \
 && cd /tmp/xapian-core-2.1.0 \
 && ./configure --prefix=/usr --disable-shared --enable-static \
        --disable-dependency-tracking \
 && make -j"$(nproc)" \
 && make install

RUN /md-fetch.sh https://github.com/BYVoid/OpenCC/archive/refs/tags/ver.1.4.2.tar.gz \
        8e5f5cf7fe195bd9b9be851adc9738c1ef7dc5c24441dd5878a56db4087a9a70 \
 && tar xzf /tmp/dl/ver.1.4.2.tar.gz -C /tmp \
 && cmake -B /tmp/opencc-build -S /tmp/OpenCC-ver.1.4.2 -GNinja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DBUILD_PYTHON_WRAPPER=OFF \
 && cmake --build /tmp/opencc-build \
 && cmake --install /tmp/opencc-build

RUN /md-fetch.sh https://distfiles.alpinelinux.org/distfiles/v3.24/lzo-2.10.tar.gz \
        c0f892943208266f9b6543b3ae308fab6284c5c90e627931446fb49b4221a072 \
 && tar xzf /tmp/dl/lzo-2.10.tar.gz -C /tmp \
 && cd /tmp/lzo-2.10 \
 && ./configure --prefix=/usr --disable-shared --enable-static \
        --disable-dependency-tracking \
 && make -j"$(nproc)" \
 && make install

RUN /md-fetch.sh https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz \
        8517f65938a4faae9ccf8ebb36631a38c1cadfb5efa85d9a72e15b9e97d25155 \
 && tar xzf /tmp/dl/v3.4.0.tar.gz -C /tmp \
 && c++ -O2 -fPIC -DTOML_HEADER_ONLY=0 -DTOML_SHARED_LIB=1 -I/usr/include/v3 \
        -c /tmp/tomlplusplus-3.4.0/src/toml.cpp -o /tmp/toml.o \
 && ar rcs /usr/lib/libtomlplusplus.a /tmp/toml.o \
 && rm -f /usr/lib/libtomlplusplus.so*

RUN /md-fetch.sh https://ffmpeg.org/releases/ffmpeg-8.1.3.tar.xz \
        7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3 \
 && tar xJf /tmp/dl/ffmpeg-8.1.3.tar.xz -C /tmp \
 && cd /tmp/ffmpeg-8.1.3 \
 && ./configure --prefix=/usr \
        --enable-static --disable-shared \
        --disable-doc --disable-programs --disable-debug \
        --disable-autodetect --disable-everything --disable-iconv \
        --enable-zlib \
        --enable-avformat --enable-avcodec --enable-avutil \
        --enable-swresample --enable-swscale \
        --disable-avfilter --disable-avdevice \
        --enable-protocol=file --enable-protocol=pipe \
        --enable-demuxer=ogg,wav,mp3,aac,adts,mov,mp4,matroska,webm,flac,asf,avi,au,aiff,caf,wv,ape,tta,amr,amrnb,amrwb,w64,ffmetadata,concat \
        --enable-muxer=wav,ogg,adts,mp3,flac,matroska,webm,mov \
        --enable-decoder=pcm_s16le,pcm_s16be,pcm_u8,pcm_s8,pcm_s24le,pcm_s24be,pcm_s32le,pcm_s32be,pcm_f32le,pcm_f64le,pcm_alaw,pcm_mulaw,adpcm_ima_wav,adpcm_ima_qt,adpcm_ms,adpcm_swf,mp3float,mp3,mp3adu,mp3adufloat,mp3on4,mp3on4float,aac,aac_fixed,aac_latm,vorbis,opus,flac,alac,tta,wavpack,ape,shorten,speex,wmav1,wmav2,nellymoser,ac3,eac3,g722,g723_1,g726le,cook,atrac3,metasound \
        --enable-encoder=pcm_s16le,pcm_s16be,pcm_f32le,flac,aac,vorbis \
        --enable-parser=mpegaudio,mpegaudio360,aac,ac3,dts,flac \
        --enable-bsf=aac_adtstoasc,vorbis_compress,opus_head,mp3_header_decompress \
 && make -j"$(nproc)" \
 && make install

RUN apk add --no-cache libsndfile-dev libsndfile-static gettext-dev gettext-static \
 && /md-fetch.sh https://freedesktop.org/software/pulseaudio/releases/pulseaudio-17.0.tar.xz \
        053794d6671a3e397d849e478a80b82a63cb9d8ca296bd35b73317bb5ceb87b5 \
 && tar xJf /tmp/dl/pulseaudio-17.0.tar.xz -C /tmp \
 && cd /tmp/pulseaudio-17.0 \
 && sed -i 's/\bshared_library(/static_library(/g' src/meson.build src/pulse/meson.build \
 && sed -i -e '/^[[:space:]]*version : libpulse/d' -e '/^[[:space:]]*vs_module_defs : /d' \
        -e '/^[[:space:]]*install_rpath : /d' src/pulse/meson.build \
 && meson setup /tmp/pa-build /tmp/pulseaudio-17.0 \
        --prefix=/usr --libdir=lib --default-library=static \
        --auto-features=disabled \
        -Dc_link_args=-lintl \
        -Ddaemon=false -Dclient=true -Ddoxygen=false -Dman=false -Dtests=false \
        -Denable-smoother-2=true \
 && meson compile -C /tmp/pa-build \
 && meson install -C /tmp/pa-build \
 && ls -l /usr/lib/libpulse.a \
 && find /usr/lib -name "libpulsecommon*" -o -name "libpulse-simple*" \
 && cat /usr/lib/pkgconfig/libpulse.pc

RUN find /usr/lib -maxdepth 1 -type l -name "*.so" -delete \
 && rm -f /usr/lib/libharfbuzz.so* /usr/lib/libharfbuzz*.a \
 && rm -f /usr/lib/pulseaudio/libpulsecommon*.so* \
 && rm -rf /usr/lib/cmake/harfbuzz /usr/lib/cmake/double-conversion \
        /usr/lib/cmake/PulseAudio /usr/lib/cmake/xapian /usr/lib/cmake/SndFile \
        /usr/lib/cmake/FLAC /usr/lib/cmake/libxml2 \
 && mkdir -p /usr/lib/cmake/PulseAudio \
 && cat > /usr/lib/cmake/PulseAudio/PulseAudioConfig.cmake <<'EOF'
if (NOT TARGET PulseAudio::PulseAudio)
  add_library(PulseAudio::PulseAudio STATIC IMPORTED)
  set_target_properties(PulseAudio::PulseAudio PROPERTIES
      IMPORTED_LOCATION "/usr/lib/libpulse.a"
      INTERFACE_INCLUDE_DIRECTORIES "/usr/include")
endif()
set(PULSEAUDIO_INCLUDE_DIR "/usr/include")
set(PULSEAUDIO_LIBRARY "/usr/lib/libpulse.a")
set(PulseAudio_FOUND TRUE)
EOF

RUN /md-fetch.sh https://dbus.freedesktop.org/releases/dbus/dbus-1.16.2.tar.xz \
        0ba2a1a4b16afe7bceb2c07e9ce99a8c2c3508e5dec290dbb643384bd6beb7e2 \
 && rm -rf dbus && tar -xf /tmp/dl/dbus-1.16.2.tar.xz && mv dbus-1.16.2 dbus \
 && cd dbus \
 && sed -i 's/^add_library(dbus-1 SHARED$/add_library(dbus-1 STATIC/' dbus/CMakeLists.txt \
 && cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
      -DDBUS_BUILD_TESTS=OFF -DDBUS_WITH_GLIB=OFF -DDBUS_BUILD_X11=OFF \
      -DENABLE_SYSTEMD=OFF -DDBUS_ENABLE_PKGCONFIG=ON \
      -DCMAKE_INSTALL_PREFIX=/usr -DSYSCONFDIR=/etc -DLOCALSTATEDIR=/var \
      -DDBUS_SESSION_SOCKET_DIR=/tmp \
 && ninja -C build && ninja -C build install \
 && test -f /usr/lib/libdbus-1.a \
 && pkg-config --modversion dbus-1

FROM deps-base AS deps

RUN cat > /qt-static-hints.cmake <<'EOF'
set(CMAKE_EXE_LINKER_FLAGS "-static" CACHE STRING "")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a;.so" CACHE STRING "")
set(CMAKE_CXX_STANDARD_LIBRARIES
    "-lfontconfig -lfreetype -lpng16 -lbrotlidec -lbrotlicommon -lexpat -lxkbcommon -lwayland-client -lwayland-cursor -lffi -lz -lm -lpthread"
    CACHE STRING "")
set(CMAKE_CXX_LINK_EXECUTABLE
    "<CMAKE_CXX_COMPILER> <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> -Wl,--start-group <LINK_LIBRARIES> -Wl,--end-group"
    CACHE STRING "")
EOF

RUN git clone --depth 1 --branch ${QT_VERSION} https://github.com/qt/qtbase \
 && git clone --depth 1 --branch ${QT_VERSION} https://github.com/qt/qtsvg \
 && git clone --depth 1 --branch ${QT_VERSION} https://github.com/qt/qtmultimedia \
 && git clone --depth 1 --branch ${QT_VERSION} https://github.com/qt/qtwayland \
 && git clone --depth 1 --branch ${QT_VERSION} https://github.com/qt/qtshadertools \
 && git clone --depth 1 --branch ${QT_VERSION} https://github.com/qt/qttools

RUN cmake -C /qt-static-hints.cmake -B qtbase-build -S qtbase -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/qt \
        -DCMAKE_CXX_STANDARD=17 \
        -DBUILD_SHARED_LIBS=OFF \
        -DQT_QPA_PLATFORMS="wayland;offscreen" \
        -DQT_FEATURE_xcb=OFF -DQT_FEATURE_xcb_xlib=OFF \
        -DQT_FEATURE_eglfs=OFF -DQT_FEATURE_linuxfb=OFF \
        -DQT_FEATURE_vkkhrdisplay=OFF \
        -DQT_FEATURE_sql=OFF -DQT_FEATURE_dbus=ON \
        -DQT_FEATURE_printsupport=OFF -DQT_FEATURE_testlib=OFF \
        -DQT_FEATURE_glib=OFF -DQT_FEATURE_icu=OFF \
        -DQT_FEATURE_egl=OFF -DINPUT_opengl=no \
        -DINPUT_freetype=system -DINPUT_harfbuzz=no \
        -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF \
 && cmake --build qtbase-build -j"$(nproc)" \
 && cmake --install qtbase-build

RUN cmake -C /qt-static-hints.cmake -B qtshadertools-build -S qtshadertools -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/qt \
        -DCMAKE_PREFIX_PATH=/opt/qt \
        -DCMAKE_CXX_STANDARD=17 \
        -DBUILD_SHARED_LIBS=OFF \
        -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF \
 && cmake --build qtshadertools-build -j"$(nproc)" \
 && cmake --install qtshadertools-build

RUN cmake -C /qt-static-hints.cmake -B qtsvg-build -S qtsvg -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/qt \
        -DCMAKE_PREFIX_PATH=/opt/qt \
        -DCMAKE_CXX_STANDARD=17 \
        -DBUILD_SHARED_LIBS=OFF \
        -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF \
 && cmake --build qtsvg-build -j"$(nproc)" \
 && cmake --install qtsvg-build

RUN cmake -C /qt-static-hints.cmake -B qtwayland-build -S qtwayland -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/qt \
        -DCMAKE_PREFIX_PATH=/opt/qt \
        -DCMAKE_CXX_STANDARD=17 \
        -DBUILD_SHARED_LIBS=OFF \
        -DQT_FEATURE_xcb=OFF -DQT_FEATURE_xcomposite=OFF \
        -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF \
 && cmake --build qtwayland-build -j"$(nproc)" \
 && cmake --install qtwayland-build

RUN cmake -C /qt-static-hints.cmake -B qtmultimedia-build -S qtmultimedia -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/qt \
        -DCMAKE_PREFIX_PATH=/opt/qt \
        -DCMAKE_CXX_STANDARD=17 \
        -DBUILD_SHARED_LIBS=OFF \
        -DQT_FEATURE_gstreamermedia=OFF \
        -DQT_FEATURE_pulseaudio=ON \
        -DQT_FEATURE_mediacontrols=OFF \
        -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF \
 && cmake --build qtmultimedia-build -j"$(nproc)" \
 && cmake --install qtmultimedia-build

RUN cmake -C /qt-static-hints.cmake -B qttools-build -S qttools -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/qt \
        -DCMAKE_PREFIX_PATH=/opt/qt \
        -DCMAKE_CXX_STANDARD=17 \
        -DBUILD_SHARED_LIBS=OFF \
        -DQT_FEATURE_designer=OFF -DQT_FEATURE_assistant=OFF \
        -DQT_FEATURE_pixeltool=OFF -DQT_FEATURE_distancefieldgenerator=OFF \
        -DQT_FEATURE_qtattributionsscanner=OFF -DQT_FEATURE_qtplugininfo=OFF \
        -DQT_FEATURE_qdbus=OFF -DQT_FEATURE_qtdiag=OFF \
        -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF \
 && cmake --build qttools-build -j"$(nproc)" \
 && cmake --install qttools-build

RUN H=/opt/qt/include/QtGui/qtgui-config.h \
 && grep -q "define QT_NO_OPENGL 1" "$H" \
 && grep -q "define QT_NO_EGL 1" "$H" \
 && grep -q "define QT_FEATURE_vulkan -1" "$H" \
 && ! grep -qE "define QT_FEATURE_(opengl[a-z_0-9]*|egl[a-z_0-9]*|vulkan[a-z_0-9]*) 1" "$H" \
 && ! apk info | grep -qE "^(mesa|libgl|libegl|libdrm|vulkan-|glslang|spirv-)" \
 && ! ls /opt/qt/plugins/platforms/*.a | grep -qE "xcb|eglfs|linuxfb|vkkhrdisplay|glx" \
 && echo "Qt in this image is software-rendering-only: no OpenGL, no EGL, no Vulkan" \
 || { echo "!! the Qt in this image is not software-rendering-only: re-check the"; \
      echo "!! -DQT_FEATURE_egl=OFF -DINPUT_opengl=no flags and the apk list above"; exit 1; }

WORKDIR /src
COPY . ./
RUN cmake -B build -S . -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH=/opt/qt \
        -DMICRODICT_STATIC_BUILD=ON \
        -DCMAKE_SKIP_RPATH=ON \
        -DIconv_IS_BUILT_IN=OFF \
        -DIconv_LIBRARY=/opt/gnu-iconv/lib/libiconv.a \
        -DIconv_INCLUDE_DIR=/opt/gnu-iconv/include \
 && for a in libqwayland.a libqoffscreen.a libqsvg.a libqjpeg.a libqgif.a libqico.a libqsvgicon.a libqxdgdesktopportal.a libffmpegmediaplugin.a; do grep -q "$a" build/build.ninja || { echo "static link is missing $a: a Qt plugin target was renamed or dropped -- check qt_import_plugins() in CMakeLists.txt"; exit 1; }; done \
 && cmake --build build -j"$(nproc)" \
 && sh packaging/docker/qtprobe.sh \
 && sh packaging/docker/portalmock.sh \
 && QT_QPA_PLATFORM=offscreen sh packaging/docker/smoke.sh \
 && QT_QPA_PLATFORM=offscreen sh packaging/docker/migtest.sh \
 && cp /tmp/portalmock-build/portalmock /opt/portalmock \
 && rm -rf /tmp/migA /tmp/migB /tmp/migC /tmp/portalmock-build /tmp/*.log \
 && rm -f /tmp/single-*-mdlockfile \
 && strip build/microdict \
 && file build/microdict \
 && ls -l build/microdict \
 && ! readelf -d build/microdict | grep -qE "NEEDED|RPATH|RUNPATH" \
 && [ "$(readelf -l build/microdict | grep -c INTERP)" = 0 ] \
 && ( strings build/microdict | grep -E "libQt6|libhunspell|libxapian|libopencc|liblzo2|libvorbis|libfmt|libwayland-|libpulse" || echo "no dynamic-library strings" ) \
 && { strings build/microdict | grep -E "libGL\.so|libEGL\.so|libvulkan|libglapi|/dev/dri|eglGet[a-z]*|vkCreateInstance" \
      && { echo "!! the binary carries a GL/EGL/Vulkan path: this build must stay"; \
           echo "!! software-rendered, because it has no business loading a driver"; exit 1; }; \
      grep -E -- "-l(GL|EGL|GLESv2|vulkan|glapi)" build/build.ninja \
      && { echo "!! the link line asks for a GL/EGL/Vulkan library"; exit 1; }; \
      echo "software rendering only: no GL/EGL/Vulkan in the binary or the link line"; }

FROM alpine:3.24 AS appimage

ARG TARGETARCH

RUN apk add --no-cache squashfs-tools curl file binutils coreutils desktop-file-utils

WORKDIR /src
COPY . ./
COPY --from=deps /src/build/microdict /src/build/microdict
COPY --from=deps /usr/share/opencc /usr/share/opencc
RUN ARCH=$( [ "$TARGETARCH" = arm64 ] && echo aarch64 || echo x86_64 ) sh packaging/docker/appimage.sh

FROM scratch AS dist

COPY --from=appimage /src/build/MicroDict-*.AppImage /

FROM alpine:3.24

RUN apk add --no-cache fontconfig font-dejavu

COPY --from=deps /src/build/microdict /usr/local/bin/microdict
COPY --from=appimage /src/build/MicroDict-*.AppImage /out/

ENTRYPOINT ["/usr/local/bin/microdict"]
