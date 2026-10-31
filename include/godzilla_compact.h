#ifndef GODZILLA_COMPACT_H
#define GODZILLA_COMPACT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Increment A. Provisional interface; production readiness NOT CERTIFIED.
 * COMMIT DISABLED. No database, integrity certification or authorization. */
typedef struct GdzCompact GdzCompact;

typedef enum {
    GDZ_COMPACT_OK = 0,
    GDZ_COMPACT_INVALID_ARGUMENT,
    GDZ_COMPACT_NOT_FOUND,
    GDZ_COMPACT_INVALID_RANGE,
    GDZ_COMPACT_FORMAT,
    GDZ_COMPACT_TRUNCATED,
    GDZ_COMPACT_NONCANONICAL_INDEX,
    GDZ_COMPACT_LIMIT,
    GDZ_COMPACT_OVERFLOW,
    GDZ_COMPACT_OOM,
    GDZ_COMPACT_CAPACITY
} GdzCompactResult;

typedef struct {
    uint64_t total_ticks;
    size_t count;
} GdzCompactInfo;

typedef struct {
    uint64_t tick;
    uint64_t bits;
} GdzSample;

typedef struct {
    uint64_t max_ticks;
    size_t max_samples;
    size_t max_input_bytes;
    size_t max_alloc_bytes;
} GdzCompactLimits;

/* All output storage must be live, aligned and disjoint from other outputs,
 * the model and call inputs. Invalid non-NULL pointers are caller errors.
 * *out must initially be NULL. Failure leaves it NULL and frees partial state.
 * data and limits are borrowed, stable until return; success owns the decoded
 * model independently of them. NULL data is allowed only with length zero
 * (TRUNCATED). NULL limits selects {100000,100000,975021,1600256}; supplied
 * limits may only restrict defaults, with positive ceilings except samples. */
GdzCompactResult gdz_compact_from_gdz1(
    const unsigned char *data, size_t length,
    const GdzCompactLimits *limits, GdzCompact **out);

/* Required outputs and handle must be non-NULL. Errors preserve outputs. */
GdzCompactResult gdz_compact_info(
    const GdzCompact *compact, GdzCompactInfo *out);
GdzCompactResult gdz_compact_get(
    const GdzCompact *compact, uint64_t tick, uint64_t *bits);

/* Inclusive local interval, 1 <= first <= last <= total_ticks. Capacity is
 * in samples. NULL/0 queries the exact count with OK. NULL/positive is invalid.
 * CAPACITY publishes only required; the buffer remains entirely unchanged.
 * Success copies only matching samples, ordered; other errors preserve outputs.
 * Immutable reads can run concurrently with disjoint caller outputs. */
GdzCompactResult gdz_compact_range(
    const GdzCompact *compact, uint64_t first, uint64_t last,
    GdzSample *buffer, size_t capacity, size_t *required);

/* Caller synchronizes destruction with all reads and aliases. Releases memory,
 * clears this owner slot; aliases become invalid. NULL *owner is idempotent OK;
 * NULL owner slot is INVALID_ARGUMENT. Reads/close allocate no memory. */
GdzCompactResult gdz_compact_close(GdzCompact **owner);

#ifdef __cplusplus
}
#endif
#endif
