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

# ════════════════════════════════════════════════════════════════════
#  CLEAN
# ════════════════════════════════════════════════════════════════════
if [[ "${3:-}" == "clean" ]]; then
    echo -e "\n${BOLD}══ CLEAN ══${NC}"
    [[ -d "${BUILD_DIR}" ]] && rm -rf "${BUILD_DIR}" && ok "로컬 build/ 삭제" || warn "build/ 없음"
    find "${PROJECT_ROOT}/libs" -name "*.so" -delete 2>/dev/null && ok ".so 삭제" || true
    [[ -f "${PROJECT_ROOT}/client" ]] && rm -f "${PROJECT_ROOT}/client" && ok "client 삭제" || true
    echo ""
    read -rsp "라즈베리파이 SSH 비밀번호: " SSHPASS; export SSHPASS; echo ""
    sshpass -e ssh -tt -o StrictHostKeyChecking=no "${PI_REMOTE}" "
        echo '${SSHPASS}' | sudo -S systemctl stop device_server 2>/dev/null || true
        echo '${SSHPASS}' | sudo -S systemctl disable device_server 2>/dev/null || true
        echo '${SSHPASS}' | sudo -S rm -f /etc/systemd/system/device_server.service
        echo '${SSHPASS}' | sudo -S systemctl daemon-reload
        rm -rf ${PI_SERVER_DIR}
    " && ok "파이 삭제 완료" || warn "파이 접속 실패"
    echo -e "\n${GREEN}Clean 완료.${NC}"
    exit 0
fi

# ════════════════════════════════════════════════════════════════════
#  의존성 확인
# ════════════════════════════════════════════════════════════════════
echo -e "\n${BOLD}══ 의존성 확인 ══${NC}"
for cmd in cmake aarch64-linux-gnu-gcc gcc sshpass; do
    command -v "$cmd" &>/dev/null && ok "$cmd" || error "$cmd 없음 → sudo apt install $cmd"
done

# ════════════════════════════════════════════════════════════════════
#  비밀번호 입력 (한 번만)
# ════════════════════════════════════════════════════════════════════
echo ""
read -rsp "라즈베리파이 SSH 비밀번호 입력: " SSHPASS
export SSHPASS
echo -e "\n"

# ════════════════════════════════════════════════════════════════════
#  서버 동작 감지 → client만 빌드
# ════════════════════════════════════════════════════════════════════
echo -e "${BOLD}══ 서버 상태 감지 ══${NC}"
SERVER_RUNNING=$(sshpass -e ssh -o StrictHostKeyChecking=no "${PI_REMOTE}" \
    "ss -tlnp 2>/dev/null | grep 9000 | wc -l" 2>/dev/null || echo "0")

if [[ "${SERVER_RUNNING}" -gt 0 ]]; then
    warn "파이에서 device_server 이미 실행 중 (port 9000 감지)"
    info "client만 빌드합니다..."
    gcc -Wall -O2 -o "${PROJECT_ROOT}/client" "${PROJECT_ROOT}/client.c"
    ok "client 빌드 완료"
    echo -e "\n  실행: ./client ${PI_HOST} 9000"
    exit 0
fi
ok "서버 미실행 → 전체 빌드 진행"

# ════════════════════════════════════════════════════════════════════
#  1. CMake 빌드 (ARM)
# ════════════════════════════════════════════════════════════════════
echo -e "\n${BOLD}══ 1. CMake 빌드 (ARM 크로스컴파일) ══${NC}"
# 이전 캐시 충돌 방지 — build/ 항상 새로 시작
[[ -d "${BUILD_DIR}" ]] && rm -rf "${BUILD_DIR}" && info "기존 build/ 캐시 삭제"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

info "Configure..."
cmake "${PROJECT_ROOT}" \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
    -DCMAKE_BUILD_TYPE=Release 2>&1 | sed 's/^/  /'

info "Build..."
cmake --build . --parallel "$(nproc)" 2>&1 | sed 's/^/  /'
ok "ARM 빌드 완료"

for so in \
    "${PROJECT_ROOT}/libs/led/libled.so" \
    "${PROJECT_ROOT}/libs/buzzer/libbuzzer.so" \
    "${PROJECT_ROOT}/libs/cds/libcds.so" \
    "${PROJECT_ROOT}/libs/7seg/lib7seg.so"; do
    [[ -f "$so" ]] && ok "생성: $so" || error "누락: $so"
done

cd "${PROJECT_ROOT}"

# ════════════════════════════════════════════════════════════════════
#  2. 클라이언트 빌드 (native gcc)
# ════════════════════════════════════════════════════════════════════
echo -e "\n${BOLD}══ 2. 클라이언트 빌드 (native gcc) ══${NC}"
gcc -Wall -O2 -DSERVER_IP="\"${PI_HOST}\"" -o "${PROJECT_ROOT}/client" "${PROJECT_ROOT}/client.c"
ok "client 완료 (SERVER_IP=${PI_HOST})"

# ════════════════════════════════════════════════════════════════════
#  3. 파이 I2C 활성화 확인 + 자동 설정
# ════════════════════════════════════════════════════════════════════
echo -e "\n${BOLD}══ 3. 파이 I2C 활성화 확인 ══${NC}"

I2C_ENABLED=$(sshpass -e ssh -o StrictHostKeyChecking=no "${PI_REMOTE}" \
    "ls /dev/i2c-1 2>/dev/null | wc -l" 2>/dev/null || echo "0")

if [[ "${I2C_ENABLED}" -eq 0 ]]; then
    warn "/dev/i2c-1 없음 → I2C 자동 활성화 중..."
    sshpass -e ssh -o StrictHostKeyChecking=no "${PI_REMOTE}" "
        echo '${SSHPASS}' | sudo -S raspi-config nonint do_i2c 0
        echo '${SSHPASS}' | sudo -S modprobe i2c-dev
        echo '${SSHPASS}' | sudo -S modprobe i2c-bcm2835
    " && ok "I2C 활성화 완료 (재부팅 불필요)"

    # 적용 확인
    I2C_CHECK=$(sshpass -e ssh -o StrictHostKeyChecking=no "${PI_REMOTE}" \
        "ls /dev/i2c-1 2>/dev/null | wc -l" 2>/dev/null || echo "0")
    if [[ "${I2C_CHECK}" -eq 0 ]]; then
        warn "/dev/i2c-1 아직 없음 → 재부팅 필요"
        info "파이 재부팅 중... (30초 대기)"
        sshpass -e ssh -o StrictHostKeyChecking=no "${PI_REMOTE}" \
            "echo '${SSHPASS}' | sudo -S reboot" 2>/dev/null || true
        sleep 10
        for i in $(seq 1 10); do
            sleep 3
            if sshpass -e ssh -o StrictHostKeyChecking=no -o ConnectTimeout=5 "${PI_REMOTE}" "echo ok" &>/dev/null; then
                ok "파이 재부팅 완료"
                break
            fi
            info "파이 연결 대기 중... ($((i*3))s)"
        done
    fi
else
    ok "I2C 이미 활성화됨 (/dev/i2c-1 확인)"
fi

# ════════════════════════════════════════════════════════════════════
#  4. 파이 디렉터리 생성
# ════════════════════════════════════════════════════════════════════
echo -e "\n${BOLD}══ 4. 파이 디렉터리 생성 ══${NC}"
sshpass -e ssh -o StrictHostKeyChecking=no "${PI_REMOTE}" \
    "mkdir -p ${PI_SERVER_DIR}/libs/led \
              ${PI_SERVER_DIR}/libs/buzzer \
              ${PI_SERVER_DIR}/libs/cds \
              ${PI_SERVER_DIR}/libs/7seg"
ok "디렉터리 생성 완료"

# ════════════════════════════════════════════════════════════════════
#  4. 파일 전송 (device_server + .so 만)
# ════════════════════════════════════════════════════════════════════
echo -e "\n${BOLD}══ 4. 파일 전송 ══${NC}"
scp_to() { sshpass -e scp -o StrictHostKeyChecking=no -r "$1" "${PI_REMOTE}:$2"; }

info ".so 전송..."
scp_to "${PROJECT_ROOT}/libs/led/libled.so"       "${PI_LIBS_DIR}/led/"
scp_to "${PROJECT_ROOT}/libs/buzzer/libbuzzer.so" "${PI_LIBS_DIR}/buzzer/"
scp_to "${PROJECT_ROOT}/libs/cds/libcds.so"       "${PI_LIBS_DIR}/cds/"
scp_to "${PROJECT_ROOT}/libs/7seg/lib7seg.so"     "${PI_LIBS_DIR}/7seg/"
ok ".so 완료"

info "device_server 전송..."
scp_to "${BUILD_DIR}/device_server" "${PI_SERVER_DIR}/"
ok "device_server 완료"

# ════════════════════════════════════════════════════════════════════
#  5. systemd 서비스 등록 (Type=simple — daemonize 없음)
# ════════════════════════════════════════════════════════════════════
echo -e "\n${BOLD}══ 5. systemd 서비스 등록 ══${NC}"

TMP_SVC="/tmp/device_server_$$.service"
cat > "${TMP_SVC}" << EOF
[Unit]
Description=Device Control Server
After=network.target
StartLimitIntervalSec=60
StartLimitBurst=5

[Service]
Type=simple
User=root
WorkingDirectory=${PI_SERVER_DIR}
ExecStart=/bin/bash -c 'LD_LIBRARY_PATH=${PI_LIBS_DIR}/led:${PI_LIBS_DIR}/buzzer:${PI_LIBS_DIR}/cds:${PI_LIBS_DIR}/7seg exec ${PI_SERVER_DIR}/device_server'
Restart=on-failure
RestartSec=3
DeviceAllow=/dev/mem rw
DeviceAllow=/dev/gpiomem rw
DeviceAllow=/dev/i2c-1 rw
SupplementaryGroups=i2c gpio

[Install]
WantedBy=multi-user.target
EOF

sshpass -e scp -o StrictHostKeyChecking=no "${TMP_SVC}" "${PI_REMOTE}:/tmp/device_server.service"
rm -f "${TMP_SVC}"

sshpass -e ssh -tt -o StrictHostKeyChecking=no "${PI_REMOTE}" "
    chmod +x ${PI_SERVER_DIR}/device_server
    echo '${SSHPASS}' | sudo -S mv /tmp/device_server.service /etc/systemd/system/device_server.service
    echo '${SSHPASS}' | sudo -S systemctl daemon-reload
    echo '${SSHPASS}' | sudo -S systemctl enable device_server
    echo '${SSHPASS}' | sudo -S systemctl restart device_server
"
ok "systemd 등록 + 시작 완료"

info "서비스 상태:"
sshpass -e ssh -tt -o StrictHostKeyChecking=no "${PI_REMOTE}" \
    "echo '${SSHPASS}' | sudo -S systemctl status device_server --no-pager -l" 2>&1 | sed 's/^/  /'

# ════════════════════════════════════════════════════════════════════
#  완료
# ════════════════════════════════════════════════════════════════════
echo -e "\n${GREEN}${BOLD}══════════════════════════════════════════${NC}"
echo -e "${GREEN}${BOLD}  빌드 + 전송 + 서비스 등록 완료!${NC}"
echo -e "${GREEN}${BOLD}══════════════════════════════════════════${NC}"
echo -e "  로그      : ssh ${PI_REMOTE} 'tail -f /tmp/device_server.log'"
echo -e "  상태      : ssh ${PI_REMOTE} 'sudo systemctl status device_server'"
echo -e "  클라이언트: ./client ${PI_HOST} 9000"
echo ""