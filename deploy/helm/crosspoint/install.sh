#!/bin/bash
#
# Crosspoint (SonoBus) — AOO connection server + Web Console.
# LGC standard layout: values.yaml (in git) + optional secrets.yml (Ansible
# vault-encrypted overlay; see kubernetes/helm/vault-values.sh in
# lgc-onpremises-iac) + this script as the deployment entrypoint.
#
# One release = one console user. Default: both components enabled, the
# console on hostNetwork (WebRTC media 40000-40019 bind directly on the
# node), UI/API reachable at <node-ip>:7070 (or node:32100 / node:32101 via
# the chart's NodePort service).
#
# Requirements: bash, kubectl, helm.
# Recommended: yq (mikefarah) — with it the script shows a fully merged config
# summary (values + profile + secrets) and runs a nodePortRange pre-flight on
# the fixed NodePorts. Without yq it still works, but the summary shows chart
# defaults only and the port checks are skipped (the cluster validates the
# values at install time).

set -uo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# ── Configuration (override with env) ──────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NAMESPACE="${NAMESPACE:-crosspoint}"
HELM_RELEASE_NAME="${HELM_RELEASE_NAME:-crosspoint}"
HELM_CHART="${SCRIPT_DIR}"
VALUES_FILE="${VALUES_FILE:-values.yaml}"
SECRETS_FILE="${SECRETS_FILE:-secrets.yml}"
PROFILE="${PROFILE:-}"              # optional per-user overlay yaml
DOMAIN="${DOMAIN:-crosspoint.app.lagreca.io}"   # public URL (external nginx TLS)
AUTO_APPROVE="${AUTO_APPROVE:-}"
SKIP_WAIT="${SKIP_WAIT:-}"          # 1 = don't --wait (e.g. images not pushed yet)

# Chart defaults — used only when yq is absent and the summary must still be
# accurate for an unmodified values.yaml.
D_AOO_PORT="10998";  D_AOO_NODEPORT="31998"
D_CON_NODEPORT="32100"; D_CON_RTC_NODEPORT="32101"
D_UDP_RANGE="40000-40019"

TMPFILES=()
cleanup() {
    for f in ${TMPFILES[@]+"${TMPFILES[@]}"}; do
        [ -n "$f" ] && rm -f -- "$f"
    done
}
trap cleanup EXIT

# ── Helpers ────────────────────────────────────────────────────────────────

# yamlget <file> <dotted.path> — print the scalar for a dotted path using
# yq (mikefarah, `ea -p=yaml`); prints "null" when missing or unreadable.
yamlget() {
    local f="$1" p="$2" v
    [ -f "$f" ] || { echo "null"; return 0; }
    v="$(yq ea -p=yaml ".${p}" "$f" 2>/dev/null | head -n 1)"
    [ -n "$v" ] || v="null"
    echo "$v"
}

# merged <dotted.path> <file>... — last non-empty value wins (helm --values
# order). Used for the display summary and pre-flight only; helm itself
# always receives every --values file.
merged() {
    local p="$1" out="" f v
    shift
    for f in "$@"; do
        [ -f "$f" ] || continue
        v="$(yamlget "$f" "$p")"
        if [ -n "$v" ] && [ "$v" != "null" ]; then
            out="$v"
        fi
    done
    echo "$out"
}

# effective_secret_file <file> — print a path usable as a --values file.
# Vault-encrypted secrets.yml is decrypted into a 0600 temp file (honors
# ANSIBLE_VAULT_PASSWORD_FILE to skip the prompt); plaintext passes through.
effective_secret_file() {
    local f="$1"
    [ -f "$f" ] || return 0
    if head -n 1 -- "$f" 2>/dev/null | grep -q '^\$ANSIBLE_VAULT;'; then
        if ! command -v ansible-vault >/dev/null 2>&1; then
            echo -e "${RED}✗ ${f} is vault-encrypted but ansible-vault is not installed${NC}" >&2
            exit 1
        fi
        local tmp
        tmp="$(mktemp -t crosspoint-secrets.XXXXXX)"
        chmod 600 "$tmp"
        TMPFILES+=("$tmp")
        [ -n "${ANSIBLE_VAULT_PASSWORD_FILE:-}" ] || \
            echo -e "${YELLOW}Decrypting ${f} — you will be prompted for the vault password.${NC}" >&2
        if ! ansible-vault view --output "$tmp" "$f" >/dev/null 2>&1; then
            rm -f -- "$tmp"
            echo -e "${RED}✗ could not decrypt ${f}${NC}" >&2
            exit 1
        fi
        echo "$tmp"
    else
        echo "$f"
    fi
}

# ── Tool check ─────────────────────────────────────────────────────────────
for t in kubectl helm; do
    if ! command -v "$t" >/dev/null 2>&1; then
        echo -e "${RED}✗ required tool '${t}' not found in PATH${NC}"
        exit 1
    fi
done
YQ_AVAILABLE=0
command -v yq >/dev/null 2>&1 && YQ_AVAILABLE=1

# ── Resolve effective values (values + optional profile + secrets) ─────────
[ -f "${SCRIPT_DIR}/${VALUES_FILE}" ] || {
    echo -e "${RED}Error: values file '${VALUES_FILE}' not found in ${SCRIPT_DIR}${NC}"
    exit 1
}
# PROFILE: a file name in the chart dir, or an absolute/relative path to a
# yaml file (for per-user overlays kept elsewhere).
PROFILE_FILE=""
if [ -n "${PROFILE:-}" ]; then
    case "$PROFILE" in
        /*|*/*) PROFILE_FILE="$PROFILE" ;;
        *)      PROFILE_FILE="${SCRIPT_DIR}/${PROFILE}" ;;
    esac
    [ -f "$PROFILE_FILE" ] || {
        echo -e "${RED}Error: profile values file '${PROFILE}' not found${NC}"
        exit 1
    }
fi

ALL_FILES=("${SCRIPT_DIR}/${VALUES_FILE}")
[ -n "$PROFILE_FILE" ] && ALL_FILES+=("$PROFILE_FILE")

SECRETS_EFFECTIVE=""
# SECRETS_FILE: a file name in the chart dir, or an absolute/relative path.
case "${SECRETS_FILE}" in
    /*|*/*) SECRETS_RESOLVED="${SECRETS_FILE}" ;;
    *)      SECRETS_RESOLVED="${SCRIPT_DIR}/${SECRETS_FILE}" ;;
esac
if [ -n "${SECRETS_FILE:-}" ] && [ -f "${SECRETS_RESOLVED}" ]; then
    SECRETS_EFFECTIVE="$(effective_secret_file "${SECRETS_RESOLVED}")"
    [ -n "$SECRETS_EFFECTIVE" ] && ALL_FILES+=("$SECRETS_EFFECTIVE")
fi

# ── Read the effective values (only with yq; otherwise fall back to the
#     chart defaults and say so in the summary) ─────────────────────────────
AOO_ENABLED="true"; AOO_IMAGE=""; AOO_PORT="$D_AOO_PORT"; AOO_NODEPORT="$D_AOO_NODEPORT"
AOO_EXPOSURE="nodePort"
CONSOLE_ENABLED="true"; CONSOLE_IMAGE=""; CONSOLE_HOSTNET="true"
CONSOLE_GROUP="studio-a"; CONSOLE_USERNAME="console"
CONSOLE_UDP_RANGE="$D_UDP_RANGE"; CONSOLE_NODEPORT="$D_CON_NODEPORT"
CONSOLE_RTC_NODEPORT="$D_CON_RTC_NODEPORT"; CONSOLE_EXPOSURE="nodePort"
if [ "$YQ_AVAILABLE" = "1" ]; then
    AOO_ENABLED="$(merged aooserver.enabled "${ALL_FILES[@]}")"
    [ -n "$AOO_ENABLED" ] || AOO_ENABLED="true"
    AOO_IMAGE="$(merged aooserver.image.repository "${ALL_FILES[@]}"):$(merged aooserver.image.tag "${ALL_FILES[@]}")"
    AOO_PORT="$(merged aooserver.port "${ALL_FILES[@]}")";          [ -n "$AOO_PORT" ] || AOO_PORT="$D_AOO_PORT"
    AOO_EXPOSURE="$(merged aooserver.exposure "${ALL_FILES[@]}")";  [ -n "$AOO_EXPOSURE" ] || AOO_EXPOSURE="nodePort"
    AOO_NODEPORT="$(merged aooserver.nodePort "${ALL_FILES[@]}")"
    CONSOLE_ENABLED="$(merged console.enabled "${ALL_FILES[@]}")"
    [ -n "$CONSOLE_ENABLED" ] || CONSOLE_ENABLED="true"
    CONSOLE_IMAGE="$(merged console.image.repository "${ALL_FILES[@]}"):$(merged console.image.tag "${ALL_FILES[@]}")"
    CONSOLE_HOSTNET="$(merged console.hostNetwork "${ALL_FILES[@]}")"; [ -n "$CONSOLE_HOSTNET" ] || CONSOLE_HOSTNET="true"
    CONSOLE_GROUP="$(merged console.config.group "${ALL_FILES[@]}")"
    CONSOLE_USERNAME="$(merged console.config.username "${ALL_FILES[@]}")"
    CONSOLE_UDP_RANGE="$(merged console.rtcUdpMin "${ALL_FILES[@]}")-$(merged console.rtcUdpMax "${ALL_FILES[@]}")"
    case "${CONSOLE_UDP_RANGE}" in
        [0-9]*-[0-9]*) ;;
        *) CONSOLE_UDP_RANGE="$D_UDP_RANGE" ;;
    esac
    CONSOLE_NODEPORT="$(merged console.nodePort "${ALL_FILES[@]}")"
    CONSOLE_RTC_NODEPORT="$(merged console.rtcNodePort "${ALL_FILES[@]}")"
    CONSOLE_EXPOSURE="$(merged console.exposure "${ALL_FILES[@]}")"
    INGRESS_HOST="$(merged console.ingress.host "${ALL_FILES[@]}")"
    [ -n "$INGRESS_HOST" ] && DOMAIN="$INGRESS_HOST"
fi

# ── Pre-flight: fixed ports vs the cluster's nodePortRange ─────────────────
# (skipped without yq — the cluster rejects bad fixed NodePorts at install)
if [ "$YQ_AVAILABLE" = "1" ]; then
    NODEPORT_RANGE="$(kubectl -n kube-system get deployment kube-apiserver \
        -o jsonpath='{.spec.template.spec.containers[?(@.name=="kube-apiserver")].args}' 2>/dev/null \
        | tr ',' '\n' | grep -- '-service-node-port-range=' | head -n 1 | cut -d= -f2)"
    [ -n "$NODEPORT_RANGE" ] || NODEPORT_RANGE="30000-32767"   # apiserver default
    NR_LO="${NODEPORT_RANGE%-*}"
    NR_HI="${NODEPORT_RANGE#*-}"
    for p in ${AOO_NODEPORT} ${CONSOLE_NODEPORT} ${CONSOLE_RTC_NODEPORT}; do
        case "$p" in
            ''|*[!0-9]*) continue ;;
        esac
        if [ "$p" -lt "$NR_LO" ] || [ "$p" -gt "$NR_HI" ]; then
            echo -e "${RED}✗ fixed NodePort ${p} is outside the cluster nodePortRange (${NODEPORT_RANGE}).${NC}"
            echo -e "  Set aooserver.nodePort / console.nodePort / console.rtcNodePort inside that range and retry.${NC}"
            exit 1
        fi
    done
    if [ "$CONSOLE_HOSTNET" = "false" ]; then
        case "${CONSOLE_UDP_RANGE}" in
            [0-9]*-[0-9]*)
                if [ "${CONSOLE_UDP_RANGE%-*}" -lt "$NR_LO" ] || [ "${CONSOLE_UDP_RANGE#*-}" -gt "$NR_HI" ]; then
                    echo -e "${YELLOW}⚠  hostNetwork=false: media range ${CONSOLE_UDP_RANGE} is outside nodePortRange (${NODEPORT_RANGE}).${NC}"
                    echo -e "  The UDP NodePort service can only publish service ports inside that range — the browser dials those, not the bound ports.${NC}"
                    echo -e "  Fix: widen nodePortRange (apiserver flag -service-node-port-range), set console.rtcUdpNodePortMin/Max inside it, or use hostNetwork=true (default).${NC}"
                fi
                ;;
        esac
    fi
fi

# ── Banner / summary / confirmation ───────────────────────────────────────
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Crosspoint Installation Script${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo -e "${YELLOW}Current Kubernetes Context:${NC}"
CURRENT_CONTEXT="$(kubectl config current-context 2>/dev/null || echo '<unknown>')"
echo -e "${GREEN}${CURRENT_CONTEXT}${NC}"
echo ""

SECRETS_DESC="${SECRETS_FILE:-<none>}"
if [ -n "$SECRETS_EFFECTIVE" ]; then
    if head -n 1 -- "${SECRETS_RESOLVED}" 2>/dev/null | grep -q '^\$ANSIBLE_VAULT;'; then
        SECRETS_DESC="${SECRETS_FILE} (vault-encrypted, decrypted for this run)"
    else
        SECRETS_DESC="${SECRETS_FILE} (plaintext)"
    fi
fi

echo -e "${YELLOW}========================================${NC}"
echo -e "${YELLOW}Configuration Summary${NC}"
echo -e "${YELLOW}========================================${NC}"
echo -e "Namespace:           ${GREEN}${NAMESPACE}${NC}"
echo -e "Helm Release:        ${GREEN}${HELM_RELEASE_NAME}${NC}"
echo -e "Chart:               ${GREEN}${HELM_CHART}${NC}"
echo -e "Values:              ${GREEN}${VALUES_FILE}${NC}"
[ -n "${PROFILE_FILE:-}" ] && echo -e "Profile overlay:     ${GREEN}${PROFILE}${NC}"
echo -e "Secrets overlay:     ${GREEN}${SECRETS_DESC}${NC}"
if [ "$YQ_AVAILABLE" = "1" ]; then
    echo -e "AOO server:          ${GREEN}${AOO_ENABLED}${NC}   ${AOO_IMAGE}   (${AOO_EXPOSURE}, port ${AOO_PORT}${AOO_NODEPORT:+, nodePort ${AOO_NODEPORT}})"
    echo -e "Web Console:         ${GREEN}${CONSOLE_ENABLED}${NC}   ${CONSOLE_IMAGE}"
    if [ "$CONSOLE_ENABLED" = "true" ]; then
        echo -e "  group / user:      ${GREEN}${CONSOLE_GROUP} / ${CONSOLE_USERNAME}${NC}"
        echo -e "  hostNetwork:       ${GREEN}${CONSOLE_HOSTNET}${NC}   (media UDP ${CONSOLE_UDP_RANGE} binds on the node)"
        echo -e "  exposure:          ${GREEN}${CONSOLE_EXPOSURE}${NC}   (public URL https://${DOMAIN})"
    fi
else
    echo -e "AOO server:          enabled (defaults)   image/port per ${VALUES_FILE}"
    echo -e "Web Console:         default values — group ${GREEN}${CONSOLE_GROUP}${NC}, media UDP ${GREEN}${CONSOLE_UDP_RANGE}${NC}"
    echo -e "${YELLOW}⚠  yq not found: showing chart defaults only — check any ${PROFILE:-overlay} files by hand; the nodePortRange pre-flight is skipped.${NC}"
fi
echo -e "${RED}⚠  The console pod binds the WebRTC media range ${CONSOLE_UDP_RANGE} directly on its node.${NC}"
echo -e "   Keep that range free on the node and allow it through any firewall.${NC}"
echo ""

if [[ "${AUTO_APPROVE}" =~ ^([Yy][Ee][Ss]|[Tt][Rr][Uu][Ee]|1)$ ]]; then
    echo -e "${YELLOW}AUTO_APPROVE enabled. Proceeding without prompt.${NC}"
    echo ""
else
    read -p "Do you want to proceed? (yes/no): " -r
    echo ""
    if [[ ! $REPLY =~ ^[Yy][Ee][Ss]$ ]]; then
        echo -e "${RED}Installation cancelled.${NC}"
        exit 0
    fi
fi

# ── Step 1: namespace ─────────────────────────────────────────────────────
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Step 1: Creating Namespace${NC}"
echo -e "${BLUE}========================================${NC}"
if kubectl get namespace "${NAMESPACE}" &> /dev/null; then
    echo -e "${YELLOW}Namespace '${NAMESPACE}' already exists.${NC}"
else
    echo -e "${GREEN}Creating namespace '${NAMESPACE}'...${NC}"
    kubectl create namespace "${NAMESPACE}"
fi
echo ""

# ── Step 2: install / upgrade ─────────────────────────────────────────────
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Step 2: Installing with Helm${NC}"
echo -e "${BLUE}========================================${NC}"

VALUES_ARGS=("--values" "${SCRIPT_DIR}/${VALUES_FILE}")
[ -n "${PROFILE_FILE:-}" ] && VALUES_ARGS+=("--values" "${PROFILE_FILE}")
[ -n "$SECRETS_EFFECTIVE" ] && VALUES_ARGS+=("--values" "$SECRETS_EFFECTIVE")

WAIT_ARGS=()
[ "${SKIP_WAIT:-}" != "1" ] && WAIT_ARGS+=("--wait" "--timeout" "10m")

# (bash 3.2 + set -u: an empty array must be expanded with the ${arr[@]+...}
# guard, so WAIT_ARGS — which is empty when SKIP_WAIT=1 — is written that way.)
echo -e "${GREEN}helm upgrade --install ${HELM_RELEASE_NAME} ${HELM_CHART} -n ${NAMESPACE} ${VALUES_ARGS[*]} ${WAIT_ARGS[*]:-}${NC}"
if ! helm upgrade --install "${HELM_RELEASE_NAME}" "${HELM_CHART}" \
        --namespace "${NAMESPACE}" \
        "${VALUES_ARGS[@]}" \
        ${WAIT_ARGS[@]+"${WAIT_ARGS[@]}"}; then
    echo -e "${RED}✗ helm failed. Recent events:${NC}"
    kubectl -n "${NAMESPACE}" get events --sort-by=.lastTimestamp 2>/dev/null | tail -n 15
    exit 1
fi
echo ""

# ── Step 3: verify ─────────────────────────────────────────────────────────
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Step 3: Verifying Installation${NC}"
echo -e "${BLUE}========================================${NC}"

LABEL="app.kubernetes.io/instance=${HELM_RELEASE_NAME}"
NPODS="$(kubectl get pods -n "${NAMESPACE}" -l "${LABEL}" --no-headers 2>/dev/null | grep -c . || true)"
if [ "${NPODS:-0}" = "0" ]; then
    echo -e "${YELLOW}No pods for this release — both components disabled in the effective values?${NC}"
    kubectl get pods -n "${NAMESPACE}"
elif [ "${SKIP_WAIT:-}" != "1" ]; then
    echo -e "${GREEN}Waiting for pods to be ready...${NC}"
    if ! kubectl wait --for=condition=Ready pod -n "${NAMESPACE}" -l "${LABEL}" --timeout=300s 2>/dev/null; then
        echo -e "${YELLOW}Pods not ready in time (image pull problems? engine join time?). Status:${NC}"
        kubectl get pods -n "${NAMESPACE}" -l "${LABEL}"
    fi
fi
echo ""
echo -e "${GREEN}Pods:${NC}"
kubectl get pods -n "${NAMESPACE}" -l "${LABEL}" 2>/dev/null
kubectl get svc -n "${NAMESPACE}" -l "${LABEL}" 2>/dev/null
kubectl get ingress -n "${NAMESPACE}" -l "${LABEL}" 2>/dev/null || true
echo ""

# Image-pull sanity (the classic first-deploy failure mode).
PENDING_STATES="$(kubectl get pods -n "${NAMESPACE}" -l "${LABEL}" --field-selector=status.phase=Pending \
    -o jsonpath='{.items[*].status.containerStatuses[*].state}' 2>/dev/null || true)"
if echo "${PENDING_STATES}" | grep -qi 'ImagePull'; then
    echo -e "${RED}✗ one or more pods cannot pull their image — push the images first (see README 'Images').${NC}"
    kubectl get pods -n "${NAMESPACE}" -l "${LABEL}"
fi
echo ""

# ── Step 4: access info ───────────────────────────────────────────────────
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}✓ Installation Complete!${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

if [ "${SKIP_WAIT:-}" = "1" ]; then
    echo -e "${YELLOW}(helm --wait was skipped — check pod status before going further)${NC}"
    echo ""
fi

echo -e "${YELLOW}Access Information:${NC}"
echo ""

if [ "$AOO_ENABLED" = "true" ]; then
    echo -e "  AOO server (TCP+UDP ${AOO_PORT}):"
    echo -e "    in-cluster:  ${GREEN}${HELM_RELEASE_NAME}-aooserver.${NAMESPACE}.svc.cluster.local:${AOO_PORT}${NC}"
    case "${AOO_EXPOSURE}" in
        nodePort)
            echo -e "    on nodes:    ${GREEN}<node-ip>:${AOO_NODEPORT:-<allocated>}${NC}"
            ;;
        loadBalancer)
            LBIP="$(kubectl get svc "${HELM_RELEASE_NAME}-aooserver" -n "${NAMESPACE}" \
                -o jsonpath='{.status.loadBalancer.ingress[0].ip}' 2>/dev/null || echo '<pending>')"
            echo -e "    on LB:       ${GREEN}${LBIP}:${AOO_PORT}${NC}"
            ;;
        *)
            echo -e "    exposure:    ClusterIP only (cluster/VPN access)"
            ;;
    esac
    echo ""
fi

if [ "$CONSOLE_ENABLED" = "true" ]; then
    POD_NODE="$(kubectl get pod -n "${NAMESPACE}" \
        -l "${LABEL}" -l "app.kubernetes.io/component=console" \
        -o jsonpath='{.items[0].spec.nodeName}' 2>/dev/null || echo "")"
    NODE_IP=""
    if [ -n "$POD_NODE" ]; then
        NODE_IP="$(kubectl get node "${POD_NODE}" \
            -o jsonpath='{.status.addresses[?(@.type=="InternalIP")].address}' 2>/dev/null | head -n 1)"
    fi
    echo -e "  Web Console (${CONSOLE_GROUP} / ${CONSOLE_USERNAME}):"
    [ -n "$POD_NODE" ] && echo -e "    pod node:    ${GREEN}${POD_NODE}${NODE_IP:+ (${NODE_IP})}${NC}"
    case "${CONSOLE_EXPOSURE:-nodePort}" in
        nodePort)
            echo -e "    UI / API:    ${GREEN}<node-ip>:${CONSOLE_NODEPORT:-32100}${NC} (engine)   ${GREEN}<node-ip>:${CONSOLE_RTC_NODEPORT:-32101}${NC} (rtc)"
            echo -e "                or via the external nginx: point it at ${NODE_IP:-<node-ip>}:7070 and ${NODE_IP:-<node-ip>}:8090"
            ;;
        ingress)
            echo -e "    UI:          ${GREEN}https://${DOMAIN}${NC}  (traefik; the external nginx does TLS)"
            ;;
        *)
            echo -e "    in-cluster:  ${GREEN}http://${HELM_RELEASE_NAME}-console.${NAMESPACE}.svc.cluster.local:7070${NC}"
            ;;
    esac
    echo -e "    sign in:     with the AOO token (secret ${HELM_RELEASE_NAME}-console-secret, key api-token)"
    if [ "$CONSOLE_HOSTNET" != "false" ]; then
        echo -e "    media UDP:   the browser dials ${NODE_IP:-<the pod node IP>}:${CONSOLE_UDP_RANGE} directly — NOT proxied"
    else
        echo -e "    media UDP:   via the ${HELM_RELEASE_NAME}-rtc-udp NodePort service (see values.yaml)"
    fi
    echo ""
fi

echo -e "${YELLOW}Next Steps / Troubleshooting:${NC}"
echo -e "  kubectl -n ${NAMESPACE} get pods,svc,ing"
echo -e "  kubectl -n ${NAMESPACE} logs ${HELM_RELEASE_NAME}-console -f        # engine + gateway, one stream"
echo -e "  kubectl -n ${NAMESPACE} logs ${HELM_RELEASE_NAME}-aooserver -f"
echo -e "  kubectl -n ${NAMESPACE} get secret ${HELM_RELEASE_NAME}-console-secret -o jsonpath='{.data.api-token}' | base64 -d"
echo -e "  # one release per user:"
echo -e "  NAMESPACE=users HELM_RELEASE_NAME=<user> PROFILE=<user>.yml ./install.sh   # disjoint media ranges per user"
echo ""
