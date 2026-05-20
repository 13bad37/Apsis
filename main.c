#include "gravity.h"
#include "benchmark_io.h"
#include "render.h"
#include "scenes.h"
#include "simulation.h"
#include "state_io.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const double CAMERA_ZOOM_FACTOR = 1.25;
static const double CAMERA_MIN_METERS_PER_PIXEL = 1.0e5;
static const double CAMERA_MAX_METERS_PER_PIXEL = 5.0e10;
static const double CAMERA_KEY_PAN_PIXELS = 80.0;
static const char *BENCHMARK_EXPORT_PATH = "apsis_benchmark.csv";
static const char *SAVE_STATE_PATH = "apsis_save.txt";

static IntegratorMode next_integrator(IntegratorMode integrator) {
    return (IntegratorMode)((integrator + 1) % INTEGRATOR_COUNT);
}

static double clamp_double(double value, double min_value, double max_value) {
    if (value < min_value) {
        return min_value;
    }

    if (value > max_value) {
        return max_value;
    }

    return value;
}

static double clamp_time_scale(double time_scale) {
    return clamp_double(time_scale, TIME_SCALE_MIN, TIME_SCALE_MAX);
}

static void adjust_time_scale(double *time_scale, double step_count) {
    *time_scale = clamp_time_scale(*time_scale * pow(TIME_SCALE_STEP_FACTOR, step_count));
}

static void reset_camera(Camera *camera) {
    camera->center = vec2(0.0, 0.0);
    camera->meters_per_pixel = VIEW_METERS_PER_PIXEL;
}

static void pan_camera_pixels(Camera *camera, double dx_pixels, double dy_pixels) {
    // Positive screen x is right, positive screen y is down.
    // Camera center uses world coordinates where positive y is up.
    camera->center.x += dx_pixels * camera->meters_per_pixel;
    camera->center.y -= dy_pixels * camera->meters_per_pixel;
}

static void refresh_viewport(SDL_Window *window, Viewport *viewport) {
    SDL_GetWindowSize(window, &viewport->width, &viewport->height);
}

static void zoom_camera_at(Camera *camera, const Viewport *viewport,
                           int screen_x, int screen_y, double zoom_factor) {
    Vec2 world_before = screen_to_world(screen_x, screen_y, camera, viewport);

    camera->meters_per_pixel = clamp_double(
        camera->meters_per_pixel * zoom_factor,
        CAMERA_MIN_METERS_PER_PIXEL,
        CAMERA_MAX_METERS_PER_PIXEL
    );

    Vec2 world_after = screen_to_world(screen_x, screen_y, camera, viewport);
    camera->center = vec_add(camera->center, vec_sub(world_before, world_after));
}

static void apply_hud_action(HudAction action, ScenePreset *current_scene,
                             Simulation *initial_state, Simulation *sim, SpawnState *spawn,
                             Camera *camera, BenchmarkRecorder *benchmark,
                             IntegratorMode *current_integrator, bool *paused,
                             double *time_scale,
                             double *accumulator, double *simulated_time_seconds,
                             DiagnosticsBaseline *diagnostics_baseline) {
    switch (action) {
        case HUD_ACTION_TOGGLE_PAUSE:
            *paused = !*paused;
            break;

        case HUD_ACTION_RESET_SCENE:
            activate_scene(*current_scene, initial_state, sim, spawn, accumulator,
                           simulated_time_seconds);
            reset_camera(camera);
            *diagnostics_baseline = make_diagnostics_baseline(sim);
            break;

        case HUD_ACTION_TIME_DOWN:
            adjust_time_scale(time_scale, -1.0);
            break;

        case HUD_ACTION_TIME_UP:
            adjust_time_scale(time_scale, 1.0);
            break;

        case HUD_ACTION_CYCLE_INTEGRATOR:
            *current_integrator = next_integrator(*current_integrator);
            *accumulator = 0.0;
            *diagnostics_baseline = make_diagnostics_baseline(sim);
            break;

        case HUD_ACTION_SAVE_STATE: {
            SaveState save_state = {0};

            save_state.simulation = *sim;
            save_state.scene = *current_scene;
            save_state.integrator = *current_integrator;
            save_state.camera = *camera;
            save_state.spawn_type = spawn->type;
            save_state.spawn_mass = spawn->mass;
            save_state.simulated_time_seconds = *simulated_time_seconds;
            save_state.time_scale = *time_scale;
            save_state.paused = *paused;

            if (!save_state_to_file(SAVE_STATE_PATH, &save_state)) {
                fprintf(stderr, "Failed to save state to %s\n", SAVE_STATE_PATH);
            }
            break;
        }

        case HUD_ACTION_LOAD_STATE: {
            SaveState loaded_state = {0};

            if (!load_state_from_file(SAVE_STATE_PATH, &loaded_state)) {
                fprintf(stderr, "Failed to load state from %s\n", SAVE_STATE_PATH);
                break;
            }

            *sim = loaded_state.simulation;
            *current_scene = loaded_state.scene;
            *current_integrator = loaded_state.integrator;
            *camera = loaded_state.camera;
            *simulated_time_seconds = loaded_state.simulated_time_seconds;
            *time_scale = loaded_state.time_scale;
            *paused = loaded_state.paused;
            *accumulator = 0.0;
            spawn->active = false;
            set_spawn_body_type(spawn, loaded_state.spawn_type);
            spawn->mass = loaded_state.spawn_mass;
            *diagnostics_baseline = make_diagnostics_baseline(sim);
            break;
        }

        case HUD_ACTION_TOGGLE_BENCHMARK:
            if (benchmark->active) {
                benchmark_recorder_stop(benchmark);
            } else {
                SimulationDiagnostics benchmark_diagnostics = compute_diagnostics(sim);
                SimulationDrift benchmark_drift =
                    compute_diagnostics_drift(&benchmark_diagnostics, diagnostics_baseline);

                if (!benchmark_recorder_start(
                        benchmark,
                        BENCHMARK_EXPORT_PATH,
                        *current_scene,
                        *current_integrator,
                        FIXED_DT,
                        diagnostics_baseline)) {
                    fprintf(stderr, "Failed to open benchmark export %s\n",
                            BENCHMARK_EXPORT_PATH);
                } else if (!benchmark_recorder_write_row(
                               benchmark,
                               *simulated_time_seconds,
                               *current_scene,
                               *current_integrator,
                               sim,
                               &benchmark_diagnostics,
                               &benchmark_drift)) {
                    fprintf(stderr, "Failed to write initial benchmark row\n");
                    benchmark_recorder_stop(benchmark);
                }
            }
            break;

        case HUD_ACTION_RESET_CAMERA:
            reset_camera(camera);
            break;

        case HUD_ACTION_RESET_BASELINE:
            *diagnostics_baseline = make_diagnostics_baseline(sim);
            break;

        case HUD_ACTION_NONE:
        default:
            break;
    }
}

static ScenePreset initial_scene_from_env(ScenePreset fallback) {
    const char *value = getenv("GRAVITYSIM_START_SCENE");

    if (value == NULL || value[0] == '\0') {
        return fallback;
    }

    if (strcmp(value, "0") == 0 || strcmp(value, "empty") == 0) {
        return SCENE_EMPTY;
    }

    if (strcmp(value, "1") == 0 || strcmp(value, "starter") == 0) {
        return SCENE_STARTER;
    }

    if (strcmp(value, "2") == 0 || strcmp(value, "chaotic") == 0 ||
        strcmp(value, "three-body") == 0 || strcmp(value, "three_body") == 0) {
        return SCENE_CHAOTIC_3_BODY;
    }

    if (strcmp(value, "3") == 0 || strcmp(value, "binary") == 0 ||
        strcmp(value, "binary-stars") == 0 || strcmp(value, "binary_stars") == 0) {
        return SCENE_BINARY_STARS;
    }
    if (strcmp(value, "4") == 0 || strcmp(value, "gas-giant-moon") == 0 || strcmp(value, "gas_giant_moon") == 0 || strcmp(value, "moon-system") == 0)
    {
        return SCENE_GAS_GIANT_MOON;
    }

    return fallback;
}

static double initial_time_scale_from_env(double fallback) {
    const char *value = getenv("GRAVITYSIM_START_TIME_SCALE");
    char *end = NULL;
    double requested;

    if (value == NULL || value[0] == '\0') {
        return fallback;
    }

    requested = strtod(value, &end);

    if (end == value) {
        return fallback;
    }

    return clamp_time_scale(requested);
}

static bool initial_hud_visibility_from_env(bool fallback) {
    const char *value = getenv("GRAVITYSIM_HIDE_HUD");

    if (value == NULL || value[0] == '\0') {
        return fallback;
    }

    if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "yes") == 0) {
        return false;
    }

    return fallback;
}

int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "Apsis",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    // Use software rendering if hardware rendering isn't an option

    if (renderer == NULL) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }

    if (renderer == NULL) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    if (!init_render_resources()) {
        fprintf(stderr, "Failed to initialize render resources\n");
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Simulation initial_state = {0};
    Simulation sim = {0};
    SpawnState spawn = {0};
    Camera camera = {0};
    Viewport viewport = {WINDOW_WIDTH, WINDOW_HEIGHT};
    BenchmarkRecorder benchmark = {0};
    ScenePreset current_scene = initial_scene_from_env(SCENE_STARTER);
    IntegratorMode current_integrator = INTEGRATOR_VELOCITY_VERLET;
    double accumulator = 0.0;
    double simulated_time_seconds = 0.0;
    double time_scale = initial_time_scale_from_env(TIME_SCALE_DEFAULT);
    DiagnosticsBaseline diagnostics_baseline = {0};
    bool hud_visible = initial_hud_visibility_from_env(true);
    bool camera_dragging = false;
    bool time_slider_dragging = false;
    int camera_drag_last_x = 0;
    int camera_drag_last_y = 0;

    activate_scene(current_scene, &initial_state, &sim, &spawn, &accumulator, &simulated_time_seconds);
    reset_camera(&camera);
    diagnostics_baseline = make_diagnostics_baseline(&sim);
    refresh_viewport(window, &viewport);

    spawn.active = false;
    set_spawn_body_type(&spawn, BODY_TYPE_ROCKY);

    bool running = true;
    bool paused = false;

    Uint64 previous_counter = SDL_GetPerformanceCounter();
    Uint64 performance_frequency = SDL_GetPerformanceFrequency();

    while (running) {
        refresh_viewport(window, &viewport);
        Uint64 current_counter = SDL_GetPerformanceCounter();
        double frame_time = (double)(current_counter - previous_counter) / (double)performance_frequency;
        previous_counter = current_counter;

        //Big frame skips can happen when moving windows or debugging so clamping stops one bad frtame from ruining the physics.
        if (frame_time > MAX_FRAME_TIME) {
            frame_time = MAX_FRAME_TIME;
        }
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_WINDOWEVENT &&
                (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                 event.window.event == SDL_WINDOWEVENT_RESIZED)) {
                refresh_viewport(window, &viewport);
            }
            if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        running = false;
                        break;

                    case SDLK_SPACE:
                        paused = !paused;
                        break;

                    case SDLK_r:
                        activate_scene(current_scene, &initial_state, &sim, &spawn, &accumulator,
                                       &simulated_time_seconds);
                        reset_camera(&camera);
                        diagnostics_baseline = make_diagnostics_baseline(&sim);
                        break;

                    case SDLK_h:
                        hud_visible = !hud_visible;
                        break;

                    case SDLK_c:
                        reset_camera(&camera);
                        break;

                    case SDLK_q:
                        zoom_camera_at(&camera, &viewport, viewport.width / 2, viewport.height / 2,
                                       CAMERA_ZOOM_FACTOR);
                        break;

                    case SDLK_e:
                        zoom_camera_at(&camera, &viewport, viewport.width / 2, viewport.height / 2,
                                       1.0 / CAMERA_ZOOM_FACTOR);
                        break;

                    case SDLK_t:
                        time_scale = TIME_SCALE_DEFAULT;
                        break;

                    case SDLK_F6:
                        if (benchmark.active) {
                            benchmark_recorder_stop(&benchmark);
                        } else {
                            SimulationDiagnostics benchmark_diagnostics = compute_diagnostics(&sim);
                            SimulationDrift benchmark_drift =
                                compute_diagnostics_drift(&benchmark_diagnostics, &diagnostics_baseline);

                            if (!benchmark_recorder_start(
                                    &benchmark,
                                    BENCHMARK_EXPORT_PATH,
                                    current_scene,
                                    current_integrator,
                                    FIXED_DT,
                                    &diagnostics_baseline)) {
                                fprintf(stderr, "Failed to open benchmark export %s\n",
                                        BENCHMARK_EXPORT_PATH);
                            } else if (!benchmark_recorder_write_row(
                                           &benchmark,
                                           simulated_time_seconds,
                                           current_scene,
                                           current_integrator,
                                           &sim,
                                           &benchmark_diagnostics,
                                           &benchmark_drift)) {
                                fprintf(stderr, "Failed to write initial benchmark row\n");
                                benchmark_recorder_stop(&benchmark);
                            }
                        }
                        break;

                    case SDLK_F5: {
                        SaveState save_state = {0};

                        save_state.simulation = sim;
                        save_state.scene = current_scene;
                        save_state.integrator = current_integrator;
                        save_state.camera = camera;
                        save_state.spawn_type = spawn.type;
                        save_state.spawn_mass = spawn.mass;
                        save_state.simulated_time_seconds = simulated_time_seconds;
                        save_state.time_scale = time_scale;
                        save_state.paused = paused;

                        if (!save_state_to_file(SAVE_STATE_PATH, &save_state)) {
                            fprintf(stderr, "Failed to save state to %s\n", SAVE_STATE_PATH);
                        }
                        break;
                    }

                    case SDLK_F9: {
                        SaveState loaded_state = {0};

                        if (!load_state_from_file(SAVE_STATE_PATH, &loaded_state)) {
                            fprintf(stderr, "Failed to load state from %s\n", SAVE_STATE_PATH);
                            break;
                        }

                        sim = loaded_state.simulation;
                        current_scene = loaded_state.scene;
                        current_integrator = loaded_state.integrator;
                        camera = loaded_state.camera;
                        simulated_time_seconds = loaded_state.simulated_time_seconds;
                        time_scale = loaded_state.time_scale;
                        paused = loaded_state.paused;
                        accumulator = 0.0;
                        spawn.active = false;
                        set_spawn_body_type(&spawn, loaded_state.spawn_type);
                        spawn.mass = loaded_state.spawn_mass;

                        // A loaded state becomes the new reference point.
                        diagnostics_baseline = make_diagnostics_baseline(&sim);
                        break;
                    }

                    case SDLK_TAB:
                        set_spawn_body_type(&spawn, next_body_type(spawn.type));
                        break;

                    case SDLK_i:
                        current_integrator = next_integrator(current_integrator);
                        accumulator = 0.0;
                        diagnostics_baseline = make_diagnostics_baseline(&sim);
                        break;

                    case SDLK_b:
                        diagnostics_baseline = make_diagnostics_baseline(&sim);
                        break;

                    case SDLK_0:
                        current_scene = SCENE_EMPTY;
                        activate_scene(current_scene, &initial_state, &sim, &spawn, &accumulator,
                                       &simulated_time_seconds);
                        reset_camera(&camera);
                        diagnostics_baseline = make_diagnostics_baseline(&sim);
                        break;

                    case SDLK_1:
                        current_scene = SCENE_STARTER;
                        activate_scene(current_scene, &initial_state, &sim, &spawn, &accumulator,
                                       &simulated_time_seconds);
                        reset_camera(&camera);
                        diagnostics_baseline = make_diagnostics_baseline(&sim);
                        break;

                    case SDLK_2:
                        current_scene = SCENE_CHAOTIC_3_BODY;
                        activate_scene(current_scene, &initial_state, &sim, &spawn, &accumulator,
                                       &simulated_time_seconds);
                        reset_camera(&camera);
                        diagnostics_baseline = make_diagnostics_baseline(&sim);
                        break;

                    case SDLK_3:
                        current_scene = SCENE_BINARY_STARS;
                        activate_scene(current_scene, &initial_state, &sim, &spawn, &accumulator,
                                       &simulated_time_seconds);
                        reset_camera(&camera);
                        diagnostics_baseline = make_diagnostics_baseline(&sim);
                        break;
                    case SDLK_4:
                        current_scene = SCENE_GAS_GIANT_MOON;
                        activate_scene(current_scene, &initial_state, &sim, &spawn, &accumulator,
                                       &simulated_time_seconds);
                        reset_camera(&camera);
                        diagnostics_baseline = make_diagnostics_baseline(&sim);
                        break;

                    case SDLK_LEFTBRACKET:
                        adjust_spawn_mass(&spawn, -1.0);
                        break;

                    case SDLK_RIGHTBRACKET:
                        adjust_spawn_mass(&spawn, 1.0);
                        break;

                    case SDLK_MINUS:
                    case SDLK_KP_MINUS:
                        adjust_time_scale(&time_scale, -1.0);
                        break;

                    case SDLK_EQUALS:
                    case SDLK_KP_PLUS:
                        adjust_time_scale(&time_scale, 1.0);
                        break;

                    default:
                        break;
                }
            }
            if (event.type == SDL_MOUSEWHEEL) {
                int wheel_y = event.wheel.y;

                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                    wheel_y = -wheel_y;
                }

                if (wheel_y != 0) {
                    SDL_Keymod modifiers = SDL_GetModState();

                    if ((modifiers & KMOD_SHIFT) != 0) {
                        adjust_spawn_mass(&spawn, (double)wheel_y);
                    } else {
                        int mouse_x;
                        int mouse_y;
                        double zoom_factor = pow(CAMERA_ZOOM_FACTOR, -wheel_y);

                        SDL_GetMouseState(&mouse_x, &mouse_y);
                        zoom_camera_at(&camera, &viewport, mouse_x, mouse_y, zoom_factor);
                    }
                }
            }

            if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (hud_time_slider_contains_point(event.button.x, event.button.y, hud_visible,
                                                       &viewport)) {
                        time_slider_dragging = true;
                        time_scale = hud_time_scale_from_point(event.button.x, hud_visible, &viewport);
                        continue;
                    }

                    if (hud_contains_point(event.button.x, event.button.y, hud_visible, &viewport)) {
                        apply_hud_action(
                            hud_action_at_point(event.button.x, event.button.y, hud_visible, &viewport),
                            &current_scene,
                            &initial_state,
                            &sim,
                            &spawn,
                            &camera,
                            &benchmark,
                            &current_integrator,
                            &paused,
                            &time_scale,
                            &accumulator,
                            &simulated_time_seconds,
                            &diagnostics_baseline
                        );
                        continue;
                    }

                    spawn.active = true;
                    spawn.start = screen_to_world(event.button.x, event.button.y, &camera, &viewport);
                    spawn.current = spawn.start;
                }

                if (event.button.button == SDL_BUTTON_RIGHT) {
                    spawn.active = false;
                }

                if (event.button.button == SDL_BUTTON_MIDDLE) {
                    camera_dragging = true;
                    camera_drag_last_x = event.button.x;
                    camera_drag_last_y = event.button.y;
                    spawn.active = false;
                }
            }

            if (event.type == SDL_MOUSEMOTION) {
                if (time_slider_dragging) {
                    time_scale = hud_time_scale_from_point(event.motion.x, hud_visible, &viewport);
                }

                if (spawn.active) {
                    spawn.current = screen_to_world(event.motion.x, event.motion.y, &camera, &viewport);
                }

                if (camera_dragging) {
                    int dx = event.motion.x - camera_drag_last_x;
                    int dy = event.motion.y - camera_drag_last_y;

                    pan_camera_pixels(&camera, -(double)dx, -(double)dy);
                    camera_drag_last_x = event.motion.x;
                    camera_drag_last_y = event.motion.y;
                }
            }

            if (event.type == SDL_MOUSEBUTTONUP &&
                event.button.button == SDL_BUTTON_LEFT &&
                time_slider_dragging) {
                time_scale = hud_time_scale_from_point(event.button.x, hud_visible, &viewport);
                time_slider_dragging = false;
                continue;
            }

            if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_MIDDLE) {
                camera_dragging = false;
            }

            if (event.type == SDL_MOUSEBUTTONUP &&
                event.button.button == SDL_BUTTON_LEFT &&
                spawn.active) {
                spawn.current = screen_to_world(event.button.x, event.button.y, &camera, &viewport);

                Vec2 launch_velocity = vec_scale(
                    vec_sub(spawn.current, spawn.start),
                    SPAWN_SPEED_PER_PIXEL / camera.meters_per_pixel
                );

                Body new_body = make_body(
                    spawn.start.x,
                    spawn.start.y,
                    launch_velocity.x,
                    launch_velocity.y,
                    spawn.type,
                    spawn.mass,
                    radius_from_mass_type(spawn.mass, spawn.type),
                    current_spawn_color(&spawn)
                );

                if (!add_body(&sim, new_body)) {
                    fprintf(stderr, "Body limit reached (%d max)\n", MAX_BODIES);
                }

                spawn.active = false;
            }
        }

        const Uint8 *keyboard = SDL_GetKeyboardState(NULL);
        double pan_pixels = CAMERA_KEY_PAN_PIXELS * frame_time * 8.0;

        if (keyboard[SDL_SCANCODE_A] || keyboard[SDL_SCANCODE_LEFT]) {
            pan_camera_pixels(&camera, -pan_pixels, 0.0);
        }

        if (keyboard[SDL_SCANCODE_D] || keyboard[SDL_SCANCODE_RIGHT]) {
            pan_camera_pixels(&camera, pan_pixels, 0.0);
        }

        if (keyboard[SDL_SCANCODE_W] || keyboard[SDL_SCANCODE_UP]) {
            pan_camera_pixels(&camera, 0.0, -pan_pixels);
        }

        if (keyboard[SDL_SCANCODE_S] || keyboard[SDL_SCANCODE_DOWN]) {
            pan_camera_pixels(&camera, 0.0, pan_pixels);
        }

        // Fixed timestep loop
        if (!paused) {
            // frame_time is real wall-clock time in seconds.
            // We scale it into simulated time before feeding the fixed-step accumulator.
            accumulator += frame_time * SIM_SECONDS_PER_REAL_SECOND * time_scale;

            while (accumulator >= FIXED_DT) {
                step_simulation(&sim, FIXED_DT, current_integrator);
                simulated_time_seconds += FIXED_DT;
                accumulator -= FIXED_DT;

                if (benchmark.active) {
                    SimulationDiagnostics benchmark_diagnostics = compute_diagnostics(&sim);
                    SimulationDrift benchmark_drift =
                        compute_diagnostics_drift(&benchmark_diagnostics, &diagnostics_baseline);

                    if (!benchmark_recorder_write_row(
                            &benchmark,
                            simulated_time_seconds,
                            current_scene,
                            current_integrator,
                            &sim,
                            &benchmark_diagnostics,
                            &benchmark_drift)) {
                        fprintf(stderr, "Failed to append benchmark data to %s\n",
                                BENCHMARK_EXPORT_PATH);
                        benchmark_recorder_stop(&benchmark);
                    }
                }
            }
        }
        SimulationDiagnostics diagnostics = compute_diagnostics(&sim);
        SimulationDrift drift = compute_diagnostics_drift(&diagnostics, &diagnostics_baseline);
        update_window_title(window, &sim, &spawn, &camera, current_scene, current_integrator,
                            benchmark.active, paused, time_scale);
        render_simulation(
            renderer,
            &sim,
            &spawn,
            &diagnostics,
            &drift,
            simulated_time_seconds,
            &camera,
            &viewport,
            current_scene,
            current_integrator,
            benchmark.active,
            paused,
            hud_visible,
            time_scale
        );
    }
    benchmark_recorder_stop(&benchmark);
    shutdown_render_resources();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
