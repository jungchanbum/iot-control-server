# Device Control Server

라즈베리파이 GPIO 장치(LED, Buzzer, CdS, 7-Segment)를 TCP 소켓으로 원격 제어하는 데몬 서버 프로젝트.

---

## 프로젝트 구조

```
.
├── Build.sh                  # 빌드 + 파이 전송 + systemd 등록 스크립트
├── client.c                  # TCP 클라이언트 (호스트 PC에서 실행)
├── CMakeLists.txt            # 크로스컴파일 빌드 설정
├── device_server.service     # systemd 서비스 파일 (참고용)
├── libs/
│   ├── led/led_lib.c         # LED PWM 제어 공유 라이브러리
│   ├── buzzer/buzzer_lib.c   # Buzzer softTone 공유 라이브러리
│   ├── cds/cds_lib.c         # CdS 조도센서 I2C 공유 라이브러리
│   └── 7seg/7seg_lib.c       # 7-Segment 카운트다운 공유 라이브러리
└── server/
    ├── include/
    │   ├── globals.h         # 전역 변수, 상수, 인라인 함수 선언
    │   ├── command.h         # 명령어 디스패처 선언
    │   ├── lib_loader.h      # .so 동적 로딩 선언
    │   └── workers.h         # 스레드 워커 선언
    └── src/
        ├── main.c            # 데몬화, epoll 이벤트 루프, 소켓 서버
        ├── command.c         # 명령어 파싱 및 라우팅
        ├── globals.c         # 전역 변수 정의
        ├── lib_loader.c      # dlopen/dlsym으로 .so 동적 로딩
        └── workers.c         # LED/Buzzer/CdS/Segment 스레드 워커
```

---

## 아키텍처

```
[클라이언트 PC]                    [라즈베리파이]
  client (x86)  ──TCP:9000──▶  device_server (ARM daemon)
                                      │
                          ┌───────────┼───────────┐
                          ▼           ▼            ▼           ▼
                      libled.so  libbuzzer.so  libcds.so  lib7seg.so
                          │           │            │           │
                        LED(PWM)   Buzzer       CdS(I2C)   7-Segment
```

### 서버 내부 구조

```
main.c (epoll 이벤트 루프)
  ├── 새 연결 → handle_new_connection()
  └── 데이터 수신 → handle_client_data()
                        └── dispatch_command()   ← command.c
                              ├── led      → spawn_led()      ← workers.c
                              ├── buzzer   → spawn_buzzer()
                              ├── cds      → spawn_cds()      (joinable thread)
                              └── segment  → spawn_segment()
```

- **epoll + Edge Trigger** — 최대 10개 클라이언트 동시 처리
- **pthread 워커** — 각 장치 명령은 별도 스레드에서 비동기 처리
- **semaphore** — led_sem / buzzer_sem / segment_sem 으로 장치 접근 동기화
- **dlopen/dlsym** — 런타임에 `.so` 동적 로딩, 서버 재시작 없이 라이브러리 교체 가능
- **데몬화** — fork() × 2 + setsid()로 터미널 완전 분리, PID 파일 `/tmp/device_server.pid`

---

## 핀 맵 (wiringPi 번호 기준)

| 장치 | wiringPi 핀 | BCM 핀 | 비고 |
|------|------------|--------|------|
| LED | 1 | GPIO 18 | PWM 출력 |
| Buzzer | 4 | GPIO 23 | softTone |
| CdS (PCF8591) | I2C (0x48) | SDA/SCL | `/dev/i2c-1` |
| 7-Segment | 22~29 | 여러 핀 | 7핀 직결 |

---

## 빌드 환경 준비

### 호스트 PC (Ubuntu/Debian)

```bash
# 크로스컴파일러
sudo apt install gcc-aarch64-linux-gnu

# 빌드 도구
sudo apt install cmake make

# SSH 자동 인증
sudo apt install sshpass

# wiringPi aarch64 크로스 라이브러리 (파이에서 복사하거나 설치)
sudo dpkg --add-architecture arm64
sudo apt update
sudo apt install libwiringpi-dev:arm64   # 없으면 아래 수동 방법 사용
```

### wiringPi 헤더/라이브러리 수동 복사 (apt로 설치 안 될 경우)

```bash
# 파이에서 호스트로 복사
scp pi@<파이IP>:/usr/include/wiringPi.h  /usr/aarch64-linux-gnu/include/
scp pi@<파이IP>:/usr/lib/libwiringPi.so  /usr/aarch64-linux-gnu/lib/
```

---

## 빌드 + 배포

### 전체 빌드 & 파이 배포 (한 번에)

```bash
chmod +x Build.sh
./Build.sh <user> <host>
```

예시:
```bash
./Build.sh jcb6477 172.20.27.219
```

실행하면 순서대로:
1. 의존성 확인 (cmake, aarch64-gcc, sshpass)
2. SSH 비밀번호 입력 (한 번만)
3. CMake로 ARM 크로스컴파일 → `.so` 4개 + `device_server` 생성
4. native gcc로 `client` 빌드
5. scp로 파이에 전송
6. systemd 서비스 등록 + 자동 시작

### Clean (로컬 + 파이 전체 삭제)

```bash
./Build.sh jcb6477 172.20.27.219 clean
```

---

## 빌드 결과물

| 파일 | 위치 | 설명 |
|------|------|------|
| `libled.so` | `libs/led/` | LED 제어 공유 라이브러리 (ARM) |
| `libbuzzer.so` | `libs/buzzer/` | Buzzer 제어 공유 라이브러리 (ARM) |
| `libcds.so` | `libs/cds/` | CdS 센서 공유 라이브러리 (ARM) |
| `lib7seg.so` | `libs/7seg/` | 7-Segment 공유 라이브러리 (ARM) |
| `device_server` | `build/` | 서버 바이너리 (ARM) |
| `client` | 프로젝트 루트 | 클라이언트 바이너리 (x86) |

---

## 클라이언트 사용법

```bash
./client <파이IP> <포트>
```

예시:
```bash
./client 172.20.27.219 9000
```

### 지원 명령어

| 명령어 | 설명 |
|--------|------|
| `led off` | LED 끄기 |
| `led low` | LED 낮은 밝기 (PWM 300) |
| `led mid` | LED 중간 밝기 (PWM 600) |
| `led max` | LED 최대 밝기 (PWM 1024) |
| `buzzer on` | 부저 켜기 (1000Hz) |
| `buzzer off` | 부저 끄기 |
| `cds` | 조도 모니터링 시작 (1초마다 출력, 아무 키로 중지) |
| `segment <0-9>` | 숫자부터 0까지 카운트다운 후 부저 3초 |

---

## 파이에서 서비스 관리

```bash
# 서비스 상태 확인
sudo systemctl status device_server

# 서비스 시작 / 중지 / 재시작
sudo systemctl start device_server
sudo systemctl stop device_server
sudo systemctl restart device_server

# 실시간 로그 확인
tail -f /tmp/device_server.log

# 부팅 자동 시작 활성화 / 비활성화
sudo systemctl enable device_server
sudo systemctl disable device_server
```

---

## 서비스 파일 구조

파이의 `/etc/systemd/system/device_server.service`:

```ini
[Unit]
Description=Device Control Server
After=network.target

[Service]
Type=forking
ExecStart=/home/<user>/server/device_server
WorkingDirectory=/home/<user>/server
Restart=on-failure
RestartSec=5
User=root
Environment=LD_LIBRARY_PATH=/home/<user>/server/libs/led: \
            /home/<user>/server/libs/buzzer: \
            /home/<user>/server/libs/cds: \
            /home/<user>/server/libs/7seg

[Install]
WantedBy=multi-user.target
```

- `Type=forking` — 서버가 직접 fork()로 데몬화하므로 forking 타입 사용
- `LD_LIBRARY_PATH` — dlopen() 상대경로 `.so` 탐색용
- `Restart=on-failure` — 크래시 시 5초 후 자동 재시작

---

## 트러블슈팅

### 서비스가 등록 안 될 때
```bash
# 파이에서 직접 확인
sudo systemctl status device_server
journalctl -u device_server -n 50
```

### 라이브러리 로드 실패 (`dlopen` 에러)
```bash
# libs 폴더 구조 확인
ls -la ~/server/libs/*/

# LD_LIBRARY_PATH 수동 설정 후 테스트
export LD_LIBRARY_PATH=~/server/libs/led:~/server/libs/buzzer:~/server/libs/cds:~/server/libs/7seg
~/server/device_server
```

### CdS I2C 연결 안 될 때
```bash
# I2C 활성화 확인
sudo raspi-config  # Interface Options → I2C → Enable

# PCF8591 주소 확인 (0x48이어야 함)
sudo i2cdetect -y 1
```

### 빌드 시 libcrypt 에러
```bash
sudo dpkg --add-architecture arm64
sudo apt update
sudo apt install libcrypt-dev:arm64
```

---

## 주요 상수 (globals.h)

| 상수 | 값 | 설명 |
|------|----|------|
| `SERVER_PORT` | 9000 | TCP 포트 |
| `MAX_CLIENTS` | 10 | 최대 동시 클라이언트 수 |
| `LED_PIN` | 1 | wiringPi LED 핀 번호 |
| `BUZZER_PIN` | 4 | wiringPi Buzzer 핀 번호 |
| `BUFFER_SIZE` | 1024 | 소켓 버퍼 크기 |
| `PCF8591_ADDR` | 0x48 | CdS ADC I2C 주소 |
| `THRESHOLD` | 180 | CdS 밝기/어둠 임계값 |

---

## 개발 환경

| 항목 | 내용 |
|------|------|
| 호스트 OS | Ubuntu (VirtualBox) |
| 타깃 보드 | Raspberry Pi (aarch64) |
| 크로스컴파일러 | aarch64-linux-gnu-gcc |
| 빌드 시스템 | CMake 3.16+ |
| 통신 | TCP Socket (epoll ET 방식) |
| GPIO 라이브러리 | wiringPi |