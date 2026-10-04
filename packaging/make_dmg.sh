#!/usr/bin/env bash
#
# Builds the release GUI front-end and packages it into a drag-and-drop macOS
# installer image (.dmg).
#
#   packaging/make_dmg.sh [options]
#
# The image is self-contained: the Whisper and Silero models are copied into
# Podcast Cleaner.app/Contents/Resources/models, so profanity censoring works
# right after the app is dragged onto /Applications.

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "${script_dir}/.." && pwd)"

configuration="Release"
build_dir="${project_dir}/build-installer"
output_dir="${project_dir}/dist"
bundle_model=1
keep_staging=0

usage()
{
    cat <<'USAGE'
Usage: packaging/make_dmg.sh [options]

Options:
  -c, --configuration <name>  CMake build type (default: Release)
  -b, --build-dir <path>      build tree to use (default: build-installer)
  -o, --output-dir <path>     where the .dmg is written (default: dist)
  -n, --no-model              leave the Whisper model out (much smaller image)
  -k, --keep-staging          keep the staging folder for inspection
  -h, --help                  show this text
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -c|--configuration) configuration="${2:?missing value for $1}"; shift 2 ;;
        -b|--build-dir)     build_dir="${2:?missing value for $1}";     shift 2 ;;
        -o|--output-dir)    output_dir="${2:?missing value for $1}";    shift 2 ;;
        -n|--no-model)      bundle_model=0; shift ;;
        -k|--keep-staging)  keep_staging=1; shift ;;
        -h|--help)          usage; exit 0 ;;
        *) echo "error: unknown option '$1'" >&2; usage >&2; exit 2 ;;
    esac
done

for tool in hdiutil codesign iconutil clang; do
    command -v "${tool}" >/dev/null 2>&1 || { echo "error: '${tool}' is required" >&2; exit 1; }
done

# CMake is often installed outside PATH (e.g. CMake.app, or a tools folder used
# by an IDE), so look in the usual places instead of relying on PATH alone.
find_cmake()
{
    if [[ -n "${CMAKE:-}" && -x "${CMAKE}" ]]; then
        printf '%s\n' "${CMAKE}"
        return 0
    fi

    if [[ -f "${build_dir}/CMakeCache.txt" ]]; then
        local cached
        cached="$(sed -n 's/^CMAKE_COMMAND:[^=]*=\(.*\)$/\1/p' "${build_dir}/CMakeCache.txt" | head -n 1)"
        if [[ -x "${cached}" ]]; then
            printf '%s\n' "${cached}"
            return 0
        fi
    fi

    if command -v cmake >/dev/null 2>&1; then
        command -v cmake
        return 0
    fi

    local candidate
    for candidate in \
        "/Applications/CMake.app/Contents/bin/cmake" \
        "${HOME}/Applications/CMake.app/Contents/bin/cmake" \
        /opt/homebrew/bin/cmake \
        /usr/local/bin/cmake
    do
        if [[ -x "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done

    # Bundled cmake copies, as used by IDE extension tool folders.
    for candidate in "${HOME}"/Library/Developer/*/*/BuildTools/cmake-*/CMake.app/Contents/bin/cmake; do
        if [[ -x "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done

    return 1
}

if ! cmake="$(find_cmake)"; then
    echo "error: 'cmake' was not found." >&2
    echo "       Install it, add it to PATH, or point the CMAKE variable at it." >&2
    exit 1
fi
echo "==> Using CMake at ${cmake}"

version="$(sed -n 's/^[[:space:]]*VERSION "\([0-9][0-9.]*\)".*/\1/p' \
               "${project_dir}/src/app/CMakeLists.txt" | head -n 1)"
version="${version:-0.0.0}"

model_source="${project_dir}/models"
if [[ ${bundle_model} -eq 1 && ! -f "${model_source}/ggml-base.bin" ]]; then
    echo "error: '${model_source}/ggml-base.bin' is missing." >&2
    echo "       Download it (see README) or pass --no-model for a model-free image." >&2
    exit 1
fi

echo "==> Configuring ${configuration} build in ${build_dir}"
"${cmake}" -S "${project_dir}/src/app" -B "${build_dir}" \
           -DCMAKE_BUILD_TYPE="${configuration}" \
           -DPODCASTCLEANER_BUILD_GUI=ON

echo "==> Building 'podcast-cleaner-gui'"
"${cmake}" --build "${build_dir}" --config "${configuration}" --target podcast-cleaner-gui --parallel

app_source="${build_dir}/podcast-cleaner-gui_artefacts/${configuration}/Podcast Cleaner.app"
if [[ ! -d "${app_source}" ]]; then
    echo "error: app bundle not found at '${app_source}'" >&2
    exit 1
fi

staging_dir="${build_dir}/dmg-staging"
echo "==> Staging in ${staging_dir}"
rm -rf "${staging_dir}"
mkdir -p "${staging_dir}"

app_dir="${staging_dir}/Podcast Cleaner.app"
/usr/bin/ditto "${app_source}" "${app_dir}"

# The models live inside the bundle so the installed app needs no extra setup.
if [[ ${bundle_model} -eq 1 ]]; then
    echo "==> Bundling speech recognition models"
    model_resources="${app_dir}/Contents/Resources/models"
    mkdir -p "${model_resources}"
    cp "${model_source}/ggml-base.bin" "${model_resources}/"
    for vad in "${model_source}"/ggml-silero-*.bin; do
        if [[ -f "${vad}" ]]; then
            cp "${vad}" "${model_resources}/"
        fi
    done
fi

# Drag-target so the user can install by dragging the app onto it.
ln -s /Applications "${staging_dir}/Applications"

echo "==> Rendering the app icon"
icon_tool="${build_dir}/make_icon"
clang -O2 -Wall -framework CoreGraphics -framework ImageIO -framework CoreFoundation \
      -o "${icon_tool}" "${script_dir}/make_icon.c"

iconset="${build_dir}/AppIcon.iconset"
rm -rf "${iconset}"
mkdir -p "${iconset}"

for size in 16 32 64 128 256 512 1024; do
    "${icon_tool}" "${size}" "${build_dir}/icon-${size}.png"
done

cp "${build_dir}/icon-16.png"   "${iconset}/icon_16x16.png"
cp "${build_dir}/icon-32.png"   "${iconset}/icon_16x16@2x.png"
cp "${build_dir}/icon-32.png"   "${iconset}/icon_32x32.png"
cp "${build_dir}/icon-64.png"   "${iconset}/icon_32x32@2x.png"
cp "${build_dir}/icon-128.png"  "${iconset}/icon_128x128.png"
cp "${build_dir}/icon-256.png"  "${iconset}/icon_128x128@2x.png"
cp "${build_dir}/icon-256.png"  "${iconset}/icon_256x256.png"
cp "${build_dir}/icon-512.png"  "${iconset}/icon_256x256@2x.png"
cp "${build_dir}/icon-512.png"  "${iconset}/icon_512x512.png"
cp "${build_dir}/icon-1024.png" "${iconset}/icon_512x512@2x.png"

iconutil -c icns "${iconset}" -o "${app_dir}/Contents/Resources/AppIcon.icns"

plist="${app_dir}/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleIconFile AppIcon" "${plist}" 2>/dev/null \
    || /usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string AppIcon" "${plist}"

# Ad-hoc signature: enough for a locally built app to launch without the
# "app is damaged" error. Distribution still needs an Apple developer identity.
echo "==> Signing the app"
codesign --force --sign - "${app_dir}"
codesign --verify --verbose=1 "${app_dir}"

cat > "${staging_dir}/Read Me.txt" <<TEXT
Podcast Cleaner ${version}
==================================================

Install
  1. Drag "Podcast Cleaner.app" onto the Applications shortcut.
  2. Open it from Launchpad, Spotlight or /Applications.

First launch
  This build is ad-hoc signed and not notarised, so macOS may refuse the
  first launch. Right-click the app in Finder, choose "Open" and confirm
  once - after that it starts normally.

What it does
  * spectral noise reduction with an adjustable strength
  * optional profanity censoring for Russian and English speech
  * loudness normalisation to -16 LUFS with a -1 dBFS true-peak ceiling

Everything runs locally and offline; nothing is uploaded. The speech
recognition model is already inside the app bundle.

Command line front-end
  The same pipeline is also available as a console tool. Build it from a
  source checkout with:
      cmake --build <build-dir> --target podcast-cleaner
TEXT

mkdir -p "${output_dir}"
dmg_path="${output_dir}/PodcastCleaner-${version}.dmg"

echo "==> Creating ${dmg_path}"
rm -f "${dmg_path}"
hdiutil create -volname "Podcast Cleaner ${version}" \
               -srcfolder "${staging_dir}" \
               -ov -format UDZO -quiet "${dmg_path}"

echo "==> Verifying the image"
hdiutil verify "${dmg_path}"

if [[ ${keep_staging} -eq 0 ]]; then
    rm -rf "${staging_dir}"
fi

echo
echo "Installer ready: ${dmg_path} ($(du -h "${dmg_path}" | cut -f1))"
echo "Open it and drag the app onto Applications to install."
