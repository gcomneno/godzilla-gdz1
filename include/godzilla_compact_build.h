#ifndef GODZILLA_COMPACT_BUILD_H
#define GODZILLA_COMPACT_BUILD_H

#include "godzilla_compact.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Increment B: isolated construction, canonical encoding and immutable edits.
 * Production readiness NOT CERTIFIED; production COMMIT DISABLED.
 * A remains the sole decoded owner. All A pointer/lifetime rules apply.
 * Inputs are borrowed and stable until return; no output may overlap inputs.
 * Supplied ceilings only restrict defaults; max_work_bytes is positive.
 * Work counts requested temporary payload + candidate A heap bytes, excluding
 * the base, caller storage, allocator overhead and runtime allocations. */
typedef struct {
    GdzCompactLimits compact;
    size_t max_work_bytes;
} GdzCompactBuildLimits;

/* NULL limits: compact={100000,100000,975021,1600256}, work=2575277.
 * *out must initially be NULL. Publish only a complete independent A owner.
 * Empty input permits NULL samples. Ticks must be strictly ascending, unique
 * and in 1..total_ticks; extent must be in 1..100000. Invalid geometry/ticks:
 * INVALID_RANGE; duplicate/reversed ordering: INVALID_ARGUMENT.
 * Limits, allocation failures and invalid input leave *out unchanged. */
GdzCompactResult gdz_compact_from_samples(
    uint64_t total_ticks, const GdzSample *samples, size_t count,
    const GdzCompactBuildLimits *limits, GdzCompact **out);

/* Heap-free canonical GDZ1 encoding. NULL/0 queries exact required with OK.
 * NULL/positive is INVALID_ARGUMENT. CAPACITY publishes exact required but
 * leaves the entire buffer unchanged. Success writes exactly required bytes;
 * bytes beyond that remain unchanged. All other errors preserve both outputs.
 * Buffer/required must be disjoint from each other and the live model.
 * Legacy noncanonical GXOR64 bytes/hashes are not preserved by re-encoding. */
GdzCompactResult gdz_compact_encode_gdz1(
    const GdzCompact *compact, unsigned char *buffer, size_t capacity,
    size_t *required);

/* Base stays immutable on every path. Insert requires an absent in-domain tick
 * (occupied: INVALID_ARGUMENT); update requires a present tick (NOT_FOUND).
 * Same-bits update also produces an independent owner. Caller synchronizes
 * base destruction with every operation, including concurrent edits/encoding. */
GdzCompactResult gdz_compact_insert(
    const GdzCompact *base, uint64_t tick, uint64_t bits,
    const GdzCompactBuildLimits *limits, GdzCompact **out);
GdzCompactResult gdz_compact_update(
    const GdzCompact *base, uint64_t tick, uint64_t bits,
    const GdzCompactBuildLimits *limits, GdzCompact **out);

#ifdef __cplusplus
}
#endif
#endif
