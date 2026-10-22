"""Reads JaCoCo XML reports into repository-relative line counts."""

from collections.abc import Iterable, Sequence
from pathlib import Path
import xml.etree.ElementTree as ET

from agdk_coverage import paths


def parse(
    xml_paths: Iterable[Path], repo: Path, source_roots: Sequence[Path]
) -> tuple[dict[str, dict[int, int]], list[str]]:
    """Returns ({repo-relative path: {line: covered instructions}}, unknown).

    Each `<package name>/<sourcefile name>` is located under the module JVM
    source roots; `unknown` lists the keys that exist under none of them.
    """
    files: dict[str, dict[int, int]] = {}
    unknown: set[str] = set()
    for xml_path in xml_paths:
        root = ET.parse(xml_path).getroot()
        for package in root.iter("package"):
            package_name = package.get("name", "").strip("/")
            for source in package.findall("sourcefile"):
                name = source.get("name", "")
                key = f"{package_name}/{name}" if package_name else name
                canonical = _locate(key, repo, source_roots)
                if canonical is None:
                    unknown.add(key)
                    continue
                lines = files.setdefault(canonical, {})
                for line in source.findall("line"):
                    line_no = int(line.get("nr", "0"))
                    if line_no > 0:
                        lines[line_no] = lines.get(line_no, 0) + int(
                            line.get("ci", "0")
                        )
    return files, sorted(unknown)


def _locate(key: str, repo: Path, source_roots: Sequence[Path]) -> str | None:
    for root in source_roots:
        candidate = root / key
        if candidate.is_file():
            return paths.resolve_in_repo(repo, str(candidate.relative_to(repo)))
    return None
