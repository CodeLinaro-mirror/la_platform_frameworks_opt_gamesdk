"""Changed line numbers of a git revision range, keyed by repository path."""

from pathlib import Path
import re
import subprocess

from agdk_coverage import paths

GIT_TIMEOUT_SECONDS = 60
_HUNK_RE = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")


def parse_unified_diff(diff_text: str) -> dict[str, set[int]]:
    """Parses `git diff --unified=0` output into {path: {added or modified}}."""
    changed: dict[str, set[int]] = {}
    current: str | None = None
    for line in diff_text.splitlines():
        if line.startswith("+++ "):
            target = line[4:].strip()
            if target == "/dev/null":
                current = None
            else:
                current = target[2:] if target.startswith("b/") else target
            continue
        match = _HUNK_RE.match(line) if current is not None else None
        if match is None:
            continue
        start = int(match.group(1))
        count = int(match.group(2)) if match.group(2) is not None else 1
        if count > 0:
            changed.setdefault(current, set()).update(range(start, start + count))
    return changed


def changed_lines(repo: Path, base: str) -> dict[str, set[int]]:
    """Lines added or modified since `base`, keyed by canonical repo path.

    Raises:
        ValueError: `base` is not a commit of `repo` (for example in a
            shallow clone), so no diff can be attributed.
        subprocess.CalledProcessError: git failed for another reason.
    """
    probe = subprocess.run(
        ["git", "-C", str(repo), "rev-parse", "--verify", "--quiet", f"{base}^{{commit}}"],
        capture_output=True,
        text=True,
        check=False,
        timeout=GIT_TIMEOUT_SECONDS,
    )
    if probe.returncode != 0:
        raise ValueError(f"{base} is not a commit of {repo}")
    diff = subprocess.run(
        ["git", "-C", str(repo), "diff", "--unified=0", "--no-color", "--no-ext-diff", base],
        capture_output=True,
        text=True,
        check=True,
        timeout=GIT_TIMEOUT_SECONDS,
    )
    return {
        paths.resolve_in_repo(repo, path) or path: lines
        for path, lines in parse_unified_diff(diff.stdout).items()
    }
