#!/usr/bin/env bash
set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

info()  { echo -e "${CYAN}[INFO]${NC}  $*"; }
ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }

if [[ $# -lt 2 ]]; then
    echo "사용법: $0 <user> <host> [clean]"
    echo "예)     $0 jcb6477 172.20.27.219"
    exit 1
fi

PI_USER="$1"
PI_HOST="$2"
PI_REMOTE="${PI_USER}@${PI_HOST}"
PI_SERVER_DIR="/home/${PI_USER}/server"
PI_LIBS_DIR="${PI_SERVER_DIR}/libs"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

if [[ "${3:-}" == "clean" ]]; then
    echo -e "\n${BOLD}══ CLEAN ══${NC}"
    [[ -d "${BUILD_DIR}" ]] && rm -rf "${BUILD_DIR}" && ok "로컬 build/ 삭제" || warn "build/ 없음"
    find "${PROJECT_ROOT}/libs" -name "*.so" -delete 2>/dev/null && ok ".so 삭제" || true
    [[ -f "${PROJECT_ROOT}/client" ]] && rm -f "${PROJECT_ROOT}/client" && ok "client 삭제" || true
    echo ""
    read -rsp "라즈베리파이 SSH 비밀번호: " SSHPASS; export SSHPASS; echo ""
    sshpass -e ssh -o StrictHostKeyChecking=no "${PI_REMOTE}" \
        "sudo systemctl stop device_server 2>/dev/null || true
         sudo systemctl disable device_server 2>/dev/null || true
         sudo rm -f /etc/systemd/system/device_server.service
         sudo systemctl daemon-reload
         rm -rf ${PI_SERVER_DIR}" \
        && ok "파이 삭제 완료" || warn "파이 접속 실패"
    echo -e "\n${GREEN}Clean 완료.${NC}"
    exit 0
fi

echo -e "\n${BOLD}══ 의존성 확인 ══${NC}"
for cmd in cmake aarch64-linux-gnu-gcc gcc sshpass; do
    command -v "$cmd" &>/dev/null && ok "$cmd" || error "$cmd 없음 → sudo apt install $cmd"
done

echo ""
read -rsp "라즈베리파이 SSH 비밀번호 입력: " SSHPASS
export SSHPASS
echo -e "\n"

echo -e "${BOLD}══ 1. CMake 빌드 ══${NC}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
info "Configure..."
cmake "${PROJECT_ROOT}" -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc -DCMAKE_BUILD_TYPE=Release 2>&1 | sed 's/^/  /'
info "Build..."
cmake --build . --parallel "$(nproc)" 2>&1 | sed 's/^/  /'
ok "ARM 빌드 완료"

for so in libled libbuzzer libcds lib7seg; do
    name="${so}.so"
    dir=$(echo $so | sed 's/lib//' | sed 's/7seg/7seg/')
    path="${PROJECT_ROOT}/libs/${dir}/${name}"
    # 직접 찾기
done

for so in \
    "${PROJECT_ROOT}/libs/led/libled.so" \
    "${PROJECT_ROOT}/libs/buzzer/libbuzzer.so" \
    "${PROJECT_ROOT}/libs/cds/libcds.so" \
    "${PROJECT_ROOT}/libs/7seg/lib7seg.so"; do
    [[ -f "$so" ]] && ok "생성: $so" || error "누락: $so"
done

cd "${PROJECT_ROOT}"

echo -e "\n${BOLD}══ 2. 클라이언트 빌드 ══${NC}"
gcc -Wall -O2 -o "${PROJECT_ROOT}/client" "${PROJECT_ROOT}/client.c"
ok "client 완료"

echo -e "\n${BOLD}══ 3. 파이 디렉터리 생성 ══${NC}"
sshpass -e ssh -o StrictHostKeyChecking=no "${PI_REMOTE}" \
    "mkdir -p ${PI_SERVER_DIR}/libs/led \
              ${PI_SERVER_DIR}/libs/buzzer \
              ${PI_SERVER_DIR}/libs/cds \
              ${PI_SERVER_DIR}/libs/7seg \
              ${PI_SERVER_DIR}/include \
              ${PI_SERVER_DIR}/src"
ok "디렉터리 생성 완료"

echo -e "\n${BOLD}══ 4. 파일 전송 ══${NC}"
scp_to() { sshpass -e scp -o StrictHostKeyChecking=no -r "$1" "${PI_REMOTE}:$2"; }

info "include/ 전송..."
scp_to "${PROJECT_ROOT}/server/include/." "${PI_SERVER_DIR}/include/"
ok "include/ 완료"

info "src/ 전송..."
scp_to "${PROJECT_ROOT}/server/src/."    "${PI_SERVER_DIR}/src/"
ok "src/ 완료"

info ".so 전송..."
scp_to "${PROJECT_ROOT}/libs/led/libled.so"       "${PI_LIBS_DIR}/led/"
scp_to "${PROJECT_ROOT}/libs/buzzer/libbuzzer.so" "${PI_LIBS_DIR}/buzzer/"
scp_to "${PROJECT_ROOT}/libs/cds/libcds.so"       "${PI_LIBS_DIR}/cds/"
scp_to "${PROJECT_ROOT}/libs/7seg/lib7seg.so"     "${PI_LIBS_DIR}/7seg/"
ok ".so 완료"

info "device_server 전송..."
scp_to "${BUILD_DIR}/device_server" "${PI_SERVER_DIR}/"
ok "device_server 완료"

echo -e "\n${BOLD}══ 5. systemd 등록 ══${NC}"
TMP_SVC="/tmp/device_server_$$.service"
cat > "${TMP_SVC}" << EOF
[Unit]
Description=Device Control Server
After=network.target

[Service]
Type=forking
ExecStart=${PI_SERVER_DIR}/device_server
WorkingDirectory=${PI_SERVER_DIR}
Restart=on-failure
RestartSec=5
User=root
Environment=LD_LIBRARY_PATH=${PI_LIBS_DIR}/led:${PI_LIBS_DIR}/buzzer:${PI_LIBS_DIR}/cds:${PI_LIBS_DIR}/7seg

[Install]
WantedBy=multi-user.target
EOF

sshpass -e scp -o StrictHostKeyChecking=no "${TMP_SVC}" "${PI_REMOTE}:/tmp/device_server.service"
rm -f "${TMP_SVC}"

sshpass -e ssh -o StrictHostKeyChecking=no "${PI_REMOTE}" \
    "chmod +x ${PI_SERVER_DIR}/device_server && \
     sudo mv /tmp/device_server.service /etc/systemd/system/device_server.service && \
     sudo systemctl daemon-reload && \
     sudo systemctl enable device_server && \
     sudo systemctl restart device_server"

ok "systemd 등록 + 시작 완료"

info "서비스 상태:"
sshpass -e ssh -o StrictHostKeyChecking=no "${PI_REMOTE}" \
    "sudo systemctl status device_server --no-pager -l" 2>&1 | sed 's/^/  /'

echo -e "\n${GREEN}${BOLD}══ 완료! ══${NC}"
echo -e "  로그   : ssh ${PI_REMOTE} 'tail -f /tmp/device_server.log'"
echo -e "  클라이언트: ./client ${PI_HOST} 9000"
