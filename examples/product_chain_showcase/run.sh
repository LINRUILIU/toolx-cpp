#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: run.sh BIN_DIR OUTPUT_DIR" >&2
  exit 2
fi

SCRIPT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "${SCRIPT_ROOT}/../.." && pwd)"
BIN_DIR="$(cd "$1" && pwd)"
OUTPUT_PARENT="$(cd "$(dirname "$2")" && pwd)"
OUTPUT_DIR="${OUTPUT_PARENT}/$(basename "$2")"

case "${OUTPUT_DIR}" in
  /|"${REPOSITORY_ROOT}"|"${BIN_DIR}")
    echo "refusing destructive showcase output path: ${OUTPUT_DIR}" >&2
    exit 2
    ;;
esac

OUTPUT_MARKER="${OUTPUT_DIR}/.toolx-showcase-output"
if [[ -e "${OUTPUT_DIR}" && ! -f "${OUTPUT_MARKER}" ]]; then
  echo "refusing to clean an existing directory without a ToolX showcase marker: ${OUTPUT_DIR}" >&2
  exit 2
fi
rm -rf -- "${OUTPUT_DIR}"
mkdir -p "${OUTPUT_DIR}"
printf '%s\n' 'ToolX product-chain showcase output' >"${OUTPUT_MARKER}"
cp -R "${SCRIPT_ROOT}/fixtures" "${OUTPUT_DIR}/input"

require_binary() {
  local name="$1"
  if [[ ! -x "${BIN_DIR}/${name}" ]]; then
    echo "required showcase binary is missing: ${BIN_DIR}/${name}" >&2
    exit 1
  fi
}

for tool in toolx-config toolx-sync toolx-pack toolx-http toolx-log toolx-inspect toolx_showcase_server; do
  require_binary "${tool}"
done

run_tool() {
  local capture="$1"
  shift
  if ! "$@" >"${OUTPUT_DIR}/${capture}" 2>"${OUTPUT_DIR}/${capture}.stderr"; then
    echo "showcase command failed: $*" >&2
    cat "${OUTPUT_DIR}/${capture}.stderr" >&2
    exit 1
  fi
}

BASE="${OUTPUT_DIR}/input/app.base.json"
OVERLAY="${OUTPUT_DIR}/input/app.local.json"
SCHEMA="${OUTPUT_DIR}/input/schema.json"
LOG_INPUT="${OUTPUT_DIR}/input/app.log"
PACKAGE_SOURCE="${OUTPUT_DIR}/input/package-src"
MERGED="${OUTPUT_DIR}/merged.json"
RESOLVED="${OUTPUT_DIR}/resolved.json"

run_tool 01-config-merge.json "${BIN_DIR}/toolx-config" merge \
  --base "${BASE}" --overlay "${OVERLAY}" --out "${MERGED}" --json
run_tool 02-config-doctor.json "${BIN_DIR}/toolx-config" doctor \
  --file "${MERGED}" --schema "${SCHEMA}" --require svc.host \
  --expect svc.port=int --json
run_tool 03-sync.json "${BIN_DIR}/toolx-sync" \
  --base "${MERGED}" --out "${RESOLVED}" --schema "${SCHEMA}" \
  --snapshot "${OUTPUT_DIR}/snapshot.json" \
  --journal "${OUTPUT_DIR}/resolved.journal" \
  --log-file "${OUTPUT_DIR}/sync.log" --json
run_tool 04-pack.json "${BIN_DIR}/toolx-pack" stage \
  --src "${PACKAGE_SOURCE}" --out "${OUTPUT_DIR}/stage" \
  --archive "${OUTPUT_DIR}/toolx-showcase.tar" \
  --include bin --include README.md --json

PORT_FILE="${OUTPUT_DIR}/showcase.port"
"${BIN_DIR}/toolx_showcase_server" --port-file "${PORT_FILE}" \
  >"${OUTPUT_DIR}/server.stdout" 2>"${OUTPUT_DIR}/server.stderr" &
SERVER_PID=$!
cleanup_server() {
  if kill -0 "${SERVER_PID}" 2>/dev/null; then
    kill "${SERVER_PID}" 2>/dev/null || true
    wait "${SERVER_PID}" 2>/dev/null || true
  fi
}
trap cleanup_server EXIT

for _ in $(seq 1 100); do
  [[ -f "${PORT_FILE}" ]] && break
  sleep 0.05
done
if [[ ! -f "${PORT_FILE}" ]]; then
  echo "loopback server did not publish its port" >&2
  exit 1
fi
PORT="$(tr -d '\r\n' <"${PORT_FILE}")"

run_tool 05-http.json "${BIN_DIR}/toolx-http" check \
  --url "http://127.0.0.1:${PORT}/health" --expect-status 200 \
  --expect-body-contains ready --no-proxy-from-env --json
wait "${SERVER_PID}"
trap - EXIT

run_tool 06-log.json "${BIN_DIR}/toolx-log" summarize \
  --file "${LOG_INPUT}" --format logsys-text --min-level warning --json
(
  cd "${OUTPUT_DIR}"
  run_tool 07-inspect.json "${BIN_DIR}/toolx-inspect" report \
    --file resolved.json --schema input/schema.json --json
  run_tool 08-inspect-frame.txt "${BIN_DIR}/toolx-inspect" render \
    --file resolved.json --schema input/schema.json --width 90 --height 18
)

(
  cd "${OUTPUT_DIR}"
  find . -type f -print | sed 's#^./##' | sort > artifacts.txt
)

echo "ToolX showcase completed: ${OUTPUT_DIR}"
