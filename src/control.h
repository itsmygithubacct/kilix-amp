/* Control socket: the remote surface kilix-amp exposes in headless mode.
 *
 * One AF_UNIX stream socket, one newline-delimited JSON object per request,
 * one per reply. Every reply carries "protocol", so a client speaking a
 * different version is told so instead of having its fields guessed at.
 *
 * The server never blocks: accepting, reading and writing all happen in
 * whatever state they are in when control_poll runs, so a client that
 * connects and goes quiet cannot stall audio decoding. */
#ifndef KA_CONTROL_H
#define KA_CONTROL_H

#include "common.h"
#include "json.h"

/* Missing versions retain the published v1 contract. V2 is explicitly
 * requested per command; clients negotiate with a read-only ping. */
#define CONTROL_PROTOCOL 1
#define CONTROL_PROTOCOL_MAX 2

#define CONTROL_MAX_CLIENTS 8
/* A request is a short command object; anything longer is a client fault. */
#define CONTROL_MAX_REQUEST 8192
/* A connection that has neither sent nor drained anything for this long is
 * dropped, so a stuck peer cannot hold a slot forever. */
#define CONTROL_IDLE_MS 15000

/* Adds reply fields for `cmd`. "protocol" is already written; the handler owns
 * "ok" and everything after it. `request` is the raw object, for json_get_*. */
typedef void (*ControlHandler)(void *ud, const char *cmd, const char *request,
                               JsonBuf *reply);

typedef struct ControlServer ControlServer;

/* $KILIX_AMP_SOCKET, else <runtime>/kilix-amp.sock where <runtime> is
 * $XDG_RUNTIME_DIR or ~/.local/gpu_terminal/kilix/session. Matches the path
 * rule kilix-music already ships. Heap string; caller frees. */
char *control_default_socket_path(void);

/* Creates the parent directory if needed and refuses to replace any existing
 * filesystem object. A live socket reports the existing backend; a stale
 * socket must be removed explicitly after the operator verifies no backend is
 * running. NULL on failure with the reason in `err`. */
ControlServer *control_listen(const char *path, char *err, size_t errn);

/* Services every ready connection once. `now_ms` drives the idle timeout. */
void control_poll(ControlServer *cs, ControlHandler handler, void *ud,
                  uint32_t now_ms);

/* Closes all connections and unlinks the socket if the path still names the
 * exact filesystem object created by control_listen(). */
void control_close(ControlServer *cs);

const char *control_socket_path(const ControlServer *cs);

#endif
