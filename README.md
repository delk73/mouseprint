# Mouseprint

Mouseprint is a local pointing-device observatory for Omarchy/Hyprland. The
current implementation is Slice 3: native libinput evidence capture with
separate Hyprland compositor context and local SQLite persistence.

## Slice 3

The collector creates a non-exclusive libinput udev context on `seat0` and
reports pointer-capable device lifecycle events, motion, buttons, and scroll
events, and persists those raw records to SQLite. The collector also samples
Hyprland context in a separate thread through its Unix IPC sockets. It does not
modify pointer configuration or grab devices.

Motion fields are deliberately labeled by domain:

- `dx_unaccelerated` / `dy_unaccelerated`: device domain, in libinput raw device coordinates.
- `dx_accelerated_collector` / `dy_accelerated_collector`: accelerated values from Mouseprint's independent libinput context.
- Hyprland compositor-space position is not captured by this slice.

The `devices` table is an identity table. Device lifecycle timing is stored in
the run-scoped `raw_input_events` table as `DEVICE_ADDED` and `DEVICE_REMOVED`
rows.

Compositor samples are stored in `pointer_context`; links to eligible raw
motion, button, and scroll rows are stored in `input_context_matches`. Raw
input rows remain unchanged and immutable. Context matching uses the nearest
sample within an initial 25,000 microsecond tolerance, recording signed
`context_delta_us` and `absolute_delta_us`.

Hyprland cursor position/displacement is the authoritative compositor-domain
observation. Collector-accelerated values are not authoritative screen-space
motion and may differ from Hyprland's own libinput acceleration and
configuration.

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
and `input_context_matches` tables. Stop with `Ctrl-C` or `SIGTERM`. The
collector reports cursor request latency, context sample success rate, and
queue drops on shutdown. Database or Hyprland context failures do not affect
input observation, and the collector does not interfere with normal pointer
operation.
