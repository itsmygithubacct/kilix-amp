#include "control.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

typedef struct {
    int fd; /* -1 when the slot is free */
    char in[CONTROL_MAX_REQUEST];
    size_t in_len;
    bool overflowed; /* request too long: drain to the newline, then refuse */
    char *out;
    size_t out_len;
    size_t out_sent;
    uint32_t last_ms;
} ControlClient;

struct ControlServer {
    int fd;
    char *path;
    dev_t path_dev;
    ino_t path_ino;
    ControlClient clients[CONTROL_MAX_CLIENTS];
};

static bool set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return flags != -1 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

char *control_default_socket_path(void)
{
    const char *override = getenv("KILIX_AMP_SOCKET");
    if (override && *override)
        return ka_strdup(override);
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime && *runtime)
        return ka_path_join(runtime, "kilix-amp.sock");
    const char *home = getenv("HOME");
    if (!home || !*home)
        home = ".";
    char *dir = ka_asprintf("%s/.local/gpu_terminal/kilix/session", home);
    char *path = ka_path_join(dir, "kilix-amp.sock");
    free(dir);
    return path;
}

/* True if something is currently accepting on `path`. */
static bool socket_is_live(const char *path)
{
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path))
        return false;
    memcpy(addr.sun_path, path, strlen(path));
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    bool live = connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0;
    close(fd);
    return live;
}

ControlServer *control_listen(const char *path, char *err, size_t errn)
{
    if (!path) {
        snprintf(err, errn, "socket path is required");
        return NULL;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(addr.sun_path)) {
        snprintf(err, errn, "socket path must be 1-%zu bytes: %s",
                 sizeof(addr.sun_path) - 1, path);
        return NULL;
    }
    memcpy(addr.sun_path, path, len);

    char *dir = ka_dirname(path);
    bool dir_existed = ka_is_dir(dir);
    if (!ka_mkdirs(dir)) {
        snprintf(err, errn, "could not create %s: %s", dir, strerror(errno));
        free(dir);
        return NULL;
    }
    /* Only tighten a directory we just made. An existing one — $XDG_RUNTIME_DIR
     * above all — belongs to whoever set it up, and is not ours to re-permission. */
    if (!dir_existed)
        chmod(dir, 0700);
    free(dir);

    struct stat existing;
    if (lstat(path, &existing) == 0) {
        if (!S_ISSOCK(existing.st_mode)) {
            snprintf(err, errn,
                     "refusing to replace non-socket path %s", path);
            return NULL;
        }
        if (existing.st_uid != geteuid()) {
            snprintf(err, errn,
                     "refusing to replace socket not owned by this user: %s",
                     path);
            return NULL;
        }
        if (socket_is_live(path)) {
            snprintf(err, errn,
                     "another kilix-amp is already listening on %s", path);
            return NULL;
        }
        /* There is no pathname-based check-and-unlink operation that can
         * guarantee the inspected socket is still the object being removed.
         * A same-user process could replace it between those two operations.
         * Fail closed and make stale-path cleanup an explicit operator act. */
        snprintf(err, errn,
                 "refusing to remove stale socket %s; remove it explicitly "
                 "after verifying no kilix-amp is running",
                 path);
        return NULL;
    } else if (errno != ENOENT) {
        snprintf(err, errn, "could not inspect socket path %s: %s", path,
                 strerror(errno));
        return NULL;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        snprintf(err, errn, "socket: %s", strerror(errno));
        return NULL;
    }
    /* Create the socket owner-only rather than widening it after bind, so it
     * is never briefly connectable by another user. */
    mode_t old = umask(0177);
    int rc = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    umask(old);
    if (rc != 0) {
        snprintf(err, errn, "bind %s: %s", path, strerror(errno));
        close(fd);
        return NULL;
    }

    /* Establish the identity immediately after bind. If another same-user
     * process has already replaced the path, close our now-unreachable socket
     * and preserve whatever currently occupies the pathname. */
    struct stat created;
    if (lstat(path, &created) != 0) {
        snprintf(err, errn, "could not verify created socket %s: %s", path,
                 strerror(errno));
        close(fd);
        return NULL;
    }
    if (!S_ISSOCK(created.st_mode)) {
        snprintf(err, errn,
                 "could not verify created socket %s: path is not a socket",
                 path);
        close(fd);
        return NULL;
    }
    if (listen(fd, CONTROL_MAX_CLIENTS) != 0) {
        snprintf(err, errn,
                 "listen %s: %s; socket path retained for explicit cleanup",
                 path, strerror(errno));
        close(fd);
        return NULL;
    }
    if (!set_nonblocking(fd)) {
        snprintf(err, errn,
                 "fcntl %s: %s; socket path retained for explicit cleanup",
                 path, strerror(errno));
        close(fd);
        return NULL;
    }

    ControlServer *cs = calloc(1, sizeof(*cs));
    if (!cs)
        abort();
    cs->fd = fd;
    cs->path = ka_strdup(path);
    cs->path_dev = created.st_dev;
    cs->path_ino = created.st_ino;
    for (int i = 0; i < CONTROL_MAX_CLIENTS; i++)
        cs->clients[i].fd = -1;
    return cs;
}

const char *control_socket_path(const ControlServer *cs)
{
    return cs ? cs->path : "";
}

static void client_drop(ControlClient *c)
{
    if (c->fd >= 0)
        close(c->fd);
    c->fd = -1;
    c->in_len = 0;
    c->overflowed = false;
    free(c->out);
    c->out = NULL;
    c->out_len = c->out_sent = 0;
}

static void client_queue(ControlClient *c, const char *text)
{
    size_t n = strlen(text);
    char *buf = realloc(c->out, c->out_len + n + 1);
    if (!buf)
        abort();
    memcpy(buf + c->out_len, text, n);
    c->out = buf;
    c->out_len += n;
    c->out[c->out_len] = '\0';
}

static void reply_error(JsonBuf *reply, const char *message)
{
    json_kv_bool(reply, "ok", false);
    json_kv_str(reply, "error", message);
}

/* Turns one complete request line into one reply line. */
static void serve_line(ControlClient *c, char *line, ControlHandler handler,
                       void *ud)
{
    JsonBuf reply = {0};
    json_obj_begin(&reply);
    /* Written before anything else and unconditionally: a client that speaks
     * another version must be able to detect that from any reply at all. */
    json_kv_int(&reply, "protocol", CONTROL_PROTOCOL);

    char cmd[64];
    if (!json_get_str(line, "cmd", cmd, sizeof(cmd)))
        reply_error(&reply, "request needs a \"cmd\" string");
    else if (!cmd[0])
        reply_error(&reply, "empty command");
    else
        handler(ud, cmd, line, &reply);

    json_obj_end(&reply);
    client_queue(c, json_text(&reply));
    client_queue(c, "\n");
    json_free(&reply);
}

static void client_flush(ControlClient *c)
{
    while (c->out_sent < c->out_len) {
        ssize_t n = send(c->fd, c->out + c->out_sent, c->out_len - c->out_sent,
                         MSG_NOSIGNAL);
        if (n > 0) {
            c->out_sent += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return; /* retry on a later poll */
        client_drop(c);
        return;
    }
    free(c->out);
    c->out = NULL;
    c->out_len = c->out_sent = 0;
}

/* Consumes every complete line currently buffered. */
static void client_consume(ControlClient *c, ControlHandler handler, void *ud)
{
    for (;;) {
        char *nl = memchr(c->in, '\n', c->in_len);
        if (!nl)
            return;
        size_t line_len = (size_t)(nl - c->in);
        size_t rest = c->in_len - line_len - 1;
        if (c->overflowed) {
            /* The line we just skipped past was the oversized one. */
            c->overflowed = false;
            JsonBuf reply = {0};
            json_obj_begin(&reply);
            json_kv_int(&reply, "protocol", CONTROL_PROTOCOL);
            reply_error(&reply, "request too long");
            json_obj_end(&reply);
            client_queue(c, json_text(&reply));
            client_queue(c, "\n");
            json_free(&reply);
        } else {
            char *line = malloc(line_len + 1);
            if (!line)
                abort();
            memcpy(line, c->in, line_len);
            line[line_len] = '\0';
            /* Tolerate CRLF from a line-oriented peer. */
            if (line_len && line[line_len - 1] == '\r')
                line[line_len - 1] = '\0';
            serve_line(c, line, handler, ud);
            free(line);
        }
        memmove(c->in, nl + 1, rest);
        c->in_len = rest;
        if (c->fd < 0)
            return;
    }
}

static void client_read(ControlClient *c, ControlHandler handler, void *ud,
                        uint32_t now_ms)
{
    for (;;) {
        if (c->in_len == sizeof(c->in)) {
            /* Full buffer with no newline in it: the request cannot be valid.
             * Keep only the tail so scanning for the terminator continues. */
            c->overflowed = true;
            c->in_len = 0;
        }
        ssize_t n = recv(c->fd, c->in + c->in_len, sizeof(c->in) - c->in_len,
                         0);
        if (n > 0) {
            c->in_len += (size_t)n;
            c->last_ms = now_ms;
            client_consume(c, handler, ud);
            if (c->fd < 0)
                return;
            continue;
        }
        if (n == 0) {
            /* Peer closed its end; let anything already queued go out. */
            client_flush(c);
            client_drop(c);
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        if (errno == EINTR)
            continue;
        client_drop(c);
        return;
    }
}

void control_poll(ControlServer *cs, ControlHandler handler, void *ud,
                  uint32_t now_ms)
{
    if (!cs)
        return;

    for (;;) {
        int fd = accept(cs->fd, NULL, NULL);
        if (fd < 0)
            break;
        int slot = -1;
        for (int i = 0; i < CONTROL_MAX_CLIENTS && slot < 0; i++)
            if (cs->clients[i].fd < 0)
                slot = i;
        if (slot < 0 || !set_nonblocking(fd)) {
            close(fd); /* all slots busy: the client sees a closed connection */
            continue;
        }
        ControlClient *c = &cs->clients[slot];
        c->fd = fd;
        c->in_len = 0;
        c->overflowed = false;
        c->out = NULL;
        c->out_len = c->out_sent = 0;
        c->last_ms = now_ms;
    }

    for (int i = 0; i < CONTROL_MAX_CLIENTS; i++) {
        ControlClient *c = &cs->clients[i];
        if (c->fd < 0)
            continue;
        client_read(c, handler, ud, now_ms);
        if (c->fd < 0)
            continue;
        if (c->out_len)
            client_flush(c);
        if (c->fd < 0)
            continue;
        if (now_ms - c->last_ms > CONTROL_IDLE_MS)
            client_drop(c);
    }
}

void control_close(ControlServer *cs)
{
    if (!cs)
        return;
    for (int i = 0; i < CONTROL_MAX_CLIENTS; i++)
        client_drop(&cs->clients[i]);
    if (cs->fd >= 0)
        close(cs->fd);
    if (cs->path) {
        /* Do not remove a file that replaced our socket while we were
         * running. Only unlink the exact filesystem object created above. */
        struct stat current;
        if (lstat(cs->path, &current) == 0 && S_ISSOCK(current.st_mode) &&
            current.st_dev == cs->path_dev && current.st_ino == cs->path_ino)
            unlink(cs->path);
        free(cs->path);
    }
    free(cs);
}
