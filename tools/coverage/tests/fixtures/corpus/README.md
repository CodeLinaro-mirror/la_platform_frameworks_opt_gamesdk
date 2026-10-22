# GCOV corpus

A small, real `.gcno`/`.gcda` set compiled once by `build_corpus.sh` with the
NDK host clang from the sources under `tree/`. `test_coverage_corpus.py` runs
the real `llvm-cov gcov` on it and compares the per-file line counts with the
pinned `expected_v2.json` file, so changes to note pairing, path mapping, or
exclusion policy show up as a reviewed fixture diff instead of an empty
report in CI.

Every recorded source path starts with the build root stored in
`metadata.json` (`/tmp/agdk_coverage_corpus/tree/gamesdk`); tests map that
root onto a temporary copy of `tree/gamesdk`.

## Cases and the repository situation they mirror

| Corpus | Mirrors |
| :--- | :--- |
| `games-frame-pacing/common/alpha.c` compiled into `swappy.dir` and `swappy_static.dir` (byte-identical notes, only the static one executed) | every Swappy source built into `swappy` and `swappy_static` |
| `src/common/util.c` compiled by absolute path from `games-frame-pacing` and through the `game-activity/prefab-src/.../src/common/util.c` symlink (same stamp, different note bytes) | `src/common/system_utils.cpp` |
| `include/swappy/swappy_api.h` included through the `games-frame-pacing/include/swappy` symlinked directory | `games-frame-pacing/include/swappy` |
| `src/common/util.h` with an inline function that no run executes | shared headers with inline helpers |
| `tree/external/ext.h` outside the repository root | `prebuilts/.../sysroot` and `external/` headers |
| `games-frame-pacing/common/never.c` linked but never called; the duplicate `swappy.dir` note without any `.gcda` | 0%-covered units that must stay in the denominator |
| `test/swappy/main_a.c`, `game-activity/src/androidTest/cpp/main_b.c` | test sources excluded by policy |
| host run: `.gcda` next to the notes under `.cxx/` | `./build.sh hostUnitTests` |
| device run: `GCOV_PREFIX_STRIP` flattens `.gcda` into `pulled/files_1.zip` | TradeFed `FilePullerLogCollector` archives |

## Pinned expectations

`expected_v2.json` is the `agdk_coverage` result: repository-relative keys,
one `src/common/util.c` row whose counts are the sum of the host run and
the device run, and `include/swappy/swappy_api.h` resolved through the
symlinked include directory.

## Regenerating

```bash
tools/coverage/tests/fixtures/corpus/build_corpus.sh [path/to/ndk]
```

Then rerun the corpus test and review the resulting `expected_v2.json` diff.
