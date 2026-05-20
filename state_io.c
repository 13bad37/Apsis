#include "state_io.h"

#include "simulation.h"

#include <stdio.h>
#include <string.h>

static bool valid_scene(ScenePreset scene) {
    return scene >= SCENE_EMPTY && scene <= SCENE_GAS_GIANT_MOON;
}

static bool valid_integrator(IntegratorMode integrator) {
    return integrator >= INTEGRATOR_SEMI_IMPLICIT_EULER &&
           integrator < INTEGRATOR_COUNT;
}

static bool valid_body_type(BodyType type) {
    return type >= BODY_TYPE_ROCKY && type < BODY_TYPE_COUNT;
}

static double legacy_time_scale_from_index(int index) {
    static const double legacy_time_scales[] = {0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0};
    int count = (int)(sizeof(legacy_time_scales) / sizeof(legacy_time_scales[0]));

    if (index < 0) {
        return legacy_time_scales[0];
    }

    if (index >= count) {
        return legacy_time_scales[count - 1];
    }

    return legacy_time_scales[index];
}

bool save_state_to_file(const char *path, const SaveState *state) {
    FILE *file;

    if (path == NULL || state == NULL) {
        return false;
    }

    file = fopen(path, "w");
    if (file == NULL) {
        return false;
    }

    // Plain text keeps the format easy to inspect and change while the
    // project is still evolving.
    fprintf(file, "APSIS_SAVE_V1\n");
    fprintf(file, "scene %d\n", (int)state->scene);
    fprintf(file, "integrator %d\n", (int)state->integrator);
    fprintf(file, "simulated_time_seconds %.17g\n", state->simulated_time_seconds);
    fprintf(file, "time_scale %.17g\n", state->time_scale);
    fprintf(file, "paused %d\n", state->paused ? 1 : 0);
    fprintf(
        file,
        "camera %.17g %.17g %.17g\n",
        state->camera.center.x,
        state->camera.center.y,
        state->camera.meters_per_pixel
    );
    fprintf(file, "spawn %d %.17g\n", (int)state->spawn_type, state->spawn_mass);
    fprintf(file, "body_count %d\n", state->simulation.body_count);

    for (int i = 0; i < state->simulation.body_count; i++) {
        const Body *body = &state->simulation.bodies[i];

        fprintf(
            file,
            "body %d %.17g %.17g %.17g %u %u %u %u %.17g %.17g %.17g %.17g\n",
            (int)body->type,
            body->mass,
            body->radius,
            body->spin_angular_momentum,
            (unsigned int)body->color.r,
            (unsigned int)body->color.g,
            (unsigned int)body->color.b,
            (unsigned int)body->color.a,
            body->position.x,
            body->position.y,
            body->velocity.x,
            body->velocity.y
        );
    }

    fclose(file);
    return true;
}

bool load_state_from_file(const char *path, SaveState *state) {
    FILE *file;
    char header[64];
    int scene_value;
    int integrator_value;
    int paused_value;
    int spawn_type_value;
    int body_count;
    bool legacy_v1;

    if (path == NULL || state == NULL) {
        return false;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }

    if (fscanf(file, "%63s", header) != 1) {
        fclose(file);
        return false;
    }

    legacy_v1 = (strcmp(header, "GRAVITYSIM_SAVE_V1") == 0);
    if (!legacy_v1 &&
        strcmp(header, "GRAVITYSIM_SAVE_V2") != 0 &&
        strcmp(header, "APSIS_SAVE_V1") != 0) {
        fclose(file);
        return false;
    }

    if (fscanf(file, " scene %d", &scene_value) != 1 ||
        fscanf(file, " integrator %d", &integrator_value) != 1 ||
        fscanf(file, " simulated_time_seconds %lf", &state->simulated_time_seconds) != 1) {
        fclose(file);
        return false;
    }

    if (legacy_v1) {
        int time_scale_index;

        if (fscanf(file, " time_scale_index %d", &time_scale_index) != 1) {
            fclose(file);
            return false;
        }

        state->time_scale = legacy_time_scale_from_index(time_scale_index);
    } else if (
        fscanf(file, " time_scale %lf", &state->time_scale) != 1 ||
        fscanf(file, " paused %d", &paused_value) != 1 ||
        fscanf(
            file,
            " camera %lf %lf %lf",
            &state->camera.center.x,
            &state->camera.center.y,
            &state->camera.meters_per_pixel
        ) != 3 ||
        fscanf(file, " spawn %d %lf", &spawn_type_value, &state->spawn_mass) != 2 ||
        fscanf(file, " body_count %d", &body_count) != 1) {
        fclose(file);
        return false;
    }

    if (legacy_v1) {
        if (fscanf(file, " paused %d", &paused_value) != 1 ||
            fscanf(
                file,
                " camera %lf %lf %lf",
                &state->camera.center.x,
                &state->camera.center.y,
                &state->camera.meters_per_pixel
            ) != 3 ||
            fscanf(file, " spawn %d %lf", &spawn_type_value, &state->spawn_mass) != 2 ||
            fscanf(file, " body_count %d", &body_count) != 1) {
            fclose(file);
            return false;
        }
    }

    state->scene = (ScenePreset)scene_value;
    state->integrator = (IntegratorMode)integrator_value;
    state->spawn_type = (BodyType)spawn_type_value;
    state->paused = (paused_value != 0);

    if (!valid_scene(state->scene) ||
        !valid_integrator(state->integrator) ||
        !valid_body_type(state->spawn_type) ||
        state->spawn_mass < body_type_min_mass(state->spawn_type) ||
        state->spawn_mass > body_type_max_mass(state->spawn_type) ||
        state->time_scale < TIME_SCALE_MIN ||
        state->time_scale > TIME_SCALE_MAX ||
        state->camera.meters_per_pixel <= 0.0 ||
        body_count < 0 ||
        body_count > MAX_BODIES) {
        fclose(file);
        return false;
    }

    state->simulation.body_count = body_count;

    for (int i = 0; i < body_count; i++) {
        int type_value;
        unsigned int color_r;
        unsigned int color_g;
        unsigned int color_b;
        unsigned int color_a;
        Body *body = &state->simulation.bodies[i];

        if (fscanf(
                file,
                " body %d %lf %lf %lf %u %u %u %u %lf %lf %lf %lf",
                &type_value,
                &body->mass,
                &body->radius,
                &body->spin_angular_momentum,
                &color_r,
                &color_g,
                &color_b,
                &color_a,
                &body->position.x,
                &body->position.y,
                &body->velocity.x,
                &body->velocity.y
            ) != 12) {
            fclose(file);
            return false;
        }

        body->type = (BodyType)type_value;
        if (!valid_body_type(body->type) || body->mass <= 0.0 || body->radius <= 0.0) {
            fclose(file);
            return false;
        }

        body->density = density_from_mass_radius(body->mass, body->radius);
        body->color.r = (Uint8)color_r;
        body->color.g = (Uint8)color_g;
        body->color.b = (Uint8)color_b;
        body->color.a = (Uint8)color_a;

        // Trails are display history, not fundamental simulation state.
        reset_trail(body);
    }

    fclose(file);
    return true;
}
