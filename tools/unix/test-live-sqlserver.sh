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
compose_file="$repo_root/tests/live/sqlserver/compose.yml"
init_sql="$repo_root/tests/live/sqlserver/init.sql"
container="sloppy-sqlserver-live"
password="Sloppy_Strong_Passw0rd!"

if [ -z "${SLOPPY_SQLSERVER_TEST_CONNECTION_STRING:-}" ]; then
  export SLOPPY_SQLSERVER_TEST_CONNECTION_STRING="Driver={ODBC Driver 18 for SQL Server};Server=tcp:127.0.0.1,51433;Database=sloppy_test;UID=sa;PWD=$password;Encrypt=yes;TrustServerCertificate=yes;"
fi
export Sloppy__Providers__sqlserver__main__connectionString="$SLOPPY_SQLSERVER_TEST_CONNECTION_STRING"

if [ "$no_docker" -eq 0 ]; then
  if ! command -v docker >/dev/null 2>&1; then
    printf 'UNAVAILABLE: Docker CLI is required for the SQL Server live-provider lane.\n' >&2
    exit 1
  fi
  docker info >/dev/null
  docker compose -f "$compose_file" up -d

  ready=0
  for _ in $(seq 1 90); do
    if docker exec "$container" /bin/bash -lc 'if [ -x /opt/mssql-tools18/bin/sqlcmd ]; then /opt/mssql-tools18/bin/sqlcmd "$@"; else /opt/mssql-tools/bin/sqlcmd "$@"; fi' -- -S localhost -U sa -P "$password" -C -b -Q 'select 1' >/dev/null 2>&1; then
      ready=1
      break
    fi
    sleep 2
  done
  if [ "$ready" -ne 1 ]; then
    printf 'UNAVAILABLE: SQL Server container did not become ready before timeout.\n' >&2
    exit 1
  fi
  docker cp "$init_sql" "$container:/tmp/sloppy-live-init.sql"
  docker exec "$container" /bin/bash -lc 'if [ -x /opt/mssql-tools18/bin/sqlcmd ]; then /opt/mssql-tools18/bin/sqlcmd "$@"; else /opt/mssql-tools/bin/sqlcmd "$@"; fi' -- -S localhost -U sa -P "$password" -C -b -i /tmp/sloppy-live-init.sql >/dev/null
  docker exec "$container" /bin/bash -lc 'if [ -x /opt/mssql-tools18/bin/sqlcmd ]; then /opt/mssql-tools18/bin/sqlcmd "$@"; else /opt/mssql-tools/bin/sqlcmd "$@"; fi' -- -S localhost -U sa -P "$password" -C -b -d sloppy_test -Q 'select 1' >/dev/null
  sleep 2
fi

cleanup() {
  if [ "$no_docker" -eq 0 ]; then
    docker compose -f "$compose_file" down -v
  fi
}
trap cleanup EXIT

cmake --build --preset "$preset"
ctest --test-dir "$repo_root/build/$preset" --output-on-failure -R 'data\.sqlserver\.live_provider|conformance\.sqlserver\.(native_live|bridge_live)'
