#include "globals.h"

/* ─── Shared Library Handles ───────────────────────────────────── */
void *led_handle     = NULL;
void *buzzer_handle  = NULL;
void *cds_handle     = NULL;
void *segment_handle = NULL;

/* ─── Function Pointers ─────────────────────────────────────────── */
void        (*control_led)(int, const char *) = NULL;
int         (*init_buzzer)(void)              = NULL;
void        (*control_buzzer)(const char *)   = NULL;
int         (*init_cds)(void)                 = NULL;
const char *(*get_cds_status)(void)           = NULL;
int         (*get_light_level)(void)          = NULL;
int         (*init_segment)(void)             = NULL;
void        (*start_countdown)(int)           = NULL;

/* ─── CdS Thread State ──────────────────────────────────────────── */
pthread_t             cds_thread      = 0;
volatile sig_atomic_t cds_loop_active = 0;

/* ─── Per-device Semaphores ─────────────────────────────────────── */
sem_t led_sem;
sem_t buzzer_sem;
sem_t segment_sem;

/* ─── Client fd table ───────────────────────────────────────────── */
int             client_fds[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;