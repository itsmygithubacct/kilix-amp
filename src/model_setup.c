#include "model_setup.h"
#include "encodec_source.h"
#include <errno.h>
#include <spawn.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/* The desktop owns the setup terminal. Only fixed profile numbers cross this
 * private datagram socket. A successful setup lets the player retry its track. */
static int desktop_offer(unsigned char profile)
{
    const char *path = SDL_getenv("KILIX_AMP_MODEL_SETUP_SOCKET");
    struct sockaddr_un host = {.sun_family = AF_UNIX}, local = {.sun_family = AF_UNIX};
    if (!path || path[0] != '/' || strlen(path) >= sizeof(host.sun_path)) return -1;
    memcpy(host.sun_path, path, strlen(path) + 1);
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    /* Linux assigns an abstract address for the desktop's one-byte reply. */
    if (bind(fd, (struct sockaddr *)&local, offsetof(struct sockaddr_un, sun_path)) == 0
        && connect(fd, (struct sockaddr *)&host, sizeof(host)) == 0
        && send(fd, &profile, 1, MSG_NOSIGNAL) == 1) return fd;
    close(fd);
    return -1;
}

static pid_t standalone_offer(SDL_Window *parent, const char *asset)
{
    char message[420];
    snprintf(message, sizeof(message),
        "This track needs %s. Review the download and licence in Model Setup. "
        "You can also run kilix models install %s in a terminal.", asset, asset);
    const char *terminal = "/usr/bin/x-terminal-emulator";
    if (access(terminal, X_OK) != 0 || access("/usr/bin/kilix", X_OK) != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "EnCodec model setup", message, parent);
        return 0;
    }
    SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Later"},
        {0, 1, "Set up model"}};
    SDL_MessageBoxData dialog = {SDL_MESSAGEBOX_INFORMATION, parent, "EnCodec model setup",
                                 message, 2, buttons, NULL};
    int answer = 0;
    if (SDL_ShowMessageBox(&dialog, &answer) < 0 || answer != 1) return 0;
    const char *root = SDL_getenv("KILIX_ENCODEC_MODEL_ROOT");
    if (!root || root[0] != '/') root = SDL_getenv("KILIX_CONTENT_ROOT");
    char *args[] = {(char *)terminal, "-e", "/usr/bin/kilix", "models", NULL, NULL, NULL, NULL, NULL};
    int i = 4;
    if (root && root[0] == '/') { args[i++] = "--root"; args[i++] = (char *)root; }
    args[i++] = "install"; args[i] = (char *)asset;
    pid_t child;
    if (posix_spawn(&child, terminal, NULL, NULL, args, environ) == 0) return child;
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Could not start Model Setup", message, parent);
    return 0;
}

void model_setup_forget(ModelSetup *setup)
{
    if (setup->channel >= 0) close(setup->channel);
    setup->channel = -1;
    setup->entry = 0;
    setup->allow_retry = false;
}

void model_setup_poll(ModelSetup *setup, Remote *remote, SDL_Window *parent)
{
    Track *track = playlist_current_track(remote->playlist);
    long long service = playlist_snapshot_pid(remote->playlist);
    bool same = remote->connected && track && track->entry_id == setup->entry && service == setup->service;
    bool retry = false;
    if (setup->terminal > 0) {
        int status = 0;
        pid_t done = waitpid(setup->terminal, &status, WNOHANG);
        if (done == 0 || (done < 0 && errno == EINTR)) return;
        setup->terminal = 0;
        retry = done > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    if (setup->channel >= 0) {
        unsigned char answer[2];
        ssize_t n = recv(setup->channel, answer, sizeof(answer), 0);
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
            if (same) return;
        } else {
            retry = n == 1 && answer[0] == 1;
        }
        close(setup->channel);
        setup->channel = -1;
    }
    unsigned int profile = remote->source.setup_profile;
    if (!remote->connected || !track || track->entry_id <= 0 || track->source_kind != KA_ENCODEC_FILE
        || !ka_encodec_asset_id(profile)) return;
    if (same) {
        if (retry && setup->allow_retry) remote_play_index(remote, remote->index);
        return;
    }
    setup->service = service;
    setup->entry = track->entry_id;
    setup->allow_retry = true;
    setup->channel = desktop_offer((unsigned char)profile);
    if (setup->channel < 0) setup->terminal = standalone_offer(parent, ka_encodec_asset_id(profile));
}
