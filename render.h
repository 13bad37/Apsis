#ifndef RENDER_H
#define RENDER_H

#include "gravity.h"

typedef enum {
    HUD_ACTION_NONE = 0,
    HUD_ACTION_TOGGLE_PAUSE,
    HUD_ACTION_RESET_SCENE,
    HUD_ACTION_TIME_DOWN,
    HUD_ACTION_TIME_UP,
    HUD_ACTION_CYCLE_INTEGRATOR,
    HUD_ACTION_SAVE_STATE,
    HUD_ACTION_LOAD_STATE,
    HUD_ACTION_TOGGLE_BENCHMARK,
    HUD_ACTION_RESET_CAMERA,
    HUD_ACTION_RESET_BASELINE
} HudAction;

bool init_render_resources(void);
void shutdown_render_resources(void);

void render_simulation(SDL_Renderer *renderer, const Simulation *sim, const SpawnState *spawn,
                       const SimulationDiagnostics *diagnostics, const SimulationDrift *drift,
                       double simulated_time_seconds, const Camera *camera, const Viewport *viewport,
                       ScenePreset scene, IntegratorMode integrator, bool benchmark_recording, bool paused,
                       bool hud_visible, double time_scale);
void update_window_title(SDL_Window *window, const Simulation *sim, const SpawnState *spawn,
                         const Camera *camera, ScenePreset scene, IntegratorMode integrator,
                         bool benchmark_recording, bool paused, double time_scale);
Vec2 screen_to_world(int screen_x, int screen_y, const Camera *camera, const Viewport *viewport);
bool hud_contains_point(int screen_x, int screen_y, bool hud_visible, const Viewport *viewport);
HudAction hud_action_at_point(int screen_x, int screen_y, bool hud_visible, const Viewport *viewport);
bool hud_time_slider_contains_point(int screen_x, int screen_y, bool hud_visible,
                                    const Viewport *viewport);
double hud_time_scale_from_point(int screen_x, bool hud_visible, const Viewport *viewport);

#endif
