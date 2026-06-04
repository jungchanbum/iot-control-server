#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <wiringPi.h>

#include "globals.h"
#include "lib_loader.h"
#include "workers.h"
#include "command.h"

#define MAX_EVENTS 32

static volatile sig_atomic_t g_shutdown = 0;
static int server_fd = -1;
static int epoll_fd  = -1;

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int client_add(int fd)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] < 0) {
            client_fds[i] = fd;
            pthread_mutex_unlock(&clients_mutex);
            return i;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return -1;
}

static void client_remove(int fd)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] == fd) {
            client_fds[i] = -1;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

/* ─── Count active clients ──────────────────────────────────────── */
static int client_count(void)
{
    int cnt = 0;
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (client_fds[i] >= 0) cnt++;
    pthread_mutex_unlock(&clients_mutex);
    return cnt;
}

static void sigint_handler(int signo)
{
    (void)signo;
    g_shutdown      = 1;
    cds_loop_active = 0;
    if (server_fd >= 0) shutdown(server_fd, SHUT_RDWR);
}

static void handle_new_connection(void)
{
    struct sockaddr_in cli_addr;
    socklen_t cli_len = sizeof(cli_addr);

    int fd = accept(server_fd, (struct sockaddr *)&cli_addr, &cli_len);
    if (fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("[main] accept");
        return;
    }

    int slot = client_add(fd);
    if (slot < 0) {
        send_to(fd, "[server] Too many clients. Try later.\n");
        close(fd);
        return;
    }

    set_nonblocking(fd);

    struct epoll_event ev = {
        .events  = EPOLLIN | EPOLLET,
        .data.fd = fd,
    };
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        perror("[main] epoll_ctl add");
        client_remove(fd);
        close(fd);
        return;
    }

    printf("[main] Client connected (slot %d, fd %d).\n", slot, fd);

    send_to(fd, "==============================================================\n");
    send_to(fd, "              Device Control Server ready                     \n");
    send_to(fd, " led [off|low|mid|max]  buzzer [on|off]  cds  segment [0-9]  \n");
    send_to(fd, "==============================================================\n");
    send_to(fd, "cmd >> ");
}

static void disconnect_client(int fd)
{
    printf("[main] Client (fd %d) disconnected.\n", fd);
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    client_remove(fd);
    close(fd);

    /* 마지막 클라이언트가 끊기면 cds 자동 종료 */
    if (client_count() == 0 && cds_loop_active) {
        printf("[main] No clients left. Stopping CdS monitor.\n");
        cds_loop_active = 0;
        pthread_join(cds_thread, NULL);
        cds_thread = 0;
    }
}

static void handle_client_data(int fd)
{
    char buf[BUFFER_SIZE];

    while (1) {
        ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("[main] recv");
            goto disconnect;
        }
        if (n == 0) goto disconnect;

        buf[n] = '\0';
        buf[strcspn(buf, "\r\n")] = '\0';
        if (buf[0] == '\0') continue;

        dispatch_command(buf);
    }
    return;

disconnect:
    disconnect_client(fd);
}

static void cleanup(void)
{
    printf("\n[main] Shutting down...\n");

    cds_loop_active = 0;
    if (cds_thread) { pthread_join(cds_thread, NULL); cds_thread = 0; }

    sem_wait(&led_sem);
    if (control_led)    control_led(LED_PIN, "off");
    sem_post(&led_sem);

    sem_wait(&buzzer_sem);
    if (control_buzzer) control_buzzer("off");
    sem_post(&buzzer_sem);

    sem_destroy(&led_sem);
    sem_destroy(&buzzer_sem);
    sem_destroy(&segment_sem);
    pthread_mutex_destroy(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++)
        if (client_fds[i] >= 0) { close(client_fds[i]); client_fds[i] = -1; }

    if (epoll_fd  >= 0) { close(epoll_fd);  epoll_fd  = -1; }
    if (server_fd >= 0) { close(server_fd); server_fd = -1; }

    unload_all_libraries();
    printf("[main] Cleanup complete.\n");
}

int main(void)
{
    signal(SIGINT,  sigint_handler);
    signal(SIGTERM, sigint_handler);

    for (int i = 0; i < MAX_CLIENTS; i++) client_fds[i] = -1;

    sem_init(&led_sem,     0, 1);
    sem_init(&buzzer_sem,  0, 1);
    sem_init(&segment_sem, 0, 1);

    if (wiringPiSetup() == -1) {
        fprintf(stderr, "[main] wiringPi init failed.\n");
        return EXIT_FAILURE;
    }
    if (load_all_libraries() < 0) {
        fprintf(stderr, "[main] Library load failed.\n");
        return EXIT_FAILURE;
    }

    pinMode(LED_PIN, PWM_OUTPUT);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("[main] socket"); return EXIT_FAILURE; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_nonblocking(server_fd);

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons(SERVER_PORT),
    };
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[main] bind"); return EXIT_FAILURE;
    }
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("[main] listen"); return EXIT_FAILURE;
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) { perror("[main] epoll_create1"); return EXIT_FAILURE; }

    struct epoll_event ev = { .events = EPOLLIN, .data.fd = server_fd };
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
        perror("[main] epoll_ctl server"); return EXIT_FAILURE;
    }

    printf("==============================================================\n");
    printf("   Device Control Server  (TCP :%d, max %d clients)         \n",
           SERVER_PORT, MAX_CLIENTS);
    printf("==============================================================\n");
    printf(" Waiting for clients...\n\n");

    struct epoll_event events[MAX_EVENTS];

    while (!g_shutdown) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, 500);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("[main] epoll_wait");
            break;
        }

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            if (fd == server_fd)
                handle_new_connection();
            else
                handle_client_data(fd);
        }
    }

    cleanup();
    return EXIT_SUCCESS;
}