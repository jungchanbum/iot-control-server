#ifndef GLOBALS_H
#define GLOBALS_H

#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>

/* ─── Constants ────────────────────────────────────────────────── */
#define LED_PIN     1
#define BUZZER_PIN  4
#define BUFFER_SIZE 1024
#define SERVER_PORT 9000
#define MAX_CLIENTS 10

/* ─── Shared Library Handles ───────────────────────────────────── */
extern void *led_handle;
extern void *buzzer_handle;
extern void *cds_handle;
extern void *segment_handle;

/* ─── Function Pointers ────────────────────────────────────────── */
extern void        (*control_led)(int, const char *);
extern int         (*init_buzzer)(void);
extern void        (*control_buzzer)(const char *);
extern int         (*init_cds)(void);
extern const char *(*get_cds_status)(void);
extern int         (*get_light_level)(void);
extern int         (*init_segment)(void);
extern void        (*start_countdown)(int);

/* ─── CdS Thread State ─────────────────────────────────────────── */
extern pthread_t             cds_thread;
extern volatile sig_atomic_t cds_loop_active;

/* ─── Per-device Semaphores ────────────────────────────────────── */
extern sem_t led_sem;      /* led_worker + cds_worker       */
extern sem_t buzzer_sem;   /* buzzer_worker + segment 부저  */
extern sem_t segment_sem;  /* segment_worker 단독           */

/* ─── Client fd table ──────────────────────────────────────────── */
extern int             client_fds[MAX_CLIENTS];
extern pthread_mutex_t clients_mutex;

/* ─── Broadcast to all connected clients ───────────────────────── */
static inline void send_msg(const char *msg)
{
    size_t len = strlen(msg);
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (client_fds[i] >= 0)
            send(client_fds[i], msg, len, 0);
    pthread_mutex_unlock(&clients_mutex);
}

/* ─── Send to one specific client ──────────────────────────────── */
static inline void send_to(int fd, const char *msg)
{
    send(fd, msg, strlen(msg), 0);
}

#endif /* GLOBALS_H */