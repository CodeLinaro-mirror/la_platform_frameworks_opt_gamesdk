"""Repository-relative path canonicalization, SDK source policy, module table."""

from dataclasses import dataclass
from pathlib import Path, PurePosixPath
import posixpath

# Top-level directories that never hold SDK sources.
_EXCLUDED_TOP_LEVEL = frozenset(
    {"test", "samples", "third_party", "buildSrc", "tools", "hooks"}
)
# Directory names anywhere below a module that hold tests or build output.
_EXCLUDED_SEGMENTS = frozenset({"test", "androidTest", ".cxx", "build"})


@dataclass(frozen=True)
class Module:
    """An AGDK module and where its coverage inputs live, repo-relative."""

    name: str
    native_build_dirs: tuple[str, ...]
    jvm_source_roots: tuple[str, ...]


# The modules whose tests CI instruments (`./build.sh testApks`). Swappy's
# instrumented tests live in a standalone test app project.
MODULES: tuple[Module, ...] = (
    Module(
        "games-frame-pacing",
        ("games-frame-pacing/.cxx", "test/swappy/testapp/app/.cxx"),
        ("games-frame-pacing/extras/src/main/java",),
    ),
    Module(
        "games-controller",
        ("games-controller/.cxx",),
        ("games-controller/src/main/java",),
    ),
    Module(
        "game-activity",
        ("game-activity/.cxx",),
        ("game-activity/src/main/java",),
    ),
    Module(
        "game-text-input",
        ("game-text-input/.cxx",),
        ("game-text-input/src/main/java",),
    ),
)

def native_build_dirs(repo: Path) -> list[Path]:
    """Default .gcno/.gcda locations: every instrumented module .cxx tree."""
    return [repo / d for module in MODULES for d in module.native_build_dirs]


def jvm_source_roots(repo: Path) -> list[Path]:
    return [repo / root for module in MODULES for root in module.jvm_source_roots]


def is_sdk_source(rel_path: str) -> bool:
    """Returns False for test, sample, third-party, tooling, and build paths."""
    parts = PurePosixPath(rel_path).parts
    if not parts or parts[0] in _EXCLUDED_TOP_LEVEL:
        return False
    return not any(part in _EXCLUDED_SEGMENTS for part in parts[1:-1])


def resolve_in_repo(repo: Path, rel_path: str) -> str | None:
    """Resolves symlinks of a repo-relative path; None if it leaves the repo."""
    resolved = (repo / rel_path).resolve()
    try:
        return resolved.relative_to(repo.resolve()).as_posix()
    except ValueError:
        return None


# Outcomes of mapping a compiler-recorded path (SourceMapper, ManifestMapper).
OK = "ok"  # repository SDK source; the canonical path is returned with it
EXTERNAL = "external"  # outside the build root (sysroot, prebuilts, external/)
EXCLUDED = "excluded"  # inside the repository but not SDK code (tests, ...)
UNKNOWN = "unknown"  # relative path, or a symlink that leaves the repository


class SourceMapper:
    """Maps compiler-recorded source paths to repository-relative SDK paths.

    `llvm-cov gcov` emits the paths the compiler recorded: absolute for
    CMake builds, which run the compiler in the build directory. `build_root`
    is the absolute repository root at compile time; it differs from `repo`
    when the report runs on another machine than the build. `repo` is the
    checkout used to resolve symlinks (prefab-src copies, include/ links).
    """

    def __init__(self, build_root: str, repo: Path | None) -> None:
        self._root = posixpath.normpath(build_root.replace("\\", "/"))
        self._repo = repo

    def __call__(self, recorded: str) -> tuple[str, str | None]:
        normalized = posixpath.normpath(recorded.strip().replace("\\", "/"))
        if not posixpath.isabs(normalized):
            return UNKNOWN, None
        if not normalized.startswith(self._root + "/"):
            return EXTERNAL, None
        rel = normalized[len(self._root) + 1 :]
        canonical = resolve_in_repo(self._repo, rel) if self._repo else rel
        if canonical is None:
            return UNKNOWN, None
        if not is_sdk_source(canonical):
            return EXCLUDED, None
        return OK, canonical
