/* MIT. A transparent stereo LADSPA tap for the shared playback service.
 * mpv passes an inherited datagram socket, never a public filesystem path.
 * Dropping a meter frame is harmless; audio must never wait for the UI. */
#include "../vendor/ladspa/ladspa.h"
#include <stdlib.h>
#include <sys/socket.h>

typedef struct {
    LADSPA_Data *ports[4];
    float frame[1024];
    unsigned used;
    int fd;
} Meter;

static LADSPA_Handle instantiate(const LADSPA_Descriptor *desc, unsigned long rate)
{
    (void)desc;
    (void)rate;
    Meter *m = calloc(1, sizeof(*m));
    if (m) {
        const char *fd = getenv("KILIX_AMP_SPECTRUM_FD");
        m->fd = fd ? atoi(fd) : -1;
    }
    return m;
}

static void connect_port(LADSPA_Handle handle, unsigned long port, LADSPA_Data *data)
{
    if (port < 4)
        ((Meter *)handle)->ports[port] = data;
}

static void run(LADSPA_Handle handle, unsigned long count)
{
    Meter *m = handle;
    for (unsigned long i = 0; i < count; i++) {
        float left = m->ports[0][i], right = m->ports[1][i];
        m->ports[2][i] = left;
        m->ports[3][i] = right;
        m->frame[m->used++] = (left + right) * 0.5f;
        if (m->used == 1024) {
            if (m->fd >= 0)
                (void)send(m->fd, m->frame, sizeof(m->frame), MSG_DONTWAIT | MSG_NOSIGNAL);
            m->used = 0;
        }
    }
}

static const LADSPA_PortDescriptor ports[] = {
    LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO, LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,
    LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO, LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,
};
static const char *const names[] = {"Left input", "Right input", "Left output", "Right output"};
static const LADSPA_PortRangeHint hints[4] = {{0}};
static const LADSPA_Descriptor descriptor = {
    .UniqueID = 0, /* private plugin, addressed by its label */
    .Label = "kilix_meter", .Name = "Kilix playback spectrum tap",
    .Maker = "Kilix", .Copyright = "MIT",
    .PortCount = 4, .PortDescriptors = ports, .PortNames = names,
    .PortRangeHints = hints, .instantiate = instantiate,
    .connect_port = connect_port, .run = run, .cleanup = free,
};

const LADSPA_Descriptor *ladspa_descriptor(unsigned long index)
{
    return index == 0 ? &descriptor : NULL;
}
