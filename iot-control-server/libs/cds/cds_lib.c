#include <wiringPi.h>
#include <wiringPiI2C.h>
#include <stdio.h>

#define PCF8591_ADDR 0x48
#define THRESHOLD    180

static int i2c_fd = -1;

int init_cds_sensor(void) {
    i2c_fd = wiringPiI2CSetupInterface("/dev/i2c-1", PCF8591_ADDR);
    if (i2c_fd < 0) {
        printf("[.so CDS] Failed to initialize I2C interface!\n");
        return -1;
    }
    printf("[.so CDS] I2C initialized.\n");
    return 0;
}

/* Returns raw ADC value (0-255), -1 if not initialized */
int get_light_level(void) {
    if (i2c_fd < 0) return -1;
    wiringPiI2CWrite(i2c_fd, 0x00);
    wiringPiI2CRead(i2c_fd);        /* dummy read */
    return wiringPiI2CRead(i2c_fd);
}

/* Returns "Bright!!" or "Dark!!" — no printf */
const char *get_environment_status(void) {
    if (i2c_fd < 0) return "Unknown";
    return (get_light_level() < THRESHOLD) ? "Bright!!" : "Dark!!";
}