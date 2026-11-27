#include "telemetry/NumericAggregate.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace EspSystem::Telemetry;

namespace {
int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #expr "\n"; \
        ++failures; \
    } \
} while (0)

Timestamp stamp(uint64_t ms, uint32_t boot = 1) {
    Timestamp t;
    t.monotonicMillis = ms;
    t.bootId = boot;
    return t;
}

NumericObservation sample(double value, uint64_t ms, uint16_t quality = 0) {
    NumericObservation o;
    o.seriesId = 1;
    o.value = value;
    o.acquiredAt = stamp(ms);
    o.qualityFlags = quality;
    return o;
}

void testBasicStatistics() {
    NumericAggregate a;
    CHECK(a.add(sample(4.0, 1000)));
    CHECK(a.add(sample(5.0, 2000)));
    CHECK(a.add(sample(6.0, 3000)));
    const auto& s = a.summary();
    CHECK(s.observationCount == 3);
    CHECK(s.validCount == 3);
    CHECK(s.invalidCount == 0);
    CHECK(std::fabs(s.mean - 5.0) < 1e-12);
    CHECK(std::fabs(s.sampleVariance() - 1.0) < 1e-12);
    CHECK(std::fabs(s.sampleStandardDeviation() - 1.0) < 1e-12);
    CHECK(s.minimum == 4.0 && s.maximum == 6.0);
    CHECK(s.minimumAt.monotonicMillis == 1000);
    CHECK(s.maximumAt.monotonicMillis == 3000);
    CHECK(s.maxGapMillis == 1000);
    CHECK(s.hasSampleVariance());
    CHECK(s.observedSpanMillis() == 2000);
}

void testInvalidQualityAndNonFinite() {
    NumericAggregate a;
    CHECK(a.add(sample(2.0, 1000)));
    CHECK(a.add(sample(100.0, 2000, qualityBits(Quality::Stale))));
    CHECK(a.add(sample(NAN, 3000)));
    CHECK(a.add(sample(4.0, 4000, qualityBits(Quality::Suspect) |
                                  qualityBits(Quality::Estimated))));
    const auto& s = a.summary();
    CHECK(s.observationCount == 4);
    CHECK(s.validCount == 2);
    CHECK(s.invalidCount == 2);
    CHECK(s.suspectCount == 1);
    CHECK(s.estimatedCount == 1);
    CHECK(s.mean == 3.0);
    CHECK(s.minimum == 2.0 && s.maximum == 4.0);
}

void testMergeUnequalBuckets() {
    NumericAggregate whole;
    NumericAggregate first;
    NumericAggregate second;
    const double values[] = {1, 2, 3, 4, 5, 6};
    for (uint64_t i = 0; i < 6; ++i) {
        CHECK(whole.add(sample(values[i], 1000 + i * 1000)));
        if (i < 2) CHECK(first.add(sample(values[i], 1000 + i * 1000)));
        else CHECK(second.add(sample(values[i], 1000 + i * 1000)));
    }
    CHECK(first.merge(second));
    const auto& a = first.summary();
    const auto& b = whole.summary();
    CHECK(a.observationCount == b.observationCount);
    CHECK(a.validCount == b.validCount);
    CHECK(std::fabs(a.mean - b.mean) < 1e-12);
    CHECK(std::fabs(a.sampleVariance() - b.sampleVariance()) < 1e-12);
    CHECK(a.minimum == b.minimum && a.maximum == b.maximum);
    CHECK(a.firstValue == b.firstValue && a.lastValue == b.lastValue);
    CHECK(a.maxGapMillis == b.maxGapMillis);
}

void testInvalidOnlyBucketMerge() {
    NumericAggregate left;
    NumericAggregate middle;
    NumericAggregate right;
    CHECK(left.add(sample(10.0, 1000)));
    CHECK(middle.add(sample(0.0, 2000, qualityBits(Quality::Invalid))));
    CHECK(middle.add(sample(0.0, 3000, qualityBits(Quality::SensorFault))));
    CHECK(right.add(sample(20.0, 4000)));
    CHECK(left.merge(middle));
    CHECK(left.merge(right));
    const auto& s = left.summary();
    CHECK(s.observationCount == 4);
    CHECK(s.validCount == 2);
    CHECK(s.invalidCount == 2);
    CHECK(s.mean == 15.0);
    CHECK(s.maxGapMillis == 1000);
}

void testRejectOutOfOrderAndOverlappingMerge() {
    NumericAggregate a;
    CHECK(a.add(sample(1.0, 2000)));
    CHECK(!a.add(sample(2.0, 1000)));

    NumericAggregate b;
    CHECK(b.add(sample(3.0, 1500)));
    CHECK(!a.merge(b));
    CHECK(a.summary().observationCount == 1);
}

void testClockDomains() {
    NumericAggregate a;
    CHECK(a.add(sample(1.0, 1000, 0)));
    auto otherBoot = sample(2.0, 2000);
    otherBoot.acquiredAt.bootId = 2;
    CHECK(!a.add(otherBoot));
    auto unknownBoot = sample(3.0, 3000);
    unknownBoot.acquiredAt.bootId = 0;
    CHECK(!a.add(unknownBoot));

    NumericAggregate utc;
    auto u1 = sample(1.0, 1000);
    u1.acquiredAt.utcValid = true;
    u1.acquiredAt.utcMillis = 100000;
    CHECK(utc.add(u1));
    auto u2 = sample(2.0, 2000);
    u2.acquiredAt.utcValid = true;
    u2.acquiredAt.utcMillis = 101000;
    CHECK(utc.add(u2));
    CHECK(utc.summary().maxGapMillis == 1000);
}

} // namespace

int main() {
    testBasicStatistics();
    testInvalidQualityAndNonFinite();
    testMergeUnequalBuckets();
    testInvalidOnlyBucketMerge();
    testRejectOutOfOrderAndOverlappingMerge();
    testClockDomains();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All telemetry core tests passed\n";
    return EXIT_SUCCESS;
}
