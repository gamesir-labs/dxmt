#!/usr/bin/env bash
# Feishu / Lark webhook helpers for macOS self-hosted CI runners.
# Used by Nightly / Component result notify on self-macos-runner.
# Push/PR/issue events stay on Windows via .github/scripts/ci-feishu.ps1.
#
# HTTP posts go through curl (macOS Secure Transport / system CA store).
# Do not use Python urllib.request: Homebrew Python OpenSSL often fails
# with corporate MITM / self-signed chains that system curl trusts.
set -euo pipefail

log() {
  printf '[dxmt-feishu] %s\n' "$*" >&2
}

die() {
  printf '[dxmt-feishu] error: %s\n' "$*" >&2
  exit 1
}

# UTF-8 Chinese labels (macOS runners use UTF-8 locales).
label() {
  case "$1" in
    repo) printf '%s' '仓库' ;;
    branch) printf '%s' '分支' ;;
    commit) printf '%s' '提交' ;;
    author) printf '%s' '提交者' ;;
    version) printf '%s' '版本' ;;
    component_package) printf '%s' '组件包' ;;
    download) printf '%s' '点击下载' ;;
    size) printf '%s' '大小' ;;
    failed_jobs) printf '%s' '失败任务' ;;
    run_link) printf '%s' '运行链接' ;;
    view_detail) printf '%s' '查看详情' ;;
    pkg_name) printf '%s' '包名' ;;
    note) printf '%s' '说明' ;;
    publish_env) printf '%s' '发布环境' ;;
    trigger_ref) printf '%s' '触发 ref' ;;
    source_sha) printf '%s' '源 SHA' ;;
    version_sha) printf '%s' '版本 SHA' ;;
    build_fail) printf '%s' '❌ DXMT Nightly 构建失败' ;;
    build_ok) printf '%s' '✅ DXMT Nightly 构建成功' ;;
    pub_fail) printf '%s' '❌ DXMT 组件发布失败' ;;
    pub_ok) printf '%s' '✅ DXMT 组件发布成功' ;;
    nightly_note) printf '%s' 'push nightly 仅构建打包，不跑测试' ;;
    component_note) printf '%s' '含完整测试 + 后端组件上传' ;;
    *) die "unknown label: $1" ;;
  esac
}

# POST JSON body to webhook via system curl (trusted CA path on macOS).
post_json() {
  local webhook="$1"
  local json_file="$2"
  command -v curl >/dev/null 2>&1 || die "curl is required to post Feishu notifications"
  if ! curl -fsS --connect-timeout 15 --max-time 30 \
    -X POST \
    -H 'Content-Type: application/json; charset=utf-8' \
    --data-binary @"${json_file}" \
    "${webhook}" >/dev/null; then
    die "Feishu webhook POST failed (curl exit $?); check network/CA and FEISHU_WEBHOOK_URL"
  fi
}

send_card() {
  local webhook="${1:-}"
  local title="${2:-}"
  local template="${3:-}"
  local markdown="${4:-}"
  local subtitle="${5:-}"
  local tmp

  if [[ -z "${webhook}" ]]; then
    log 'FEISHU_WEBHOOK_URL is not configured; skipping notification.'
    return 0
  fi

  tmp="$(mktemp "${TMPDIR:-/tmp}/dxmt-feishu.XXXXXX.json")"
  # shellcheck disable=SC2064
  trap 'rm -f "'"${tmp}"'"' RETURN

  python3 - "$title" "$template" "$markdown" "$subtitle" >"${tmp}" <<'PY'
import json, sys

title, template, markdown, subtitle = sys.argv[1:5]
header = {
    "title": {"tag": "plain_text", "content": title},
    "template": template,
}
if subtitle.strip():
    header["subtitle"] = {"tag": "plain_text", "content": subtitle}

payload = {
    "msg_type": "interactive",
    "card": {
        "schema": "2.0",
        "config": {"update_multi": True},
        "header": header,
        "body": {
            "direction": "vertical",
            "padding": "12px 12px 12px 12px",
            "elements": [{"tag": "markdown", "content": markdown}],
        },
    },
}
json.dump(payload, sys.stdout, ensure_ascii=False, separators=(",", ":"))
PY
  post_json "${webhook}" "${tmp}"
  log 'card notification sent'
}

send_card_v1() {
  local webhook="${1:-}"
  local title="${2:-}"
  local template="${3:-}"
  local tmp

  if [[ -z "${webhook}" ]]; then
    log 'FEISHU_WEBHOOK_URL is not configured; skipping notification.'
    return 0
  fi

  shift 3
  (($# > 0)) || die 'send-card-v1 requires at least one markdown section'

  tmp="$(mktemp "${TMPDIR:-/tmp}/dxmt-feishu.XXXXXX.json")"
  # shellcheck disable=SC2064
  trap 'rm -f "'"${tmp}"'"' RETURN

  python3 - "$title" "$template" "$@" >"${tmp}" <<'PY'
import json, sys

title, template, *sections = sys.argv[1:]
elements = []
for section in sections:
    if elements:
        elements.append({"tag": "hr"})
    elements.append({
        "tag": "div",
        "text": {"tag": "lark_md", "content": section},
    })

payload = {
    "msg_type": "interactive",
    "card": {
        "header": {
            "title": {"tag": "plain_text", "content": title},
            "template": template,
        },
        "elements": elements,
    },
}
json.dump(payload, sys.stdout, ensure_ascii=False, separators=(",", ":"))
PY
  post_json "${webhook}" "${tmp}"
  log 'card-v1 notification sent'
}

usage() {
  cat <<'EOF'
usage: ci-feishu.sh <command> [args]

commands:
  send-card <webhook> <title> <template> <markdown> [subtitle]
  send-card-v1 <webhook> <title> <template> <markdown-section> [markdown-section...]
  label <name>
EOF
}

cmd="${1:-}"
shift || true
case "${cmd}" in
  send-card) send_card "$@" ;;
  send-card-v1) send_card_v1 "$@" ;;
  label) label "${1:-}" ; printf '\n' ;;
  ""|-h|--help) usage ;;
  *) usage; die "unknown command: ${cmd}" ;;
esac
