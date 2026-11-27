#pragma once

#include "TelemetryTypes.h"
#include <cstdint>

namespace EspSystem {
namespace Telemetry {

struct NumericAggregateSummary {
    uint64_t observationCount = 0;
    uint64_t validCount = 0;
    uint64_t invalidCount = 0;
    uint64_t suspectCount = 0;
    uint64_t estimatedCount = 0;

    bool hasObservations = false;
    bool hasValidValues = false;
    Timestamp firstObservationAt{};
    Timestamp lastObservationAt{};
    Timestamp firstValidAt{};
    Timestamp lastValidAt{};
    Timestamp minimumAt{};
    Timestamp maximumAt{};

    double firstValue = 0.0;
    double lastValue = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    double mean = 0.0;
    double m2 = 0.0; // Sum of squared deviations; mergeable without raw samples.

    uint64_t maxGapMillis = 0;

    bool hasSampleVariance() const { return validCount > 1; }
    double sampleVariance() const;
    double sampleStandardDeviation() const;
    uint64_t observedSpanMillis() const;
};

// A bounded-memory, mergeable numeric accumulator. add() expects observations
// in nondecreasing timestamp order. merge() expects a later, non-overlapping
// bucket from the same comparable clock domain.
class NumericAggregate {
public:
    bool add(const NumericObservation& observation);
    bool merge(const NumericAggregate& later);

    const NumericAggregateSummary& summary() const { return summary_; }
    void reset() { summary_ = NumericAggregateSummary{}; }

private:
    NumericAggregateSummary summary_{};
};

} // namespace Telemetry
} // namespace EspSystem
