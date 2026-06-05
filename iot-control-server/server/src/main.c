#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <wiringPi.h>

#include "globals.h"
#include "lib_loader.h"
#include "workers.h"
#include "command.h"

#define MAX_EVENTS  32
#define LOG_FILE    "/tmp/device_server.log"
#define PID_FILE    "/tmp/device_server.pid"

static volatile sig_atomic_t g_shutdown = 0;
static int server_fd = -1;
static int epoll_fd  = -1;
static FILE *log_fp  = NULL;

static void log_msg(const char *msg)
{
    if (log_fp) {
        fprintf(log_fp, "%s", msg);
        fflush(log_fp);
    }
}

/* daemonize — systemd 없이 직접 실행할 때를 위해 보존 */
static void daemonize(void)
{
    pid_t pid;

    pid = fork();
    if (pid < 0) { perror("fork"); exit(EXIT_FAILURE); }
    if (pid > 0) exit(EXIT_SUCCESS);

    if (setsid() < 0) { perror("setsid"); exit(EXIT_FAILURE); }

    pid = fork();
    if (pid < 0) { perror("fork2"); exit(EXIT_FAILURE); }
    if (pid > 0) exit(EXIT_SUCCESS);

    /* 실행 파일 위치 기준으로 작업 디렉터리 결정 */
    char exec_path[512] = {0};
    char work_dir[512]  = {0};
    ssize_t len = readlink("/proc/self/exe", exec_path, sizeof(exec_path) - 1);
    if (len > 0) {
        exec_path[len] = '\0';
        strncpy(work_dir, exec_path, sizeof(work_dir) - 1);
        char *slash = strrchr(work_dir, '/');
        if (slash) *slash = '\0';
    } else {
        if (getcwd(work_dir, sizeof(work_dir)) == NULL)
            strncpy(work_dir, ".", sizeof(work_dir) - 1);
    }
    if (chdir(work_dir) < 0)
        fprintf(stderr, "[daemon] chdir(%s) failed\n", work_dir);

    umask(0);

    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        close(devnull);
    }

    log_fp = fopen(LOG_FILE, "a");

    FILE *pf = fopen(PID_FILE, "w");
    if (pf) { fprintf(pf, "%d\n", getpid()); fclose(pf); }

    log_msg("[daemon] Started.\n");
}

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
        if (client_fds[i] == fd) { client_fds[i] = -1; break; }
    }
    pthread_mutex_unlock(&clients_mutex);
}

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
            log_msg("[main] accept error\n");
        return;
    }

    int slot = client_add(fd);
    if (slot < 0) {
        send_to(fd, "[server] Too many clients. Try later.\n");
        close(fd);
        return;
    }

    set_nonblocking(fd);

    struct epoll_event ev = { .events = EPOLLIN | EPOLLET, .data.fd = fd };
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        log_msg("[main] epoll_ctl add failed\n");
        client_remove(fd);
        close(fd);
        return;
    }

    log_msg("[main] Client connected.\n");

    send_to(fd, "==============================================================\n");
    send_to(fd, "              Device Control Server ready                     \n");
    send_to(fd, " led [off|low|mid|max]  buzzer [on|off]  cds  segment [0-9]  \n");
    send_to(fd, "==============================================================\n");
    send_to(fd, "cmd >> ");
}

static void disconnect_client(int fd)
{
    log_msg("[main] Client disconnected.\n");
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    client_remove(fd);
    close(fd);

    if (client_count() == 0 && cds_loop_active) {
        log_msg("[main] No clients. Stopping CdS.\n");
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
    log_msg("[main] Shutting down...\n");

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
    remove(PID_FILE);

    log_msg("[main] Cleanup complete.\n");
    if (log_fp) { fclose(log_fp); log_fp = NULL; }
}

int main(void)
{
    /* systemd로 관리 시 daemonize 불필요 — 주석 처리
     * 직접 실행 시엔 아래 주석 해제
     * daemonize();
     */

    /* systemd Type=simple 이므로 로그파일 직접 열기 */
    log_fp = fopen(LOG_FILE, "a");

    signal(SIGINT,  sigint_handler);
    signal(SIGTERM, sigint_handler);

    for (int i = 0; i < MAX_CLIENTS; i++) client_fds[i] = -1;

    sem_init(&led_sem,     0, 1);
    sem_init(&buzzer_sem,  0, 1);
    sem_init(&segment_sem, 0, 1);

    if (wiringPiSetup() == -1) {
        log_msg("[main] wiringPi init failed.\n");
        return EXIT_FAILURE;
    }
    if (load_all_libraries() < 0) {
        log_msg("[main] Library load failed.\n");
        return EXIT_FAILURE;
    }

    pinMode(LED_PIN, PWM_OUTPUT);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { log_msg("[main] socket failed\n"); return EXIT_FAILURE; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_nonblocking(server_fd);

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons(SERVER_PORT),
    };
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_msg("[main] bind failed\n"); return EXIT_FAILURE;
    }
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        log_msg("[main] listen failed\n"); return EXIT_FAILURE;
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) { log_msg("[main] epoll_create1 failed\n"); return EXIT_FAILURE; }

    struct epoll_event ev = { .events = EPOLLIN, .data.fd = server_fd };
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
        log_msg("[main] epoll_ctl failed\n"); return EXIT_FAILURE;
    }

    log_msg("[main] Waiting for clients...\n");

    struct epoll_event events[MAX_EVENTS];

    while (!g_shutdown) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, 500);
        if (n < 0) {
            if (errno == EINTR) continue;
            log_msg("[main] epoll_wait error\n");
            break;
        }
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            if (fd == server_fd) handle_new_connection();
            else                 handle_client_data(fd);
        }
    }

    cleanup();
    return EXIT_SUCCESS;
}