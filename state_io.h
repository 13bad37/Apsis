#ifndef STATE_IO_H
#define STATE_IO_H

#include "gravity.h"

typedef struct {
    Simulation simulation;
    ScenePreset scene;
    IntegratorMode integrator;
    Camera camera;
    BodyType spawn_type;
    double spawn_mass;
    double simulated_time_seconds;
    double time_scale;
    bool paused;
} SaveState;

bool save_state_to_file(const char *path, const SaveState *state);
bool load_state_from_file(const char *path, SaveState *state);

#endif
