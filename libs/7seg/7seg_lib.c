// segment_lib.c
#include <wiringPi.h>
#include <stdio.h>


const int segment_pins[7] = {24, 25, 29, 28, 27, 23, 22};


const int number_patterns[10][7] = {
    {0, 0, 0, 0, 0, 0, 1},  // 0
    {1, 0, 0, 1, 1, 1, 1},  // 1
    {0, 0, 1, 0, 0, 1, 0},  // 2
    {0, 0, 0, 0, 1, 1, 0},  // 3
    {1, 0, 0, 1, 1, 0, 0},  // 4
    {0, 1, 0, 0, 1, 0, 0},  // 5
    {0, 1, 0, 0, 0, 0, 0},  // 6
    {0, 0, 0, 1, 1, 1, 1},  // 7
    {0, 0, 0, 0, 0, 0, 0},  // 8
    {0, 0, 0, 0, 1, 0, 0}   // 9
};

void clear_display(void) {
    for (int i = 0; i < 7; i++) {
        digitalWrite(segment_pins[i], HIGH);
    }
}

void display_number(int num) {
    for (int i = 0; i < 7; i++) {
        digitalWrite(segment_pins[i], number_patterns[num][i]);
    }
}

int init_segment_display(void) {
    for (int i = 0; i < 7; i++) {
        pinMode(segment_pins[i], OUTPUT);
    }
    clear_display();
    printf("[.so SEGMENT] Hardware initialization complete.\n");
    return 0;
}

void start_segment_countdown(int start_num) {
    if (start_num < 0 || start_num > 9) {
        printf("[.so SEGMENT] Error: Input %d is out of range (0-9).\n", start_num);
        return;
    }

    printf("[.so SEGMENT] Starting countdown from %d...\n", start_num);
    for (int i = start_num; i >= 0; i--) {
        display_number(i);
        delay(1000); 
    }
    printf("[.so SEGMENT] Countdown finished! (Holding '0')\n");
}