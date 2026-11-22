#ifndef KA_MODEL_SETUP_H
#define KA_MODEL_SETUP_H
#include <SDL.h>
#include <sys/types.h>
#include "remote.h"

typedef struct {
    long long service, entry;
    int channel;
    pid_t terminal;
    bool allow_retry;
} ModelSetup;

void model_setup_poll(ModelSetup *setup, Remote *remote, SDL_Window *parent);
void model_setup_forget(ModelSetup *setup);
#endif
