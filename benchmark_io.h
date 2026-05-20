#ifndef BENCHMARK_IO_H
#define BENCHMARK_IO_H

#include "gravity.h"

#include <stdio.h>

typedef struct {
    bool active;
    FILE *file;
} BenchmarkRecorder;

bool benchmark_recorder_start(BenchmarkRecorder *recorder, const char *path,
                              ScenePreset scene, IntegratorMode integrator,
                              double fixed_dt, const DiagnosticsBaseline *baseline);
void benchmark_recorder_stop(BenchmarkRecorder *recorder);
bool benchmark_recorder_write_row(BenchmarkRecorder *recorder, double simulated_time_seconds,
                                  ScenePreset scene, IntegratorMode integrator,
                                  const Simulation *sim,
                                  const SimulationDiagnostics *diagnostics,
                                  const SimulationDrift *drift);

#endif
