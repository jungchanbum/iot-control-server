#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

/* SERVER_IP는 컴파일 시 -DSERVER_IP="\"...\"" 로 주입됨 */
#ifndef SERVER_IP
#  define SERVER_IP "127.0.0.1"
#endif

#define SERVER_PORT 9000
#define BUFFER_SIZE 1024

static int sock_fd = -1;
static volatile sig_atomic_t g_shutdown = 0;

static void sigint_handler(int signo)
{
    (void)signo;
    g_shutdown = 1;
    if (sock_fd >= 0) shutdown(sock_fd, SHUT_RDWR);
}

static void *recv_thread(void *arg)
{
    (void)arg;
    char buf[BUFFER_SIZE];
    ssize_t n;

    while (!g_shutdown) {
        n = recv(sock_fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = '\0';
        printf("%s", buf);
        fflush(stdout);
    }
    g_shutdown = 1;
    return NULL;
}

static void send_loop(void)
{
    char buf[BUFFER_SIZE];
    fd_set fds;

    while (!g_shutdown) {
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);

        struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };
        int ret = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);

        if (ret < 0)  break;
        if (ret == 0) continue;
        if (g_shutdown) break;

        if (fgets(buf, sizeof(buf), stdin) == NULL) break;

        ssize_t len = (ssize_t)strlen(buf);
        if (send(sock_fd, buf, len, 0) < 0) break;
    }
}

int main(void)
{
    signal(SIGINT,  sigint_handler);
    signal(SIGTERM, sigint_handler);

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) { perror("[client] socket"); return EXIT_FAILURE; }

    struct sockaddr_in srv = {
        .sin_family = AF_INET,
        .sin_port   = htons(SERVER_PORT),
    };
    if (inet_pton(AF_INET, SERVER_IP, &srv.sin_addr) <= 0) {
        fprintf(stderr, "[client] Invalid IP: %s\n", SERVER_IP);
        return EXIT_FAILURE;
    }
    if (connect(sock_fd, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        perror("[client] connect");
        return EXIT_FAILURE;
    }
    printf("[client] Connected to %s:%d  (Ctrl+C to quit)\n", SERVER_IP, SERVER_PORT);

    pthread_t t;
    pthread_create(&t, NULL, recv_thread, NULL);
    pthread_detach(t);

    send_loop();

    close(sock_fd);
    printf("\n[client] Disconnected.\n");
    return EXIT_SUCCESS;
}