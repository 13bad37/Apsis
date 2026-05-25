#include "render.h"

#include "scenes.h"
#include "simulation.h"

#include <SDL2/SDL_ttf.h>
#include <math.h>
#include <stdio.h>

static const char *HUD_FONT_CANDIDATES[] = {
    "assets/fonts/NotoSans-Regular.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/Library/Fonts/Arial.ttf",
    "C:/Windows/Fonts/arial.ttf"
};

static TTF_Font *g_hud_title_font = NULL;
static TTF_Font *g_hud_body_font = NULL;
static bool g_started_ttf = false;

enum {
    HUD_BUTTON_PAUSE = 0,
    HUD_BUTTON_RESET_SCENE,
    HUD_BUTTON_SAVE_STATE,
    HUD_BUTTON_LOAD_STATE,
    HUD_BUTTON_BENCHMARK,
    HUD_BUTTON_TIME_DOWN,
    HUD_BUTTON_TIME_UP,
    HUD_BUTTON_INTEGRATOR,
    HUD_BUTTON_RESET_CAMERA,
    HUD_BUTTON_RESET_BASELINE,
    HUD_BUTTON_COUNT
};

typedef struct {
    SDL_Rect status_panel;
    SDL_Rect controls_panel;
    SDL_Rect buttons[HUD_BUTTON_COUNT];
    SDL_Rect time_slider_track;
    SDL_Rect time_slider_hit_box;
    int status_text_x;
    int controls_text_x;
    int controls_hint_y;
    int value_x;
    int right_column_x;
} HudLayout;

static void draw_filled_circle(SDL_Renderer *renderer, int cx, int cy, int radius, SDL_Color color);

static TTF_Font *open_font_from_candidates(int point_size) {
    int candidate_count = (int)(sizeof(HUD_FONT_CANDIDATES) / sizeof(HUD_FONT_CANDIDATES[0]));

    for (int i = 0; i < candidate_count; i++) {
        TTF_Font *font = TTF_OpenFont(HUD_FONT_CANDIDATES[i], point_size);

        if (font != NULL) {
            return font;
        }
    }

    return NULL;
}

bool init_render_resources(void) {
    if (TTF_WasInit() == 0) {
        if (TTF_Init() != 0) {
            fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
            return false;
        }

        g_started_ttf = true;
    }

    g_hud_title_font = open_font_from_candidates(22);
    g_hud_body_font = open_font_from_candidates(14);

    if (g_hud_title_font == NULL || g_hud_body_font == NULL) {
        fprintf(stderr, "Failed to load HUD font: %s\n", TTF_GetError());
        shutdown_render_resources();
        return false;
    }

    TTF_SetFontHinting(g_hud_title_font, TTF_HINTING_LIGHT);
    TTF_SetFontHinting(g_hud_body_font, TTF_HINTING_LIGHT);
    return true;
}

void shutdown_render_resources(void) {
    if (g_hud_title_font != NULL) {
        TTF_CloseFont(g_hud_title_font);
        g_hud_title_font = NULL;
    }

    if (g_hud_body_font != NULL) {
        TTF_CloseFont(g_hud_body_font);
        g_hud_body_font = NULL;
    }

    if (g_started_ttf) {
        TTF_Quit();
        g_started_ttf = false;
    }
}

static void viewport_dimensions(const Viewport *viewport, int *width, int *height) {
    if (viewport != NULL) {
        *width = viewport->width;
        *height = viewport->height;
        return;
    }

    *width = WINDOW_WIDTH;
    *height = WINDOW_HEIGHT;
}

static int hud_title_height(void) {
    if (g_hud_title_font != NULL) {
        return TTF_FontHeight(g_hud_title_font);
    }

    return 22;
}

static int hud_body_step(void) {
    if (g_hud_body_font != NULL) {
        return TTF_FontLineSkip(g_hud_body_font) + 1;
    }

    return 18;
}

static HudLayout get_hud_layout(const Viewport *viewport) {
    HudLayout layout = {0};
    int viewport_width;
    int viewport_height;
    int title_height = hud_title_height();
    int body_step = hud_body_step();
    int controls_button_width;
    int button_gap = 8;
    int button_height = 28;
    int controls_buttons_y;
    int status_rows_start_y;
    int time_row_y;
    bool compact;

    viewport_dimensions(viewport, &viewport_width, &viewport_height);
    compact = viewport_width < 720;

    layout.status_panel.x = 18;
    layout.status_panel.y = 18;
    layout.status_panel.w = compact ? viewport_width - 36 : 428;
    layout.status_panel.h = compact
        ? 28 + title_height + 18 + (6 * body_step) + 18
        : 28 + title_height + 18 + (14 * body_step) + 36;

    layout.controls_panel.x = 18;
    layout.controls_panel.w = compact ? viewport_width - 36 : 430;
    layout.controls_panel.h = 28 + title_height + 18 + (2 * button_height) + body_step + 38;
    layout.controls_panel.y = viewport_height - layout.controls_panel.h - 18;
    if (layout.controls_panel.y < 18) {
        layout.controls_panel.y = 18;
    }

    layout.status_text_x = layout.status_panel.x + 16;
    layout.controls_text_x = layout.controls_panel.x + 16;
    layout.value_x = layout.status_panel.x + (compact ? 102 : 118);
    layout.right_column_x = layout.status_panel.x + (compact ? 214 : 220);

    status_rows_start_y = layout.status_panel.y + 14 + title_height + 18;
    time_row_y = status_rows_start_y + ((compact ? 3 : 5) * body_step) + 2;
    layout.time_slider_track.x = layout.value_x;
    layout.time_slider_track.y = time_row_y + 6;
    layout.time_slider_track.w = compact ? 130 : 176;
    layout.time_slider_track.h = 8;
    layout.time_slider_hit_box.x = layout.time_slider_track.x;
    layout.time_slider_hit_box.y = time_row_y - 3;
    layout.time_slider_hit_box.w = layout.time_slider_track.w;
    layout.time_slider_hit_box.h = body_step + 6;

    controls_button_width = (layout.controls_panel.w - 32 - (4 * button_gap)) / 5;
    controls_buttons_y = layout.controls_panel.y + 14 + title_height + 18;

    for (int i = 0; i < 5; i++) {
        layout.buttons[i].x = layout.controls_panel.x + 16 + (i * (controls_button_width + button_gap));
        layout.buttons[i].y = controls_buttons_y;
        layout.buttons[i].w = controls_button_width;
        layout.buttons[i].h = button_height;

        layout.buttons[i + 5].x = layout.buttons[i].x;
        layout.buttons[i + 5].y = controls_buttons_y + button_height + button_gap;
        layout.buttons[i + 5].w = controls_button_width;
        layout.buttons[i + 5].h = button_height;
    }

    layout.controls_hint_y = controls_buttons_y + (2 * button_height) + button_gap + 10;
    (void)viewport_width;
    return layout;
}

static bool point_in_rect(int x, int y, const SDL_Rect *rect) {
    return x >= rect->x &&
           x < rect->x + rect->w &&
           y >= rect->y &&
           y < rect->y + rect->h;
}

static SDL_Point world_to_screen(Vec2 world, const Camera *camera, const Viewport *viewport) {
    SDL_Point point;
    int viewport_width;
    int viewport_height;

    viewport_dimensions(viewport, &viewport_width, &viewport_height);

    point.x = (int)lround((viewport_width * 0.5) +
                          ((world.x - camera->center.x) / camera->meters_per_pixel));
    point.y = (int)lround((viewport_height * 0.5) -
                          ((world.y - camera->center.y) / camera->meters_per_pixel));
    return point;
}

Vec2 screen_to_world(int screen_x, int screen_y, const Camera *camera, const Viewport *viewport) {
    int viewport_width;
    int viewport_height;

    viewport_dimensions(viewport, &viewport_width, &viewport_height);

    return vec2(
        camera->center.x + (((double)screen_x - (viewport_width * 0.5)) * camera->meters_per_pixel),
        camera->center.y + (((viewport_height * 0.5) - (double)screen_y) * camera->meters_per_pixel)
    );
}

static int draw_radius_pixels(double physical_radius, const Camera *camera) {
    double real_radius_pixels = physical_radius / camera->meters_per_pixel;

    // Real radii are too small to read at orbital scale, so the renderer
    // compresses visible size while leaving the physical radius untouched.
    double compressed_radius_pixels = 2.0 + (1.8 * cbrt(physical_radius / 1.0e6));
    double draw_radius = fmax(real_radius_pixels, compressed_radius_pixels);

    if (draw_radius < 4.0) {
        draw_radius = 4.0;
    }

    return (int)lround(draw_radius);
}

static void draw_text_line(SDL_Renderer *renderer, TTF_Font *font, int x, int y,
                           SDL_Color color, const char *text) {
    SDL_Surface *surface;
    SDL_Texture *texture;
    SDL_Rect destination;

    if (font == NULL || text == NULL || text[0] == '\0') {
        return;
    }

    surface = TTF_RenderUTF8_Blended(font, text, color);
    if (surface == NULL) {
        return;
    }

    texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture == NULL) {
        SDL_FreeSurface(surface);
        return;
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

    destination.x = x;
    destination.y = y;
    destination.w = surface->w;
    destination.h = surface->h;

    SDL_RenderCopy(renderer, texture, NULL, &destination);
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

static void draw_panel(SDL_Renderer *renderer, int x, int y, int width, int height) {
    SDL_Rect panel = {x, y, width, height};
    SDL_Rect border = {x, y, width, height};

    SDL_SetRenderDrawColor(renderer, 10, 14, 24, 222);
    SDL_RenderFillRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 76, 94, 132, 255);
    SDL_RenderDrawRect(renderer, &border);
}

static void draw_rule(SDL_Renderer *renderer, int x, int y, int width, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawLine(renderer, x, y, x + width, y);
}

static double clamp_unit_interval(double value) {
    if (value < 0.0) {
        return 0.0;
    }

    if (value > 1.0) {
        return 1.0;
    }

    return value;
}

static double clamp_time_scale(double time_scale) {
    if (time_scale < TIME_SCALE_MIN) {
        return TIME_SCALE_MIN;
    }

    if (time_scale > TIME_SCALE_MAX) {
        return TIME_SCALE_MAX;
    }

    return time_scale;
}

static double time_scale_to_slider_fraction(double time_scale) {
    double clamped = clamp_time_scale(time_scale);
    double log_min = log(TIME_SCALE_MIN);
    double log_max = log(TIME_SCALE_MAX);
    double log_value = log(clamped);

    return (log_value - log_min) / (log_max - log_min);
}

static double slider_fraction_to_time_scale(double fraction) {
    double clamped_fraction = clamp_unit_interval(fraction);
    double log_min = log(TIME_SCALE_MIN);
    double log_max = log(TIME_SCALE_MAX);

    return exp(log_min + (clamped_fraction * (log_max - log_min)));
}

static void draw_time_scale_slider(SDL_Renderer *renderer, const SDL_Rect *track, double time_scale) {
    SDL_Rect filled = *track;
    double fraction = time_scale_to_slider_fraction(time_scale);
    int handle_x = track->x + (int)lround(fraction * (double)track->w);

    filled.w = (int)lround(fraction * (double)track->w);

    SDL_SetRenderDrawColor(renderer, 40, 50, 74, 255);
    SDL_RenderFillRect(renderer, track);

    if (filled.w > 0) {
        SDL_SetRenderDrawColor(renderer, 255, 210, 120, 255);
        SDL_RenderFillRect(renderer, &filled);
    }

    SDL_SetRenderDrawColor(renderer, 76, 94, 132, 255);
    SDL_RenderDrawRect(renderer, track);

    draw_filled_circle(
        renderer,
        handle_x,
        track->y + (track->h / 2),
        6,
        (SDL_Color){255, 214, 140, 255}
    );
}

static void format_scientific(char *buffer, size_t buffer_size, double value) {
    snprintf(buffer, buffer_size, "%.3e", value);
}

static double vec_magnitude(Vec2 v) {
    return sqrt((v.x * v.x) + (v.y * v.y));
}

static void draw_key_value_row(SDL_Renderer *renderer, TTF_Font *font, int x, int y,
                               int value_x, SDL_Color label_color, SDL_Color value_color,
                               const char *label, const char *value) {
    draw_text_line(renderer, font, x, y, label_color, label);
    draw_text_line(renderer, font, value_x, y, value_color, value);
}

static void draw_button(SDL_Renderer *renderer, TTF_Font *font, const SDL_Rect *rect,
                        SDL_Color fill_color, SDL_Color border_color, SDL_Color text_color,
                        const char *label, bool hovered) {
    SDL_Rect inset = {rect->x + 1, rect->y + 1, rect->w - 2, rect->h - 2};
    SDL_Surface *surface;
    SDL_Texture *texture;
    SDL_Rect destination;

    if (hovered) {
        fill_color.r = (Uint8)fmin(255, fill_color.r + 18);
        fill_color.g = (Uint8)fmin(255, fill_color.g + 18);
        fill_color.b = (Uint8)fmin(255, fill_color.b + 18);
    }

    SDL_SetRenderDrawColor(renderer, border_color.r, border_color.g, border_color.b, border_color.a);
    SDL_RenderFillRect(renderer, rect);
    SDL_SetRenderDrawColor(renderer, fill_color.r, fill_color.g, fill_color.b, fill_color.a);
    SDL_RenderFillRect(renderer, &inset);

    if (font == NULL || label == NULL || label[0] == '\0') {
        return;
    }

    surface = TTF_RenderUTF8_Blended(font, label, text_color);
    if (surface == NULL) {
        return;
    }

    texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture == NULL) {
        SDL_FreeSurface(surface);
        return;
    }

    destination.w = surface->w;
    destination.h = surface->h;
    destination.x = rect->x + ((rect->w - destination.w) / 2);
    destination.y = rect->y + ((rect->h - destination.h) / 2);

    SDL_RenderCopy(renderer, texture, NULL, &destination);
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

static void draw_filled_circle(SDL_Renderer *renderer, int cx, int cy, int radius, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    for (int y = -radius; y <= radius; y++) {
        int x_span = (int)sqrt((double)((radius * radius) - (y * y)));
        SDL_RenderDrawLine(renderer, cx - x_span, cy + y, cx + x_span, cy + y);
    }
}

static void draw_trail(SDL_Renderer *renderer, const Body *body, const Camera *camera,
                       const Viewport *viewport) {
    if (body->trail_count < 2) {
        return;
    }

    int oldest = (body->trail_next - body->trail_count + TRAIL_LENGTH) % TRAIL_LENGTH;

    for (int i = 1; i < body->trail_count; i++) {
        int index_a = (oldest + i - 1) % TRAIL_LENGTH;
        int index_b = (oldest + i) % TRAIL_LENGTH;
        double t = (double)i / (double)(body->trail_count - 1);
        Uint8 alpha = (Uint8)(20.0 + (160.0 * t));
        SDL_Point point_a = world_to_screen(body->trail[index_a], camera, viewport);
        SDL_Point point_b = world_to_screen(body->trail[index_b], camera, viewport);

        SDL_SetRenderDrawColor(renderer, body->color.r, body->color.g, body->color.b, alpha);
        SDL_RenderDrawLine(renderer, point_a.x, point_a.y, point_b.x, point_b.y);
    }
}

static void draw_spawn_preview(SDL_Renderer *renderer, const SpawnState *spawn, const Camera *camera,
                               const Viewport *viewport) {
    if (!spawn->active) {
        return;
    }

    SDL_Color color = current_spawn_color(spawn);
    SDL_Color ghost_color = color;
    SDL_Color cursor_color = {255, 255, 255, 180};
    SDL_Point start = world_to_screen(spawn->start, camera, viewport);
    SDL_Point current = world_to_screen(spawn->current, camera, viewport);

    ghost_color.a = 120;

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 220);
    SDL_RenderDrawLine(renderer, start.x, start.y, current.x, current.y);

    draw_filled_circle(
        renderer,
        start.x,
        start.y,
        draw_radius_pixels(radius_from_mass_type(spawn->mass, spawn->type), camera),
        ghost_color
    );

    draw_filled_circle(renderer, current.x, current.y, 3, cursor_color);
}

static void draw_hud(SDL_Renderer *renderer, const Simulation *sim, const SpawnState *spawn,
                     const SimulationDiagnostics *diagnostics, const SimulationDrift *drift,
                     double simulated_time_seconds, const Camera *camera, const Viewport *viewport,
                     ScenePreset scene,
                     IntegratorMode integrator, bool benchmark_recording, bool paused,
                     double time_scale) {
    const SDL_Color title_color = {255, 214, 140, 255};
    const SDL_Color text_color = {228, 233, 245, 255};
    const SDL_Color muted_color = {146, 160, 188, 255};
    const SDL_Color rule_color = {58, 72, 104, 255};
    const SDL_Color positive_color = {160, 255, 190, 255};
    const SDL_Color negative_color = {255, 190, 120, 255};
    const SDL_Color button_fill = {18, 24, 38, 255};
    SimulationDiagnostics zero_diagnostics = {0};
    SimulationDrift zero_drift = {0};
    HudLayout layout = get_hud_layout(viewport);
    char line[160];
    char value[64];
    int title_height;
    int body_step;
    int status_line_y;
    int mouse_x;
    int mouse_y;
    double simulated_days = simulated_time_seconds / 86400.0;
    double view_km_per_pixel = camera->meters_per_pixel / 1000.0;
    double momentum_magnitude;
    bool compact = viewport != NULL && viewport->width < 720;

    if (diagnostics == NULL) {
        diagnostics = &zero_diagnostics;
    }

    if (drift == NULL) {
        drift = &zero_drift;
    }

    momentum_magnitude = vec_magnitude(diagnostics->total_momentum);
    SDL_GetMouseState(&mouse_x, &mouse_y);
    title_height = TTF_FontHeight(g_hud_title_font);
    body_step = TTF_FontLineSkip(g_hud_body_font) + 1;

    draw_panel(renderer, layout.status_panel.x, layout.status_panel.y,
               layout.status_panel.w, layout.status_panel.h);
    draw_text_line(renderer, g_hud_title_font, layout.status_text_x, layout.status_panel.y + 14,
                   title_color, "Apsis");
    draw_text_line(renderer, g_hud_body_font, layout.right_column_x, layout.status_panel.y + 18,
                   benchmark_recording ? negative_color : muted_color,
                   benchmark_recording ? "benchmark recording" : "benchmark idle");
    draw_rule(renderer, layout.status_panel.x + 16, layout.status_panel.y + 14 + title_height + 8,
              layout.status_panel.w - 32, rule_color);

    status_line_y = layout.status_panel.y + 14 + title_height + 18;

    if (compact) {
        draw_key_value_row(renderer, g_hud_body_font, layout.status_text_x, status_line_y,
                           layout.value_x, muted_color, text_color, "Scene", scene_name(scene));
        status_line_y += body_step;

        draw_key_value_row(renderer, g_hud_body_font, layout.status_text_x, status_line_y,
                           layout.value_x, muted_color, text_color, "Method",
                           integrator_name(integrator));
        status_line_y += body_step;

        snprintf(line, sizeof(line), "%d/%d  %s", sim->body_count, MAX_BODIES,
                 paused ? "paused" : "running");
        draw_key_value_row(renderer, g_hud_body_font, layout.status_text_x, status_line_y,
                           layout.value_x, muted_color,
                           paused ? negative_color : positive_color, "Bodies", line);
        status_line_y += body_step + 2;

        draw_key_value_row(renderer, g_hud_body_font, layout.status_text_x, status_line_y,
                           layout.value_x, muted_color, text_color, "Time", " ");
        draw_time_scale_slider(renderer, &layout.time_slider_track, time_scale);
        snprintf(line, sizeof(line), "%.2gx", time_scale);
        draw_text_line(renderer, g_hud_body_font,
                       layout.time_slider_track.x + layout.time_slider_track.w + 10,
                       status_line_y, text_color, line);
        status_line_y += body_step + 8;

        snprintf(line, sizeof(line), "Drift  dE %.1e   dP %.1e   dL %.1e",
                 drift->energy_relative,
                 drift->momentum_relative,
                 drift->angular_momentum_relative);
        draw_text_line(renderer, g_hud_body_font, layout.status_text_x, status_line_y,
                       muted_color, line);
        goto draw_controls;
    }

    draw_key_value_row(renderer, g_hud_body_font, layout.status_text_x, status_line_y, layout.value_x,
                       muted_color, text_color, "Scene", scene_name(scene));
    draw_key_value_row(renderer, g_hud_body_font, layout.right_column_x, status_line_y,
                       layout.right_column_x + 58, muted_color,
                       paused ? negative_color : positive_color,
                       "Status", paused ? "paused" : "running");
    status_line_y += body_step;

    draw_key_value_row(renderer, g_hud_body_font, layout.status_text_x, status_line_y, layout.value_x,
                       muted_color, text_color, "Integrator", integrator_name(integrator));
    status_line_y += body_step;

    snprintf(line, sizeof(line), "%.2f", simulated_days);
    draw_key_value_row(renderer, g_hud_body_font, layout.status_text_x, status_line_y, layout.value_x,
                       muted_color, text_color, "Sim days", line);
    snprintf(line, sizeof(line), "%d/%d", sim->body_count, MAX_BODIES);
    draw_key_value_row(renderer, g_hud_body_font, layout.right_column_x, status_line_y,
                       layout.right_column_x + 56, muted_color, text_color, "Bodies", line);
    status_line_y += body_step;

    snprintf(line, sizeof(line), "%.3g km/px", view_km_per_pixel);
    draw_key_value_row(renderer, g_hud_body_font, layout.status_text_x, status_line_y, layout.value_x,
                       muted_color, text_color, "View", line);
    status_line_y += body_step;

    snprintf(
        line,
        sizeof(line),
        "%s %.2f %s",
        body_type_name(spawn->type),
        spawn->mass / body_type_mass_display_scale(spawn->type),
        body_type_mass_display_unit(spawn->type)
    );
    draw_key_value_row(renderer, g_hud_body_font, layout.status_text_x, status_line_y, layout.value_x,
                       muted_color, text_color, "Spawn", line);
    status_line_y += body_step + 2;

    draw_key_value_row(renderer, g_hud_body_font, layout.status_text_x, status_line_y, layout.value_x,
                       muted_color, text_color, "Time scale", " ");
    draw_time_scale_slider(renderer, &layout.time_slider_track, time_scale);
    snprintf(line, sizeof(line), "%.3gx", time_scale);
    draw_text_line(renderer, g_hud_body_font, layout.time_slider_track.x + layout.time_slider_track.w + 14,
                   status_line_y,
                   text_color, line);
    status_line_y += body_step + 6;

    draw_rule(renderer, layout.status_panel.x + 16, status_line_y - 2,
              layout.status_panel.w - 32, rule_color);
    draw_text_line(renderer, g_hud_body_font, layout.status_text_x, status_line_y + 6,
                   title_color, "Diagnostics");
    status_line_y += body_step + 8;

    format_scientific(value, sizeof(value), diagnostics->total_energy);
    draw_key_value_row(renderer, g_hud_body_font, layout.status_text_x, status_line_y, layout.value_x,
                       muted_color, text_color, "Total E", value);
    status_line_y += body_step;

    format_scientific(value, sizeof(value), diagnostics->kinetic_energy);
    draw_key_value_row(renderer, g_hud_body_font, layout.status_text_x, status_line_y, layout.value_x,
                       muted_color, text_color, "Kinetic", value);
    status_line_y += body_step;

    format_scientific(value, sizeof(value), diagnostics->potential_energy);
    draw_key_value_row(renderer, g_hud_body_font, layout.status_text_x, status_line_y, layout.value_x,
                       muted_color, text_color, "Potential", value);
    status_line_y += body_step;

    format_scientific(value, sizeof(value), momentum_magnitude);
    draw_key_value_row(renderer, g_hud_body_font, layout.status_text_x, status_line_y, layout.value_x,
                       muted_color, text_color, "|P|", value);
    status_line_y += body_step;

    format_scientific(value, sizeof(value), diagnostics->angular_momentum_z);
    draw_key_value_row(renderer, g_hud_body_font, layout.status_text_x, status_line_y, layout.value_x,
                       muted_color, text_color, "Lz", value);
    status_line_y += body_step;

    snprintf(line, sizeof(line), "dE %.2e   dP %.2e   dL %.2e",
             drift->energy_relative,
             drift->momentum_relative,
             drift->angular_momentum_relative);
    draw_text_line(renderer, g_hud_body_font, layout.status_text_x, status_line_y,
                   muted_color, line);

draw_controls:
    draw_panel(renderer, layout.controls_panel.x, layout.controls_panel.y,
               layout.controls_panel.w, layout.controls_panel.h);
    draw_text_line(renderer, g_hud_title_font, layout.controls_text_x, layout.controls_panel.y + 14,
                   title_color, "Controls");
    draw_rule(renderer, layout.controls_panel.x + 16, layout.controls_panel.y + 14 + title_height + 8,
              layout.controls_panel.w - 32, rule_color);

    draw_button(renderer, g_hud_body_font, &layout.buttons[HUD_BUTTON_PAUSE],
                paused ? negative_color : button_fill, rule_color, text_color,
                paused ? "Resume" : "Pause",
                point_in_rect(mouse_x, mouse_y, &layout.buttons[HUD_BUTTON_PAUSE]));
    draw_button(renderer, g_hud_body_font, &layout.buttons[HUD_BUTTON_RESET_SCENE],
                button_fill, rule_color, text_color, "Reset",
                point_in_rect(mouse_x, mouse_y, &layout.buttons[HUD_BUTTON_RESET_SCENE]));
    draw_button(renderer, g_hud_body_font, &layout.buttons[HUD_BUTTON_SAVE_STATE],
                button_fill, rule_color, text_color, "Save",
                point_in_rect(mouse_x, mouse_y, &layout.buttons[HUD_BUTTON_SAVE_STATE]));
    draw_button(renderer, g_hud_body_font, &layout.buttons[HUD_BUTTON_LOAD_STATE],
                button_fill, rule_color, text_color, "Load",
                point_in_rect(mouse_x, mouse_y, &layout.buttons[HUD_BUTTON_LOAD_STATE]));
    draw_button(renderer, g_hud_body_font, &layout.buttons[HUD_BUTTON_BENCHMARK],
                benchmark_recording ? negative_color : button_fill, rule_color, text_color,
                benchmark_recording ? "Stop" : (compact ? "Bench" : "Bench rec"),
                point_in_rect(mouse_x, mouse_y, &layout.buttons[HUD_BUTTON_BENCHMARK]));

    draw_button(renderer, g_hud_body_font, &layout.buttons[HUD_BUTTON_TIME_DOWN],
                button_fill, rule_color, text_color, "Time -",
                point_in_rect(mouse_x, mouse_y, &layout.buttons[HUD_BUTTON_TIME_DOWN]));
    draw_button(renderer, g_hud_body_font, &layout.buttons[HUD_BUTTON_TIME_UP],
                button_fill, rule_color, text_color, "Time +",
                point_in_rect(mouse_x, mouse_y, &layout.buttons[HUD_BUTTON_TIME_UP]));
    draw_button(renderer, g_hud_body_font, &layout.buttons[HUD_BUTTON_INTEGRATOR],
                button_fill, rule_color, text_color, compact ? "Int." : "Integr.",
                point_in_rect(mouse_x, mouse_y, &layout.buttons[HUD_BUTTON_INTEGRATOR]));
    draw_button(renderer, g_hud_body_font, &layout.buttons[HUD_BUTTON_RESET_CAMERA],
                button_fill, rule_color, text_color, compact ? "Cam" : "Camera",
                point_in_rect(mouse_x, mouse_y, &layout.buttons[HUD_BUTTON_RESET_CAMERA]));
    draw_button(renderer, g_hud_body_font, &layout.buttons[HUD_BUTTON_RESET_BASELINE],
                button_fill, rule_color, text_color, compact ? "Base" : "Baseline",
                point_in_rect(mouse_x, mouse_y, &layout.buttons[HUD_BUTTON_RESET_BASELINE]));

    draw_text_line(renderer, g_hud_body_font, layout.controls_text_x, layout.controls_hint_y,
                   muted_color, compact
                       ? "Drag space to spawn; tap buttons to control"
                       : "Mouse: wheel/QE zoom, drag time slider, middle/WASD pan");
    draw_text_line(renderer, g_hud_body_font, layout.controls_text_x,
                   layout.controls_hint_y + body_step,
                   muted_color, compact
                       ? "Keys: 0-4 scenes, I method, H hide HUD"
                       : "Keys: Tab type, [ ] mass, 0-4 scenes, H hide HUD");
}

bool hud_contains_point(int screen_x, int screen_y, bool hud_visible, const Viewport *viewport) {
    HudLayout layout;

    if (!hud_visible) {
        return false;
    }

    layout = get_hud_layout(viewport);
    return point_in_rect(screen_x, screen_y, &layout.status_panel) ||
           point_in_rect(screen_x, screen_y, &layout.controls_panel);
}

HudAction hud_action_at_point(int screen_x, int screen_y, bool hud_visible, const Viewport *viewport) {
    HudLayout layout;

    if (!hud_visible) {
        return HUD_ACTION_NONE;
    }

    layout = get_hud_layout(viewport);

    if (point_in_rect(screen_x, screen_y, &layout.buttons[HUD_BUTTON_PAUSE])) {
        return HUD_ACTION_TOGGLE_PAUSE;
    }

    if (point_in_rect(screen_x, screen_y, &layout.buttons[HUD_BUTTON_RESET_SCENE])) {
        return HUD_ACTION_RESET_SCENE;
    }

    if (point_in_rect(screen_x, screen_y, &layout.buttons[HUD_BUTTON_SAVE_STATE])) {
        return HUD_ACTION_SAVE_STATE;
    }

    if (point_in_rect(screen_x, screen_y, &layout.buttons[HUD_BUTTON_LOAD_STATE])) {
        return HUD_ACTION_LOAD_STATE;
    }

    if (point_in_rect(screen_x, screen_y, &layout.buttons[HUD_BUTTON_BENCHMARK])) {
        return HUD_ACTION_TOGGLE_BENCHMARK;
    }

    if (point_in_rect(screen_x, screen_y, &layout.buttons[HUD_BUTTON_TIME_DOWN])) {
        return HUD_ACTION_TIME_DOWN;
    }

    if (point_in_rect(screen_x, screen_y, &layout.buttons[HUD_BUTTON_TIME_UP])) {
        return HUD_ACTION_TIME_UP;
    }

    if (point_in_rect(screen_x, screen_y, &layout.buttons[HUD_BUTTON_INTEGRATOR])) {
        return HUD_ACTION_CYCLE_INTEGRATOR;
    }

    if (point_in_rect(screen_x, screen_y, &layout.buttons[HUD_BUTTON_RESET_CAMERA])) {
        return HUD_ACTION_RESET_CAMERA;
    }

    if (point_in_rect(screen_x, screen_y, &layout.buttons[HUD_BUTTON_RESET_BASELINE])) {
        return HUD_ACTION_RESET_BASELINE;
    }

    return HUD_ACTION_NONE;
}

bool hud_time_slider_contains_point(int screen_x, int screen_y, bool hud_visible,
                                    const Viewport *viewport) {
    HudLayout layout;

    if (!hud_visible) {
        return false;
    }

    layout = get_hud_layout(viewport);
    return point_in_rect(screen_x, screen_y, &layout.time_slider_hit_box);
}

double hud_time_scale_from_point(int screen_x, bool hud_visible, const Viewport *viewport) {
    HudLayout layout;
    double fraction;

    if (!hud_visible) {
        return TIME_SCALE_DEFAULT;
    }

    layout = get_hud_layout(viewport);
    fraction = (double)(screen_x - layout.time_slider_track.x) / (double)layout.time_slider_track.w;
    return slider_fraction_to_time_scale(fraction);
}

void render_simulation(SDL_Renderer *renderer, const Simulation *sim, const SpawnState *spawn,
                       const SimulationDiagnostics *diagnostics, const SimulationDrift *drift,
                       double simulated_time_seconds, const Camera *camera, const Viewport *viewport,
                       ScenePreset scene, IntegratorMode integrator, bool benchmark_recording, bool paused,
                       bool hud_visible, double time_scale) {
    SDL_SetRenderDrawColor(renderer, 8, 12, 20, 255);
    SDL_RenderClear(renderer);

    for (int i = 0; i < sim->body_count; i++) {
        draw_trail(renderer, &sim->bodies[i], camera, viewport);
    }

    for (int i = 0; i < sim->body_count; i++) {
        const Body *body = &sim->bodies[i];
        SDL_Point point = world_to_screen(body->position, camera, viewport);

        draw_filled_circle(
            renderer,
            point.x,
            point.y,
            draw_radius_pixels(body->radius, camera),
            body->color
        );
    }

    draw_spawn_preview(renderer, spawn, camera, viewport);

    if (hud_visible) {
        draw_hud(
            renderer,
            sim,
            spawn,
            diagnostics,
            drift,
            simulated_time_seconds,
            camera,
            viewport,
            scene,
            integrator,
            benchmark_recording,
            paused,
            time_scale
        );
    }

    SDL_RenderPresent(renderer);
}

void update_window_title(SDL_Window *window, const Simulation *sim, const SpawnState *spawn,
                         const Camera *camera, ScenePreset scene, IntegratorMode integrator,
                         bool benchmark_recording, bool paused, double time_scale) {
    char title[256];

    snprintf(
        title,
        sizeof(title),
        "Apsis | %s | scene %s | %s | bench %s | bodies %d/%d | spawn %s %.2f %s | time %.3gx | %.3g km/px",
        paused ? "paused" : "running",
        scene_name(scene),
        integrator_name(integrator),
        benchmark_recording ? "rec" : "idle",
        sim->body_count,
        MAX_BODIES,
        body_type_name(spawn->type),
        spawn->mass / body_type_mass_display_scale(spawn->type),
        body_type_mass_display_unit(spawn->type),
        time_scale,
        camera->meters_per_pixel / 1000.0
    );
    SDL_SetWindowTitle(window, title);
}
