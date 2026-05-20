#include "benchmark_io.h"

#include "scenes.h"
#include "simulation.h"

#include <math.h>

static double momentum_magnitude(Vec2 momentum) {
    return sqrt((momentum.x * momentum.x) + (momentum.y * momentum.y));
}

bool benchmark_recorder_start(BenchmarkRecorder *recorder, const char *path,
                              ScenePreset scene, IntegratorMode integrator,
                              double fixed_dt, const DiagnosticsBaseline *baseline) {
    FILE *file;
    double baseline_momentum = 0.0;

    if (recorder == NULL || path == NULL) {
        return false;
    }

    benchmark_recorder_stop(recorder);

    file = fopen(path, "w");
    if (file == NULL) {
        return false;
    }

    if (baseline != NULL && baseline->valid) {
        baseline_momentum = momentum_magnitude(baseline->diagnostics.total_momentum);
    }

    fprintf(file, "# apsis_benchmark_v1\n");
    fprintf(file, "# start_scene,%s\n", scene_name(scene));
    fprintf(file, "# start_integrator,%s\n", integrator_name(integrator));
    fprintf(file, "# fixed_dt_seconds,%.17g\n", fixed_dt);

    if (baseline != NULL && baseline->valid) {
        fprintf(file, "# baseline_total_energy_j,%.17g\n", baseline->diagnostics.total_energy);
        fprintf(file, "# baseline_momentum_magnitude_kg_m_s,%.17g\n", baseline_momentum);
        fprintf(file, "# baseline_angular_momentum_z_kg_m2_s,%.17g\n",
                baseline->diagnostics.angular_momentum_z);
    }

    fprintf(
        file,
        "time_seconds,sim_days,scene,integrator,body_count,kinetic_energy_j,potential_energy_j,total_energy_j,"
        "momentum_magnitude_kg_m_s,angular_momentum_z_kg_m2_s,drift_dE,drift_dP,drift_dL\n"
    );

    fflush(file);

    recorder->file = file;
    recorder->active = true;
    return true;
}

void benchmark_recorder_stop(BenchmarkRecorder *recorder) {
    if (recorder == NULL) {
        return;
    }

    if (recorder->file != NULL) {
        fclose(recorder->file);
        recorder->file = NULL;
    }

    recorder->active = false;
}

bool benchmark_recorder_write_row(BenchmarkRecorder *recorder, double simulated_time_seconds,
                                  ScenePreset scene, IntegratorMode integrator,
                                  const Simulation *sim,
                                  const SimulationDiagnostics *diagnostics,
                                  const SimulationDrift *drift) {
    double simulated_days;
    double momentum;

    if (recorder == NULL || !recorder->active || recorder->file == NULL ||
        sim == NULL || diagnostics == NULL || drift == NULL) {
        return false;
    }

    simulated_days = simulated_time_seconds / 86400.0;
    momentum = momentum_magnitude(diagnostics->total_momentum);

    fprintf(
        recorder->file,
        "%.17g,%.17g,%s,%s,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\n",
        simulated_time_seconds,
        simulated_days,
        scene_name(scene),
        integrator_name(integrator),
        sim->body_count,
        diagnostics->kinetic_energy,
        diagnostics->potential_energy,
        diagnostics->total_energy,
        momentum,
        diagnostics->angular_momentum_z,
        drift->energy_relative,
        drift->momentum_relative,
        drift->angular_momentum_relative
    );

    return fflush(recorder->file) == 0;
}
