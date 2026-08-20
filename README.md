# Mouseprint

Mouseprint is a local pointing-device observatory for Omarchy/Hyprland. The
current implementation is Slice 1: native libinput evidence capture with
local SQLite persistence.

## Slice 1

The collector creates a non-exclusive libinput udev context on `seat0` and
reports pointer-capable device lifecycle events, motion, buttons, and scroll
events, and persists those raw records to SQLite. It does not modify pointer
configuration, grab devices, or communicate with Hyprland.

Motion fields are deliberately labeled by domain:

- `dx_unaccelerated` / `dy_unaccelerated`: device domain, in libinput raw device coordinates.
- `dx_accelerated_collector` / `dy_accelerated_collector`: accelerated values from Mouseprint's independent libinput context.
- Hyprland compositor-space position is not captured by this slice.

Collector-accelerated values are not authoritative screen-space motion and may
differ from Hyprland's own libinput acceleration and configuration.

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
verify the `collector_runs`, `devices`, and `raw_input_events` tables. Stop
with `Ctrl-C` or `SIGTERM`. Database failures disable persistence but do not
affect input observation, and the collector only reads the input stream without
interfering with normal pointer operation.
