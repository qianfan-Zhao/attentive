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

/*
 * Wait device ready and disable modem's echo.
 */
static int wait_device_disable_echo(struct at *at, int timeout)
{
    const char *resp;
    int fail;

    while (prog_exit == 0 && timeout-- > 0) {
        resp = at_command(at, "ATE0");
        fail = errno;

        if (resp && !strcmp(resp, "")) {
            printf("device ready\n");
            at_command(at, "ATE0"); /* make sure ATE0 executed */
            break;
        } else if (fail == ENODEV) {
            return -1;
        } else {
            printf(".");
            fflush(stdout);
        }
        sleep(1);
    }

    return timeout > 0 ? 0 : -1;
}

int main(int argc, char *argv[])
{
    const char *serialport = argv[1];
    const char **cmd, *resp;
    struct at *at;

    if (argc < 2 || !serialport) {
        fprintf(stderr, "Usage: %s serialport\n", argv[0]);
        return -1;
    }

    if (!(at = at_alloc_unix())) {
        fprintf(stderr, "alloc at unix failed\n");
        return -1;
    }

    signal(SIGINT, signal_handler);
    at_set_timeout(at, 1);
    at_set_callbacks(at, &callbacks, NULL);

    while (prog_exit == 0) {
    again:
        /* Waiting usb serial port enumed */
        while (prog_exit == 0) {
            if (!at_open(at, serialport, B115200)) {
                break;
            } else {
                printf("+");
                fflush(stdout);
            }
            sleep(5);
        }

        if (wait_device_disable_echo(at, 240) < 0) {
            fprintf(stderr, "waiting device %s timeout, exit\n", serialport);
            break;
        }

        while (prog_exit == 0) {
            for (cmd = commands; *cmd != NULL; cmd++) {
                resp = at_command(at, "%s", *cmd);
                int fail = errno;

                if (!resp) { /* maybe usb serial port pulled out */
                    printf("=> %s: %m\n", *cmd);
                    if (fail == ENODEV) {
                        printf("device %s was pulled out", serialport);
                        at_close(at);
                        goto again;
                    } else if (fail == ETIMEDOUT) {
                        goto again;
                    }
                } else {
                    printf("=> %s: %s\n", *cmd, resp);
                }
            }

            sleep(5);
        }
    }

    at_close(at);
    at_free(at);

    return 0;
}

