#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "api/api.h"
#include "core/store.h"
#include "infra/http.h"
#include "infra/util.h"

#define APP_VERSION "1.0.0"
#define DEFAULT_PORT 8787
#define BIND_HOST "127.0.0.1"

static void on_signal(int sig) {
    (void)sig;
    http_stop();
}

static void usage(FILE *out) {
    fprintf(out,
            "habits %s - a local habit tracker\n"
            "\n"
            "Usage: habits [options]\n"
            "\n"
            "Options:\n"
            "  --port <n>       listen on port n (default %d, 0 = any free port)\n"
            "  --data <dir>     data directory (default: <exe dir>/data)\n"
            "  --web-dir <dir>  serve the UI from a directory instead of the bundle\n"
            "  --no-open        do not open the browser\n"
            "  --verbose        log every request to stderr\n"
            "  --version        print version and exit\n"
            "  --help           print this help and exit\n"
            "\n"
            "Data location precedence: --data, $HABITS_DATA_DIR, <exe dir>/data.\n",
            APP_VERSION, DEFAULT_PORT);
}

static int acquire_lock(const char *data_dir, char *err, size_t errsz) {
    char *locks = path_join(data_dir, "locks");
    if (mkdir_p(locks) != 0) {
        snprintf(err, errsz, "cannot create %s: %s", locks, strerror(errno));
        free(locks);
        return -1;
    }
    char *path = path_join(locks, "habits.lock");
    int fd = open(path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        snprintf(err, errsz, "cannot open %s: %s", path, strerror(errno));
        free(path);
        free(locks);
        return -1;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        snprintf(err, errsz, "another instance is already running (lock %s)", path);
        close(fd);
        free(path);
        free(locks);
        return -1;
    }
    free(path);
    free(locks);
    return fd;
}

static void open_browser(const char *url);

static void print_and_open(int port, void *ud) {
    bool *no_open = ud;
    char url[128];
    snprintf(url, sizeof url, "http://%s:%d/", BIND_HOST, port);
    printf("habits: listening on %s\n", url);
    fflush(stdout);
    if (!*no_open) open_browser(url);
}

static void open_browser(const char *url) {
    pid_t pid = fork();
    if (pid == 0) {
        execlp("xdg-open", "xdg-open", url, (char *)NULL);
        _exit(127);
    }
}

int main(int argc, char **argv) {
    int port = DEFAULT_PORT;
    const char *data_arg = NULL;
    const char *web_dir = NULL;
    bool no_open = false;
    bool verbose = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
            if (port < 0 || port > 65535) {
                fprintf(stderr, "habits: invalid port\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            data_arg = argv[++i];
        } else if (strcmp(argv[i], "--web-dir") == 0 && i + 1 < argc) {
            web_dir = argv[++i];
        } else if (strcmp(argv[i], "--no-open") == 0) {
            no_open = true;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("habits %s\n", APP_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "habits: unknown option %s (try --help)\n", argv[i]);
            return 2;
        }
    }

    char *exe = exe_dir();
    char *data_dir = NULL;
    if (data_arg) {
        data_dir = xstrdup(data_arg);
    } else if (getenv("HABITS_DATA_DIR")) {
        data_dir = xstrdup(getenv("HABITS_DATA_DIR"));
    } else if (exe) {
        data_dir = path_join(exe, "data");
    } else {
        data_dir = xstrdup("data");
        fprintf(stderr, "habits: warning: cannot resolve the executable path, using ./data\n");
    }
    free(exe);

    if (mkdir_p(data_dir) != 0) {
        fprintf(stderr, "habits: cannot create data directory %s: %s\n", data_dir,
                strerror(errno));
        fprintf(stderr, "habits: hint: pass --data <dir>\n");
        free(data_dir);
        return 1;
    }
    if (!dir_writable(data_dir)) {
        fprintf(stderr, "habits: data directory %s is not writable\n", data_dir);
        fprintf(stderr, "habits: hint: pass --data <dir> or move the executable to a user folder\n");
        free(data_dir);
        return 1;
    }

    char lock_err[512] = "";
    int lock_fd = acquire_lock(data_dir, lock_err, sizeof lock_err);
    if (lock_fd < 0) {
        fprintf(stderr, "habits: %s\n", lock_err);
        free(data_dir);
        return 1;
    }

    DB db;
    char err[512] = "";
    if (!store_load(&db, data_dir, err, sizeof err)) {
        fprintf(stderr, "habits: %s\n", err);
        close(lock_fd);
        free(data_dir);
        return 2;
    }

    Server server = {&db, data_dir, web_dir, APP_VERSION, verbose};

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("habits %s: data in %s\n", APP_VERSION, data_dir);
    fflush(stdout);

    HttpConfig cfg = {BIND_HOST, port, api_handle, &server, print_and_open, &no_open};
    int actual_port = port;
    int rc = http_serve(&cfg, &actual_port, err, sizeof err);
    if (rc != 0) {
        fprintf(stderr, "habits: %s\n", err);
        rc = 1;
    }

    /* Persist once more so an unclean shutdown still leaves a consistent file. */
    if (!store_save(&db, data_dir, err, sizeof err))
        fprintf(stderr, "habits: warning: final save failed: %s\n", err);

    db_free(&db);
    close(lock_fd);
    free(data_dir);
    return rc;
}
