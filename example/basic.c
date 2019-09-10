#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include "at-unix.h"

volatile int prog_exit = 0;

void signal_handler(int sig)
{
    fprintf(stderr, "got signal and will exit\n");
    prog_exit = 1;
}

static void handle_urc(const char *buf, size_t len, void *arg)
{
    printf("urc [%d]: %.*s\n", (int)len, (int)len, buf);
}

static const struct at_callbacks callbacks = {
    .handle_urc = handle_urc,
};

const char *commands[] = {
    "at",
    "at+cops?",
    "at+csq",
    "at+creg?",
    "at+ccid",
    "at+unknowcommand",
    NULL,
};

int main(int argc, char *argv[])
{
    const char **cmd, *resp;
    struct at *at;

    if (!(at = at_alloc_unix())) {
        fprintf(stderr, "alloc at unix failed\n");
        return -1;
    }

    signal(SIGINT, signal_handler);
    at_set_timeout(at, 1);

    if (at_open(at, argv[1], B115200) < 0) {
        fprintf(stderr, "open %s failed: %m\n", argv[1]);
        prog_exit = 1;
        at_free(at);
    }
    at_set_callbacks(at, &callbacks, NULL);

    /* waiting until modem startup and then disable echo */
    while (prog_exit == 0) {
        resp = at_command(at, "ATE0");
        if (resp && !strcmp(resp, "")) {
                printf("device ready\n");
                at_command(at, "ATE0"); /* make sure ATE0 executed */
                break;
        } else {
                printf(".");
                fflush(stdout);
        }
        sleep(1);
    }

    while (prog_exit == 0) {
        for (cmd = commands; *cmd != NULL; cmd++) {
            resp = at_command(at, "%s", *cmd);
            if (!resp) {
                fprintf(stderr, "=> %s no responds: %m\n", *cmd);
            } else {
                printf("=> %s: %s\n", *cmd, resp);
            }
        }

        sleep(10);
    }

    at_close(at);
    at_free(at);

    return 0;
}

