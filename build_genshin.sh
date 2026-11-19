#!/bin/zsh
set -eu

project_dir="$(cd "$(dirname "$0")" && pwd)"
output_dir="${1:-$project_dir/.build}"
mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd)"
sdk_dir="$(xcrun --sdk macosx --show-sdk-path)"
plugin_file="$output_dir/GenshinDesktopMode_R46_Basic.dylib"
motion_flags=(-DGDM_EMBEDDED_MOTION_PACK=0)

if [[ -n ${GDM_MOTION_PACK:-} ]]; then
    motion_pack="$GDM_MOTION_PACK"
    if [[ ! -f "$motion_pack" ]]; then
        printf 'Motion pack not found: %s\n' "$motion_pack" >&2
        exit 1
    fi
    expected_hash="$(sed -n 's/.*NativeResourceSHA256\[\]="\([a-f0-9]*\)".*/\1/p' "$project_dir/genshin/resource_manifest.h")"
    actual_hash="$(shasum -a 256 < "$motion_pack" | awk '{print $1}')"
    if [[ -z "$expected_hash" || "$actual_hash" != "$expected_hash" ]]; then
        printf 'Motion pack SHA-256 does not match this source revision.\n' >&2
        exit 1
    fi
    plugin_file="$output_dir/GenshinDesktopMode_R46.dylib"
    motion_flags=(-DGDM_EMBEDDED_MOTION_PACK=1 -Xlinker -sectcreate
        -Xlinker __GDM -Xlinker __motion_pack -Xlinker "$motion_pack")
fi

xcrun clang++ -dynamiclib -std=c++17 -O2 -fobjc-arc \
    "${motion_flags[@]}" \
    -target arm64-apple-ios15.0-macabi -isysroot "$sdk_dir" \
    -iframework "$sdk_dir/System/iOSSupport/System/Library/Frameworks" \
    -F"$sdk_dir/System/iOSSupport/System/Library/Frameworks" \
    -framework Foundation -framework UIKit -framework GameController \
    -framework CoreGraphics -framework QuartzCore -framework CoreHaptics \
    -framework CoreAudio -framework AudioToolbox -framework IOKit \
    -Wno-deprecated-declarations \
    -install_name '@rpath/libGenshinDesktopMode.dylib' \
    -o "$plugin_file" "$project_dir/genshin/probe.mm"
codesign --force --sign - --identifier local.genshin.desktopmode.r46 "$plugin_file"
codesign --verify --strict "$plugin_file"
printf '%s\n' "$plugin_file"
