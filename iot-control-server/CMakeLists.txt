cmake_minimum_required(VERSION 3.16)
project(DeviceServer C)

# ══════════════════════════════════════════════════════════════════
#  Cross-compile Toolchain  (aarch64-linux-gnu-gcc)
# ══════════════════════════════════════════════════════════════════
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_C_FLAGS    "-Wall -Wextra -O2 -fPIC -Wno-unused-result -Wno-unused-function")

# ARM 타깃에서 wiringPi 헤더/라이브러리 탐색 경로
# (sysroot 또는 실제 파이에서 가져온 헤더가 있을 경우 아래 경로 수정)
set(WIRING_INCLUDE_DIR "/usr/aarch64-linux-gnu/include" CACHE PATH "wiringPi include dir")
set(WIRING_LIB_DIR     "/usr/aarch64-linux-gnu/lib"     CACHE PATH "wiringPi lib dir")

include_directories(${WIRING_INCLUDE_DIR})
link_directories(${WIRING_LIB_DIR})

# ══════════════════════════════════════════════════════════════════
#  공유 라이브러리 4개  (.so → libs/<name>/)
# ══════════════════════════════════════════════════════════════════

# --- LED ---
add_library(led SHARED libs/led/led_lib.c)
target_link_libraries(led wiringPi)
set_target_properties(led PROPERTIES
    OUTPUT_NAME      led
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/libs/led
)

# --- Buzzer ---
add_library(buzzer SHARED libs/buzzer/buzzer_lib.c)
target_link_libraries(buzzer wiringPi)
set_target_properties(buzzer PROPERTIES
    OUTPUT_NAME      buzzer
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/libs/buzzer
)

# --- CdS ---
add_library(cds SHARED libs/cds/cds_lib.c)
target_link_libraries(cds wiringPi)
set_target_properties(cds PROPERTIES
    OUTPUT_NAME      cds
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/libs/cds
)

# --- 7-Segment ---
add_library(7seg SHARED libs/7seg/7seg_lib.c)
target_link_libraries(7seg wiringPi)
set_target_properties(7seg PROPERTIES
    OUTPUT_NAME      7seg
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/libs/7seg
)

# ══════════════════════════════════════════════════════════════════
#  서버 바이너리  (ARM)  →  build/device_server
# ══════════════════════════════════════════════════════════════════
set(SERVER_SRCS
    server/src/main.c
    server/src/command.c
    server/src/globals.c
    server/src/lib_loader.c
    server/src/workers.c
)

add_executable(device_server ${SERVER_SRCS})

target_include_directories(device_server PRIVATE
    server/include
    ${WIRING_INCLUDE_DIR}
)

target_link_libraries(device_server
    wiringPi
    pthread
    dl
    crypt           # libwiringPi.so → crypt@XCRYPT_2.0 의존성 해결
)

# libcrypt.so.1 위치를 링커에게 알려줌 (크로스컴파일 환경 누락 대응)
target_link_options(device_server PRIVATE
    -Wl,-rpath-link,${WIRING_LIB_DIR}
)

set_target_properties(device_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build
)

# ══════════════════════════════════════════════════════════════════
#  클라이언트  (native gcc, ARM 아님)
# ══════════════════════════════════════════════════════════════════
add_executable(client client.c)

# 클라이언트는 호스트 gcc로 빌드 — toolchain override
set_target_properties(client PROPERTIES
    C_COMPILER gcc               # host gcc
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# CMake 변수로 컴파일러를 직접 오버라이드 (toolchain 파일 없이)
if(CMAKE_CROSSCOMPILING)
    set_target_properties(client PROPERTIES
        LINKER_LANGUAGE C
    )
    # 클라이언트만 호스트 컴파일러로 따로 빌드하도록 EXCLUDE
    # → build.sh 에서 gcc client.c -o client 로 별도 처리
    set_target_properties(client PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()