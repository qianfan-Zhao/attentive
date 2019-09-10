/*
 * Copyright © 2014 Kosma Moczek <kosma@cloudyourcar.com>
 * This program is free software. It comes without any warranty, to the extent
 * permitted by applicable law. You can redistribute it and/or modify it under
 * the terms of the Do What The Fuck You Want To Public License, Version 2, as
 * published by Sam Hocevar. See the COPYING file for more details.
 */

#include <at.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <syslog.h>

#if _POSIX_TIMERS > 0
#include <time.h>
#else
#include <sys/time.h>
#endif

// Remove once you refactor this out.
#define AT_COMMAND_LENGTH 80

struct at_unix {
    struct at at;

    const char *devpath;    /**< Serial port device path. */
    speed_t baudrate;       /**< Serial port baudate. */

    int timeout;            /**< Command timeout in seconds. */
    const char *response;

    pthread_t thread;       /**< Reader thread. */
    pthread_mutex_t mutex;  /**< Protects variables below and the parser. */
    pthread_cond_t cond;    /**< For signalling open/busy release. */
    pthread_condattr_t cattr; /**< Set pthread_cond_signal use CLOCK_MONOTONIC */

    int fd;                 /**< Serial port file descriptor. */
    bool running : 1;       /**< Reader thread should be running. */
    bool open : 1;          /**< FD is valid. Set/cleared by open()/close(). */
    bool busy : 1;          /**< FD is in use. Set/cleared by reader thread. */
    bool waiting : 1;       /**< Waiting for response callback to arrive. */
};

void *at_reader_thread(void *arg);

static void handle_sigusr1(int signal)
{
    (void)signal;
}

static void handle_response(const char *buf, size_t len, void *arg)
{
    struct at_unix *priv = (struct at_unix *) arg;

    /* The mutex is held by the reader thread; don't reacquire. */
    priv->response = buf;
    (void) len;
    priv->waiting = false;
    pthread_cond_signal(&priv->cond);
}

static void handle_urc(const char *buf, size_t len, void *arg)
{
    struct at *at = (struct at *) arg;

    /* Forward to caller's URC callback, if any. */
    if (at->cbs && at->cbs->handle_urc)
        at->cbs->handle_urc(buf, len, at->arg);
}

enum at_response_type scan_line(const char *line, size_t len, void *arg)
{
    struct at *at = (struct at *) arg;

    enum at_response_type type = AT_RESPONSE_UNKNOWN;
    if (at->command_scanner)
        type = at->command_scanner(line, len, at->arg);
    if (!type && at->cbs && at->cbs->scan_line)
        type = at->cbs->scan_line(line, len, at->arg);
    return type;
}

static const struct at_parser_callbacks parser_callbacks = {
    .handle_response = handle_response,
    .handle_urc = handle_urc,
    .scan_line = scan_line,
};

struct at *at_alloc_unix(void)
{
    /* allocate instance */
    struct at_unix *priv = malloc(sizeof(struct at_unix));
    if (!priv) {
        errno = ENOMEM;
        return NULL;
    }
    memset(priv, 0, sizeof(struct at_unix));

    /* allocate underlying parser */
    priv->at.parser = at_parser_alloc(&parser_callbacks, 256, (void *) priv);
    if (!priv->at.parser) {
        free(priv);
        return NULL;
    }

    priv->running = true;
    pthread_mutex_init(&priv->mutex, NULL);

    if (pthread_condattr_init(&priv->cattr) < 0) {
        at_parser_free(priv->at.parser);
        free(priv);
        return NULL;
    }
    pthread_condattr_setclock(&priv->cattr, CLOCK_MONOTONIC);
    pthread_cond_init(&priv->cond, &priv->cattr);

    return (struct at *) priv;
}

int at_open(struct at *at, const char *devpath, speed_t baudrate)
{
    struct at_unix *priv = (struct at_unix *) at;

    pthread_mutex_lock(&priv->mutex);
    if (priv->open) {
        pthread_mutex_unlock(&priv->mutex);
        return 0;
    }

    priv->fd = open(devpath, O_RDWR);
    if (priv->fd == -1) {
        pthread_mutex_unlock(&priv->mutex);
        return -1;
    }

    struct termios attr;
    tcgetattr(priv->fd, &attr);
    cfmakeraw(&attr);
    cfsetispeed(&attr, baudrate);
    cfsetospeed(&attr, baudrate);
    tcsetattr(priv->fd, TCSANOW, &attr);

    priv->open = true;
    priv->devpath = devpath;
    priv->baudrate = baudrate;

    /* install empty SIGUSR1 handler */
    struct sigaction sa = {
        .sa_handler = handle_sigusr1,
    };
    sigaction(SIGUSR1, &sa, NULL);
    pthread_create(&priv->thread, NULL, at_reader_thread, (void *) priv);

    pthread_mutex_unlock(&priv->mutex);

    return 0;
}

int at_close(struct at *at)
{
    struct at_unix *priv = (struct at_unix *) at;

    pthread_mutex_lock(&priv->mutex);
    if (!priv->open) {
        pthread_mutex_unlock(&priv->mutex);
        return 0;
    }

    /* ask the reader thread to terminate */
    priv->running = false;
    priv->open = false;
    pthread_mutex_unlock(&priv->mutex);

    if (pthread_kill(priv->thread, 0) != ESRCH) {
        /* Interrupt read() in the reader thread. */
        pthread_kill(priv->thread, SIGUSR1);
        /* wait for the reader thread to terminate */
        pthread_join(priv->thread, NULL);
    }

    if (priv->fd != -1) {
        /* Close the file descriptor. */
        close(priv->fd);
        priv->fd = -1;
    }

    return 0;
}

void at_free(struct at *at)
{
    struct at_unix *priv = (struct at_unix *) at;

    /* make sure the channel is closed */
    at_close(at);

    pthread_cond_destroy(&priv->cond);
    pthread_mutex_destroy(&priv->mutex);

    /* free up resources */
    pthread_condattr_destroy(&priv->cattr);
    free(priv->at.parser);
    free(priv);
}

void at_set_callbacks(struct at *at, const struct at_callbacks *cbs, void *arg)
{
    at->cbs = cbs;
    at->arg = arg;
}

void at_set_command_scanner(struct at *at, at_line_scanner_t scanner)
{
    at->command_scanner = scanner;
}

void at_set_timeout(struct at *at, int timeout)
{
    struct at_unix *priv = (struct at_unix *) at;

    priv->timeout = timeout;
}

void at_expect_dataprompt(struct at *at)
{
    at_parser_expect_dataprompt(at->parser);
}

static const char *_at_command(struct at_unix *priv, const void *data, size_t size)
{
    pthread_mutex_lock(&priv->mutex);

    /* Bail out if the channel is closing or closed. */
    if (!priv->open) {
        pthread_mutex_unlock(&priv->mutex);
        errno = ENODEV;
        return NULL;
    }

    /* Prepare parser. */
    at_parser_await_response(priv->at.parser);

    /* Send the command. */
    // FIXME: handle interrupts, short writes, errors, etc.
    write(priv->fd, data, size);

    /* Wait for the parser thread to collect a response. */
    priv->waiting = true;
    if (priv->timeout) {
        struct timespec ts;

        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_sec += priv->timeout;

        while (priv->open && priv->waiting)
            if (pthread_cond_timedwait(&priv->cond, &priv->mutex, &ts) == ETIMEDOUT)
                break;
    } else {
        while (priv->open && priv->waiting)
            pthread_cond_wait(&priv->cond, &priv->mutex);
    }

    const char *result;
    if (!priv->open) {
        /* The serial port was closed behind our back. */
        errno = ENODEV;
        result = NULL;
    } else if (priv->waiting) {
        /* Timed out waiting for a response. */
        at_parser_reset(priv->at.parser);
        errno = ETIMEDOUT;
        result = NULL;
    } else {
        /* Response arrived. */
        result = priv->response;
    }

    /* Reset per-command settings. */
    priv->at.command_scanner = NULL;

    pthread_mutex_unlock(&priv->mutex);

    return result;
}

const char *at_command(struct at *at, const char *format, ...)
{
    struct at_unix *priv = (struct at_unix *) at;

    /* Build command string. */
    va_list ap;
    va_start(ap, format);
    char line[AT_COMMAND_LENGTH];
    int len = vsnprintf(line, sizeof(line)-1, format, ap);
    va_end(ap);

    /* Bail out if we run out of space. */
    if (len >= (int)(sizeof(line)-1)) {
        errno = ENOMEM;
        return NULL;
    }

    syslog(LOG_DEBUG, "> %s", line);

    /* Append modem-style newline. */
    line[len++] = '\r';

    /* Send the command. */
    return _at_command(priv, line, len);
}

const char *at_command_raw(struct at *at, const void *data, size_t size)
{
    struct at_unix *priv = (struct at_unix *) at;

    syslog(LOG_DEBUG, "> [%zu bytes]", size);

    return _at_command(priv, data, size);
}

void *at_reader_thread(void *arg)
{
    struct at_unix *priv = (struct at_unix *)arg;

    syslog(LOG_DEBUG, "at_reader_thread[%s]: starting", priv->devpath);

    while (true) {
        pthread_mutex_lock(&priv->mutex);

        if (!priv->running) {
            /* Time to die. */
            pthread_mutex_unlock(&priv->mutex);
            break;
        }

        /* Lock access to the port descriptor. */
        priv->busy = true;
        pthread_mutex_unlock(&priv->mutex);

        /* Attempt to read some data. */
        char buffer[128];
        int result = read(priv->fd, buffer, sizeof(buffer));
        int why = errno;

        pthread_mutex_lock(&priv->mutex);
        /* Unlock access to the port descriptor. */
        priv->busy = false;
        /* Close the port if it is disconnected */
        if (result == 0) {
                priv->open = false;
                close(priv->fd);
                priv->fd = -1;
        }
        pthread_mutex_unlock(&priv->mutex);

        if (result > 0) {
            /* Data received, feed the parser. */
            pthread_mutex_lock(&priv->mutex);
            at_parser_feed(priv->at.parser, buffer, result);
            pthread_mutex_unlock(&priv->mutex);
        } else if (result == -1) {
            syslog(LOG_ERR, "at_reader_thread[%s]: %s", priv->devpath,
                   strerror(why));
            if (why == EINTR)
                continue;
            else
                break;
        } else {
            syslog(LOG_ERR, "at_reader_thread[%s]: received EOF",
                   priv->devpath);
            break;
        }
    }

    syslog(LOG_DEBUG, "at_reader_thread[%s]: finished", priv->devpath);

    return NULL;
}

/* vim: set ts=4 sw=4 et: */
