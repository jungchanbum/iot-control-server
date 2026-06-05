  # Device Control Server

라즈베리파이 GPIO 장치(LED, Buzzer, CdS, 7-Segment)를 TCP 소켓으로 원격 제어하는 데몬 서버 프로젝트.

---

## 프로젝트 구조

```
.
├── Build.sh                  # 빌드 + 파이 전송 + systemd 등록 스크립트
├── client.c                  # TCP 클라이언트 (호스트 PC에서 실행)
├── CMakeLists.txt            # 크로스컴파일 빌드 설정
├── libs/
│   ├── led/led_lib.c
│   ├── buzzer/buzzer_lib.c
│   ├── cds/cds_lib.c
│   └── 7seg/7seg_lib.c
└── server/
    ├── include/
    │   ├── globals.h
    │   ├── command.h
    │   ├── lib_loader.h
    │   └── workers.h
    └── src/
        ├── main.c
        ├── command.c
        ├── globals.c
        ├── lib_loader.c
        └── workers.c
```

---

## 호스트 PC 환경 준비

Ubuntu/Debian 기준.

```bash
# 크로스컴파일러
sudo apt install gcc-aarch64-linux-gnu

# 빌드 도구
sudo apt install cmake make

# SSH 자동 인증
sudo apt install sshpass
```

wiringPi 크로스컴파일 헤더/라이브러리가 없을 경우 파이에서 직접 복사:

```bash
scp <user>@<pi_IP>:/usr/include/wiringPi.h  /usr/aarch64-linux-gnu/include/
scp <user>@<pi_IP>:/usr/lib/libwiringPi.so  /usr/aarch64-linux-gnu/lib/
```

---

## 빌드 및 배포

### 전체 빌드 + 파이 배포 + 서비스 등록

```bash
chmod +x Build.sh
./Build.sh <user> <host>
```

예시:

```bash
./Build.sh jcb6477 172.20.27.219
```

실행하면 순서대로 진행:

```
1. 의존성 확인 (cmake, aarch64-linux-gnu-gcc, gcc, sshpass)
2. SSH 비밀번호 입력 (한 번만)
3. 파이 포트 9000 감지
   └── 이미 실행 중이면 client만 빌드하고 종료
4. CMake ARM 크로스컴파일
   └── libled.so / libbuzzer.so / libcds.so / lib7seg.so / device_server 생성
5. native gcc로 client 빌드 (SERVER_IP 자동 주입)
6. 파이 I2C 활성화 확인
   └── /dev/i2c-1 없으면 자동 활성화 (modprobe)
7. 파이로 파일 전송
   └── device_server + .so 4개만 전송
8. systemd 서비스 등록 + 자동 시작
```

### Clean (로컬 + 파이 전체 삭제)

```bash
./Build.sh <user> <host> clean
```

---

## 빌드 결과물

| 파일 | 위치 | 설명 |
|------|------|------|
| `libled.so` | `libs/led/` | LED 제어 라이브러리 (ARM) |
| `libbuzzer.so` | `libs/buzzer/` | Buzzer 제어 라이브러리 (ARM) |
| `libcds.so` | `libs/cds/` | CdS 센서 라이브러리 (ARM) |
| `lib7seg.so` | `libs/7seg/` | 7-Segment 라이브러리 (ARM) |
| `device_server` | `build/` | 서버 바이너리 (ARM) |
| `client` | 프로젝트 루트 | 클라이언트 바이너리 (x86) |

파이 전송 대상: `device_server` + `.so` 4개만.

---

## 클라이언트 실행

빌드 시 입력한 파이 IP가 자동으로 `client` 에 주입되므로 별도 설정 없이 실행:

```bash
./client
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
# 상태 확인
sudo systemctl status device_server

# 시작 / 중지 / 재시작
sudo systemctl start device_server
sudo systemctl stop device_server
sudo systemctl restart device_server

# 실시간 로그
tail -f /tmp/device_server.log

# systemd 로그
sudo journalctl -u device_server -f
```

---

## 핀 맵 (wiringPi 번호 기준)

| 장치 | wiringPi 핀 | BCM 핀 | 비고 |
|------|------------|--------|------|
| LED | 1 | GPIO 18 | PWM 출력 |
| Buzzer | 4 | GPIO 23 | softTone |
| CdS (PCF8591) | I2C (0x48) | SDA/SCL | `/dev/i2c-1` |
| 7-Segment | 22~29 | 여러 핀 | 7핀 직결 |

---

## I2C 확인

```bash
# /dev/i2c-1 존재 확인
ls /dev/i2c*

# PCF8591 센서 주소 확인 (0x48 이어야 함)
sudo i2cdetect -y 1
```

I2C가 비활성화된 파이는 `Build.sh` 실행 시 자동으로 활성화됨.

---

## 트러블슈팅

### 서비스 실패 시

```bash
sudo journalctl -u device_server -n 30 --no-pager
```

### 라이브러리 로드 실패

```bash
ls -la ~/server/libs/*/
export LD_LIBRARY_PATH=~/server/libs/led:~/server/libs/buzzer:~/server/libs/cds:~/server/libs/7seg
~/server/device_server
```

### libcrypt 빌드 에러

```bash
sudo dpkg --add-architecture arm64
sudo apt update
sudo apt install libcrypt-dev:arm64
```

### CMake 캐시 충돌

```bash
rm -rf build/
./Build.sh <user> <host>
```

---

## 주요 상수

| 상수 | 값 | 설명 |
|------|----|------|
| `SERVER_PORT` | 9000 | TCP 포트 |
| `MAX_CLIENTS` | 10 | 최대 동시 클라이언트 |
| `LED_PIN` | 1 | wiringPi LED 핀 |
| `BUZZER_PIN` | 4 | wiringPi Buzzer 핀 |
| `PCF8591_ADDR` | 0x48 | CdS I2C 주소 |
| `THRESHOLD` | 180 | CdS 밝기 임계값 |

---

## 개발 환경

| 항목 | 내용 |
|------|------|
| 호스트 OS | Ubuntu (VirtualBox) |
| 타깃 보드 | Raspberry Pi 4B (aarch64) |
| 크로스컴파일러 | aarch64-linux-gnu-gcc |
| 빌드 시스템 | CMake 3.16+ |
| 통신 | TCP Socket (epoll ET) |
| GPIO 라이브러리 | wiringPi |
