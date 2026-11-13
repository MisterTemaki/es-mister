#!/usr/bin/env bash
# Builds EmulationStation for the MiSTer FPGA: one static ARMv7 binary (musl libc, through zig cc), its
# libraries built from source at pinned versions, and the package to copy to the SD card.
#
#   mister/build.sh [deps|es|package|all]      default: all
#
# Needs: git, cmake, ninja, python3 with ziglang (pip install ziglang), autoconf, automake, libtool,
# gettext (msgfmt), and for the package the Carbon theme (fetched here). Everything goes in
# build-mister/ (MISTER_WORK to choose another folder).

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
WORK=${MISTER_WORK:-$ROOT/build-mister}
SRC=$WORK/src
PREFIX=$WORK/prefix
TOOLS=$WORK/toolchain
JOBS=${JOBS:-$(nproc)}
TARGET=arm-linux-musleabihf
CPU=cortex_a9

mkdir -p "$SRC" "$PREFIX/include" "$PREFIX/lib" "$TOOLS"

# only the target's own packages (fribidi from the build machine must not be found)
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig"
export PKG_CONFIG_PATH=""

# --- the toolchain: zig cc for ARMv7 with NEON (the DE10-Nano's Cortex-A9) -------------------------
toolchain() {
	local zig
	zig=$(python3 -c 'import ziglang, os; print(os.path.join(os.path.dirname(ziglang.__file__), "zig"))')
	for tool in cc c++; do
		cat >"$TOOLS/z$tool" <<EOF
#!/bin/sh
exec "$zig" $tool -target $TARGET -mcpu=$CPU -Wno-error=date-time "\$@"
EOF
	done
	printf '#!/bin/sh\nexec "%s" ar "$@"\n' "$zig" >"$TOOLS/zar"
	printf '#!/bin/sh\nexec "%s" ranlib "$@"\n' "$zig" >"$TOOLS/zranlib"
	chmod +x "$TOOLS"/z*

	cat >"$TOOLS/armhf.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER $TOOLS/zcc)
set(CMAKE_CXX_COMPILER $TOOLS/zc++)
set(CMAKE_AR $TOOLS/zar CACHE FILEPATH "")
set(CMAKE_RANLIB $TOOLS/zranlib CACHE FILEPATH "")
set(CMAKE_FIND_ROOT_PATH $PREFIX)
set(CMAKE_PREFIX_PATH $PREFIX)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF
}

# --- sources at pinned versions ------------------------------------------------------------------
fetch() { # name url ref
	local dir=$SRC/$1
	if [ ! -d "$dir" ]; then
		git clone -q --depth 1 -b "$3" "$2" "$dir"
	fi
}

sources() {
	fetch freetype https://github.com/freetype/freetype.git VER-2-14-3
	fetch alsa-lib https://github.com/alsa-project/alsa-lib.git v1.2.16.1
	fetch SDL2 https://github.com/libsdl-org/SDL.git release-2.32.10
	fetch SDL2_mixer https://github.com/libsdl-org/SDL_mixer.git release-2.8.2
	fetch curl https://github.com/curl/curl.git curl-8_19_0
	fetch rapidjson https://github.com/Tencent/rapidjson.git master
	fetch vlc https://github.com/videolan/vlc.git 3.0.21
}

cmake_dep() { # source dir, build dir, options...
	local src=$1 build=$2
	shift 2
	cmake -S "$src" -B "$build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TOOLS/armhf.cmake" -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX="$PREFIX" -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=OFF "$@" >/dev/null
	cmake --build "$build" -j "$JOBS" >/dev/null
	cmake --install "$build" >/dev/null
}

deps() {
	toolchain
	sources
	export CC="$TOOLS/zcc" CXX="$TOOLS/zc++" AR="$TOOLS/zar" RANLIB="$TOOLS/zranlib"
	# no zlib of its own: ES's libcheevos brings one, and two would clash; nothing here needs it

	if [ ! -f "$PREFIX/lib/libfreetype.a" ]; then
		echo "== freetype"
		cmake_dep "$SRC/freetype" "$WORK/obj/freetype" -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON \
			-DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON -DFT_DISABLE_ZLIB=ON
	fi

	if [ ! -f "$PREFIX/lib/libasound.a" ]; then
		echo "== alsa-lib"
		(
			cd "$SRC/alsa-lib"
			# its git tree asks for autoconf 2.72; 2.71 (Ubuntu 24.04's) makes the same configure
			sed -i 's/AC_PREREQ(\[\?2\.72\]\?)/AC_PREREQ(2.71)/' configure.ac
			[ -x configure ] || autoreconf -fi >/dev/null 2>&1
			mkdir -p "$WORK/obj/alsa-lib"
			cd "$WORK/obj/alsa-lib"
			# the MiSTer's own ALSA configuration is read at run time, from where its Linux keeps it
			"$SRC/alsa-lib/configure" --host=$TARGET --prefix="$PREFIX" --enable-static --disable-shared \
				--disable-python --disable-topology --disable-ucm --with-configdir=/usr/share/alsa \
				--with-plugindir=/usr/lib/alsa-lib >/dev/null
			make -j "$JOBS" >/dev/null
			make install >/dev/null
		)
	fi

	if [ ! -f "$PREFIX/lib/libSDL2.a" ]; then
		echo "== SDL2"
		(cd "$SRC/SDL2" && git apply --check "$ROOT"/mister/patches/sdl2/*.patch 2>/dev/null && git apply "$ROOT"/mister/patches/sdl2/*.patch || true)
		cmake_dep "$SRC/SDL2" "$WORK/obj/SDL2" -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF \
			-DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_VULKAN=OFF \
			-DSDL_RPI=OFF -DSDL_DIRECTFB=OFF -DSDL_OFFSCREEN=OFF -DSDL_DUMMYVIDEO=ON \
			-DSDL_ALSA=ON -DSDL_ALSA_SHARED=OFF -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF \
			-DSDL_SNDIO=OFF -DSDL_OSS=OFF -DSDL_LIBSAMPLERATE=OFF -DSDL_HIDAPI=OFF -DSDL_LIBUDEV=OFF \
			-DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_FCITX=OFF -DSDL_SYSTEM_ICONV=OFF -DSDL_LIBICONV=OFF
	fi

	if [ ! -f "$PREFIX/lib/libSDL2_mixer.a" ]; then
		echo "== SDL2_mixer"
		# no FLAC: its dr_flac would clash with the one ES's libcheevos brings
		cmake_dep "$SRC/SDL2_mixer" "$WORK/obj/SDL2_mixer" -DSDL2MIXER_VENDORED=OFF -DSDL2MIXER_SAMPLES=OFF \
			-DSDL2MIXER_DEPS_SHARED=OFF -DSDL2MIXER_CMD=OFF -DSDL2MIXER_FLAC=OFF \
			-DSDL2MIXER_GME=OFF -DSDL2MIXER_MOD=OFF -DSDL2MIXER_MIDI=OFF \
			-DSDL2MIXER_MP3=ON -DSDL2MIXER_MP3_MINIMP3=ON -DSDL2MIXER_MP3_MPG123=OFF -DSDL2MIXER_OPUS=OFF \
			-DSDL2MIXER_VORBIS=STB -DSDL2MIXER_WAVE=ON -DSDL2MIXER_WAVPACK=OFF -DSDL2MIXER_INSTALL=ON
	fi

	if [ ! -f "$PREFIX/lib/libcurl.a" ]; then
		echo "== curl"
		# HTTP only for now: HTTPS (the scrapers, theme downloads) comes with a TLS library later
		cmake_dep "$SRC/curl" "$WORK/obj/curl" -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF \
			-DCURL_ENABLE_SSL=OFF -DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF -DUSE_NGHTTP2=OFF -DUSE_LIBIDN2=OFF \
			-DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DCURL_ZLIB=OFF -DCURL_DISABLE_LDAP=ON -DENABLE_CURL_MANUAL=OFF \
			-DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF -DCURL_USE_PKGCONFIG=OFF
	fi

	echo "== headers: RapidJSON, libVLC"
	cp -r "$SRC/rapidjson/include/rapidjson" "$PREFIX/include/"
	mkdir -p "$PREFIX/include/vlc"
	cp "$SRC"/vlc/include/vlc/*.h "$PREFIX/include/vlc/"
	cat >"$PREFIX/include/vlc/libvlc_version.h" <<'EOF'
/* libVLC's version, for the headers only: the MiSTer build stubs libVLC's functions */
#ifndef LIBVLC_VERSION_H
#define LIBVLC_VERSION_H 1
#define LIBVLC_VERSION_MAJOR (3)
#define LIBVLC_VERSION_MINOR (0)
#define LIBVLC_VERSION_REVISION (21)
#define LIBVLC_VERSION_EXTRA (0)
#define LIBVLC_VERSION(maj, min, rev, extra) (((maj) << 24) | ((min) << 16) | ((rev) << 8) | (extra))
#define LIBVLC_VERSION_INT LIBVLC_VERSION(LIBVLC_VERSION_MAJOR, LIBVLC_VERSION_MINOR, LIBVLC_VERSION_REVISION, LIBVLC_VERSION_EXTRA)
#endif
EOF
}

es() {
	toolchain
	local build=$WORK/obj/es
	# the static libraries in the order they need each other
	local sdl="$PREFIX/lib/libSDL2.a;$PREFIX/lib/libasound.a;m;pthread"
	cmake -S "$ROOT" -B "$build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TOOLS/armhf.cmake" -DCMAKE_BUILD_TYPE=Release \
		-DMISTER=ON -DCEC=OFF -DENABLE_PULSE=OFF \
		-DCMAKE_EXE_LINKER_FLAGS="-static" \
		-DSDL2_INCLUDE_DIR="$PREFIX/include/SDL2" -DSDL2_LIBRARY="$sdl" \
		-DSDLMIXER_INCLUDE_DIR="$PREFIX/include/SDL2" -DSDLMIXER_LIBRARY="$PREFIX/lib/libSDL2_mixer.a" \
		-DFREETYPE_INCLUDE_DIRS="$PREFIX/include/freetype2" -DFREETYPE_LIBRARY="$PREFIX/lib/libfreetype.a" \
		-DCURL_INCLUDE_DIR="$PREFIX/include" -DCURL_LIBRARY="$PREFIX/lib/libcurl.a" \
		-DALSA_INCLUDE_DIR="$PREFIX/include" -DALSA_LIBRARY="$PREFIX/lib/libasound.a" \
		-DRAPIDJSON_INCLUDE_DIRS="$PREFIX/include" -DVLC_INCLUDE_DIR="$PREFIX/include" \
		-DUDEV_FOUND=OFF >/dev/null
	cmake --build "$build" -j "$JOBS" --target emulationstation
	"$TOOLS/zcc" --version >/dev/null 2>&1 || true
	# the binary lands in the source folder, as ES's CMake puts it; keep the ARM one apart
	mkdir -p "$WORK/out"
	mv -f "$ROOT/emulationstation" "$WORK/out/emulationstation"
	git -C "$ROOT" checkout -q -- locale/ 2>/dev/null || true
	file "$WORK/out/emulationstation"
}

case "${1:-all}" in
deps) deps ;;
es) es ;;
package) "$ROOT/mister/package.sh" ;;
all)
	deps
	es
	"$ROOT/mister/package.sh"
	;;
*)
	echo "usage: $0 [deps|es|package|all]" >&2
	exit 1
	;;
esac
