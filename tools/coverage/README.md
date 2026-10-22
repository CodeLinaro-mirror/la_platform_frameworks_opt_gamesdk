# AGDK code coverage tool

`tools/coverage` turns the gcov (`--coverage`) and JaCoCo data produced by the
AGDK test suites into one report per build: `coverage_summary.json` for the
`AgdkCoverage` Gerrit presubmit chip and a static HTML drill-down. It covers
every module that CI instruments (`games-frame-pacing`, `games-controller`,
`game-activity`, `game-text-input`; see `MODULES` in
[`agdk_coverage/paths.py`](agdk_coverage/paths.py)).

```text
python3 tools/coverage report --out DIR [--diff-base REV] [--jacoco-xml FILE]...
        [--native-build-dir DIR]... [--gcda-dir DIR|ZIP]... [--bundle FILE]
        [--repo ROOT] [--build-root ROOT] [--llvm-cov BIN]
python3 tools/coverage bundle --out FILE [--diff-base REV]
        [--native-build-dir DIR]... [--with-sources]
```

## Data flow

| Stage | Produces | Consumed by |
| :--- | :--- | :--- |
| `./build.sh hostUnitTests --coverage` (or `hostUnitTests` followed by `hostCoverageReport`) runs the host GTest and GameActivity JVM tests with `-PenableCoverage=true`, then `coverageTests` and `hostCoverageReport` | `host_coverage/coverage_summary.json` + `index.html` | presubmit analyzer, developers |
| `./build.sh testApks` builds the instrumented test APKs, then `coverageBundle` | `agdk_coverage_bundle.tar.gz` (manifest + `.gcno` notes) | presubmit analyzer |
| ATP `cuttlefish_gamesdk_tests` runs the APKs; TradeFed pulls `.gcda` files | `files_*.zip` in AnTS | presubmit analyzer |
| google3 `AgdkCoverage` analyzer | pairs `.gcda` with the bundled notes, runs `llvm-cov`, merges with the host summary | Gerrit chip and inline findings |

`check_coverage_artifacts.py` fails the CI build when a report has no
executed line for a language or the bundle misses a module.

## How paths are resolved

Every file key is repository-relative with symlinks resolved, so
`game-activity/prefab-src/.../src/common/system_utils.cpp` and
`src/common/system_utils.cpp` are the same file. `llvm-cov gcov` emits the
absolute paths the compiler recorded; `SourceMapper` strips the build root
(`--build-root`, the checkout itself by default), resolves symlinks in the
checkout, and applies the SDK policy (`test/`, `samples/`, `third_party/`,
`buildSrc/`, `tools/`, `androidTest/`, `src/test/` are not SDK code). Paths
outside the build root (sysroot, `prebuilts/`, `external/`) are dropped.

A compilation unit is identified by its stamp (the CFG checksum clang
writes into both `.gcno` and `.gcda`) plus the repository files it covers,
so the same source built into several CMake targets or reached through a
symlink counts once, and every `.gcda` with a matching stem and stamp is
applied exactly once. The `gcov` block of the summary reports the counters
(`notes`, `units`, `paired_gcda`, `unmatched_gcda`, `unknown_files`).

The bundle manifest (`manifest.json`, `schema_version` 2) carries the
`files` table from a notes-only `llvm-cov` pass, so a consumer without the
checkout never maps paths itself.

## Tests

```bash
./build.sh coverageTests
python3 -m unittest discover -s tools/coverage/tests -t tools/coverage
```

`tests/fixtures/corpus/` holds a small real `.gcno`/`.gcda` set (see its
README); the corpus tests need `llvm-cov` from the NDK and fail in CI when
it is missing (`AGDK_COVERAGE_REQUIRE_LLVM_COV=1`).
