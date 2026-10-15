/* Round-trip latency of the real gs_lookahead_server binary over pipes, as the Java bridge sees it.
 *
 *   cc -std=c11 -O2 -o bench_lookahead_server ocean/osrs/tests/bench_lookahead_server.c
 *   bench_lookahead_server COUNT REQUESTS.txt -- gs_lookahead_server MODEL.fit EAT_MIXTURE.fit [WORLDS HORIZON [THREADS]]     (cwd = repo root)
 *
 * REQUESTS.txt holds requests (LA ... END, protocol v2); they are replayed in a cycle for COUNT requests, each written with one write() and answered
 * by one line (R ... or UNAVAILABLE ...). Reports spawn-to-READY, the first (cold) request, and p50/p99/max over all requests including the first,
 * for answered (R) requests and for all requests. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char** environ;

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static int cmp(const void* a, const void* b) { double x = *(const double*)a, y = *(const double*)b; return x < y ? -1 : x > y; }

static void die(const char* what) { fprintf(stderr, "bench_lookahead_server: %s (%s)\n", what, strerror(errno)); exit(2); }

/* Reads one '\n'-terminated line from fd into line (NUL-terminated); returns its length. Chunked reads, leftovers stay for the next call. */
static size_t read_line(int fd, char* line, size_t capacity) {
    static char buffer[1 << 16];
    static size_t have, at;
    size_t n = 0;
    for (;;) {
        if (at == have) {
            ssize_t got = read(fd, buffer, sizeof(buffer));
            if (got <= 0) { fprintf(stderr, "server closed its output\n"); exit(2); }
            have = (size_t)got;
            at = 0;
        }
        char c = buffer[at++];
        if (c == '\n') { line[n] = 0; return n; }
        if (n + 1 >= capacity) { fprintf(stderr, "response line exceeds %zu bytes\n", capacity); exit(2); }
        line[n++] = c;
    }
}

static void report(const char* name, double* us, long n) {
    if (!n) { printf("%-12s n=0\n", name); return; }
    qsort(us, (size_t)n, sizeof(double), cmp);
    double sum = 0;
    for (long i = 0; i < n; i++) sum += us[i];
    printf("%-12s n=%-6ld mean %7.0f p50 %7.0f p90 %7.0f p99 %7.0f max %7.0f us\n", name, n, sum / (double)n, us[n / 2], us[n * 90 / 100], us[n * 99 / 100], us[n - 1]);
}

int main(int argc, char** argv) {
    if (argc < 6 || strcmp(argv[3], "--")) { fprintf(stderr, "usage: %s COUNT REQUESTS.txt -- SERVER ARGS...\n", argv[0]); return 2; }
    long count = atol(argv[1]);
    if (count <= 0) { fprintf(stderr, "COUNT must be positive\n"); return 2; }
    FILE* f = fopen(argv[2], "rb");
    if (!f) die("cannot open the request file");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = malloc((size_t)size + 1);
    if (!text || fread(text, 1, (size_t)size, f) != (size_t)size) die("cannot read the request file");
    text[size] = 0;
    fclose(f);
    enum { MAX_REQUESTS = 1 << 16 };
    char** request = malloc(MAX_REQUESTS * sizeof(char*));
    size_t* length = malloc(MAX_REQUESTS * sizeof(size_t));
    int requests = 0;
    for (char* p = text; *p;) {
        char* end = strstr(p, "END\n");
        if (!end) break;
        end += 4;
        if (requests == MAX_REQUESTS) { fprintf(stderr, "too many requests\n"); return 2; }
        request[requests] = p;
        length[requests++] = (size_t)(end - p);
        p = end;
    }
    if (!requests) { fprintf(stderr, "no requests in %s\n", argv[2]); return 2; }

    int to_server[2], from_server[2];
    if (pipe(to_server) || pipe(from_server)) die("pipe");
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, to_server[0], 0);
    posix_spawn_file_actions_adddup2(&actions, from_server[1], 1);
    posix_spawn_file_actions_addclose(&actions, to_server[1]);
    posix_spawn_file_actions_addclose(&actions, from_server[0]);
    pid_t pid;
    double spawned = now();
    if (posix_spawn(&pid, argv[4], &actions, NULL, argv + 4, environ)) die("cannot start the server");
    close(to_server[0]);
    close(from_server[1]);
    signal(SIGPIPE, SIG_IGN);

    static char line[1 << 16];
    read_line(from_server[0], line, sizeof(line));
    double ready = now();
    if (strncmp(line, "READY ", 6)) { fprintf(stderr, "expected READY, got: %.60s\n", line); return 2; }
    printf("%.*s\n", 80, line);
    printf("spawn to READY %.0f ms\n", (ready - spawned) * 1e3);

    double* all = malloc((size_t)count * sizeof(double));
    double* answered = malloc((size_t)count * sizeof(double));
    long answered_count = 0, unavailable = 0;
    double first = 0;
    for (long i = 0; i < count; i++) {
        int r = (int)(i % requests);
        double t0 = now();
        size_t sent = 0;
        while (sent < length[r]) {
            ssize_t w = write(to_server[1], request[r] + sent, length[r] - sent);
            if (w < 0) die("write to the server");
            sent += (size_t)w;
        }
        read_line(from_server[0], line, sizeof(line));
        double us = (now() - t0) * 1e6;
        if (i == 0) first = us;
        all[i] = us;
        if (line[0] == 'R') answered[answered_count++] = us;
        else if (!strncmp(line, "UNAVAILABLE", 11)) unavailable++;
        else { fprintf(stderr, "unexpected response: %.80s\n", line); return 2; }
    }
    printf("first request (cold) %.0f us; %ld answered, %ld unavailable\n", first, answered_count, unavailable);
    report("answered", answered, answered_count);
    report("all", all, count);
    close(to_server[1]);
    int status;
    waitpid(pid, &status, 0);
    return 0;
}
