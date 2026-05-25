#include "../simulation.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static double vec_length_sq_public(Vec2 value) {
    return (value.x * value.x) + (value.y * value.y);
}

static double total_mass(const Simulation *sim) {
    double mass = 0.0;

    for (int i = 0; i < sim->body_count; i++) {
        mass += sim->bodies[i].mass;
    }

    return mass;
}

static Vec2 total_momentum(const Simulation *sim) {
    Vec2 momentum = vec2(0.0, 0.0);

    for (int i = 0; i < sim->body_count; i++) {
        momentum = vec_add(momentum, vec_scale(sim->bodies[i].velocity, sim->bodies[i].mass));
    }

    return momentum;
}

static void expect_true(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "test failure: %s\n", message);
        exit(1);
    }
}

static void expect_near(double actual, double expected, double relative_tolerance,
                        const char *message) {
    double scale = fmax(1.0, fabs(expected));
    double error = fabs(actual - expected);

    if (error > (relative_tolerance * scale)) {
        fprintf(stderr, "test failure: %s (actual=%.17g expected=%.17g)\n",
                message, actual, expected);
        exit(1);
    }
}

static void test_low_speed_collision_merges(void) {
    Simulation sim = {0};
    double radius = radius_from_mass_type(EARTH_MASS, BODY_TYPE_ROCKY);
    double total_mass_before;

    sim.body_count = 2;
    sim.bodies[0] = make_body(
        0.0, 0.0, 0.0, 0.0,
        BODY_TYPE_ROCKY, EARTH_MASS, radius,
        current_spawn_color(&(SpawnState){.type = BODY_TYPE_ROCKY})
    );
    sim.bodies[1] = make_body(
        1.5 * radius, 0.0, 0.0, 0.0,
        BODY_TYPE_ROCKY, EARTH_MASS, radius,
        current_spawn_color(&(SpawnState){.type = BODY_TYPE_ROCKY})
    );

    total_mass_before = total_mass(&sim);
    step_simulation(&sim, 0.0, INTEGRATOR_VELOCITY_VERLET);

    expect_true(sim.body_count == 1, "low-speed overlap should merge into one body");
    expect_near(sim.bodies[0].mass, total_mass_before, 1.0e-12,
                "merged body should conserve total mass");
}

static void test_high_speed_collision_bounces_and_preserves_mass_and_momentum(void) {
    Simulation sim = {0};
    double radius = radius_from_mass_type(EARTH_MASS, BODY_TYPE_ROCKY);
    double total_mass_before;
    Vec2 total_momentum_before;
    double initial_mass_a = EARTH_MASS;
    double initial_mass_b = EARTH_MASS;
    double distance_after;
    double target_distance_after;

    sim.body_count = 2;
    sim.bodies[0] = make_body(
        0.0, 0.0, 20000.0, 0.0,
        BODY_TYPE_ROCKY, initial_mass_a, radius,
        current_spawn_color(&(SpawnState){.type = BODY_TYPE_ROCKY})
    );
    sim.bodies[1] = make_body(
        1.5 * radius, 0.0, -20000.0, 0.0,
        BODY_TYPE_ROCKY, initial_mass_b, radius,
        current_spawn_color(&(SpawnState){.type = BODY_TYPE_ROCKY})
    );

    total_mass_before = total_mass(&sim);
    total_momentum_before = total_momentum(&sim);
    step_simulation(&sim, 0.0, INTEGRATOR_VELOCITY_VERLET);

    expect_true(sim.body_count == 2, "high-speed collision should not merge");
    expect_near(total_mass(&sim), total_mass_before, 1.0e-12,
                "bounce path should conserve total mass");
    expect_near(total_momentum(&sim).x, total_momentum_before.x, 1.0e-12,
                "bounce path should conserve x momentum");
    expect_near(total_momentum(&sim).y, total_momentum_before.y, 1.0e-12,
                "bounce path should conserve y momentum");
    expect_true(fabs(sim.bodies[0].mass - initial_mass_a) > 1.0,
                "bounce path should transfer some mass into damage/accretion");
    expect_true(fabs(sim.bodies[1].mass - initial_mass_b) > 1.0,
                "bounce path should strip some mass from the loser");

    distance_after = sqrt(vec_length_sq_public(vec_sub(sim.bodies[1].position, sim.bodies[0].position)));
    target_distance_after = sim.bodies[0].radius + sim.bodies[1].radius;
    expect_true(distance_after >= target_distance_after,
                "bounce path should separate bodies after impact");
}

int main(void) {
    test_low_speed_collision_merges();
    test_high_speed_collision_bounces_and_preserves_mass_and_momentum();
    printf("collision tests passed\n");
    return 0;
}
