#pragma once

#include <cstdint>

namespace EspSystem {
namespace Telemetry {

// Semantic types describe what a series means, not how it is serialized.
// Persistent encoding and public ABI decisions remain separate.
enum class ValueType : uint8_t {
    Float64 = 0,
    SignedInteger = 1,
    UnsignedInteger = 2,
    Boolean = 3,
    Enumeration = 4
};

enum class SeriesKind : uint8_t {
    Gauge = 0,          // A value observed at a point in time (temperature, voltage).
    State = 1,          // A condition that persists until changed (compressor on/off).
    Counter = 2,        // A cumulative value (energy, pulse count).
    Event = 3           // A discrete occurrence, not a continuously sampled value.
};

enum class AggregationKind : uint8_t {
    NumericStatistics = 0,
    StateDuration = 1,
    CounterDelta = 2,
    EnumDuration = 3,
    PreserveEvents = 4
};

// Quality is orthogonal to the value. SUSPECT and ESTIMATED readings may be
// retained as valid observations, but remain distinguishable in summaries.
enum class Quality : uint16_t {
    None = 0,
    Invalid = 1u << 0,
    Suspect = 1u << 1,
    Stale = 1u << 2,
    Estimated = 1u << 3,
    Saturated = 1u << 4,
    SensorFault = 1u << 5,
    TimeUncertain = 1u << 6
};

constexpr uint16_t qualityBits(Quality value) {
    return static_cast<uint16_t>(value);
}

constexpr Quality operator|(Quality lhs, Quality rhs) {
    return static_cast<Quality>(qualityBits(lhs) | qualityBits(rhs));
}

constexpr bool hasQuality(uint16_t flags, Quality flag) {
    return (flags & qualityBits(flag)) != 0;
}

// A timestamp can carry both wall-clock and monotonic information. Monotonic
// time is meaningful only within bootId. UTC is optional: devices may collect
// data before NTP or another clock source has established wall time.
struct Timestamp {
    int64_t utcMillis = 0;
    uint64_t monotonicMillis = 0;
    uint32_t bootId = 0;
    bool utcValid = false;
};

// Descriptors are registered once and stored separately from observations.
// String ownership/lifetime is left to the registry/backend layer.
struct SeriesDescriptor {
    uint16_t id = 0;                 // Stable within a retained data set.
    const char* key = nullptr;       // Machine-stable name, e.g. "cold.storage.temperature".
    const char* name = nullptr;      // Human-readable label.
    const char* unit = nullptr;      // e.g. "degC", "V", "%".
    ValueType valueType = ValueType::Float64;
    SeriesKind kind = SeriesKind::Gauge;
    AggregationKind aggregation = AggregationKind::NumericStatistics;
    bool eventDriven = false;
};

struct NumericObservation {
    uint16_t seriesId = 0;
    double value = 0.0;
    Timestamp acquiredAt{};
    uint64_t sequence = 0;
    uint16_t qualityFlags = qualityBits(Quality::None);
    bool hasSequence = false;
};

struct EventObservation {
    uint16_t eventId = 0;
    uint16_t code = 0;
    uint8_t severity = 0;
    Timestamp occurredAt{};
    uint32_t sourceId = 0;
    uint64_t sequence = 0;
    bool hasSequence = false;
};

} // namespace Telemetry
} // namespace EspSystem
