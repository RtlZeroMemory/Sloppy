#!/usr/bin/env bash
set -euo pipefail

preset="${SLOPPY_CMAKE_PRESET:-linux-clang}"
no_docker=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --preset)
      preset="$2"
      shift 2
      ;;
    --no-docker)
      no_docker=1
      shift
      ;;
    *)
      printf 'usage: %s [--preset <cmake-preset>] [--no-docker]\n' "$0" >&2
      exit 2
      ;;
  esac
done

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
compose_file="$repo_root/tests/live/postgres/compose.yml"
export SLOPPY_POSTGRES_TEST_URL="${SLOPPY_POSTGRES_TEST_URL:-postgres://sloppy:sloppy@localhost:55432/sloppy_test}"

if [ "$no_docker" -eq 0 ]; then
  if ! command -v docker >/dev/null 2>&1; then
    printf 'UNAVAILABLE: Docker CLI is required for the PostgreSQL live-provider lane.\n' >&2
    exit 1
  fi
  docker info >/dev/null
  docker compose -f "$compose_file" up -d --wait
fi

cleanup() {
  if [ "$no_docker" -eq 0 ]; then
    docker compose -f "$compose_file" down -v
  fi
}
trap cleanup EXIT

cmake --build --preset "$preset"
ctest --test-dir "$repo_root/build/$preset" --output-on-failure -R 'data\.postgres\.live_provider|conformance\.postgres\.(native_live|bridge_live)'
