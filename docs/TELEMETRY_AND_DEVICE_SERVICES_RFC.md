# Telemetry and device services — architecture proposal

**Status:** Exploratory RFC. No public API, persistent record format, partition layout, or retention policy is frozen by this document.

## 1. Purpose

ESP System should grow from an OTA/connectivity library into a reusable device-services layer. Applications supply domain-specific measurements, events, controls, and policies; the shared layer provides common infrastructure for:

- persistent telemetry and event history;
- timekeeping and acquisition timestamps;
- retention, aggregation, compaction, querying, and export;
- system diagnostics and support bundles;
- power-aware scheduling and sleep coordination;
- transport-independent store-and-forward synchronization;
- OTA eligibility and maintenance policy for intermittently connected devices.

The cold-storage controller is the first test application. A multifunctional solar lamp is a second, materially different test case: it may be battery-powered, sleep for long periods, act as a light and environmental/motion sensor, and synchronize only when a gateway or parent is available.

This is not a proposal to move application control loops or sensor drivers into ESP System.

## 2. Design boundary

| Concern | ESP System | Application |
|---|---|---|
| Device identity, firmware metadata, reset/wake diagnostics | Common implementation | Supplies product/hardware identity |
| Clock source, UTC timestamps, time quality | Common service | Supplies domain schedule assumptions |
| Telemetry storage and event journal | Common implementation | Defines series and emits observations/events |
| Retention and aggregation engine | Common policies and merge machinery | Selects series-specific aggregation semantics |
| Sensor acquisition | No | Yes |
| Measurement meaning, units, calibration and quality | Stores metadata | Defines and reports it |
| Control algorithm and actuator policy | No | Yes |
| Power/sleep coordination primitives | Common optional service | Requests sleep and supplies safety/energy policy |
| Network transports and store-forward queue | Common interfaces and adapters | Selects transports and application payloads |
| Application dashboard | Generic viewer/API may be provided | Defines application-specific presentation |
| OTA installation, rollback and health framework | Common mechanism | Supplies safe-update and health predicates |

System services should be optional and independently configurable. A minimal application should not pay the flash, RAM, or power cost of telemetry, Zigbee, or deep-sleep policy that it does not use.

## 3. Telemetry model: extensible semantics, compact storage

### 3.1 Do not freeze the on-flash format yet

Design the API and semantic model before committing to bytes on flash. Keep the in-memory API typed and reasonably stable, but allow the persistent encoding to evolve through explicit format versions.

Avoid both extremes:
- a fixed, application-specific C++ struct that changes whenever a project adds a metric;
- an elaborate general database or dynamic object model that consumes excessive RAM, flash, and implementation effort.

A likely direction is a small common record envelope plus a registry of application-defined series and typed record payloads. On-flash blocks can later use compact integer encodings and optional tagged fields. Unknown optional fields should be skippable; incompatible changes need an explicit format version and recovery policy.

Series IDs must be stable for the lifetime of a stored dataset. The series registry records their definitions and schema version. Renaming a display label should not silently change a series identity.

### 3.2 Series descriptors

A series descriptor should be able to describe:
- stable ID, human-readable name, description, and unit;
- value type: floating-point/scaled numeric, signed/unsigned integer, boolean, enum, counter, or another explicitly supported type;
- semantic kind: gauge, state, cumulative counter, rate, event-like observation, or distribution;
- sampling expectations and whether observations are event-driven;
- source identity and optional calibration/provenance metadata;
- quality interpretation and optional reported uncertainty;
- aggregation policy and whether gaps or held values are meaningful.

Do not store long strings in every sample. Keep descriptors in a versioned registry; data records refer to compact IDs.

Applications may register and update descriptors. Schema changes must not reinterpret old data silently. A unit or calibration change may require a new series ID or an explicit descriptor version and conversion rule.

### 3.3 Observation envelope

An observation should conceptually distinguish:
- acquisition time: when the physical quantity was measured;
- logging/acceptance time: when the application decided the observation was due for retention;
- storage time: when the persistent write completed;
- value and data type;
- quality flags (valid, suspect, stale, estimated, saturated, sensor fault, etc.);
- optional source-provided uncertainty;
- optional sequence number or source timestamp for duplicate detection.

These are semantic requirements, not a final binary struct. The normal case can omit fields that are derivable or unnecessary.

For intermittent sampling, a repeated cached value is not a new observation. The storage API should identify duplicate sequence/timestamp pairs. If a value is held for display or control, its age should remain explicit; visualization must not draw a held value as if it were newly measured.

### 3.4 Aggregates and uncertainty

Aggregates must retain mergeable sufficient statistics rather than just averaging already-averaged values.

Candidate numeric statistics include:
- observation count and valid/invalid counts;
- first and last values/times;
- minimum and maximum with timestamps;
- mean and a numerically stable variance accumulator (for example count, mean, and M2);
- optional time-weighted integral/mean for irregularly spaced observations;
- time coverage, maximum gap, and data-quality counts;
- uncertainty metadata where supplied.

Standard deviation of observed readings describes variation in the observations. Instrument measurement uncertainty describes knowledge of the true value. They are different quantities and must not be merged or labelled as one another.

If uncertainty is supplied per observation, preserve its provenance and meaning (standard uncertainty, confidence interval, accuracy bound, etc.). Unknown uncertainty remains unknown, not zero. Aggregation must not imply independent errors when calibration bias or other systematic errors are shared.

Aggregation should be type-specific:
- temperature/voltage/current gauges: mean, min, max, variance, first/last, sample count, coverage;
- boolean states: time-in-state/duty fraction, transitions, longest on/off run;
- counters: first/last and delta, with reset/wrap indication;
- enumerated states: duration/count by state and transition counts;
- event streams: preserve exact timestamps and typed event details.

A generic statistics container should not force every type to provide every statistic.

### 3.5 Retention tiers and age-based compaction

A configurable age-based hierarchy is preferred to a single destructive half-life operation. Pair adjacent compatible time buckets, preserving mergeable statistics and exact event times.

An illustrative policy—not a default commitment—could retain raw observations for recent hours, one-minute aggregates for the next few days, five-minute aggregates for the following week, and coarser aggregates for the rest of a 30-day window.

The exact horizons, bucket widths, and storage quotas should be established by measurement on 4 MiB and 8 MiB targets.

Preserving min/max and variance keeps excursions and variability visible but cannot reconstruct the exact frequency, phase, or reaction time of an oscillation. Consider retaining short raw windows around significant events (pre-trigger and post-trigger buffers), and preserve event timestamps exactly for the retention period.

Retention must be power-fail safe. Compaction should write and verify a new block/index before reclaiming its source. An interrupted final record should be detectable by length, CRC, and format-version checks. Never silently format the telemetry partition on boot. Expose storage usage, oldest retained time, dropped-record counts, and compaction errors.

Do not use NVS as a high-rate history store. NVS is suited to infrequently changed settings; use a dedicated data partition, filesystem, or append-oriented partition design for telemetry. See Espressif's [filesystem and NVS considerations](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/file-system-considerations.html).

## 4. Query API and visualization

The query API is important enough to design alongside the write API. It should operate on time ranges and series IDs, independent of the underlying storage format.

Candidate operations:
- list series and their descriptors;
- query a time range for selected series;
- request target resolution or maximum points;
- request raw, aggregate, or automatic resolution;
- query events in a time range;
- export a range as CSV or JSON;
- inspect storage health and retention statistics.

A range query should report the actual resolution used, returned point count, time coverage, truncation/overflow state, and known gaps. It must never silently replace missing intervals with interpolated measurements.

A graph request should send start/end UTC timestamps, selected series, a point budget, and a resolution preference. The server should downsample to a bounded result, preserving min/max envelopes and event locations. The browser should not download a month's raw data to draw a few hundred pixels.

The system management interface on port 8080 could provide a generic telemetry/event viewer. An application dashboard on port 80 can query the same service through an application-owned same-origin route or a shared in-process interface. Do not create a dependency on a public CDN for core UI assets; local operation must work without Internet access. A charting library, if used, should be bundled with the firmware or served locally.

## 5. System events and diagnostics

ESP System should emit common events into the same time-indexed history:
- boot, reset cause, brownout/watchdog and wake reason;
- uptime checkpoints and firmware/build identity;
- Wi-Fi/other transport connect, disconnect, RSSI/link quality, suspend/resume;
- OTA check, candidate, deferral, installation, rollback and health result;
- clock synchronization and time-quality changes;
- storage mount, write, recovery, compaction and capacity warnings;
- power-mode transitions and reasons.

Applications add domain events: compressor transition, condenser trip, sensor failure, lamp activation, occupancy event, charge/discharge threshold, etc.

A support bundle can later combine firmware metadata, recent system events, reset diagnostics, telemetry health and application-selected diagnostic details. Never log secrets or credentials.

## 6. Solar-lamp use case: power and networking are foundational

A multifunctional solar lamp is not simply a small always-on Wi-Fi controller. Candidate series include panel voltage/current/power, battery voltage/current/state-of-charge estimate, charge-controller state, enclosure/LED-driver temperature, ambient lux, presence/motion, LED current/PWM/dimming state, and energy consumed/harvested. Events include illumination transitions, occupancy, charge/discharge thresholds, low-battery load shedding, sensor faults, and network synchronization.

This suggests several reusable system directions.

### 6.1 Power-aware lifecycle service

Provide common primitives for active, idle, light-sleep and deep-sleep states; wake reason capture; scheduled wake deadlines; peripheral/radio coordination; and safe transition hooks.

Applications own the policy: what may be switched off, what minimum battery reserve is required, whether a motion event justifies waking, and which loads to shed. ESP System coordinates and reports the lifecycle; it does not decide how bright a lamp should be.

ESP32-C6 supports timer and GPIO wake sources. In deep sleep most digital peripherals and CPU state are powered down, so boot-style restoration is required. Wi-Fi and Bluetooth connections are not maintained through deep sleep. Light-sleep preserves more runtime state, and ESP-IDF power management can coordinate dynamic frequency scaling and automatic light sleep. See [ESP-IDF sleep modes](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/system/sleep_modes.html) and [power management](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/system/power_management.html).

### 6.2 Energy-aware maintenance and OTA

Add an application-supplied update-eligibility policy: minimum battery reserve, external power/charging state, safe load state, temperature bounds, or operator-approved maintenance window. An update that is available but currently unsafe should be deferred and reported, not repeatedly attempted.

An OTA lifecycle should tolerate intermittent connectivity, schedule windows, and enough time to complete transfer, verification, installation, reboot and health confirmation. Delta OTA over a constrained transport may be useful later; it should not be mixed into the first telemetry implementation.

### 6.3 Store-and-forward, not permanent connectivity

A sleepy node can collect observations locally, then batch-send them when a gateway or parent is available. Records need stable device identity, sequence numbers, UTC time where known, and a monotonic fallback when wall-clock time is unavailable. The receiver should deduplicate batches and acknowledge accepted sequence ranges.

Do not assume the node can provide a continuously available web dashboard while in deep sleep. A mains-powered gateway or another always-on node may provide the user interface and aggregate history.

### 6.4 Transport and mesh roles are distinct

Keep telemetry independent of Wi-Fi, BLE, Zigbee, Thread, or a future transport. The current UpdateSource seam is a useful pattern, but the whole current System profile remains Wi-Fi-oriented; a Zigbee update source alone would not make a Zigbee-only device profile.

For Zigbee, distinguish always-on routers from sleepy end devices. Espressif's Arduino Zigbee documentation recommends router role for mains-powered devices and identifies the end-device role as typical for battery-powered devices that can sleep. A sleepy end device needs a parent and keep-alive/polling policy; it should not be treated as a mesh router. See [Arduino-ESP32 Zigbee roles](https://docs.espressif.com/projects/arduino-esp32/en/latest/zigbee/zigbee_core.html) and the [ESP Zigbee SDK sleepy-device FAQ](https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32c6/faq.html).

This suggests a likely lamp topology: some mains-powered gateway/router lamps, plus battery-powered sleepy sensor/lamp nodes. Router duty should not be enabled merely because every lamp has a radio.

### 6.5 Energy-aware telemetry

A low-power device should batch flash writes and network transfers, keep a small local queue, and flush when the energy/network budget allows. Event-triggered samples can temporarily increase resolution around interesting changes. A policy may reduce ordinary sampling under low battery while preserving safety-critical fault and cutoff events.

## 7. Recommended implementation stages

1. **Finish current Wi-Fi radio suspension work and hardware validation.** Keep it separate from telemetry. The existing ESP System and cold-storage pull requests are still draft; the latter builds on both partition targets, but hardware behavior still needs confirmation.
2. **Agree the semantic model and query contract.** Write API-level types and host-test aggregation/merge behavior. Do not choose exact flash bytes or resize partitions yet.
3. **Build a storage prototype with synthetic data.** Measure bytes per series/sample/event, compaction amplification, RAM use, power-loss recovery, and month-equivalent retention on 4 MiB and 8 MiB budgets.
4. **Integrate with cold storage.** Add acquisition timestamps for ADC1, use existing ADC2 acquisition timestamps, log only new observations when due, emit refrigeration/system events, and use range queries to power a zoomable chart.
5. **Abstract and reuse.** Move the proven store/query engine into ESP System; add generic system diagnostics and a generic management viewer. Keep cold-storage series and chart semantics in the application.
6. **Use a solar-lamp prototype to test the second profile.** Add power lifecycle, wake diagnostics, store-forward batching and power-aware OTA after the always-on cold-storage profile works.

## 8. Explicit non-decisions

This RFC does not select:
- a fixed C++ ABI or final public class names;
- a specific on-flash encoding (custom binary, tagged records, CBOR, etc.);
- LittleFS versus a raw partition/ring log as the final backend;
- a final 4 MiB partition resize;
- fixed retention horizons or compaction ratios;
- a universal graph layout;
- Wi-Fi versus Zigbee/Thread as a universal transport;
- a full cloud/fleet management service.

These should be decided by a minimal prototype and measurements, not by guessing from one application's current data volume.

## 9. Acceptance criteria for the first telemetry release

- Applications can register series and write typed observations/events without knowing the storage backend.
- Acquisition timestamp, quality and optional uncertainty are represented without forcing those fields into every record.
- Duplicate/cached observations are distinguishable from new measurements.
- Aggregation statistics merge correctly across multiple compaction levels, including irregular sample counts.
- Boolean/state/counter series use appropriate summaries rather than numeric means.
- Range queries are bounded and return metadata about resolution and gaps.
- Interrupted writes/compaction recover without silently formatting history.
- Storage health and retained time range are observable.
- Cold-storage history can be visualized across at least 30 days within the measured partition budget.
- Application code remains responsible for physical sampling, refrigeration policy and lamp behavior.
