#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
scan_roots=(stdlib tests tools examples templates packages benchmarks)

if ! command -v node >/dev/null 2>&1; then
  echo "node was not found; JS syntax lint cannot run." >&2
  exit 1
fi

is_js_syntax_path() {
  local path="$1"
  case "$path" in
    */node_modules/*|node_modules/*|build/*|artifacts/*|compiler/target/*|target/*|.sdeps/*|.sloppy/*)
      return 1
      ;;
  esac
  case "$path" in
    *.js|*.mjs|*.cjs) return 0 ;;
    *) return 1 ;;
  esac
}

files=()
if command -v git >/dev/null 2>&1; then
  while IFS= read -r path; do
    if [[ -f "$repo_root/$path" ]] && is_js_syntax_path "$path"; then
      files+=("$path")
    fi
  done < <(cd "$repo_root" && git ls-files --cached --others --exclude-standard -- "${scan_roots[@]}")
fi

if [[ "${#files[@]}" -eq 0 ]]; then
  while IFS= read -r path; do
    relative="${path#"$repo_root/"}"
    if is_js_syntax_path "$relative"; then
      files+=("$relative")
    fi
  done < <(find "${scan_roots[@]/#/$repo_root/}" -type f 2>/dev/null)
fi

failures=()
for file in "${files[@]}"; do
  if ! output="$(cd "$repo_root" && node --check "$file" 2>&1)"; then
    failures+=("$file"$'\n'"$output")
  fi
done

if [[ "${#failures[@]}" -gt 0 ]]; then
  echo "JS syntax violations found:" >&2
  for failure in "${failures[@]}"; do
    echo "$failure" >&2
  done
  exit 1
fi

echo "JS syntax check passed (${#files[@]} files)."
