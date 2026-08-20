# Mouseprint

Mouseprint is a local pointing-device observatory for Omarchy/Hyprland. The
current implementation is Slice 1: native libinput evidence capture.

## Slice 1

The collector creates a non-exclusive libinput udev context on `seat0` and
reports pointer-capable device lifecycle events, motion, buttons, and scroll
events. It does not modify pointer configuration, grab devices, write a
database, or communicate with Hyprland.

Motion fields are deliberately labeled by domain:

- `dx_unaccelerated` / `dy_unaccelerated`: device domain, in libinput raw device coordinates.
- `dx_accelerated_collector` / `dy_accelerated_collector`: accelerated values from Mouseprint's independent libinput context.
- Hyprland compositor-space position is not captured by Slice 1.

Collector-accelerated values are not authoritative screen-space motion and may
differ from Hyprland's own libinput acceleration and configuration.

## Build

Requirements are the installed `libinput`, `libudev`, and a C++17 compiler:

```sh
make -C collector
```

The binary is `build/mouseprint-collector`.

## Run

The user running the collector needs read access to `/dev/input/event*`,
normally provided by the `input` group. Root is not required.

```sh
./build/mouseprint-collector
```

Move the mouse, click, and scroll to see human-readable evidence lines. Stop
with `Ctrl-C` or `SIGTERM`. The collector only reads the input stream and does
not interfere with normal pointer operation.
