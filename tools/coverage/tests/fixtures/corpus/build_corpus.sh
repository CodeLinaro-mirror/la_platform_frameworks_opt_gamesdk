#!/bin/bash
# Regenerates the checked-in GCOV corpus (notes/, pulled/, metadata.json) from
# tree/ with the NDK host clang. Run it only when tree/ changes or a newer
# compiler must be exercised; the test suite consumes the checked-in output
# and never compiles anything.
#
# Usage: ./build_corpus.sh [path/to/ndk]
set -euo pipefail

CORPUS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GAMESDK_DIR="$(cd "$CORPUS_DIR" && git rev-parse --show-toplevel)"
NDK_DIR="${1:-${ANDROID_NDK_HOME:-$GAMESDK_DIR/../prebuilts/ndk/r27}}"
CLANG="$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/bin/clang"
if [ ! -x "$CLANG" ]; then
    echo "clang not found at $CLANG; pass the NDK path as the first argument" >&2
    exit 1
fi

# Fixed, machine-neutral build root. It is recorded in every .gcno, so the
# test maps it back onto a copy of tree/ instead of this checkout.
WORK=/tmp/agdk_coverage_corpus
ROOT="$WORK/tree/gamesdk"
rm -rf "$WORK"
mkdir -p "$WORK"
cp -a "$CORPUS_DIR/tree" "$WORK/tree"

CFLAGS=(--coverage -O0 -g -no-canonical-prefixes)
SWAPPY_BUILD="$ROOT/games-frame-pacing/.cxx/Debug/h1/x86_64"
GA_BUILD="$ROOT/game-activity/.cxx/Debug/h2/x86_64"
TEST_BUILD="$ROOT/test/swappy/testapp/app/.cxx/Debug/h3/x86_64"
SWAPPY_CMAKE="$SWAPPY_BUILD/CMakeFiles"
GA_CMAKE="$GA_BUILD/CMakeFiles"
TEST_CMAKE="$TEST_BUILD/CMakeFiles"

# compile <build dir> <absolute source> <object path> [extra flags]
# Like the CMake Ninja generator, the compiler runs in the build directory
# with absolute source paths, so every recorded source path is absolute.
compile() {
    local build_dir="$1" src="$2" obj="$3"
    shift 3
    mkdir -p "$(dirname "$obj")"
    (cd "$build_dir" && "$CLANG" "${CFLAGS[@]}" "$@" -c "$src" -o "$obj")
}

SWAPPY_INC=(-I"$ROOT/games-frame-pacing/include" -I"$ROOT/src/common")
compile "$SWAPPY_BUILD" "$ROOT/games-frame-pacing/common/alpha.c" \
    "$SWAPPY_CMAKE/swappy.dir/common/alpha.c.o" "${SWAPPY_INC[@]}"
compile "$SWAPPY_BUILD" "$ROOT/games-frame-pacing/common/alpha.c" \
    "$SWAPPY_CMAKE/swappy_static.dir/common/alpha.c.o" "${SWAPPY_INC[@]}"
compile "$SWAPPY_BUILD" "$ROOT/games-frame-pacing/common/never.c" \
    "$SWAPPY_CMAKE/swappy_static.dir/common/never.c.o"
# CMake mirrors out-of-tree sources under <target>.dir/<absolute path>.
compile "$SWAPPY_BUILD" "$ROOT/src/common/util.c" \
    "$SWAPPY_CMAKE/swappy_static.dir$ROOT/src/common/util.c.o"

GA_SRC="$ROOT/game-activity/prefab-src/modules/game-activity/src"
GA_INC=(-I"$ROOT/src/common" -I"$ROOT/../external")
# The same util.c, reached through the prefab-src symlink.
compile "$GA_BUILD" "$GA_SRC/common/util.c" \
    "$GA_CMAKE/game-activity_static.dir/prefab-src/modules/game-activity/src/common/util.c.o" \
    "${GA_INC[@]}"
compile "$GA_BUILD" "$GA_SRC/game-activity/beta.c" \
    "$GA_CMAKE/game-activity_static.dir/prefab-src/modules/game-activity/src/game-activity/beta.c.o" \
    "${GA_INC[@]}"
compile "$GA_BUILD" "$ROOT/game-activity/src/androidTest/cpp/main_b.c" \
    "$GA_CMAKE/gameactivity_test_native.dir/src/androidTest/cpp/main_b.c.o"
compile "$TEST_BUILD" "$ROOT/test/swappy/main_a.c" \
    "$TEST_CMAKE/swappy_test.dir/main_a.c.o"

# Host-style run: .gcda files land next to their .gcno notes.
"$CLANG" --coverage -o "$WORK/exe_a" \
    "$TEST_CMAKE/swappy_test.dir/main_a.c.o" \
    "$SWAPPY_CMAKE/swappy_static.dir/common/alpha.c.o" \
    "$SWAPPY_CMAKE/swappy_static.dir/common/never.c.o" \
    "$SWAPPY_CMAKE/swappy_static.dir$ROOT/src/common/util.c.o"
"$WORK/exe_a"

# Device-style run: GCOV_PREFIX_STRIP flattens the .gcda files into one
# directory, which TradeFed then pulls as a zip archive.
"$CLANG" --coverage -o "$WORK/exe_b" \
    "$GA_CMAKE/gameactivity_test_native.dir/src/androidTest/cpp/main_b.c.o" \
    "$GA_CMAKE/game-activity_static.dir/prefab-src/modules/game-activity/src/common/util.c.o" \
    "$GA_CMAKE/game-activity_static.dir/prefab-src/modules/game-activity/src/game-activity/beta.c.o"
mkdir -p "$WORK/pulled"
GCOV_PREFIX="$WORK/pulled" GCOV_PREFIX_STRIP=20 "$WORK/exe_b"

rm -rf "$CORPUS_DIR/notes" "$CORPUS_DIR/pulled"
mkdir -p "$CORPUS_DIR/notes" "$CORPUS_DIR/pulled"
(cd "$ROOT" && find . \( -name '*.gcno' -o -name '*.gcda' \) -print0 \
    | cpio -0 -pdm --quiet "$CORPUS_DIR/notes")
(cd "$WORK/pulled" && zip -q -j "$CORPUS_DIR/pulled/files_1.zip" ./*.gcda)

python3 - "$CORPUS_DIR/metadata.json" "$ROOT" "$("$CLANG" --version | head -n 1)" <<'EOF'
import json
import sys
out, root, compiler = sys.argv[1:]
with open(out, "w", encoding="utf-8") as f:
    json.dump({"build_root": root, "compiler": compiler}, f, indent=2)
    f.write("\n")
EOF
echo "Corpus regenerated under $CORPUS_DIR (build root $ROOT)."
