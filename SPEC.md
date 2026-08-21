# Mouseprint v0.1 — Product and Implementation Specification

## 1. Project

**Name:** Mouseprint
**Platform:** Omarchy Quattro / Hyprland / libinput

**Primary goal:** Build a local pointer-interaction observatory that captures
low-level libinput evidence plus compositor-space pointer context, derives
evidence-backed interaction measurements, and makes them inspectable through an
Omarchy-native interface.

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

## Implementation status and roadmap

### Shipped foundation

The current implementation includes:

* Slice 1: native libinput pointer capture.
* Slice 2: local SQLite persistence for raw evidence.
* Slice 3: separate Hyprland compositor context capture.
* Evidence-safe timestamp correlation between input and context.
* Slice 4: movement episodes and foundational device/compositor metrics.
* Slice 5: queryable episode trajectory points.

### Next

* A narrow, read-only inspection UI over the existing derived data.

### Later descriptive measurement

* Explicit sessions and aggregate summaries.
* Monitor-aware spatial products once sufficient monitor geometry is retained.
* Click and targeting features.
* Scroll-derived features.
* Aggregate collector-transform metrics.

### Later higher-level features

* Interaction-pattern detection.
* Passive guidance.
* Accessibility and motor-friction observations.
* User-approved adaptive assistance.

### Optional later research

* Behavioral-security characterization, without identity or authentication
  claims.

---

# 2. Product Boundary

## Current product boundary

Mouseprint currently captures pointer motion, button, scroll, device identity,
Hyprland context, and correlation evidence locally. It preserves both
unaccelerated motion and independently collected accelerated motion where
available, then derives movement episodes, foundational metrics, and trajectory
points from that evidence.

The next product slice is a read-only inspection UI. Sessions, summaries,
heatmaps, click features, scroll features, profiles, adaptive assistance, and
higher-level interpretation remain roadmap work. Recording pause/clear controls
remain product requirements, but are not part of the current collector or the
initial inspection UI.

The architecture keeps capture, context/correlation, derived products, future
aggregation, and UI as separate concerns. All data remains local.

## Explicitly out of scope

The following remain outside the shipped foundation and must not be treated as
implemented:

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

The product also makes no accessibility, ergonomic, behavioral, or security
judgment from the current metrics.

---

# 3. Architecture

Use separate evidence and interpretation layers.

```text
raw_input_events            pointer_context
       |                          |
       |                          |
       +---------- input_context_matches
                         |
                         v
                episode derivation
                         |
                         v
                movement_episodes
                    /          \
                   v            v
 movement_episode_members   movement_episode_trajectory_points
                   \            /
                    \          /
                     v        v
                read-only inspection UI

future: sessions, summaries, spatial products, profiles, and higher-level
interpretation
```

The shipped SQLite evidence and derived tables are:

```text
raw_input_events
pointer_context
input_context_matches
movement_episode_members
movement_episodes
movement_episode_trajectory_points
```

`movement_episode_members` and `movement_episodes` describe Slice 4 episode
membership and metrics. `movement_episode_trajectory_points` describes the
Slice 5 motion trajectory while preserving raw-event and correlation
provenance.

Keep raw input evidence, compositor context, correlation, and derived products
separate. Raw/context/correlation evidence is not rewritten by derivation.
Derived products remain recomputable, and missing evidence remains explicit
rather than being replaced with fabricated values.

The UI should not derive metrics from raw events itself.

The capture layer does not know about heatmaps, accessibility interpretation,
or profile scoring.

---

# 4. Measurement Domains

Mouseprint shall explicitly distinguish three measurement domains.

## Device-space behavior

Derived primarily from libinput unaccelerated motion.

Represents motion closer to what the physical pointing device produced.
These are raw device-coordinate units, not physical centimeters.

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
trajectory points
```

Do not silently mix these domains.

Hyprland cursor position is the authoritative desktop-result observation.
Collector-accelerated deltas do not equal compositor displacement.

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

Mouseprint is currently a native collector integrated with Omarchy/Hyprland;
the read-only inspection UI is the next implementation target.

Preferred composition:

```text
Mouseprint
├── native collector
│   └── libinput event acquisition
│
├── context bridge
│   └── timestamped Hyprland cursor/window/monitor state
│
├── SQLite evidence and derived products
│   └── episodes and trajectory points
│
└── future inspection UI
    └── read-only views
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

libinput and Hyprland/compositor data do not arrive through the same source.

Correlate them by monotonic timestamp.

Do not require exact timestamp equality.

The shipped correlator uses the nearest valid compositor context within the
current initial tolerance of 25,000 microseconds:

```text
nearest valid context sample
within 25,000 us
```

The tolerance is an initial implementation value, not a permanent normative
requirement. The match records signed `context_delta_us` and
`absolute_delta_us`.

Record enough information to determine whether a raw event was successfully associated with compositor context.

Example:

```text
context_status = matched
context_delta_us = -820
absolute_delta_us = 820
```

Do not hide synchronization uncertainty.

The shipped statuses are:

* `matched`: a valid context sample qualified.
* `unmatched_context_error`: a failed/unavailable context sample qualified, but
  no valid sample did.
* `unmatched_outside_tolerance`: context exists, but no valid sample qualified
  within tolerance and no failed sample qualified.
* `unmatched_no_context`: no context was observed.

Failed context samples remain in `pointer_context`, and their correlation rows
remain in `input_context_matches`; unavailable evidence is not hidden.

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

Future feature extraction may derive events such as:

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

The future UI should expose clear controls for:

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
movement_episodes
movement_episode_members
movement_episode_trajectory_points
device/compositor velocity
device path distance
compositor path distance
compositor displacement
compositor path efficiency
directional reversal count
```

The trajectory table contains one row per MOTION member. It preserves
`raw_event_id`, `match_id` where available, an episode-relative ordinal, source
time, device trajectory values, and compositor observation values.

Do not overwrite raw input with derived values.

---

# 14. Motion Features

The shipped base metrics are calculated separately by domain. Additional
features remain planned.

## Device-space

Shipped:

* unaccelerated path distance
* device-domain average and peak velocity
* directional reversal count

Planned:

* acceleration
* curvature
* pause distribution

Optional:

* jerk
* movement magnitude distribution

## Compositor-space

Shipped:

* compositor path distance
* compositor-space average and peak velocity
* displacement
* path efficiency
* monitor/workspace transition status

Trajectory points are shipped separately as described in the trajectory section.

---

# 15. Collector Transformation Metrics (Planned)

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

Slice 1 preserves the raw accelerated values. Aggregate collector-transform
analysis is not yet shipped and remains planned.

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

The shipped segmenter has these semantics:

* Episodes are independent per `run_id` and `device_id`.
* Only `MOTION` contributes movement membership.
* Evidence is ordered by `source_time_us`, then `receive_sequence`, then
  `event_id` for deterministic ties.
* The current idle gap is 100,000 microseconds. An idle gap closes at the
  previous motion timestamp.
* Zero-valued motion remains evidence and remains an episode member.
* `BUTTON_DOWN` terminates an active episode and is retained as its terminating
  member. `BUTTON_UP` does not terminate an episode.
* `SCROLL` does not start or end an episode and is not movement membership.
* Run end closes an active episode at its last motion timestamp.

The 100,000 microsecond threshold is a fixed initial value. Segmentation
thresholds are intended to become configurable rather than normative.

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

Current device metrics use unaccelerated deltas only. There is no accelerated
substitution. Device path distance, duration, device velocity, and directional
reversal count remain device-domain metrics. Zero vectors are retained for
path/timing evidence but ignored when comparing consecutive non-zero vectors
for directional reversal.

Current compositor metrics use valid matched Hyprland cursor samples only. They
do not interpolate missing observations. Consecutive duplicate context IDs are
collapsed for path calculation, and context sample timestamps are used for
compositor velocity. A monitor or workspace transition invalidates compositor
episode metrics. Motion from another pointing device during an episode also
invalidates compositor metrics because Hyprland cursor state is global; device
metrics remain available.

---

## Trajectory Data Products

`movement_episode_trajectory_points` contains one row per `MOTION` member. Each
row has an episode-relative ordinal, `raw_event_id`, `match_id` where
available, source time, and materialized device/context trajectory values.

## Device trajectory

* Coordinates are cumulative unaccelerated libinput units relative to the
  episode origin, not physical centimeters.
* The first motion applies its delta to `(0, 0)`.
* Zero-valued motions remain trajectory points.
* Cumulative path is the sum of Euclidean magnitudes of unaccelerated deltas.
* Missing unaccelerated `dx` or `dy` produces NULL local device deltas and NULL
  cumulative x, y, and path at that point.
* Cumulative device x, y, and path remain unavailable for later points in that
  episode. Later valid local dx/dy values may still be retained; continuity is
  never fabricated across the missing delta.

## Compositor trajectory

* Coordinates come only from valid matched Hyprland cursor observations.
* The context sample timestamp is retained for each available observation.
* Consecutive repeated context IDs do not create artificial movement.
* A missing or invalid observation is not interpolated and breaks cumulative
  compositor-path continuity.
* Later valid cursor positions and context provenance remain observable, but
  cumulative compositor path remains NULL after the gap.

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

# 19. Targeting / Click Features (Planned)

For future click-derived episodes, derive:

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

# 20. Click Approach Traces (Planned)

Later feature extraction may retain a short pre-click trajectory window.

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

# 21. Scroll Features (Planned)

The collector currently preserves scroll as raw evidence only. Later feature
extraction may derive:

* scroll event count
* horizontal and vertical values
* signed direction
* direction reversals
* scroll burst duration
* pauses between bursts

Preserve low-level values first.

Derived scroll behavior remains separate from pointer movement metrics.

---

# 22. Spatial Metrics (Planned)

Monitor-aware spatial products are planned. Monitor-normalized coordinates are
not currently implemented: persisted context retains monitor identity but not
sufficient monitor geometry for evidence-backed normalization. Do not infer
monitor width or height from current data.

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

# 23. Application Context (Planned)

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

# 24. Sessions (Planned)

Define explicit Mouseprint sessions beyond collector runs.

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

# 25. Profile Layer (Planned)

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

# 26. User Interface: Next Slice

The next implementation target is a narrow, read-only inspection UI over
existing derived data. It should feel like an instrument for inspecting
human-computer interaction, not a second feature-extraction engine.

Initial inspection surfaces:

* Recent run summary.
* Episode list and episode metrics.
* Selected device and compositor trajectories.
* Evidence and match-quality indicators.
* Provenance inspection where useful.

Pause and clear controls remain product requirements, but are not part of this
initial read-only slice unless explicitly added later.

Avoid normative language such as:

```text
good
bad
healthy
poor
```

unless such interpretation is later validated.

---

# 27. Three-Domain Visualization (Planned)

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

# 28. Acceleration View (Planned)

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

# 29. Heatmap (Planned)

Provide at least one compositor-space heatmap only after monitor-aware spatial
evidence is sufficient.

Possible modes:

```text
movement
dwell
clicks
```

One mode is sufficient for v0.1.

Keep per-monitor coordinate normalization.

---

# 30. Movement Visualization (Next UI)

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

# 31. Click Approach Visualization (Planned)

Stretch goal:

Overlay normalized pre-click traces around a common final click origin.

This may reveal:

* approach direction
* correction geometry
* overshoot
* repeated movement structures

Use a recent sample rather than attempting enormous datasets in v0.1.

---

# 32. Accessibility Foundation (Later)

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

The shipped logical tables are:

```text
devices
raw_input_events
pointer_context
input_context_matches
movement_episodes
movement_episode_members
movement_episode_trajectory_points
```

Future tables/products may add click features, sessions, and profile summaries.

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

# 39. Durable Roadmap

The implementation is organized as product slices rather than a short sprint:

* Shipped foundation: libinput capture, SQLite raw evidence, Hyprland context,
  timestamp correlation, movement episodes, foundational metrics, and
  trajectory points.
* Next: read-only inspection UI over runs, episodes, metrics, trajectories,
  evidence quality, and provenance.
* Later descriptive measurement: sessions and summaries, monitor-aware spatial
  products, click/targeting features, scroll features, and collector-transform
  aggregate metrics.
* Later higher-level features: interaction-pattern detection, passive guidance,
  accessibility/motor-friction observations, and user-approved adaptive
  assistance.
* Optional research: behavioral-security characterization without identity or
  authentication claims.

No dates or completion schedule are implied by this roadmap.

---

# 40. Acceptance Tests

## Shipped foundation

* libinput motion is recorded continuously.
* Collector-accelerated motion values are preserved and labeled as such.
* Unaccelerated motion values are preserved where available.
* Button events are captured.
* Scroll events are captured.
* Pointing device identity is preserved.
* Movement episodes are recomputable from raw/context/correlation evidence.
* Episode replacement and trajectory replacement are atomic.
* Device and compositor trajectory provenance is queryable.

## Correlation and context

* Hyprland cursor position/displacement is the authoritative compositor-domain observation.
* Monitor identity is preserved.
* Compositor samples are stored separately from raw input evidence.
* Correlation is stored separately from both raw input and compositor samples.
* Context-correlation uncertainty is represented with signed `context_delta_us` and `absolute_delta_us`.
* Only motion, button, and scroll rows are correlated.

## Data boundaries

* Raw events use monotonic timing.
* Raw input and derived features remain separable.
* Raw/context/correlation evidence remains separate from derived data.

## Current derived analysis

* Device-space velocity calculation has tests.
* Compositor-space velocity calculation has tests.
* Path efficiency has tests.
* Directional reversal ignores zero vectors for comparison while retaining them
  as evidence.
* Trajectory data preserves missing values rather than fabricating continuity.

The deterministic collector tests cover correlation statuses, episode
segmentation, foundational metrics, trajectory ordinals/provenance, duplicate
contexts, missing device/context observations, and re-derivation.

## Planned acceptance

The following are roadmap criteria, not current implementation claims:

* A read-only UI can inspect recent runs, episodes, metrics, trajectories,
  evidence quality, and provenance.
* Sessions and summaries are defined.
* Spatial products, click features, scroll features, and collector-transform
  aggregates are derived with documented evidence semantics.
* Pause and clear controls are implemented.

## Privacy

* No keyboard events are captured.
* No screenshots are captured.
* Collected data remains local; the shipped foundation performs no remote
  telemetry or storage.
* Storage location is documented.
* Data remains local and can be cleared when the future clear control is
  implemented.

---

# 41. Implementation Findings and Open Design

These are planned follow-ups, not failures of the shipped foundation:

* Monitor geometry persistence is needed before normalized spatial products can
  be evidence-backed.
* The segmentation threshold is currently fixed at 100,000 microseconds and is
  intended to become configurable.
* Collector-transform aggregate analysis is still pending.
* Click, double-click, and drag semantics are not yet derived.
* Scroll remains raw evidence only.
* Sessions are not yet defined beyond collector runs.
* Pause and clear controls are not yet implemented.

---

# 42. Non-Goals for the Autocoder

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

# 43. Engineering Principle

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

# 44. Current Checkpoint and Future Definition of Done

The shipped checkpoint lets a user:

1. Build and run the native collector.
2. Use the desktop normally while Mouseprint observes pointer activity.
3. Accumulate local libinput, collector-transform, and compositor-context evidence.
4. Preserve unaccelerated and collector-accelerated motion separately.
5. Query movement episodes and foundational metrics from SQLite.
6. Query device and compositor trajectory points with provenance.

The next UI checkpoint should allow read-only inspection of those existing
products. Later work may provide sessions, normalized spatial products, click
and scroll features, adaptive assistance, and optional behavioral-security
research. None of those claims are part of the shipped foundation.

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
