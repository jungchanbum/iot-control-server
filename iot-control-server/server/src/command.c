#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "command.h"
#include "workers.h"
#include "globals.h"

/* ─── Individual command handlers ───────────────────────────────── */

static void handle_led(const char *arg)
{
    if (!*arg) {
        send_msg("[command] Usage: led <off|low|mid|max>\n");
        return;
    }
    char msg[BUFFER_SIZE];
    snprintf(msg, sizeof(msg), "[command] LED → %s\n", arg);
    send_msg(msg);
    spawn_led(arg);
}

static void handle_buzzer(const char *arg)
{
    if (!*arg) {
        send_msg("[command] Usage: buzzer <on|off>\n");
        return;
    }
    char msg[BUFFER_SIZE];
    snprintf(msg, sizeof(msg), "[command] Buzzer → %s\n", arg);
    send_msg(msg);
    spawn_buzzer(arg);
}

static void handle_cds(void)
{
    spawn_cds();
    send_msg("[command] CdS monitoring ENABLED. Press any key to stop.\n");
}

static void handle_segment(const char *arg)
{
    char *endptr;
    int   count = (int)strtol(arg, &endptr, 10);
    if (endptr == arg) {
        send_msg("[command] Error: 'segment' requires a numeric argument.\n");
        return;
    }
    if (count < 0 || count > 9) {
        send_msg("[command] Error: value out of range (0-9).\n");
        return;
    }
    char msg[BUFFER_SIZE];
    snprintf(msg, sizeof(msg), "[command] Segment countdown started: %d\n", count);
    send_msg(msg);
    spawn_segment(count);
}

/* ─── Public dispatcher ─────────────────────────────────────────── */

void dispatch_command(const char *buf)
{
    /* cds 활성화 중이면 어떤 입력이든 cds 종료 후 프롬프트 복귀 */
    if (cds_loop_active) {
        cds_loop_active = 0;
        pthread_join(cds_thread, NULL);
        cds_thread = 0;
        send_msg("[command] CdS monitoring DISABLED.\n");
        send_msg("cmd >> ");
        return;
    }

    if      (strncmp(buf, "led ",     4) == 0) handle_led(buf + 4);
    else if (strncmp(buf, "buzzer ",  7) == 0) handle_buzzer(buf + 7);
    else if (strcmp (buf, "cds")        == 0)  handle_cds();
    else if (strncmp(buf, "segment ", 8) == 0) handle_segment(buf + 8);
    else {
        char msg[BUFFER_SIZE];
        snprintf(msg, sizeof(msg),
            "[command] Unknown: '%s'\n"
            "          Available: led / buzzer / cds / segment\n", buf);
        send_msg(msg);
    }
}