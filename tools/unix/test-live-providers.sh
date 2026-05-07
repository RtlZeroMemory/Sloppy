#!/usr/bin/env bash
set -euo pipefail

provider="all"
args=()
while [ "$#" -gt 0 ]; do
  case "$1" in
    --provider)
      provider="$2"
      shift 2
      ;;
    --preset|--no-docker)
      args+=("$1")
      if [ "$1" = "--preset" ]; then
        args+=("$2")
        shift 2
      else
        shift
      fi
      ;;
    *)
      printf 'usage: %s [--provider postgres|sqlserver|all] [--preset <cmake-preset>] [--no-docker]\n' "$0" >&2
      exit 2
      ;;
  esac
done

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

case "$provider" in
  postgres)
    "$script_dir/test-live-postgres.sh" "${args[@]}"
    ;;
  sqlserver)
    "$script_dir/test-live-sqlserver.sh" "${args[@]}"
    ;;
  all)
    "$script_dir/test-live-postgres.sh" "${args[@]}"
    "$script_dir/test-live-sqlserver.sh" "${args[@]}"
    ;;
  *)
    printf 'unknown provider selector: %s\n' "$provider" >&2
    exit 2
    ;;
esac
