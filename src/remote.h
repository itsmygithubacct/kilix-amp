#ifndef KA_REMOTE_H
#define KA_REMOTE_H
#include "playlist.h"
#include "audio.h"

/* A view of the shared service. Closing this controller leaves playback up. */
typedef struct {
    Playlist *playlist;
    char *path;
    char state[16], title[512], error[512];
    char command_error[512];
    uint32_t error_until;
    int position, duration, volume, balance, index;
    long long revision, service_pid;
    bool connected, eq_enabled, shuffle;
    int repeat;
    double preamp;
    float eq[10], spectrum[AUDIO_SPECTRUM_BANDS];
    AudioTags tags;
    AudioSourceInfo source;
    uint32_t last_poll;
} Remote;

Remote *remote_new(Playlist *playlist, const char *path);
bool remote_start(Remote *r);
/* stdin can only be transferred when starting a new service. */
bool remote_start_stdin(Remote *r);
bool remote_poll(Remote *r, bool force);
void remote_request(Remote *r, JsonBuf *request); /* closes the JSON object */
void remote_command(Remote *r, const char *command);
void remote_number(Remote *r, const char *command, const char *key, double value);
void remote_boolean(Remote *r, const char *command, const char *key, bool value);
void remote_play_index(Remote *r, int index);
void remote_eq_band(Remote *r, int band, double gain);
void remote_free(Remote *r);
#endif
