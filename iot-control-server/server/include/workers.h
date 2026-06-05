#ifndef WORKERS_H
#define WORKERS_H

/* ─── Thread Worker Entry Points ────────────────────────────────── */
void *led_worker(void *arg);
void *buzzer_worker(void *arg);
void *cds_worker(void *arg);
void *segment_worker(void *arg);

/* ─── Spawn Helpers (alloc args + pthread_create + detach) ─────── */
void spawn_led(const char *cmd);
void spawn_buzzer(const char *cmd);
void spawn_segment(int count);

/* cds thread is joinable, so handle is kept in globals */
void spawn_cds(void);

#endif /* WORKERS_H */
