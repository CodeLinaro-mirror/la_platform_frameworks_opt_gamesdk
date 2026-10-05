#!/bin/bash

COMMIT="${1:-HEAD}"
shift || true

PY_FILES=()
for f in "$@"; do
  if [[ "$f" == *.py ]] && [ -f "$f" ]; then
    PY_FILES+=("$f")
  fi
done

if [ "${#PY_FILES[@]}" -eq 0 ]; then
  exit 0
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_TOP="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# Same pylintrc the builtin repohooks pylint3 hook uses.
PYLINTRC="${REPO_TOP}/tools/repohooks/tools/pylintrc"
if [ ! -f "$PYLINTRC" ]; then
  echo "pylintrc not found at ${PYLINTRC}" >&2
  exit 1
fi

if ! command -v pylint >/dev/null 2>&1; then
  echo "pylint is not installed" >&2
  exit 1
fi

LINT_OUTPUT=$(pylint --rcfile "$PYLINTRC" "${PY_FILES[@]}" 2>&1)
RET=$?

if [ "$RET" -eq 0 ]; then
  exit 0
fi

report_and_fail() {
  echo "pylint found issues in commit ${COMMIT:0:12}:"
  printf '%s\n' "$LINT_OUTPUT"
  exit 1
}

# pylint has no --fix mode. Like git-clang-format, ruff reformats only the
# lines the commit touched (fixing line-too-long and bad wrapping there), then
# fixes the rules that map 1:1 to pylint messages: unused-import (F401),
# f-string-without-interpolation (F541), trailing-whitespace (W291, W293) and
# missing-final-newline (W292). Anything else is left to the author.
if ! command -v ruff >/dev/null 2>&1 || ! { : >/dev/tty; } 2>/dev/null; then
  report_and_fail
fi

# Prints the line ranges that the commit added or modified in $1 as ruff
# "--range start-end" values, where end is the first line after the hunk (ruff
# treats it as line:column 1, excluding that line's content). Ranges are
# printed bottom-up so formatting one keeps earlier line numbers valid.
changed_ranges() {
  git diff -U0 "${COMMIT}^" "${COMMIT}" -- "$1" 2>/dev/null | \
    sed -nE 's/^@@ -[0-9,]+ \+([0-9]+)(,([0-9]+))? @@.*/\1 \3/p' | \
    while read -r start count; do
      count="${count:-1}"
      if [ "$count" -gt 0 ]; then
        echo "${start}-$((start + count))"
      fi
    done | tac
}

TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT

TMP_FILES=()
for f in "${PY_FILES[@]}"; do
  mkdir -p "$(dirname "$TMP_DIR/$f")"
  cp -p "$f" "$TMP_DIR/$f"
  TMP_FILES+=("$TMP_DIR/$f")
  for range in $(changed_ranges "$f"); do
    ruff format --isolated --line-length 80 \
      --config "format.quote-style='preserve'" \
      --range "$range" "$TMP_DIR/$f" >/dev/null 2>&1 || true
  done
done

ruff check --isolated --fix --select F401,F541,W291,W292,W293 \
  "${TMP_FILES[@]}" >/dev/null 2>&1 || true

FIX_DIFF=""
for f in "${PY_FILES[@]}"; do
  if ! cmp -s "$f" "$TMP_DIR/$f"; then
    FIX_DIFF+="$(git diff --no-index -- "$f" "$TMP_DIR/$f" | \
      sed -e "s|^diff --git .*|diff --git a/$f b/$f|" \
          -e "s|^+++ .*|+++ b/$f|")"$'\n'
  fi
done

if [ -z "$FIX_DIFF" ]; then
  report_and_fail
fi

{
  echo ""
  echo "pylint found issues in commit ${COMMIT:0:12}:"
  printf '%s\n' "$LINT_OUTPUT"
  echo ""
  echo "Proposed ruff auto-fix:"
  printf '%s\n' "$FIX_DIFF"
  echo ""
} >/dev/tty

while true; do
  printf "Apply ruff auto-fix for %s? (y/n): " "${COMMIT:0:12}" >/dev/tty
  if ! IFS= read -r REPLY </dev/tty; then
    echo "" >/dev/tty
    break
  fi
  case "$REPLY" in
    [Yy]|[Yy][Ee][Ss])
      for f in "${PY_FILES[@]}"; do
        cp -p "$TMP_DIR/$f" "$f"
      done
      echo "ruff auto-fix applied to working tree for ${COMMIT:0:12};" \
        "amend the commit and upload again."
      exit 1
      ;;
    [Nn]|[Nn][Oo])
      break
      ;;
    *)
      echo "Please answer y or n." >/dev/tty
      ;;
  esac
done

report_and_fail
