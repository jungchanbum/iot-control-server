#ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#include "workers.h"
#include "globals.h"

static char *heap_str(const char *s)
{
    char *copy = malloc(strlen(s) + 1);
    if (!copy) { perror("[workers] malloc"); return NULL; }
    strcpy(copy, s);
    return copy;
}

/* ══════════════════════════════════════════════════════════════════
 *  Worker Functions
 * ══════════════════════════════════════════════════════════════════ */

void *led_worker(void *arg)
{
    char *cmd = (char *)arg;
    sem_wait(&led_sem);
    control_led(LED_PIN, cmd);
    sem_post(&led_sem);
    free(cmd);
    return NULL;
}

void *buzzer_worker(void *arg)
{
    char *cmd = (char *)arg;
    sem_wait(&buzzer_sem);
    control_buzzer(cmd);
    sem_post(&buzzer_sem);
    free(cmd);
    return NULL;
}

void *cds_worker(void *arg)
{
    (void)arg;
    char msg[BUFFER_SIZE];

    if (!get_cds_status) {
        send_msg("[cds_worker] Error: get_cds_status not loaded.\n");
        return NULL;
    }

    send_msg("[cds_worker] Monitoring started. (any key to stop)\n");

    /* Continuously send status + level every 500ms */
    while (cds_loop_active) {
        sem_wait(&led_sem);
        int level = get_light_level();
        const char *raw   = get_cds_status();
        sem_post(&led_sem);

        if (level >= 0)
            snprintf(msg, sizeof(msg), "[cds] %s  (level: %d)\n", raw, level);
        else
            snprintf(msg, sizeof(msg), "[cds] %s\n", raw);
        send_msg(msg);

        /* LED control */
        sem_wait(&led_sem);
        if      (strcmp(raw, "Bright!!") == 0) control_led(LED_PIN, "off");
        else if (strcmp(raw, "Dark!!")   == 0) control_led(LED_PIN, "max");
        sem_post(&led_sem);

        sleep(1);
    }

    send_msg("[cds_worker] Monitoring stopped.\n");
    return NULL;
}

void *segment_worker(void *arg)
{
    int count = *(int *)arg;
    free(arg);

    char msg[BUFFER_SIZE];
    snprintf(msg, sizeof(msg), "[segment_worker] Countdown started from %d.\n", count);
    send_msg(msg);

    sem_wait(&segment_sem);
    start_countdown(count);
    sem_post(&segment_sem);

    send_msg("[segment_worker] Countdown complete. Buzzer on.\n");

    sem_wait(&buzzer_sem);
    control_buzzer("on");
    sem_post(&buzzer_sem);

    sleep(3);

    sem_wait(&buzzer_sem);
    control_buzzer("off");
    sem_post(&buzzer_sem);

    send_msg("[segment_worker] Buzzer off.\n");
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════
 *  Spawn Helpers
 * ══════════════════════════════════════════════════════════════════ */

void spawn_led(const char *cmd)
{
    char *arg = heap_str(cmd);
    if (!arg) return;
    pthread_t t;
    if (pthread_create(&t, NULL, led_worker, arg) != 0) {
        perror("[workers] pthread_create led"); free(arg);
    } else pthread_detach(t);
}

void spawn_buzzer(const char *cmd)
{
    char *arg = heap_str(cmd);
    if (!arg) return;
    pthread_t t;
    if (pthread_create(&t, NULL, buzzer_worker, arg) != 0) {
        perror("[workers] pthread_create buzzer"); free(arg);
    } else pthread_detach(t);
}

void spawn_segment(int count)
{
    int *arg = malloc(sizeof(int));
    if (!arg) { perror("[workers] malloc"); return; }
    *arg = count;
    pthread_t t;
    if (pthread_create(&t, NULL, segment_worker, arg) != 0) {
        perror("[workers] pthread_create segment"); free(arg);
    } else pthread_detach(t);
}

void spawn_cds(void)
{
    cds_loop_active = 1;
    if (pthread_create(&cds_thread, NULL, cds_worker, NULL) != 0) {
        perror("[workers] pthread_create cds");
        cds_loop_active = 0;
    }
}