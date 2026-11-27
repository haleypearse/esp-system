#include "NumericAggregate.h"

#include <cmath>

namespace EspSystem {
namespace Telemetry {
namespace {

bool sameClockDomain(const Timestamp& a, const Timestamp& b) {
    if (a.utcValid && b.utcValid) return true;
    return !a.utcValid && !b.utcValid && a.bootId != 0 && a.bootId == b.bootId;
}

uint64_t orderMillis(const Timestamp& timestamp) {
    return timestamp.utcValid
        ? static_cast<uint64_t>(timestamp.utcMillis)
        : timestamp.monotonicMillis;
}

bool isInvalid(uint16_t flags) {
    return hasQuality(flags, Quality::Invalid) ||
           hasQuality(flags, Quality::Stale) ||
           hasQuality(flags, Quality::Saturated) ||
           hasQuality(flags, Quality::SensorFault);
}

} // namespace

double NumericAggregateSummary::sampleVariance() const {
    return validCount > 1 ? m2 / static_cast<double>(validCount - 1) : 0.0;
}

double NumericAggregateSummary::sampleStandardDeviation() const {
    return std::sqrt(sampleVariance());
}

uint64_t NumericAggregateSummary::observedSpanMillis() const {
    if (!hasObservations) return 0;
    const uint64_t first = orderMillis(firstObservationAt);
    const uint64_t last = orderMillis(lastObservationAt);
    return last >= first ? last - first : 0;
}

bool NumericAggregate::add(const NumericObservation& observation) {
    const uint64_t time = orderMillis(observation.acquiredAt);
    if (summary_.hasObservations) {
        if (!sameClockDomain(summary_.lastObservationAt, observation.acquiredAt)) return false;
        const uint64_t previous = orderMillis(summary_.lastObservationAt);
        if (time < previous) return false;
        const uint64_t gap = time - previous;
        if (gap > summary_.maxGapMillis) summary_.maxGapMillis = gap;
    }

    if (!summary_.hasObservations) {
        summary_.hasObservations = true;
        summary_.firstObservationAt = observation.acquiredAt;
    }
    summary_.lastObservationAt = observation.acquiredAt;
    ++summary_.observationCount;

    if (hasQuality(observation.qualityFlags, Quality::Suspect)) ++summary_.suspectCount;
    if (hasQuality(observation.qualityFlags, Quality::Estimated)) ++summary_.estimatedCount;

    if (isInvalid(observation.qualityFlags) || !std::isfinite(observation.value)) {
        ++summary_.invalidCount;
        return true;
    }

    const uint64_t n = summary_.validCount;
    if (n == 0) {
        summary_.hasValidValues = true;
        summary_.firstValidAt = observation.acquiredAt;
        summary_.firstValue = observation.value;
        summary_.minimum = observation.value;
        summary_.maximum = observation.value;
        summary_.minimumAt = observation.acquiredAt;
        summary_.maximumAt = observation.acquiredAt;
        summary_.mean = observation.value;
        summary_.m2 = 0.0;
    } else {
        if (observation.value < summary_.minimum) {
            summary_.minimum = observation.value;
            summary_.minimumAt = observation.acquiredAt;
        }
        if (observation.value > summary_.maximum) {
            summary_.maximum = observation.value;
            summary_.maximumAt = observation.acquiredAt;
        }

        // Welford's numerically stable online variance update.
        const double delta = observation.value - summary_.mean;
        const double nextMean = summary_.mean + delta / static_cast<double>(n + 1);
        summary_.m2 += delta * (observation.value - nextMean);
        summary_.mean = nextMean;
    }

    ++summary_.validCount;
    summary_.lastValue = observation.value;
    summary_.lastValidAt = observation.acquiredAt;
    return true;
}

bool NumericAggregate::merge(const NumericAggregate& later) {
    const auto& rhs = later.summary_;
    if (!rhs.hasObservations) return true;
    if (!summary_.hasObservations) {
        summary_ = rhs;
        return true;
    }

    if (!sameClockDomain(summary_.lastObservationAt, rhs.firstObservationAt)) return false;
    const uint64_t leftEnd = orderMillis(summary_.lastObservationAt);
    const uint64_t rightStart = orderMillis(rhs.firstObservationAt);
    if (rightStart < leftEnd) return false;

    NumericAggregateSummary merged = summary_;
    const uint64_t boundaryGap = rightStart - leftEnd;
    if (boundaryGap > merged.maxGapMillis) merged.maxGapMillis = boundaryGap;
    if (rhs.maxGapMillis > merged.maxGapMillis) merged.maxGapMillis = rhs.maxGapMillis;

    merged.observationCount += rhs.observationCount;
    merged.validCount += rhs.validCount;
    merged.invalidCount += rhs.invalidCount;
    merged.suspectCount += rhs.suspectCount;
    merged.estimatedCount += rhs.estimatedCount;
    merged.lastObservationAt = rhs.lastObservationAt;

    if (rhs.hasValidValues) {
        if (!merged.hasValidValues) {
            merged.hasValidValues = true;
            merged.firstValidAt = rhs.firstValidAt;
            merged.lastValidAt = rhs.lastValidAt;
            merged.firstValue = rhs.firstValue;
            merged.lastValue = rhs.lastValue;
            merged.minimum = rhs.minimum;
            merged.maximum = rhs.maximum;
            merged.minimumAt = rhs.minimumAt;
            merged.maximumAt = rhs.maximumAt;
            merged.mean = rhs.mean;
            merged.m2 = rhs.m2;
        } else {
            // Chan's parallel-variance merge. It correctly weights unequal
            // bucket sizes; averaging the two bucket means would be incorrect.
            const uint64_t leftN = summary_.validCount;
            const uint64_t rightN = rhs.validCount;
            const uint64_t totalN = leftN + rightN;
            const double delta = rhs.mean - summary_.mean;
            merged.mean = summary_.mean + delta *
                (static_cast<double>(rightN) / static_cast<double>(totalN));
            merged.m2 = summary_.m2 + rhs.m2 + delta * delta *
                (static_cast<double>(leftN) * static_cast<double>(rightN) /
                 static_cast<double>(totalN));

            merged.lastValidAt = rhs.lastValidAt;
            merged.lastValue = rhs.lastValue;
            if (rhs.minimum < merged.minimum) {
                merged.minimum = rhs.minimum;
                merged.minimumAt = rhs.minimumAt;
            }
            if (rhs.maximum > merged.maximum) {
                merged.maximum = rhs.maximum;
                merged.maximumAt = rhs.maximumAt;
            }
        }
    }

    summary_ = merged;
    return true;
}

} // namespace Telemetry
} // namespace EspSystem
