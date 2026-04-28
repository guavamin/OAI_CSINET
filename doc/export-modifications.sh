#!/bin/bash
# Export current modifications for transfer to another repo.
#
# Usage:
#   ./doc/export-modifications.sh [OPTIONS] [BASE_REF] [OUTDIR]
#
# OPTIONS:
#   --worktree, -w   Export working tree vs BASE_REF (default BASE: HEAD).
#                    Writes: working-tree.patch (uncommitted changes only if BASE=HEAD).
#   --help, -h       Show this help.
#
# Without --worktree:
#   Exports committed range BASE_REF..HEAD (default BASE: HEAD~1).
#
# OUTDIR defaults to ../oai-modifications-export
#
# Apply in a fresh clone at the same BASE commit:
#   git apply --check OUTDIR/full-diff.patch && git apply OUTDIR/full-diff.patch
#
set -e
WORKTREE=false
while [ $# -gt 0 ]; do
  case "$1" in
    --worktree|-w)
      WORKTREE=true
      shift
      ;;
    --help|-h)
      sed -n '2,25p' "$0" | sed 's/^# //'
      exit 0
      ;;
    *)
      break
      ;;
  esac
done

BASE="${1:-}"
OUTDIR="${2:-../oai-modifications-export}"

if [ "$WORKTREE" = true ]; then
  # Args: [BASE] [OUTDIR] — BASE must be a valid ref if two args; if one arg is not a ref, treat as OUTDIR
  if [ $# -eq 0 ]; then
    BASE="HEAD"
    OUTDIR="../oai-modifications-export"
  elif [ $# -eq 1 ]; then
    if git rev-parse "$1^{commit}" >/dev/null 2>&1; then
      BASE="$1"
      OUTDIR="../oai-modifications-export"
    else
      BASE="HEAD"
      OUTDIR="$1"
    fi
  else
    BASE="$1"
    OUTDIR="$2"
  fi
  mkdir -p "$OUTDIR"
  # Working tree vs index+HEAD: uncommitted changes relative to BASE commit/tree
  git diff "$BASE" > "$OUTDIR/working-tree.patch" || true
  git diff "$BASE" --stat > "$OUTDIR/changed-files.txt"
  echo "Base tree: $BASE" > "$OUTDIR/README.txt"
  echo "Mode: working tree (git diff $BASE)" >> "$OUTDIR/README.txt"
  echo "Exported: $(date -Iseconds)" >> "$OUTDIR/README.txt"
  echo "Recorded BASE commit: $(git rev-parse "$BASE" 2>/dev/null || echo '?')" >> "$OUTDIR/README.txt"
  echo "Exported to $OUTDIR"
  echo "To apply on a clean repo at commit $(git rev-parse "$BASE"): git apply $OUTDIR/working-tree.patch"
  exit 0
fi

# Committed range (original behaviour)
BASE="${1:-HEAD~1}"
OUTDIR="${2:-../oai-modifications-export}"
mkdir -p "$OUTDIR"
git diff "$BASE"..HEAD > "$OUTDIR/full-diff.patch" || true
git diff "$BASE"..HEAD --stat > "$OUTDIR/changed-files.txt"
git log --oneline "$BASE"..HEAD > "$OUTDIR/commits.txt"
echo "Base: $BASE" > "$OUTDIR/README.txt"
echo "Exported: $(date -Iseconds)" >> "$OUTDIR/README.txt"
echo "Exported to $OUTDIR (base: $BASE)"
echo "To apply in a clean clone at same base: git apply $OUTDIR/full-diff.patch"
