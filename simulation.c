#include "simulation.h"

#include <math.h>

// The simulation now uses SI units internally:
// meters, kilograms, and seconds.
// Rendering is handled separately so the physics code can stay unit-clean.

const double G = 6.67430e-11;
const double FIXED_DT = 1800.0;
const double MAX_FRAME_TIME = 0.25;
const double SOFTENING = 1.0e7;
const double VIEW_METERS_PER_PIXEL = 5.0e8;
const double SPAWN_SPEED_PER_PIXEL = 300.0;
const double EARTH_MASS = 5.9722e24;
const double EARTH_RADIUS = 6.371e6;
const double EARTH_DENSITY = 5514.0;
const double JUPITER_MASS = 1.89813e27;
const double JUPITER_RADIUS = 6.9911e7;
const double JUPITER_DENSITY = 1326.0;
const double SOLAR_MASS = 1.98847e30;
const double SOLAR_RADIUS = 6.9634e8;
const double AU = 1.495978707e11;

static const double COLLISION_MERGE_ESCAPE_FACTOR = 1.20;
static const double COLLISION_RESTITUTION = 0.30;
static const double COLLISION_DAMAGE_TRANSFER_MAX = 0.18;
static const double COLLISION_SEPARATION_EPSILON = 1.0;

Vec2 vec2(double x, double y) {
    Vec2 value = {x, y};
    return value;
}

Vec2 vec_add(Vec2 a, Vec2 b) {
    return vec2(a.x + b.x, a.y + b.y);
}

Vec2 vec_sub(Vec2 a, Vec2 b) {
    return vec2(a.x - b.x, a.y - b.y);
}

Vec2 vec_scale(Vec2 v, double scale) {
    return vec2(v.x * scale, v.y * scale);
}

static double vec_length_sq(Vec2 v) {
    return (v.x * v.x) + (v.y * v.y);
}

static double vec_length(Vec2 v) {
    return sqrt(vec_length_sq(v));
}

static double vec_dot(Vec2 a, Vec2 b) {
    return (a.x * b.x) + (a.y * b.y);
}

static Vec2 vec_normalize(Vec2 value) {
    double length = vec_length(value);

    if (length <= 0.0) {
        return vec2(1.0, 0.0);
    }

    return vec_scale(value, 1.0 / length);
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

const char *body_type_name(BodyType type) {
    switch (type) {
        case BODY_TYPE_ROCKY:
            return "rocky";

        case BODY_TYPE_GAS_GIANT:
            return "gas giant";

        case BODY_TYPE_STAR:
            return "star";

        default:
            return "unknown";
    }
}

double body_type_density(BodyType type) {
    switch (type) {
        case BODY_TYPE_ROCKY:
            return EARTH_DENSITY;

        case BODY_TYPE_GAS_GIANT:
            return JUPITER_DENSITY;

        case BODY_TYPE_STAR:
            return density_from_mass_radius(SOLAR_MASS, SOLAR_RADIUS);

        default:
            return EARTH_DENSITY;
    }
}

double body_type_default_mass(BodyType type) {
    switch (type) {
        case BODY_TYPE_ROCKY:
            return EARTH_MASS;

        case BODY_TYPE_GAS_GIANT:
            return JUPITER_MASS;

        case BODY_TYPE_STAR:
            return SOLAR_MASS;

        default:
            return EARTH_MASS;
    }
}

double body_type_min_mass(BodyType type) {
    switch (type) {
        case BODY_TYPE_ROCKY:
            return 0.05 * EARTH_MASS;
        case BODY_TYPE_GAS_GIANT:
            return 0.10 * JUPITER_MASS;

        case BODY_TYPE_STAR:
            // Rough lower bound for a hydrogen-burning star.
            return 0.08 * SOLAR_MASS;

        default:
            return 0.05 * EARTH_MASS;
    }
}

double body_type_max_mass(BodyType type) {
    switch (type) {
        case BODY_TYPE_ROCKY:
            return 15.0 * EARTH_MASS;

        case BODY_TYPE_GAS_GIANT:
            return 10.0 * JUPITER_MASS;

        case BODY_TYPE_STAR:
            return 5.0 * SOLAR_MASS;

        default:
            return 15.0 * EARTH_MASS;
    }
}

double body_type_mass_step(BodyType type) {
    switch (type) {
        case BODY_TYPE_ROCKY:
            return 0.25 * EARTH_MASS;
        case BODY_TYPE_GAS_GIANT:
            return 0.10 * JUPITER_MASS;

        case BODY_TYPE_STAR:
            return 0.05 * SOLAR_MASS;

        default:
            return 0.25 * EARTH_MASS;
    }
}

double body_type_mass_display_scale(BodyType type) {
    switch (type) {
        case BODY_TYPE_ROCKY:
            return EARTH_MASS;

        case BODY_TYPE_GAS_GIANT:
            return JUPITER_MASS;

        case BODY_TYPE_STAR:
            return SOLAR_MASS;

        default:
            return EARTH_MASS;
    }
}

const char *body_type_mass_display_unit(BodyType type) {
    switch (type) {
        case BODY_TYPE_ROCKY:
            return "Earth";

        case BODY_TYPE_GAS_GIANT:
            return "Jupiter";

        case BODY_TYPE_STAR:
            return "Solar";

        default:
            return "Earth";
    }
}

BodyType next_body_type(BodyType type) {
    return (BodyType)((type + 1) % BODY_TYPE_COUNT);
}

void set_spawn_body_type(SpawnState *spawn, BodyType type) {
    double min_mass = body_type_min_mass(type);
    double max_mass = body_type_max_mass(type);

    spawn->type = type;

    // If the current mass makes no sense for the new body class,
    // jump to that class's default instead of dragging across scales.
    if (spawn->mass < min_mass || spawn->mass > max_mass) {
        spawn->mass = body_type_default_mass(type);
    } else {
        spawn->mass = clamp_double(spawn->mass, min_mass, max_mass);
    }
}


double radius_from_mass_density(double mass, double density) {
    const double pi = 3.14159265358979323846;

    if (mass <= 0.0 || density <= 0.0) {
        return 0.0;
    }

    // m = rho * (4/3) * pi * r^3, so r = cbrt((3m) / (4pi rho)).
    return cbrt((3.0 * mass) / (4.0 * pi * density));
}

double density_from_mass_radius(double mass, double radius) {
    const double pi = 3.14159265358979323846;
    double volume;

    if (mass <= 0.0 || radius <= 0.0) {
        return 0.0;
    }

    volume = (4.0 / 3.0) * pi * radius * radius * radius;
    return mass / volume;
}

double radius_from_mass_type(double mass, BodyType type) {
    return radius_from_mass_density(mass, body_type_density(type));
}

SDL_Color current_spawn_color(const SpawnState *spawn) {
    switch (spawn->type) {
        case BODY_TYPE_ROCKY:
            return (SDL_Color){120, 200, 255, 255};

        case BODY_TYPE_GAS_GIANT:
            return (SDL_Color){255, 190, 120, 255};

        case BODY_TYPE_STAR:
            return (SDL_Color){255, 214, 140, 255};

        default:
            return (SDL_Color){200, 200, 200, 255};
    }
}

void adjust_spawn_mass(SpawnState *spawn, double step_count) {
    double delta = step_count * body_type_mass_step(spawn->type);

    spawn->mass = clamp_double(
        spawn->mass + delta,
        body_type_min_mass(spawn->type),
        body_type_max_mass(spawn->type)
    );
}

bool add_body(Simulation *sim, Body body) {
    if (sim->body_count >= MAX_BODIES) {
        return false;
    }

    sim->bodies[sim->body_count] = body;
    sim->body_count++;
    return true;
}

void push_trail_point(Body *body) {
    body->trail[body->trail_next] = body->position;
    body->trail_next = (body->trail_next + 1) % TRAIL_LENGTH;

    if (body->trail_count < TRAIL_LENGTH) {
        body->trail_count++;
    }
}

void reset_trail(Body *body) {
    body->trail_next = 0;
    body->trail_count = 0;
    push_trail_point(body);
}

Body make_body(double x, double y, double vx, double vy, BodyType type,
               double mass, double radius, SDL_Color color) {
    Body body = {0};
    body.type = type;
    body.position = vec2(x, y);
    body.velocity = vec2(vx, vy);
    body.mass = mass;
    body.radius = radius;
    body.density = density_from_mass_radius(mass, radius);
    body.spin_angular_momentum = 0.0;
    body.color = color;
    reset_trail(&body);
    return body;
}

static double orbital_angular_momentum_z(const Body *body) {
    return body->mass * ((body->position.x * body->velocity.y) -
                         (body->position.y * body->velocity.x));
}

static double moment_of_inertia(const Body *body) {
    // Approximate each body as a solid sphere for spin bookkeeping.
    return 0.4 * body->mass * body->radius * body->radius;
}

static SDL_Color merge_colors(const Body *a, const Body *b, double merged_mass) {
    SDL_Color color = {0};
    double weight_a;
    double weight_b;

    if (merged_mass <= 0.0) {
        return a->color;
    }

    weight_a = a->mass / merged_mass;
    weight_b = b->mass / merged_mass;

    color.r = (Uint8)lround((a->color.r * weight_a) + (b->color.r * weight_b));
    color.g = (Uint8)lround((a->color.g * weight_a) + (b->color.g * weight_b));
    color.b = (Uint8)lround((a->color.b * weight_a) + (b->color.b * weight_b));
    color.a = 255;
    return color;
}

static BodyType dominant_body_type(const Body *a, const Body *b) {
    if (a->mass >= b->mass) {
        return a->type;
    }

    return b->type;
}

static void merge_body_pair(Simulation *sim, int index_a, int index_b) {
    Body *a = &sim->bodies[index_a];
    Body *b = &sim->bodies[index_b];
    Body merged = {0};
    double merged_mass = a->mass + b->mass;
    Vec2 merged_momentum;
    Vec2 merged_position;
    double volume_a;
    double volume_b;
    double merged_density;
    double total_angular_momentum;
    double merged_orbital_angular_momentum;
    int last_index = sim->body_count - 1;

    if (merged_mass <= 0.0) {
        return;
    }

    merged_momentum = vec_add(
        vec_scale(a->velocity, a->mass),
        vec_scale(b->velocity, b->mass)
    );

    merged_position = vec_scale(
        vec_add(
            vec_scale(a->position, a->mass),
            vec_scale(b->position, b->mass)
        ),
        1.0 / merged_mass
    );

    volume_a = (a->density > 0.0) ? (a->mass / a->density) : 0.0;
    volume_b = (b->density > 0.0) ? (b->mass / b->density) : 0.0;

    if (volume_a + volume_b > 0.0) {
        merged_density = merged_mass / (volume_a + volume_b);
    } else {
        merged_density = body_type_density(dominant_body_type(a, b));
    }

    merged.type = dominant_body_type(a, b);
    merged.mass = merged_mass;
    merged.position = merged_position;
    merged.velocity = vec_scale(merged_momentum, 1.0 / merged_mass);
    merged.density = merged_density;
    merged.radius = radius_from_mass_density(merged.mass, merged.density);
    merged.color = merge_colors(a, b, merged_mass);

    total_angular_momentum =
        orbital_angular_momentum_z(a) + a->spin_angular_momentum +
        orbital_angular_momentum_z(b) + b->spin_angular_momentum;

    merged_orbital_angular_momentum = orbital_angular_momentum_z(&merged);
    merged.spin_angular_momentum = total_angular_momentum - merged_orbital_angular_momentum;

    // Old trails no longer mean anything after a merge, so restart the trail.
    merged.trail_next = 0;
    merged.trail_count = 0;

    sim->bodies[index_a] = merged;

    if (index_b != last_index) {
        sim->bodies[index_b] = sim->bodies[last_index];
    }

    sim->body_count--;
}

static double mutual_escape_speed(const Body *a, const Body *b) {
    double contact_distance = a->radius + b->radius;

    if (contact_distance <= 0.0) {
        return 0.0;
    }

    return sqrt((2.0 * G * (a->mass + b->mass)) / contact_distance);
}

static double collision_damage_fraction(double relative_speed, double escape_speed) {
    double impact_ratio;
    double excess_ratio;

    if (escape_speed <= 0.0) {
        return 0.0;
    }

    impact_ratio = relative_speed / escape_speed;
    excess_ratio = clamp_double(
        (impact_ratio - COLLISION_MERGE_ESCAPE_FACTOR) / (3.0 - COLLISION_MERGE_ESCAPE_FACTOR),
        0.0,
        1.0
    );

    return 0.02 + (COLLISION_DAMAGE_TRANSFER_MAX - 0.02) * excess_ratio;
}

static Vec2 collision_normal(const Body *a, const Body *b) {
    Vec2 delta = vec_sub(b->position, a->position);
    double distance_sq = vec_length_sq(delta);

    if (distance_sq > 0.0) {
        return vec_normalize(delta);
    }

    delta = vec_sub(b->velocity, a->velocity);
    if (vec_length_sq(delta) > 0.0) {
        return vec_normalize(delta);
    }

    return vec2(1.0, 0.0);
}

static void separate_overlapping_bodies(Body *a, Body *b, Vec2 normal) {
    double current_distance = vec_length(vec_sub(b->position, a->position));
    double target_distance = a->radius + b->radius + COLLISION_SEPARATION_EPSILON;
    double overlap = target_distance - current_distance;
    double total_mass = a->mass + b->mass;
    double move_a;
    double move_b;

    if (overlap <= 0.0 || total_mass <= 0.0) {
        return;
    }

    move_a = overlap * (b->mass / total_mass);
    move_b = overlap * (a->mass / total_mass);

    a->position = vec_sub(a->position, vec_scale(normal, move_a));
    b->position = vec_add(b->position, vec_scale(normal, move_b));
}

static void resolve_high_speed_collision(Body *a, Body *b) {
    Vec2 normal = collision_normal(a, b);
    Vec2 tangent = vec2(-normal.y, normal.x);
    Vec2 relative_velocity = vec_sub(b->velocity, a->velocity);
    double relative_speed = vec_length(relative_velocity);
    double escape_speed = mutual_escape_speed(a, b);
    double total_angular_momentum_before =
        orbital_angular_momentum_z(a) + a->spin_angular_momentum +
        orbital_angular_momentum_z(b) + b->spin_angular_momentum;
    double mass_a = a->mass;
    double mass_b = b->mass;
    double velocity_a_normal = vec_dot(a->velocity, normal);
    double velocity_b_normal = vec_dot(b->velocity, normal);
    double velocity_a_tangent = vec_dot(a->velocity, tangent);
    double velocity_b_tangent = vec_dot(b->velocity, tangent);
    double velocity_a_normal_after;
    double velocity_b_normal_after;
    Body *receiver;
    Body *donor;
    double transfer_mass;
    double orbital_angular_momentum_after;
    double combined_inertia;
    double target_spin_total;

    velocity_a_normal_after =
        ((mass_a - (COLLISION_RESTITUTION * mass_b)) * velocity_a_normal +
         ((1.0 + COLLISION_RESTITUTION) * mass_b * velocity_b_normal)) /
        (mass_a + mass_b);
    velocity_b_normal_after =
        ((mass_b - (COLLISION_RESTITUTION * mass_a)) * velocity_b_normal +
         ((1.0 + COLLISION_RESTITUTION) * mass_a * velocity_a_normal)) /
        (mass_a + mass_b);

    a->velocity = vec_add(vec_scale(normal, velocity_a_normal_after),
                          vec_scale(tangent, velocity_a_tangent));
    b->velocity = vec_add(vec_scale(normal, velocity_b_normal_after),
                          vec_scale(tangent, velocity_b_tangent));

    // First pass damage model: the heavier body captures a small fraction
    // of the lighter body when the impact is too energetic to merge cleanly.
    if (a->mass >= b->mass) {
        receiver = a;
        donor = b;
    } else {
        receiver = b;
        donor = a;
    }

    transfer_mass = collision_damage_fraction(relative_speed, escape_speed) * donor->mass;
    if (transfer_mass > 0.0 && transfer_mass < donor->mass) {
        receiver->velocity = vec_scale(
            vec_add(
                vec_scale(receiver->velocity, receiver->mass),
                vec_scale(donor->velocity, transfer_mass)
            ),
            1.0 / (receiver->mass + transfer_mass)
        );

        donor->mass -= transfer_mass;
        receiver->mass += transfer_mass;
        donor->radius = radius_from_mass_density(donor->mass, donor->density);
        receiver->radius = radius_from_mass_density(receiver->mass, receiver->density);
    }

    separate_overlapping_bodies(a, b, normal);

    orbital_angular_momentum_after = orbital_angular_momentum_z(a) + orbital_angular_momentum_z(b);
    target_spin_total = total_angular_momentum_before - orbital_angular_momentum_after;
    combined_inertia = moment_of_inertia(a) + moment_of_inertia(b);

    if (combined_inertia > 0.0) {
        a->spin_angular_momentum = target_spin_total * (moment_of_inertia(a) / combined_inertia);
        b->spin_angular_momentum = target_spin_total - a->spin_angular_momentum;
    } else {
        a->spin_angular_momentum = 0.0;
        b->spin_angular_momentum = 0.0;
    }
}

static void resolve_collisions(Simulation *sim) {
    bool resolved_any = true;

    while (resolved_any) {
        resolved_any = false;

        for (int i = 0; i < sim->body_count; i++) {
            for (int j = i + 1; j < sim->body_count; j++) {
                Body *a = &sim->bodies[i];
                Body *b = &sim->bodies[j];
                Vec2 delta = vec_sub(sim->bodies[j].position, sim->bodies[i].position);
                double collision_distance = sim->bodies[i].radius + sim->bodies[j].radius;
                double relative_speed;
                double escape_speed;

                if (vec_length_sq(delta) <= collision_distance * collision_distance) {
                    relative_speed = vec_length(vec_sub(b->velocity, a->velocity));
                    escape_speed = mutual_escape_speed(a, b);

                    if (relative_speed <= COLLISION_MERGE_ESCAPE_FACTOR * escape_speed) {
                        merge_body_pair(sim, i, j);
                    } else {
                        resolve_high_speed_collision(a, b);
                    }

                    resolved_any = true;
                    goto collision_pass_complete;
                }
            }
        }

collision_pass_complete:
        ;
    }
}

SimulationDiagnostics compute_diagnostics(const Simulation *sim) {
    SimulationDiagnostics diagnostics = {0};

    for (int i = 0; i < sim->body_count; i++) {
        const Body *body = &sim->bodies[i];
        double spin_inertia = moment_of_inertia(body);

        diagnostics.kinetic_energy += 0.5 * body->mass * vec_length_sq(body->velocity);

        if (spin_inertia > 0.0) {
            diagnostics.kinetic_energy +=
                (body->spin_angular_momentum * body->spin_angular_momentum) / (2.0 * spin_inertia);
        }

        diagnostics.total_momentum = vec_add(
            diagnostics.total_momentum,
            vec_scale(body->velocity, body->mass)
        );

        diagnostics.angular_momentum_z +=
            orbital_angular_momentum_z(body) + body->spin_angular_momentum;
    }

    for (int i = 0; i < sim->body_count; i++) {
        for (int j = i + 1; j < sim->body_count; j++) {
            Vec2 delta = vec_sub(sim->bodies[j].position, sim->bodies[i].position);
            double softened_distance = sqrt(vec_length_sq(delta) + (SOFTENING * SOFTENING));

            // Use the softened potential that matches the softened force law.
            diagnostics.potential_energy +=
                (-G * sim->bodies[i].mass * sim->bodies[j].mass) / softened_distance;
        }
    }

    diagnostics.total_energy = diagnostics.kinetic_energy + diagnostics.potential_energy;
    return diagnostics;
}

DiagnosticsBaseline make_diagnostics_baseline(const Simulation *sim) {
    DiagnosticsBaseline baseline = {0};
    double momentum_scale = 0.0;
    double angular_scale = 0.0;

    baseline.diagnostics = compute_diagnostics(sim);
    baseline.energy_scale = fmax(fabs(baseline.diagnostics.total_energy), fabs(baseline.diagnostics.kinetic_energy) + fabs(baseline.diagnostics.potential_energy));

    for (int i = 0; i < sim->body_count; i++) {
        const Body *body = &sim->bodies[i];

        momentum_scale += vec_length(vec_scale(body->velocity, body->mass));
        angular_scale += fabs(orbital_angular_momentum_z(body)) + fabs(body->spin_angular_momentum);
    }

    baseline.energy_scale = fmax(baseline.energy_scale, 1.0);
    baseline.momentum_scale = fmax(momentum_scale, 1.0);
    baseline.angular_momentum_scale = fmax(fabs(baseline.diagnostics.angular_momentum_z), angular_scale);
    baseline.angular_momentum_scale = fmax(baseline.angular_momentum_scale, 1.0);
    baseline.valid = true;
    return baseline;
}

SimulationDrift compute_diagnostics_drift(const SimulationDiagnostics *current, const DiagnosticsBaseline *baseline) {
    SimulationDrift drift = {0};
    Vec2 momentum_delta;

    if (current == NULL || baseline == NULL || !baseline->valid) {
        return drift;
    }

    drift.energy_relative = (current->total_energy - baseline->diagnostics.total_energy) / baseline->energy_scale;

    momentum_delta = vec_sub(current->total_momentum, baseline->diagnostics.total_momentum);
    drift.momentum_relative = vec_length(momentum_delta) / baseline->momentum_scale;

    drift.angular_momentum_relative = (current->angular_momentum_z - baseline->diagnostics.angular_momentum_z) / baseline->angular_momentum_scale;

    return drift;
}


static void compute_accelerations_for_positions(const Simulation *sim,
                                                const Vec2 positions[MAX_BODIES],
                                                Vec2 accelerations[MAX_BODIES]) {
    for (int i = 0; i < MAX_BODIES; i++) {
        accelerations[i] = vec2(0.0, 0.0);
    }

    // Compute each pair once and apply equal-and-opposite accelerations.
    for (int i = 0; i < sim->body_count; i++) {
        for (int j = i + 1; j < sim->body_count; j++) {
            Vec2 delta = vec_sub(positions[j], positions[i]);

            // Softening keeps the force from going really high at tiny distances.
            double distance_sq = vec_length_sq(delta) + (SOFTENING * SOFTENING);
            double distance = sqrt(distance_sq);
            double inv_distance_cubed = 1.0 / (distance_sq * distance);

            // a = G * m_other * delta / |delta|^3
            Vec2 direction_term = vec_scale(delta, inv_distance_cubed);

            accelerations[i] = vec_add(
                accelerations[i],
                vec_scale(direction_term, G * sim->bodies[j].mass)
            );

            accelerations[j] = vec_sub(
                accelerations[j],
                vec_scale(direction_term, G * sim->bodies[i].mass)
            );
        }
    }
}

static void compute_accelerations(const Simulation *sim, Vec2 accelerations[MAX_BODIES]) {
    Vec2 positions[MAX_BODIES];

    for (int i = 0; i < sim->body_count; i++) {
        positions[i] = sim->bodies[i].position;
    }

    compute_accelerations_for_positions(sim, positions, accelerations);
}

const char *integrator_name(IntegratorMode integrator) {
    switch (integrator) {
        case INTEGRATOR_SEMI_IMPLICIT_EULER:
            return "semi-implicit euler";

        case INTEGRATOR_VELOCITY_VERLET:
            return "velocity verlet";

        case INTEGRATOR_RK4:
            return "rk4";

        default:
            return "unknown";
    }
}

static void finish_simulation_step(Simulation *sim) {
    // Merge overlapping bodies after the step. This models a perfectly
    // inelastic collision: mass and linear momentum are conserved, while
    // mechanical energy can be lost.
    resolve_collisions(sim);

    for (int i = 0; i < sim->body_count; i++) {
        push_trail_point(&sim->bodies[i]);
    }
}

static void step_semi_implicit_euler(Simulation *sim, double dt) {
    Vec2 accelerations[MAX_BODIES];

    compute_accelerations(sim, accelerations);

    for (int i = 0; i < sim->body_count; i++) {
        Body *body = &sim->bodies[i];

        // Semi-implicit Euler:
        // first update velocity from the current acceleration,
        // then move using that new velocity.
        body->velocity = vec_add(body->velocity, vec_scale(accelerations[i], dt));
        body->position = vec_add(body->position, vec_scale(body->velocity, dt));
    }

    finish_simulation_step(sim);
}

static void step_velocity_verlet(Simulation *sim, double dt) {
    Vec2 accelerations_before[MAX_BODIES];
    Vec2 accelerations_after[MAX_BODIES];

    compute_accelerations(sim, accelerations_before);

    for (int i = 0; i < sim->body_count; i++) {
        Body *body = &sim->bodies[i];

        // Velocity Verlet position step:
        // x(t + dt) = x(t) + v(t)dt + 1/2 a(t)dt^2
        Vec2 position_step = vec_add(
            vec_scale(body->velocity, dt),
            vec_scale(accelerations_before[i], 0.5 * dt * dt)
        );

        body->position = vec_add(body->position, position_step);
    }

    compute_accelerations(sim, accelerations_after);

    for (int i = 0; i < sim->body_count; i++) {
        Body *body = &sim->bodies[i];

        // Velocity Verlet velocity step:
        // v(t + dt) = v(t) + 1/2 (a_old + a_new) dt
        Vec2 average_acceleration = vec_scale(
            vec_add(accelerations_before[i], accelerations_after[i]),
            0.5
        );

        body->velocity = vec_add(body->velocity, vec_scale(average_acceleration, dt));
    }

    finish_simulation_step(sim);
}

static void step_rk4(Simulation *sim, double dt) {
    Vec2 positions_0[MAX_BODIES];
    Vec2 velocities_0[MAX_BODIES];
    Vec2 stage_positions[MAX_BODIES];
    Vec2 stage_velocities[MAX_BODIES];
    Vec2 k1_x[MAX_BODIES];
    Vec2 k1_v[MAX_BODIES];
    Vec2 k2_x[MAX_BODIES];
    Vec2 k2_v[MAX_BODIES];
    Vec2 k3_x[MAX_BODIES];
    Vec2 k3_v[MAX_BODIES];
    Vec2 k4_x[MAX_BODIES];
    Vec2 k4_v[MAX_BODIES];

    for (int i = 0; i < sim->body_count; i++) {
        positions_0[i] = sim->bodies[i].position;
        velocities_0[i] = sim->bodies[i].velocity;
    }

    compute_accelerations_for_positions(sim, positions_0, k1_v);
    for (int i = 0; i < sim->body_count; i++) {
        k1_x[i] = velocities_0[i];
        stage_positions[i] = vec_add(positions_0[i], vec_scale(k1_x[i], 0.5 * dt));
        stage_velocities[i] = vec_add(velocities_0[i], vec_scale(k1_v[i], 0.5 * dt));
    }

    compute_accelerations_for_positions(sim, stage_positions, k2_v);
    for (int i = 0; i < sim->body_count; i++) {
        k2_x[i] = stage_velocities[i];
        stage_positions[i] = vec_add(positions_0[i], vec_scale(k2_x[i], 0.5 * dt));
        stage_velocities[i] = vec_add(velocities_0[i], vec_scale(k2_v[i], 0.5 * dt));
    }

    compute_accelerations_for_positions(sim, stage_positions, k3_v);
    for (int i = 0; i < sim->body_count; i++) {
        k3_x[i] = stage_velocities[i];
        stage_positions[i] = vec_add(positions_0[i], vec_scale(k3_x[i], dt));
        stage_velocities[i] = vec_add(velocities_0[i], vec_scale(k3_v[i], dt));
    }

    compute_accelerations_for_positions(sim, stage_positions, k4_v);
    for (int i = 0; i < sim->body_count; i++) {
        Body *body = &sim->bodies[i];
        Vec2 position_sum;
        Vec2 velocity_sum;

        k4_x[i] = stage_velocities[i];

        // RK4 combines four slope estimates:
        // y_next = y + dt / 6 * (k1 + 2k2 + 2k3 + k4)
        position_sum = vec_add(
            vec_add(k1_x[i], vec_scale(k2_x[i], 2.0)),
            vec_add(vec_scale(k3_x[i], 2.0), k4_x[i])
        );
        velocity_sum = vec_add(
            vec_add(k1_v[i], vec_scale(k2_v[i], 2.0)),
            vec_add(vec_scale(k3_v[i], 2.0), k4_v[i])
        );

        body->position = vec_add(positions_0[i], vec_scale(position_sum, dt / 6.0));
        body->velocity = vec_add(velocities_0[i], vec_scale(velocity_sum, dt / 6.0));
    }

    finish_simulation_step(sim);
}

void step_simulation(Simulation *sim, double dt, IntegratorMode integrator) {
    switch (integrator) {
        case INTEGRATOR_SEMI_IMPLICIT_EULER:
            step_semi_implicit_euler(sim, dt);
            break;

        case INTEGRATOR_VELOCITY_VERLET:
            step_velocity_verlet(sim, dt);
            break;

        case INTEGRATOR_RK4:
            step_rk4(sim, dt);
            break;

        default:
            step_velocity_verlet(sim, dt);
            break;
    }
}
