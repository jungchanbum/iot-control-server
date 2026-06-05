// led_lib.c
#include <wiringPi.h>
#include <stdio.h>
#include <string.h>

// Dynamic function that takes a pin number and a command string
void control_led_brightness(int pin, const char *command) {
    if (strcmp(command, "off") == 0) {
        pwmWrite(pin, 0);
        printf("[.so LIB] LED (Pin %d) OFF\n", pin);
    } 
    else if (strcmp(command, "low") == 0) {
        pwmWrite(pin, 300);
        printf("[.so LIB] LED (Pin %d) LOW\n", pin);
    } 
    else if (strcmp(command, "mid") == 0) {
        pwmWrite(pin, 600);
        printf("[.so LIB] LED (Pin %d) MEDIUM\n", pin);
    } 
    else if (strcmp(command, "max") == 0) {
        pwmWrite(pin, 1024);
        printf("[.so LIB] LED (Pin %d) MAX\n", pin);
    } 
    else {
        printf("[.so LIB] Wrong command! (%s)\n", command);
    }
}
