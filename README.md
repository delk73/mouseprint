# Mouseprint

Mouseprint is a local pointing-device observatory for Omarchy/Hyprland. The
current implementation includes native libinput evidence capture with separate
Hyprland compositor context, local SQLite persistence, and read-only Qt Quick
inspection through Slices 6 and 7A-7C.

## Shipped foundation: Slices 1-7

The collector creates a non-exclusive libinput udev context on `seat0` and
reports pointer-capable device lifecycle events, motion, buttons, and scroll
events, and persists those raw records to SQLite. The collector also samples
Hyprland context in a separate thread through its Unix IPC sockets. It does not
modify pointer configuration or grab devices.

Motion fields are deliberately labeled by domain:

- `dx_unaccelerated` / `dy_unaccelerated`: device domain, in libinput raw device coordinates.
- `dx_accelerated_collector` / `dy_accelerated_collector`: accelerated values from Mouseprint's independent libinput context.
- Hyprland compositor-space positions remain separate context evidence in `pointer_context`.

The `devices` table is an identity table. Device lifecycle timing is stored in
the run-scoped `raw_input_events` table as `DEVICE_ADDED` and `DEVICE_REMOVED`
rows.

Compositor samples are stored in `pointer_context`; links to eligible raw
motion, button, and scroll rows are stored in `input_context_matches`. Raw
input rows remain unchanged and immutable. Context matching uses the nearest
sample within an initial 25,000 microsecond tolerance, recording signed
`context_delta_us` and `absolute_delta_us`. Failed context samples are retained;
eligible input with no qualifying valid sample in a failed sampling window is
classified as `unmatched_context_error`, distinct from a healthy sparse-sample
miss.

Hyprland cursor position/displacement is the authoritative compositor-domain
observation. Collector-accelerated values are not authoritative screen-space
motion and may differ from Hyprland's own libinput acceleration and
configuration.

After correlation is finalized for a completed run, the collector derives
movement episodes into `movement_episodes` and
`movement_episode_members`. Episodes are independent per run and device, use a
100,000 microsecond idle gap, and retain raw event and correlation provenance.
Device metrics use only unaccelerated libinput deltas. Compositor geometry uses
valid matched context samples, collapses consecutive duplicate context IDs,
and uses context sample timestamps for compositor velocity. Missing or invalid
context never produces interpolated cursor positions. Derived data may be
absent when a collector run ends abnormally and remains recomputable from the
evidence tables.

Each motion member also has a row in `movement_episode_trajectory_points`.
Device coordinates are cumulative unaccelerated libinput units relative to the
episode origin; they are not physical centimeters. Zero-valued motion remains
as a trajectory point, and unavailable unaccelerated values are represented as
NULL rather than replaced with accelerated values. Compositor coordinates are
Hyprland cursor coordinates from valid matched context samples. Missing
compositor observations are not interpolated, and repeated context IDs do not
create artificial movement. Monitor-normalized coordinates are not produced;
monitor geometry is not currently retained as evidence and is a follow-up
design finding.

## Slices 6 and 7

The read-only inspector uses the existing `QueryRepository` with
`SQLITE_OPEN_READONLY` and `PRAGMA query_only=ON`. It shows completed
collector-run-backed sessions, movement episode metrics, separate
device/compositor trajectories, trajectory provenance, correlation statuses,
and explicit unavailable values or trajectory gaps. It does not modify the
collector database or derive metrics in QML.

Slice 7 uses a query-time session model: one completed collector run is one
Mouseprint session. Only completed runs are sessions; there is no persisted
session table, cross-run grouping, or inferred session boundary. The inspector
supports recent-session selection and switching, selected-session summaries,
per-device summaries, exact correlation and metric-status counts, and correct
handling of zero-input, raw-only, and zero-episode sessions. Session changes
clear stale episode, trajectory, and provenance state.

Session-level descriptive aggregates include compositor-space path distance over
available episode values, compositor path available/unavailable counts, and the
device directional reversal total with its available/unavailable counts.
Unavailable values remain unavailable and legitimate zero values remain zero.
There is no cross-device device-space path total. Compositor path totals exclude
unavailable episodes and do not imply uninterrupted cursor travel.

Build it with:

```sh
make -C ui
```

Launch it with an explicit database:

```sh
./build/mouseprint-inspector --database PATH
```

The inspector reads completed runs read-only, initially selecting the most recent
completed session. Cross-run or manual session grouping, normalized spatial
products, click/scroll semantics, collector-transform aggregates, configurable
segmentation, and pause/clear controls remain deferred.

## Build

Requirements are the installed `libinput`, `libudev`, SQLite, and a C++17
compiler:

```sh
make -C collector
```

The binary is `build/mouseprint-collector`. By default, data is stored at
`$XDG_STATE_HOME/mouseprint/mouseprint.sqlite3`, or
`$HOME/.local/state/mouseprint/mouseprint.sqlite3` when `XDG_STATE_HOME` is not
set. Use `--database PATH` to select another location.

## Run

The user running the collector needs read access to `/dev/input/event*`,
normally provided by the `input` group. Root is not required.

```sh
./build/mouseprint-collector
```

Move the mouse, click, and scroll to see human-readable evidence lines and
verify the `collector_runs`, `devices`, `raw_input_events`, `pointer_context`,
`input_context_matches`, `movement_episodes`, `movement_episode_members`, and
`movement_episode_trajectory_points` tables. Stop with `Ctrl-C` or `SIGTERM`. The
collector reports cursor request latency, context sample success rate, and
queue drops on shutdown. Database or Hyprland context failures do not affect
input observation, and the collector does not interfere with normal pointer
operation.
