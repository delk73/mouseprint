# Mouseprint v0.1 — Four-Day Standup Spec

## 1. Project

**Name:** Mouseprint
**Platform:** Omarchy Quattro / Hyprland / libinput
**Duration:** 4-day implementation sprint

**Primary goal:** Build a local mouse-interaction observatory that captures low-level libinput motion plus compositor-space pointer context, derives useful motor-interaction metrics, and presents them through an Omarchy-native interface.

Mouseprint v0.1 is **not an authentication product**.

Its purpose is to establish a measurement foundation for future work in:

* accessibility
* personalized pointer behavior
* ergonomics
* UI friction detection
* motor-pattern characterization
* behavioral-security research

Core thesis:

> Measure both what the user physically does with the pointing device and what the desktop turns that input into, so future accessibility systems can distinguish user behavior from pointer-system behavior.

---

# 2. Product Boundary

## In scope

Mouseprint v0.1 shall:

1. Capture pointer motion from libinput.
2. Preserve unaccelerated motion and independently collected accelerated motion data where exposed.
3. Capture button and scroll events.
4. Associate low-level events with compositor-space cursor position where practical.
5. Associate events with monitor and active application/window context where practical.
6. Store enough evidence to reproduce derived metrics.
7. Derive useful interaction features.
8. Maintain aggregate and session-level statistics.
9. Display an Omarchy-native inspection UI.
10. Provide heatmaps and trajectory-oriented visualizations.
11. Allow recording to be paused.
12. Allow collected data to be cleared.
13. Keep all data local.
14. Establish stable boundaries between capture, synchronization/context, feature extraction, profile aggregation, and UI.

## Explicitly out of scope

Do **not** implement during this sprint:

* authentication
* stranger detection
* identity claims
* automatic pointer acceleration changes
* automatic accessibility adaptation
* gaze tracking
* keyboard logging
* screenshots
* text/content capture
* remote telemetry
* cloud storage
* machine-learning models
* biometric enrollment
* cross-device synchronization

---

# 3. Architecture

Use five conceptual layers.

```text
┌──────────────────────────────┐
│  Omarchy UI / Visualization │
└──────────────▲───────────────┘
               │
┌──────────────┴───────────────┐
│       Profile / Summary      │
│ rolling distributions       │
│ session statistics          │
└──────────────▲───────────────┘
               │
┌──────────────┴───────────────┐
│       Feature Extraction     │
│ trajectories / corrections  │
│ velocity / efficiency       │
└──────────────▲───────────────┘
               │
┌──────────────┴───────────────┐
│ Context / Synchronization   │
│ cursor pos / monitor / app  │
└──────────────▲───────────────┘
               │
┌──────────────┴───────────────┐
│       libinput Capture       │
│ raw + collector-transformed │
│ motion                      │
│ buttons / scroll / device   │
└──────────────────────────────┘
```

Keep these layers separate.

The UI should not derive metrics from raw events itself.

The capture layer should not know about heatmaps, accessibility interpretation, or profile scoring.

---

# 4. Measurement Domains

Mouseprint shall explicitly distinguish three measurement domains.

## Device-space behavior

Derived primarily from libinput unaccelerated motion.

Represents motion closer to what the physical pointing device produced.

Examples:

```text
dx_unaccelerated
dy_unaccelerated
device-space velocity
device-space acceleration
device-space curvature
```

## Collector transform domain

Derived from the accelerated deltas returned by Mouseprint's independent
libinput context.

This is a libinput transformation observation, not authoritative compositor
output. The collector's accelerated deltas may differ from the deltas produced
by Hyprland's own libinput context because the contexts may use different
acceleration settings, configuration, or processing state.

Examples:

```text
dx_accelerated_collector
dy_accelerated_collector
collector transform gain
collector-space velocity
```

## Compositor-space behavior

Derived from Hyprland cursor position and displacement.

Represents what the user actually experiences on screen.

Examples:

```text
cursor_x
cursor_y
compositor displacement
compositor-space velocity
compositor-space path efficiency
click approach geometry
```

Do not silently mix these domains.

Every derived metric should document its source domain.

---

# 5. Why Preserve Three Domains

One long-term purpose of Mouseprint is distinguishing:

```text
user/device behavior
```

from the collector's independent libinput transformation and:

```text
desktop transformation
```

For example:

```text
stable device-space motion
        +
        high compositor-space overshoot
        ↓
possible gain/acceleration/UI interaction issue
```

versus:

```text
irregular device-space corrections
        +
        similar compositor-space corrections
        ↓
different class of interaction behavior
```

Collector-accelerated deltas are not a substitute for compositor cursor
displacement. Mouseprint v0.1 does **not** interpret differences diagnostically.

It preserves enough evidence to investigate them later.

---

# 6. Omarchy / Hyprland Integration

Mouseprint should be implemented as an Omarchy Quattro plugin where practical.

Preferred composition:

```text
Mouseprint
├── native collector
│   └── libinput event acquisition
│
├── context bridge
│   └── timestamped Hyprland cursor/window/monitor state
│
├── analysis/storage
│   └── derived metrics and sessions
│
└── Omarchy plugin UI
    └── overlay/panel
```

Do not force libinput acquisition into QML.

Native capture should remain independent from the presentation layer.

Hyprland is primarily used for compositor context, not as the sole input measurement source.

The compositor context bridge is independent of libinput capture. It uses the
persistent Hyprland event socket for state changes and direct request-socket
JSON queries for cursor position and initial state. Context capture must not
block the libinput loop.

---

# 7. Raw libinput Event Model

The collector should preserve the smallest useful low-level record.

Conceptual structure:

```text
InputEvent {
    timestamp_monotonic
    device_id
    event_type

    dx_accelerated_collector
    dy_accelerated_collector

    dx_unaccelerated
    dy_unaccelerated

    button
    button_state

    scroll_x
    scroll_y
}
```

Exact available fields depend on libinput event type.

Do not fabricate unavailable values.

---

# 8. Context Record

Compositor context should be treated separately.

Conceptual structure:

```text
PointerContext {
    sample_monotonic_us
    request_start_us
    request_end_us
    request_latency_us
    sample_status

    cursor_x
    cursor_y

    monitor_id
    workspace_id

    active_app
    active_window_class
}
```

Avoid arbitrary window-title capture by default.

Application identifiers/window classes are preferred.

Hyprland cursor position and displacement are the authoritative compositor-
domain observations. Collector-accelerated libinput deltas remain a separate
collector-transform domain and must not be used as a substitute.

---

# 9. Event Correlation

libinput and Hyprland/compositor data will not necessarily arrive through the same source.

Correlate them by monotonic timestamp.

Do not require exact timestamp equality.

Use a documented bounded matching strategy such as:

```text
nearest context sample
within tolerance T
```

Record enough information to determine whether a raw event was successfully associated with compositor context.

Example:

```text
context_status = matched
context_delta_us = -820
absolute_delta_us = 820
```

Do not hide synchronization uncertainty.

Raw libinput evidence remains immutable. Correlation is stored separately from
both raw input and compositor context, and only MOTION, BUTTON_DOWN,
BUTTON_UP, and SCROLL rows are eligible for correlation.

---

# 10. Required Event Classes

At minimum preserve:

```text
MOTION
BUTTON_DOWN
BUTTON_UP
SCROLL
```

Prefer derived events such as:

```text
CLICK
DOUBLE_CLICK
DRAG_START
DRAG_MOVE
DRAG_END
```

to be generated later by feature extraction rather than inserted into the raw libinput stream.

---

# 11. Device Identification

Preserve enough device information to distinguish pointing devices.

At minimum:

```text
device_id
device_name
```

if available.

Future analysis may need to distinguish:

```text
mouse
trackball
touchpad
different physical mice
```

Do not combine multiple pointing devices into one behavioral profile without preserving their identity.

---

# 12. Privacy Constraints

Mouseprint deliberately observes interaction behavior, so its boundaries must be obvious.

The collector shall not record:

* keyboard input
* entered text
* clipboard contents
* screenshots
* document contents
* browser content
* arbitrary window contents

Prefer active context such as:

```text
application identifier
window class
workspace
monitor
```

over potentially sensitive titles.

Raw data remains local.

The UI shall expose clear controls for:

```text
Recording: ON / PAUSED
```

and:

```text
Clear collected data
```

---

# 13. Raw vs Derived Storage

Preserve a clear distinction between:

```text
raw evidence
```

and:

```text
derived interpretation
```

For example:

```text
RAW
timestamp
dx_unaccelerated
dy_unaccelerated
dx
dy
button
scroll

COMPOSITOR CONTEXT (separate)
cursor position
monitor
workspace
active application/window class

CORRELATION (separate)
context_delta_us
absolute_delta_us
match status

DERIVED
velocity
trajectory
path efficiency
correction count
click approach
```

Do not overwrite raw input with derived values.

---

# 14. Motion Features

Calculate separately where useful for device-space, collector-transform, and
compositor-space behavior.

## Device-space

* total unaccelerated motion
* mean motion magnitude
* velocity
* acceleration
* directional change
* curvature
* pause distribution

Optional:

* jerk
* movement magnitude distribution

## Compositor-space

* cursor distance
* compositor-space velocity
* displacement
* path geometry
* trajectory efficiency
* monitor transitions

---

# 15. Collector Transformation Metrics

Because both accelerated and unaccelerated deltas are available, preserve the
collector's basic transformation information. These metrics describe the
independent collector libinput context; they do not describe Hyprland's actual
screen transformation.

For each motion event where meaningful:

```text
raw_magnitude =
sqrt(dx_unaccelerated² + dy_unaccelerated²)

collector_accelerated_magnitude =
sqrt(dx_accelerated_collector² + dy_accelerated_collector²)
```

Possible derived value:

```text
collector_gain =
collector_accelerated_magnitude / raw_magnitude
```

Handle zero magnitude safely.

Aggregate later as distributions rather than treating one event's gain as meaningful.

Potential future metrics:

* median collector gain
* gain by raw velocity
* gain variation
* direction-dependent transformation

v0.1 should preserve the data even if UI exposure is minimal.

---

# 16. Pointer Segmentation

Segment continuous motion into meaningful movement episodes.

Approximate concept:

```text
movement begins
    ↓
cursor/device moves
    ↓
pause, slowing, or click
    ↓
movement episode ends
```

Represent device-space and compositor-space trajectories where possible, and
retain collector-transform values separately when they are available.

```text
MovementEpisode {
    start_time
    end_time

    start_compositor_position
    end_compositor_position

    device_path_distance
    compositor_path_distance

    compositor_displacement

    duration
    event_count

    terminating_action
}
```

Segmentation thresholds must be configurable.

Do not treat initial thresholds as normative.

---

# 17. Path Efficiency

Path efficiency is primarily a compositor-space metric.

```text
displacement =
distance(start_compositor_position, end_compositor_position)

actual_distance =
sum(compositor trajectory segment lengths)

path_efficiency =
displacement / actual_distance
```

Bounds:

```text
0.0 <= path_efficiency <= 1.0
```

Use terms such as:

* path efficiency
* excess travel
* indirect travel
* correction travel

Do not label all non-straight motion as waste.

---

# 18. Device-Space, Collector-Transform, and Compositor-Space Efficiency

Where useful, preserve analogous geometry for device-space motion.

This lets future analysis compare:

```text
device-space trajectory
        ↓ collector libinput transformation
collector-transform trajectory
        ↓ Hyprland's independent processing and compositor state
compositor-space trajectory
```

Do not assume a one-to-one relationship.

This comparison is one of the reasons libinput is the primary measurement
source, while Hyprland cursor position remains the authoritative compositor
domain.

---

# 19. Targeting / Click Features

For episodes ending in a click, derive:

* final approach velocity
* final approach angle
* movement-to-click duration
* dwell before click
* direction reversals
* corrective submovements
* compositor-space travel before click
* device-space travel before click

Investigate a simple overshoot/correction heuristic.

Conceptually:

```text
approach
   ↓
pass/depart from eventual click region
   ↓
reverse/correct
   ↓
click
```

Document this as heuristic, not ground truth.

---

# 20. Click Approach Traces

Retain a short pre-click trajectory window.

Suggested range:

```text
250–500 ms
```

Store both:

```text
device-space pre-click motion
```

and:

```text
compositor-space pre-click trajectory
```

Normalize the final compositor-space click position to:

```text
(0, 0)
```

for aggregate visualizations.

This representation may later support motor-profile characterization.

---

# 21. Scroll Features

Capture at least:

* scroll event count
* horizontal and vertical values
* signed direction
* direction reversals
* scroll burst duration
* pauses between bursts

Preserve low-level values first.

Derived scroll behavior remains separate from pointer movement metrics.

---

# 22. Spatial Metrics

Normalize screen coordinates against monitor geometry.

Example:

```text
normalized_x =
(x - monitor_left) / monitor_width

normalized_y =
(y - monitor_top) / monitor_height
```

Track:

* pointer occupancy
* dwell
* click distribution
* edge use
* corner use
* monitor transitions
* workspace transitions where useful

Do not merge monitors before retaining the monitor identity.

---

# 23. Application Context

Where practical, aggregate behavior by application/window class.

Example:

```text
Browser
    screen path efficiency
    corrections/click
    compositor travel
    device travel

Terminal
    screen path efficiency
    corrections/click
    compositor travel
    device travel
```

This is intended to reveal context-specific interaction patterns, not application surveillance.

---

# 24. Sessions

Define explicit Mouseprint sessions.

Possible boundaries:

```text
collector start
long inactivity
manual new session
system login/logout
```

Store aggregate metrics per session.

This enables future comparisons such as:

```text
session A vs session B
morning vs evening
device A vs device B
before vs after pointer-setting change
```

Do not automatically infer causation from session differences.

---

# 25. Profile Layer

The profile is not a biometric identity record.

It is a rolling statistical description of observed pointing behavior.

Potential profile dimensions:

```text
device-space velocity distribution
device-space acceleration distribution
compositor-space velocity distribution
path-efficiency distribution
movement-distance distribution
click-approach distribution
corrections-per-click distribution
dwell distribution
scroll behavior
screen-region use
collector acceleration-gain distribution
```

Prefer:

```text
median
quantiles
histograms
distributions
```

over only arithmetic means.

---

# 26. User Interface

The UI should feel like an instrument for inspecting human-computer interaction.

Primary sections:

## Overview

Show:

```text
Recording duration
Pointer travel
Device motion
Movement episodes
Clicks
Scroll activity
Path efficiency
Correction rate
```

Avoid normative language such as:

```text
good
bad
healthy
poor
```

unless such interpretation is later validated.

---

# 27. Three-Domain Visualization

At least one UI view should make the device, collector-transform, and
compositor distinctions visible.

For example:

```text
DEVICE INPUT

raw motion       124,802 units
median velocity  ...
corrections      ...

COLLECTOR TRANSFORM
accelerated motion ...
collector gain     ...

COMPOSITOR RESULT

cursor travel    2.31 km
path efficiency  0.81
overshoots       ...
```

The exact raw units should be labeled correctly rather than pretending they are physical centimeters.

---

# 28. Acceleration View

Stretch goal:

Show how the collector transforms device motion, while keeping the separate
compositor result visible when available.

Possible representation:

```text
raw movement magnitude
        ↓
collector libinput acceleration/transformation
        ↓
collector-accelerated movement magnitude
```

Even a basic distribution or median collector gain is useful. It must not be
presented as the desktop's authoritative compositor-space gain.

Do not claim that high or low gain is undesirable.

---

# 29. Heatmap

Provide at least one compositor-space heatmap.

Possible modes:

```text
movement
dwell
clicks
```

One mode is sufficient for v0.1.

Keep per-monitor coordinate normalization.

---

# 30. Movement Visualization

Provide at least one trajectory-oriented view.

Possible options:

### Recent path

Last several seconds of compositor-space movement.

### Sample episode

Display one complete movement trajectory.

Optionally pair it with a simplified device-space trace.

The purpose is partly analytical validation:

> Do the derived numbers correspond to movement a human recognizes?

---

# 31. Click Approach Visualization

Stretch goal:

Overlay normalized pre-click traces around a common final click origin.

This may reveal:

* approach direction
* correction geometry
* overshoot
* repeated movement structures

Use a recent sample rather than attempting enormous datasets in v0.1.

---

# 32. Accessibility Foundation

No adaptation is performed in v0.1.

The data model should preserve enough information for future investigation of:

```text
targeting instability
pointer acceleration mismatch
repeated overshoot
excess corrections
drag difficulty
edge interaction difficulty
fatigue/session drift
context-specific UI friction
long repetitive traversals
device-specific differences
```

Future architecture:

```text
observe
   ↓
characterize
   ↓
separate device/user/system effects
   ↓
identify recurring interaction friction
   ↓
suggest adaptation
   ↓
measure after adaptation
   ↓
compare before / after
```

---

# 33. Storage

SQLite is preferred.

Suggested logical tables:

```text
devices
raw_input_events
pointer_context
input_context_matches
movement_episodes
click_features
sessions
profile_summary
```

If synchronization is easier with one correlated event table later, that can be introduced after evidence capture works.

Do not prematurely denormalize.

`raw_input_events` must not gain compositor columns or be overwritten. The
`pointer_context` table stores compositor samples independently, and
`input_context_matches` stores nearest-sample links and uncertainty.

---

# 34. Sampling and Event Fidelity

Before choosing downsampling, measure actual libinput event volume.

An initial implementation may sample Hyprland cursor position at 60 Hz using
direct request-socket JSON queries. This is an implementation choice to be
measured for request latency, success rate, CPU cost, and correlation quality;
it is not a permanent product sampling requirement.

Investigate:

* events per second during typical use
* database growth
* CPU cost
* batching behavior
* effect of resampling on derived metrics

Preserve enough temporal resolution for:

```text
velocity
acceleration
curvature
correction detection
click approach
```

If resampling is introduced, document:

```text
source rate
retained rate
algorithm
```

---

# 35. Timing

Use monotonic timestamps for event correlation and movement calculations.

Use `CLOCK_MONOTONIC` for bridge event receipt and cursor request start/end
timestamps. Record the cursor sample timestamp as the request midpoint. Keep
the signed `context_delta_us` and its absolute value `absolute_delta_us`; do
not use an ambiguous context-age field.

Do not rely solely on wall-clock timestamps for:

* velocity
* acceleration
* sequence ordering
* libinput/Hyprland synchronization

Wall clock may be stored additionally for session display/history.

---

# 36. Performance Requirement

Mouseprint must be operationally invisible during normal desktop use.

Requirements:

* no blocking compositor path
* no heavy synchronous processing per event
* batched persistence where appropriate
* analysis decoupled from input capture
* Hyprland IPC must not block the libinput capture loop
* low idle CPU
* bounded memory
* collector failure must not interfere with pointer operation

The measurement system must not materially alter the behavior it measures.

---

# 37. Failure Handling

Mouseprint must fail open with respect to desktop operation.

If:

```text
collector crashes
database unavailable
UI crashes
context bridge unavailable
```

the mouse and compositor must continue normally.

Log the failure.

Do not attempt aggressive automatic recovery that affects the input stack.

---

# 38. Proposed Repository Structure

```text
mouseprint/
├── manifest.json
├── README.md
│
├── collector/
│   ├── src/
│   └── ...
│
├── context/
│   └── ...
│
├── analysis/
│   └── ...
│
├── plugin/
│   ├── Service.qml
│   ├── Overlay.qml
│   └── components/
│
├── schema/
│   └── ...
│
├── docs/
│   ├── architecture.md
│   ├── metrics.md
│   └── privacy.md
│
└── tests/
```

Keep the native collector independent of the Omarchy UI.

---

# 39. Four-Day Execution Plan

## Day 1 — Capture Input Evidence

Goal:

> Trustworthy low-level input evidence plus compositor context.

Implement:

* repository skeleton
* Omarchy plugin manifest
* libinput collector
* device identification
* motion events
* collector-accelerated deltas
* unaccelerated deltas
* button events
* scroll events
* monotonic timestamps
* local persistence
* Hyprland cursor/context acquisition
* timestamp correlation prototype
* pause/resume

Measure:

* event frequency
* CPU usage
* storage growth

End-of-day test:

Move the mouse deliberately through known patterns and verify:

```text
raw/unaccelerated device motion exists
collector-accelerated motion exists
compositor cursor position changes coherently
button events align
timing is plausible
```

---

## Day 2 — Characterize

Goal:

> Convert evidence into useful interaction measurements.

Implement:

* movement segmentation
* device-space path metrics
* compositor-space path metrics
* velocity
* acceleration
* path efficiency
* collector gain
* click-associated movement
* correction/reversal heuristic
* pre-click traces
* normalized monitor coordinates
* session summaries

Add unit tests for the mathematical transforms.

Example output:

```text
Session
Duration:                  01:42:17
Screen pointer distance:   2.31 km
Device motion events:      184,221
Movement episodes:         3,821
Clicks:                    911
Median path efficiency:    0.81
Median corrections/click:  1
Median collector gain:     ...
```

---

## Day 3 — See

Goal:

> Make the measurements comprehensible.

Build Omarchy overlay/panel.

Include:

* recording state
* session summary
* compositor travel
* device-space activity
* path efficiency
* corrections
* collector gain summary
* heatmap
* trajectory inspection

Stretch:

* click approach view
* application breakdown

---

## Day 4 — Profile

Goal:

> Turn a session recorder into the basis of longitudinal interaction characterization.

Implement:

* session history
* rolling distributions
* current vs previous session comparison
* device-specific summaries
* storage controls
* clear/delete
* install test
* README
* architecture documentation
* metric definitions
* privacy documentation

Stretch:

Compare behavior with two different pointer acceleration settings without changing settings automatically.

---

# 40. Acceptance Tests

## Capture

* libinput motion is recorded continuously.
* Collector-accelerated motion values are preserved and labeled as such.
* Unaccelerated motion values are preserved where available.
* Button events are captured.
* Scroll events are captured.
* Pointing device identity is preserved.
* Recording can be paused.

## Context

* Hyprland cursor position/displacement is the authoritative compositor-domain observation.
* Monitor identity is preserved.
* Compositor samples are stored separately from raw input evidence.
* Correlation is stored separately from both raw input and compositor samples.
* Context-correlation uncertainty is represented with signed `context_delta_us` and `absolute_delta_us`.
* Only motion, button, and scroll rows are correlated.

## Data

* Raw events use monotonic timing.
* Raw input and derived features remain separable.
* Session data survives UI restart.
* Data can be deleted locally.

## Analysis

* Device-space velocity calculation has tests.
* Compositor-space velocity calculation has tests.
* Path efficiency has tests.
* Pointer-gain calculation has tests.
* Click-associated movement can be extracted.
* Correction detection is explicitly documented as heuristic.

## Visualization

* User can summon Mouseprint from Omarchy.
* Recording state is obvious.
* User can inspect device-space, collector-transform, and compositor-space summaries.
* At least one heatmap works.
* At least one trajectory can be inspected.

## Privacy

* No keyboard events are captured.
* No screenshots are captured.
* No remote transmission is required.
* Storage location is documented.
* Data can be cleared.

---

# 41. Non-Goals for the Autocoder

The implementation agent must not:

* replace or patch libinput
* modify pointer behavior
* modify acceleration settings automatically
* redesign Hyprland input handling
* patch Omarchy core unless unavoidable
* introduce cloud dependencies
* introduce ML
* add authentication
* call profiles biometric identities
* infer disability or medical status
* claim accessibility benefits not yet measured
* collect keystrokes or content
* add unrelated monitoring
* broaden the project into generic desktop telemetry

Prefer the smallest implementation satisfying the acceptance criteria.

---

# 42. Engineering Principle

Preserve observable evidence before interpretation.

Prefer storing:

```text
timestamp
device
unaccelerated delta
accelerated delta
screen position
trajectory
derived metric
algorithm/version
```

over storing only:

```text
"poor targeting"
```

Mouseprint should make it possible to answer:

> What happened?

> In which measurement domain?

> How was this number calculated?

Interpretation comes later.

---

# 43. Definition of Done

At the end of four days, a user on Omarchy should be able to:

1. Install and enable Mouseprint.
2. Use the desktop normally.
3. Accumulate local libinput, collector-transform, and compositor-space pointer evidence.
4. Preserve unaccelerated and collector-accelerated motion separately.
5. Summon Mouseprint.
6. See how far the cursor traveled.
7. Inspect low-level device motion characteristics.
8. Inspect path efficiency and correction behavior.
9. Inspect the relationship between physical input and desktop-transformed input.
10. View at least one spatial heatmap.
11. Inspect at least one actual trajectory.
12. Compare basic session statistics.
13. Pause recording.
14. Delete collected data.

The resulting code and schema must leave a direct path toward:

```text
accessibility experiments
        +
pointer-configuration personalization
        +
ergonomic analysis
        +
motor-interaction profiling
        +
later behavioral-security research
```

without claiming any of those problems are already solved.
