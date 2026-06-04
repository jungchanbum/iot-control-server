// buzzer_lib.c
#include <wiringPi.h>
#include <softTone.h>
#include <stdio.h>
#include <string.h>

#define BUZZER_PIN 23 // BCM 23은 wiringPi 번호로 4번이야!

// 👈 초기화 함수 추가
int init_buzzer(void) {
    if (softToneCreate(4) != 0) { // wiringPiSetup 기준이므로 pin 4 사용
        printf("[.so LIB] Buzzer thread creation failed!\n");
        return -1;
    }
    printf("[.so LIB] Buzzer thread initialized on wiringPi pin 4.\n");
    return 0;
}

void control_buzzer(const char *command) {
    if (strcmp(command, "on") == 0) {
        softToneWrite(4, 1000); // wiringPi pin 4
        printf("[.so LIB] Buzzer ON - 1000Hz\n");
    } 
    else if (strcmp(command, "off") == 0) {
        softToneWrite(4, 0);
        printf("[.so LIB] Buzzer OFF\n");
    } 
    else {
        printf("[.so LIB] Wrong command! (%s)\n", command);
    }
}