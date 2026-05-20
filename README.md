# Apsis

<p align="center">
  <img alt="Language C" src="https://img.shields.io/badge/Language-C-00599C?style=for-the-badge&logo=c&logoColor=white" />
  <img alt="Graphics SDL2" src="https://img.shields.io/badge/Graphics-SDL2-2ea44f?style=for-the-badge" />
  <img alt="Text SDL2_ttf" src="https://img.shields.io/badge/Text-SDL2__ttf-8a2be2?style=for-the-badge" />
</p>

<p align="center">
  <img alt="Status active development" src="https://img.shields.io/badge/Status-Active%20Development-1f6feb?style=for-the-badge" />
  <img alt="Roadmap progress" src="https://img.shields.io/badge/Roadmap-22%2F26%20complete-2ea44f?style=for-the-badge" height="28" />
</p>

Apsis is a real time 2D gravity simulation written in C with SDL2. It started as a fun curiosity project, but the direction changed pretty quickly. The goal now is to keep the interaction side enjoyable while pushing the simulation itself toward more defensible physics and better numerical behaviour.

The current build already supports physically meaningful units, multiple integrators, preset scenes, interactive spawning, collision merging, save/load state handling, CSV benchmark export, camera controls, and live diagnostics for energy, momentum, angular momentum, and drift. It is still actively being built out, but the foundation is in place and the roadmap is clear.

## Preview

### Default View

<p align="center">
  <img src="assets/media/gravitysim-starter.png" alt="Apsis default starter scene with the HUD visible" width="92%" />
</p>

### Scene Clips

<table align="center">
  <tr>
    <th width="33%">Starter Scene</th>
    <th width="33%">Chaotic Three-Body</th>
    <th width="33%">Binary Stars</th>
  </tr>
  <tr>
    <td align="center"><img src="assets/media/starter-orbit.gif" alt="Starter scene GIF" width="100%" /></td>
    <td align="center"><img src="assets/media/chaotic-three-body.gif" alt="Chaotic three-body scene GIF" width="100%" /></td>
    <td align="center"><img src="assets/media/binary-stars.gif" alt="Binary stars scene GIF" width="100%" /></td>
  </tr>
</table>

## Current Feature Set

- Real time N body gravity simulation in C
- SDL2 rendering with trails, HUD, and camera controls
- Physically meaningful simulation units
- Runtime integrator comparison with Euler, velocity Verlet, and RK4
- Interactive body spawning with drag based launch velocity
- Preset scenes for empty space, a solar baseline, three body motion, binary stars, and a gas giant + moon system
- Perfectly inelastic collision merging with mass, momentum, and density aware radius updates
- Save and load support for reproducible simulation states
- CSV benchmark export for integrator, energy, momentum, angular momentum, and drift tracking
- Diagnostics for total energy, total momentum, angular momentum, and relative drift from a resettable baseline

## Core Physics Concepts

Apsis uses Newtonian gravity. Each body feels the sum of gravitational acceleration from every other body:

$$
\mathbf{a}_i = \sum_{j \ne i} G\,m_j \frac{\mathbf{r}_j - \mathbf{r}_i}{\lVert \mathbf{r}_j - \mathbf{r}_i \rVert^3}
$$

The simulation currently supports semi implicit Euler, velocity Verlet, and RK4. Velocity Verlet is still the default because it is a much better fit for orbital systems than a simple Euler step, while still staying light enough for real time use.

Collisions are handled as perfectly inelastic merges. Mass and linear momentum are conserved, merged radius is recomputed from mass and density, and off-centre impacts keep angular momentum bookkeeping through a stored spin term.

There is also a drift system in the HUD. Implemented during debugging but kept it since I felt if I was asking myself: “does this look right?”, other people might too. It measures how far the current state has numerically moved away from a chosen baseline, which makes it easier to judge integrator quality and general simulation health.

More informal write-ups live in [Theory notes/Notes.md](Theory%20notes/Notes.md).

## Build

### Dependencies

- C compiler with C11 support
- `SDL2`
- `SDL2_ttf`
- `make`
- `pkg-config`

### Build And Run

```bash
make
make run
```

### Arch Linux

```bash
sudo pacman -S sdl2 sdl2_ttf pkgconf
make
make run
```

My main development environment is Arch Linux, but the code is intended to stay portable anywhere SDL2 and SDL2_ttf are available.

## Controls

- `LMB drag`: spawn a body and set its initial velocity
- `RMB`: cancel spawn
- `Shift + mouse wheel` or `[ ]`: change spawn mass
- `Mouse wheel` or `Q / E`: zoom
- `Middle mouse drag` or `W A S D` / arrow keys: pan camera
- `C`: reset camera
- `- / =`: slow down or speed up simulated time
- `T`: reset time scale
- `I`: cycle the active integrator
- `F5 / F9`: save or load the current state
- `F6`: start or stop benchmark recording to CSV
- `0 / 1 / 2 / 3 / 4`: switch scenes
- `R`: reset the current scene
- `B`: reset the diagnostics baseline
- `Space`: pause
- `H`: hide the HUD
- `Esc`: quit

## Roadmap

The badge above is generated automatically from this checklist by a GitHub Action.

### Completed

- [x] SDL2 window, renderer, and timestep-based simulation loop
- [x] Multiple gravitating bodies with mass, position, and velocity
- [x] Trail rendering
- [x] Pause and reset controls
- [x] Interactive body spawning
- [x] Preset scenes
- [x] Codebase split into modules
- [x] Move from sandbox-style units to physically meaningful units
- [x] Velocity Verlet integration
- [x] Integrator comparison mode
- [x] Time scaling controls
- [x] In-window HUD
- [x] Camera zoom and pan
- [x] Diagnostics for energy, momentum, and angular momentum
- [x] Drift tracking from a resettable baseline
- [x] Collision detection with perfectly inelastic merging
- [x] Spin bookkeeping during off-centre merges
- [x] SDL_ttf HUD cleanup
- [x] Better density and body-type models
- [x] More physically defined preset scenes
- [x] Save and load simulation states
- [x] Data export and benchmarking tools

### Planned

- [ ] Barnes-Hut approximation for larger body counts
- [ ] Collision model extensions beyond simple merging
- [ ] Black hole / extreme gravity experiment mode
- [ ] General polish for public releases
