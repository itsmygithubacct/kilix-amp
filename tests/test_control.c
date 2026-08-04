/* Control socket: framing, the protocol envelope, and startup recovery. */
#include "ktest.h"

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "control.h"

static char *g_dir;
static int g_calls;

/* Echoes what it was given so the test can see exactly what was dispatched. */
static void test_handler(void *ud, const char *cmd, const char *request,
                         JsonBuf *reply)
{
    (void)ud;
    g_calls++;
    json_kv_bool(reply, "ok", true);
    json_kv_str(reply, "saw", cmd);
    long long index = -1;
    if (json_get_int(request, "index", &index))
        json_kv_int(reply, "index", index);
}

static int client_connect(const char *path)
{
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    struct timeval tv = {2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return fd;
}

/* Reads one newline-terminated reply into a static buffer. */
static const char *read_line(int fd)
{
    static char buf[4096];
    size_t len = 0;
    while (len + 1 < sizeof(buf)) {
        ssize_t n = recv(fd, buf + len, 1, 0);
        if (n <= 0)
            break;
        if (buf[len] == '\n')
            break;
        len++;
    }
    buf[len] = '\0';
    return buf;
}

static ControlServer *listen_at(const char *name, char *path, size_t n)
{
    snprintf(path, n, "%s/%s", g_dir, name);
    char err[512] = {0};
    ControlServer *cs = control_listen(path, err, sizeof(err));
    if (!cs)
        printf("    (listen failed: %s)\n", err);
    return cs;
}

static void test_round_trip(void)
{
    char path[96];
    ControlServer *cs = listen_at("rt.sock", path, sizeof(path));
    ASSERT_TRUE(cs != NULL);
    if (!cs)
        return;
    int fd = client_connect(path);
    ASSERT_TRUE(fd >= 0);

    const char *req = "{\"cmd\":\"play\",\"protocol\":1,\"index\":3}\n";
    ASSERT_TRUE(send(fd, req, strlen(req), 0) > 0);
    control_poll(cs, test_handler, NULL, 1000);
    ASSERT_STR_EQ(read_line(fd),
                  "{\"protocol\":1,\"ok\":true,\"saw\":\"play\",\"index\":3}");

    close(fd);
    control_close(cs);
}

/* One connection may carry several commands; kilix-music sends one and closes,
 * but the framing must not depend on that. */
static void test_multiple_requests_one_connection(void)
{
    char path[96];
    ControlServer *cs = listen_at("multi.sock", path, sizeof(path));
    ASSERT_TRUE(cs != NULL);
    if (!cs)
        return;
    int fd = client_connect(path);
    ASSERT_TRUE(fd >= 0);

    const char *reqs = "{\"cmd\":\"state\"}\n{\"cmd\":\"playlist\"}\n";
    ASSERT_TRUE(send(fd, reqs, strlen(reqs), 0) > 0);
    control_poll(cs, test_handler, NULL, 1000);
    ASSERT_STR_EQ(read_line(fd),
                  "{\"protocol\":1,\"ok\":true,\"saw\":\"state\"}");
    ASSERT_STR_EQ(read_line(fd),
                  "{\"protocol\":1,\"ok\":true,\"saw\":\"playlist\"}");

    close(fd);
    control_close(cs);
}

/* A request split across reads is still one request. */
static void test_partial_request(void)
{
    char path[96];
    ControlServer *cs = listen_at("partial.sock", path, sizeof(path));
    ASSERT_TRUE(cs != NULL);
    if (!cs)
        return;
    int fd = client_connect(path);
    ASSERT_TRUE(fd >= 0);

    ASSERT_TRUE(send(fd, "{\"cmd\":\"pl", 10, 0) > 0);
    control_poll(cs, test_handler, NULL, 1000);
    ASSERT_TRUE(send(fd, "ay\"}\n", 5, 0) > 0);
    control_poll(cs, test_handler, NULL, 1000);
    ASSERT_STR_EQ(read_line(fd),
                  "{\"protocol\":1,\"ok\":true,\"saw\":\"play\"}");

    close(fd);
    control_close(cs);
}

/* CRLF from a line-oriented peer is tolerated. */
static void test_crlf(void)
{
    char path[96];
    ControlServer *cs = listen_at("crlf.sock", path, sizeof(path));
    ASSERT_TRUE(cs != NULL);
    if (!cs)
        return;
    int fd = client_connect(path);
    ASSERT_TRUE(fd >= 0);

    const char *req = "{\"cmd\":\"state\"}\r\n";
    ASSERT_TRUE(send(fd, req, strlen(req), 0) > 0);
    control_poll(cs, test_handler, NULL, 1000);
    ASSERT_STR_EQ(read_line(fd),
                  "{\"protocol\":1,\"ok\":true,\"saw\":\"state\"}");

    close(fd);
    control_close(cs);
}

/* Malformed input is answered, not dispatched: a reply always carries the
 * protocol version so a mismatched client can say so instead of guessing. */
static void test_malformed_is_answered_not_dispatched(void)
{
    char path[96];
    ControlServer *cs = listen_at("bad.sock", path, sizeof(path));
    ASSERT_TRUE(cs != NULL);
    if (!cs)
        return;

    const char *cases[] = {"not json\n", "{}\n", "{\"cmd\":123}\n",
                           "{\"cmd\":\"\"}\n"};
    for (size_t i = 0; i < KA_LEN(cases); i++) {
        int fd = client_connect(path);
        ASSERT_TRUE(fd >= 0);
        int before = g_calls;
        ASSERT_TRUE(send(fd, cases[i], strlen(cases[i]), 0) > 0);
        control_poll(cs, test_handler, NULL, 1000);
        const char *reply = read_line(fd);
        ASSERT_TRUE(strstr(reply, "\"protocol\":1") != NULL);
        ASSERT_TRUE(strstr(reply, "\"ok\":false") != NULL);
        ASSERT_EQ_INT(g_calls, before);
        close(fd);
    }
    control_close(cs);
}

/* An oversized line is refused with a reply, and the connection recovers. */
static void test_request_too_long(void)
{
    char path[96];
    ControlServer *cs = listen_at("big.sock", path, sizeof(path));
    ASSERT_TRUE(cs != NULL);
    if (!cs)
        return;
    int fd = client_connect(path);
    ASSERT_TRUE(fd >= 0);

    char chunk[1024];
    memset(chunk, 'x', sizeof(chunk));
    for (int i = 0; i < (CONTROL_MAX_REQUEST / (int)sizeof(chunk)) + 2; i++) {
        ASSERT_TRUE(send(fd, chunk, sizeof(chunk), 0) > 0);
        control_poll(cs, test_handler, NULL, 1000);
    }
    int before = g_calls;
    ASSERT_TRUE(send(fd, "\n", 1, 0) > 0);
    control_poll(cs, test_handler, NULL, 1000);
    const char *reply = read_line(fd);
    ASSERT_TRUE(strstr(reply, "\"error\":\"request too long\"") != NULL);
    ASSERT_EQ_INT(g_calls, before);

    /* The next well-formed request on the same connection still works. */
    const char *req = "{\"cmd\":\"state\"}\n";
    ASSERT_TRUE(send(fd, req, strlen(req), 0) > 0);
    control_poll(cs, test_handler, NULL, 1000);
    ASSERT_STR_EQ(read_line(fd),
                  "{\"protocol\":1,\"ok\":true,\"saw\":\"state\"}");

    close(fd);
    control_close(cs);
}

/* A connection that says nothing is dropped rather than holding a slot. */
static void test_idle_client_dropped(void)
{
    char path[96];
    ControlServer *cs = listen_at("idle.sock", path, sizeof(path));
    ASSERT_TRUE(cs != NULL);
    if (!cs)
        return;
    int fd = client_connect(path);
    ASSERT_TRUE(fd >= 0);
    control_poll(cs, test_handler, NULL, 1000);
    control_poll(cs, test_handler, NULL, 1000 + CONTROL_IDLE_MS + 1);
    /* The server closed its end, so the client reads EOF. */
    char byte;
    ASSERT_EQ_INT(recv(fd, &byte, 1, 0), 0);
    close(fd);
    control_close(cs);
}

static void test_socket_is_owner_only(void)
{
    char path[96];
    ControlServer *cs = listen_at("mode.sock", path, sizeof(path));
    ASSERT_TRUE(cs != NULL);
    if (!cs)
        return;
    struct stat st;
    ASSERT_EQ_INT(stat(path, &st), 0);
    ASSERT_EQ_INT(st.st_mode & 0777, 0600);
    control_close(cs);
    /* Closing removes the socket, so a later run finds a clean path. */
    ASSERT_EQ_INT(access(path, F_OK), -1);
}

/* A socket left by a run that was killed must not block the next one. */
static void test_stale_socket_recovered(void)
{
    char path[96];
    snprintf(path, sizeof(path), "%s/stale.sock", g_dir);

    /* What a SIGKILL leaves behind: the file is there, nobody is accepting. */
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT_TRUE(fd >= 0);
    ASSERT_EQ_INT(bind(fd, (struct sockaddr *)&addr, sizeof(addr)), 0);
    ASSERT_EQ_INT(listen(fd, 1), 0);
    close(fd);
    ASSERT_EQ_INT(access(path, F_OK), 0);

    char err[512] = {0};
    ControlServer *second = control_listen(path, err, sizeof(err));
    ASSERT_TRUE(second != NULL);
    if (!second)
        printf("    (stale recovery failed: %s)\n", err);
    else
        control_close(second);
}

/* Two backends on one socket would fight over the audio device. */
static void test_second_listener_refused(void)
{
    char path[96];
    ControlServer *cs = listen_at("dup.sock", path, sizeof(path));
    ASSERT_TRUE(cs != NULL);
    if (!cs)
        return;
    char err[512] = {0};
    ControlServer *dup = control_listen(path, err, sizeof(err));
    ASSERT_TRUE(dup == NULL);
    ASSERT_TRUE(strstr(err, "already listening") != NULL);
    control_close(cs);
}

/* The path rule is a published contract: kilix-music resolves it identically. */
static void test_default_socket_path(void)
{
    setenv("KILIX_AMP_SOCKET", "/custom/amp.sock", 1);
    char *p = control_default_socket_path();
    ASSERT_STR_EQ(p, "/custom/amp.sock");
    free(p);

    /* An empty override is no override, matching the client's `or` chain. */
    setenv("KILIX_AMP_SOCKET", "", 1);
    setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
    p = control_default_socket_path();
    ASSERT_STR_EQ(p, "/run/user/1000/kilix-amp.sock");
    free(p);

    unsetenv("KILIX_AMP_SOCKET");
    setenv("XDG_RUNTIME_DIR", "", 1);
    setenv("HOME", "/home/tester", 1);
    p = control_default_socket_path();
    ASSERT_STR_EQ(p, "/home/tester/.local/gpu_terminal/kilix/session/"
                     "kilix-amp.sock");
    free(p);

    unsetenv("XDG_RUNTIME_DIR");
    p = control_default_socket_path();
    ASSERT_STR_EQ(p, "/home/tester/.local/gpu_terminal/kilix/session/"
                     "kilix-amp.sock");
    free(p);
}

int main(void)
{
    g_dir = kt_tmpdir();
    RUN(test_round_trip);
    RUN(test_multiple_requests_one_connection);
    RUN(test_partial_request);
    RUN(test_crlf);
    RUN(test_malformed_is_answered_not_dispatched);
    RUN(test_request_too_long);
    RUN(test_idle_client_dropped);
    RUN(test_socket_is_owner_only);
    RUN(test_stale_socket_recovered);
    RUN(test_second_listener_refused);
    RUN(test_default_socket_path);
    int rc = kt_summary("control");
    free(g_dir);
    return rc;
}
