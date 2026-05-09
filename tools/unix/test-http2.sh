#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
preset=""
url=""
run_h2spec=0
run_curl=0
run_nghttp=0
run_h2load=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)
      preset="${2:?missing value for --preset}"
      shift 2
      ;;
    --url)
      url="${2:?missing value for --url}"
      shift 2
      ;;
    --h2spec)
      run_h2spec=1
      shift
      ;;
    --curl)
      run_curl=1
      shift
      ;;
    --nghttp)
      run_nghttp=1
      shift
      ;;
    --h2load-smoke)
      run_h2load=1
      shift
      ;;
    -h|--help)
      cat <<'EOF'
Usage: tools/unix/test-http2.sh [--preset NAME] [--url URL] [--h2spec] [--curl] [--nghttp] [--h2load-smoke]

Runs local HTTP/2 CTest lanes and optional external HTTP/2 tools. External
tools report UNAVAILABLE when missing and SKIPPED when no --url is provided.
--h2spec runs the full h2spec suite.
EOF
      exit 0
      ;;
    *)
      echo "sloppy http2 test: unknown option '$1'" >&2
      exit 2
      ;;
  esac
done

host_preset() {
  if [[ -n "$preset" ]]; then
    printf '%s\n' "$preset"
    return
  fi
  case "$(uname -s)" in
    Darwin) printf 'macos-clang\n' ;;
    Linux) printf 'linux-clang\n' ;;
    *)
      echo "sloppy http2 test: unsupported Unix platform: $(uname -s)" >&2
      exit 1
      ;;
  esac
}

evidence() {
  printf '%s\t%s\t%s\n' "$1" "$2" "$3"
}

run_optional_tool() {
  local lane="$1"
  local tool="$2"
  shift 2
  if ! command -v "$tool" >/dev/null 2>&1; then
    evidence "$lane" "UNAVAILABLE" "$tool is not on PATH"
    return
  fi
  if [[ -z "$url" ]]; then
    evidence "$lane" "SKIPPED" "provide --url to run $tool against a live HTTP/2 endpoint"
    return
  fi
  if "$tool" "$@"; then
    evidence "$lane" "PASS" "$tool completed"
  else
    local code=$?
    evidence "$lane" "FAIL" "$tool exited with code $code"
    exit "$code"
  fi
}

run_curl_http2() {
  if ! command -v curl >/dev/null 2>&1; then
    evidence "http2.curl" "UNAVAILABLE" "curl is not on PATH"
    return
  fi
  if ! curl --version | grep -Eq '\bHTTP2\b'; then
    evidence "http2.curl" "UNAVAILABLE" "curl was built without HTTP/2 support"
    return
  fi
  if [[ -z "$url" ]]; then
    evidence "http2.curl" "SKIPPED" "provide --url to run curl against a live HTTP/2 endpoint"
    return
  fi
  curl_scheme="$(python3 - "$url" <<'PY'
import sys
from urllib.parse import urlparse
u = urlparse(sys.argv[1])
if u.scheme not in ("http", "https") or not u.hostname:
    raise SystemExit(1)
print(u.scheme)
PY
)" || {
    evidence "http2.curl" "FAIL" "malformed --url: $url"
    exit 1
  }
  curl_args=(--fail --silent --show-error --output /dev/null --write-out "%{http_version}")
  if [[ "$curl_scheme" == "http" ]]; then
    curl_args+=(--http2-prior-knowledge)
  else
    curl_args+=(--http2)
  fi
  if curl_version="$(curl "${curl_args[@]}" "$url")"; then
    :
  else
    code=$?
    evidence "http2.curl" "FAIL" "curl exited with code $code"
    exit "$code"
  fi
  if [[ "$curl_version" != "2" ]]; then
    evidence "http2.curl" "FAIL" "curl negotiated HTTP/$curl_version"
    exit 1
  fi
  evidence "http2.curl" "PASS" "curl negotiated HTTP/2"
}

build_dir="$repo_root/build/$(host_preset)"
if [[ ! -d "$build_dir" ]]; then
  evidence "http2.local_ctest" "UNAVAILABLE" "build preset directory not found: $build_dir"
  exit 0
fi

ctest --test-dir "$build_dir" -R 'core\.http2|conformance\.transport\.http2_' --output-on-failure
evidence "http2.local_ctest" "PASS" "core and transport HTTP/2 lanes passed"

if [[ "$run_h2spec$run_curl$run_nghttp$run_h2load" == "0000" || "$run_h2spec" == "1" ]]; then
  h2spec_lane="http2.h2spec"
  if [[ -n "$url" ]]; then
    h2spec_host="$(python3 - "$url" <<'PY'
import sys
from urllib.parse import urlparse
u = urlparse(sys.argv[1])
if u.scheme not in ("http", "https") or not u.hostname:
    raise SystemExit(1)
port = u.port or (80 if u.scheme == "http" else 443)
print(u.hostname or "")
print(port)
print(u.scheme)
PY
)" || {
      evidence "$h2spec_lane" "FAIL" "malformed --url: $url"
      exit 1
    }
    h2spec_host_name="$(printf '%s\n' "$h2spec_host" | sed -n '1p')"
    h2spec_port="$(printf '%s\n' "$h2spec_host" | sed -n '2p')"
    h2spec_scheme="$(printf '%s\n' "$h2spec_host" | sed -n '3p')"
    h2spec_args=(-h "$h2spec_host_name" -p "$h2spec_port")
    if [[ "$h2spec_scheme" == "https" ]]; then
      h2spec_args+=(-t)
    fi
    run_optional_tool "$h2spec_lane" "h2spec" "${h2spec_args[@]}"
  else
    run_optional_tool "$h2spec_lane" "h2spec"
  fi
fi
if [[ "$run_h2spec$run_curl$run_nghttp$run_h2load" == "0000" || "$run_curl" == "1" ]]; then
  run_curl_http2
fi
if [[ "$run_h2spec$run_curl$run_nghttp$run_h2load" == "0000" || "$run_nghttp" == "1" ]]; then
  run_optional_tool "http2.nghttp" "nghttp" -nv "$url"
fi
if [[ "$run_h2spec$run_curl$run_nghttp$run_h2load" == "0000" || "$run_h2load" == "1" ]]; then
  run_optional_tool "http2.h2load" "h2load" -n 10 -c 1 "$url"
fi
