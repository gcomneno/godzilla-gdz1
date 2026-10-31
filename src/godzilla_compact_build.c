#include "godzilla_compact_build.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(CHAR_BIT == 8, "GDZ1 requires eight-bit bytes");
_Static_assert(sizeof(uint64_t) == 8, "GDZ1 requires exact uint64_t");
_Static_assert(SIZE_MAX >= UINT32_MAX, "GDZ1 requires 32-bit size_t");

/* Private C11-compatible completion; keep identical to godzilla_compact.c.
 * Sequential reads borrow the existing arrays under the base lifetime contract.
 * No layout or array pointer is exposed through the public headers. */
struct GdzCompact {
    uint64_t total_ticks;
    size_t count;
    uint64_t *ticks;
    uint64_t *values;
};

/* Operation-local borrowed view, never a second owned sample representation.
 * An edit overlays a single sample on an ascending immutable source. */
typedef enum { UNCHANGED, INSERT, UPDATE } Edit;
typedef struct {
    const GdzSample *samples;
    const GdzCompact *base;
    uint64_t total;
    size_t count;
    Edit edit;
    GdzSample changed;
} Source;

typedef struct {
    const Source *source;
    size_t position;
    int inserted;
} Scan;

typedef struct {
    size_t index_bytes, length;
    unsigned mode;
} Plan;

static GdzCompactResult select_limits(const GdzCompactBuildLimits *limits,
                                      GdzCompactBuildLimits *selected)
{
    *selected = (GdzCompactBuildLimits){{100000, 100000, 975021, 1600256},
                                       2575277};
    if (limits) *selected = *limits;
    const GdzCompactLimits *c = &selected->compact;
    if (!c->max_ticks || c->max_ticks > 100000 || c->max_samples > 100000 ||
        !c->max_input_bytes || c->max_input_bytes > 975021 ||
        !c->max_alloc_bytes || c->max_alloc_bytes > 1600256 ||
        !selected->max_work_bytes || selected->max_work_bytes > 2575277)
        return GDZ_COMPACT_INVALID_ARGUMENT;
    return GDZ_COMPACT_OK;
}

static Scan start_scan(const Source *source)
{
    Scan scan = {0};
    scan.source = source;
    return scan;
}

/* Visit only present samples, once in order, without range searches or staging
 * copies. An inserted sample does not consume the current base position.
 * Planning validates geometry; stable inputs and synchronized base lifetime
 * make the writing scan equally infallible, with no allocation or decoding. */
static int next_sample(Scan *scan, GdzSample *sample)
{
    const Source *s = scan->source;
    size_t count = s->base ? s->base->count : s->count;
    int present = scan->position < count;
    if (s->edit == INSERT && !scan->inserted &&
        (!present || s->changed.tick < s->base->ticks[scan->position])) {
        *sample = s->changed;
        scan->inserted = 1;
        return 1;
    }
    if (!present) return 0;
    if (s->base)
        *sample = (GdzSample){s->base->ticks[scan->position],
                              s->base->values[scan->position]};
    else *sample = s->samples[scan->position];
    ++scan->position;
    if (s->edit == UPDATE && sample->tick == s->changed.tick)
        sample->bits = s->changed.bits;
    return 1;
}

/* Called only for nonzero XOR. Loops never shift by 64, including width 64. */
static void xor_window(uint64_t value, unsigned *leading, unsigned *trailing,
                       unsigned *width)
{
    unsigned high = 0, low = 0;
    uint64_t probe = value;
    while (!(probe & (UINT64_C(1) << 63))) { ++high; probe <<= 1; }
    probe = value;
    while (!(probe & 1u)) { ++low; probe >>= 1; }
    *leading = high;
    *trailing = low;
    *width = 64u - high - low;
}

static GdzCompactResult plan_encoding(const Source *source, Plan *plan)
{
    size_t sparse = 0, bits = 32, count = 0;
    uint64_t previous_tick = 0, previous_bits = 0;
    Scan scan = start_scan(source);
    GdzSample sample;
    if (!source->total || source->total > 100000)
        return GDZ_COMPACT_INVALID_RANGE;
    if (source->count > source->total) return GDZ_COMPACT_INVALID_ARGUMENT;
    while (next_sample(&scan, &sample)) {
        size_t token_bits;
        unsigned leading, trailing, width;
        if (!sample.tick || sample.tick > source->total)
            return GDZ_COMPACT_INVALID_RANGE;
        if (sample.tick <= previous_tick) return GDZ_COMPACT_INVALID_ARGUMENT;
        uint64_t delta = sample.tick - previous_tick;
        do {
            if (sparse == SIZE_MAX) return GDZ_COMPACT_OVERFLOW;
            ++sparse;
            delta >>= 7;
        } while (delta);
        if (!count) token_bits = 64;
        else if (sample.bits == previous_bits) token_bits = 1;
        else {
            xor_window(sample.bits ^ previous_bits, &leading, &trailing, &width);
            token_bits = 13u + width;
        }
        if (bits > SIZE_MAX - token_bits) return GDZ_COMPACT_OVERFLOW;
        bits += token_bits;
        if (count == SIZE_MAX) return GDZ_COMPACT_OVERFLOW;
        ++count;
        previous_tick = sample.tick;
        previous_bits = sample.bits;
    }
    if (count != source->count) return GDZ_COMPACT_INVALID_ARGUMENT;
    size_t bitmap = (size_t)(source->total / 8 + (source->total % 8 != 0));
    plan->mode = bitmap <= sparse ? 0u : 1u;
    plan->index_bytes = bitmap <= sparse ? bitmap : sparse;
    size_t value_bytes = bits / 8 + (bits % 8 != 0);
    if (plan->index_bytes > UINT32_MAX || count > UINT32_MAX ||
        plan->index_bytes > SIZE_MAX - 18 ||
        value_bytes > SIZE_MAX - 18 - plan->index_bytes)
        return GDZ_COMPACT_OVERFLOW;
    plan->length = 18 + plan->index_bytes + value_bytes;
    return GDZ_COMPACT_OK;
}

static void put_be(unsigned char *buffer, uint64_t value, unsigned bytes)
{
    for (unsigned i = bytes; i > 0; --i) {
        buffer[i - 1] = (unsigned char)(value & 255u);
        value >>= 8;
    }
}

static void put_bits(unsigned char *buffer, size_t *position, uint64_t value,
                     unsigned width)
{
    for (unsigned i = width; i > 0; --i) {
        size_t bit = (*position)++;
        if ((value >> (i - 1)) & 1u)
            buffer[bit / 8] |= (unsigned char)(1u << (7 - bit % 8));
    }
}

static void write_encoding(const Source *source, const Plan *plan,
                           unsigned char *buffer)
{
    Scan scan = start_scan(source);
    GdzSample sample;
    size_t index_position = 0, bit_position = 32, count = 0;
    uint64_t previous_tick = 0, previous_bits = 0;
    unsigned char *index = buffer + 18;
    unsigned char *values = index + plan->index_bytes;
    memset(buffer, 0, plan->length); /* includes unused bitmap/padding bits */
    memcpy(buffer, "GDZ1", 4);
    buffer[4] = 1;
    buffer[5] = (unsigned char)plan->mode;
    put_be(buffer + 6, source->total, 8);
    put_be(buffer + 14, (uint64_t)plan->index_bytes, 4);
    put_be(values, (uint64_t)source->count, 4);
    while (next_sample(&scan, &sample)) {
        if (plan->mode == 0) {
            size_t bit = (size_t)(sample.tick - 1);
            index[bit / 8] |= (unsigned char)(1u << (7 - bit % 8));
        } else {
            uint64_t delta = sample.tick - previous_tick;
            do {
                unsigned byte = (unsigned)(delta & 127u);
                delta >>= 7;
                index[index_position++] = (unsigned char)(byte | (delta ? 128u : 0u));
            } while (delta);
        }
        if (!count) put_bits(values, &bit_position, sample.bits, 64);
        else {
            uint64_t difference = sample.bits ^ previous_bits;
            put_bits(values, &bit_position, difference != 0, 1);
            if (difference) {
                unsigned leading, trailing, width;
                xor_window(difference, &leading, &trailing, &width);
                put_bits(values, &bit_position, leading, 6);
                put_bits(values, &bit_position, width - 1u, 6);
                put_bits(values, &bit_position, difference >> trailing, width);
            }
        }
        ++count;
        previous_tick = sample.tick;
        previous_bits = sample.bits;
    }
}

static GdzCompactResult build(const Source *source,
                              const GdzCompactBuildLimits *limits,
                              GdzCompact **out)
{
    Plan plan;
    GdzCompactLimits candidate = limits->compact;
    if (!source->total || source->total > 100000)
        return GDZ_COMPACT_INVALID_RANGE;
    if (source->total > candidate.max_ticks || source->count > candidate.max_samples)
        return GDZ_COMPACT_LIMIT;
    GdzCompactResult result = plan_encoding(source, &plan);
    if (result != GDZ_COMPACT_OK) return result;
    if (plan.length > candidate.max_input_bytes || plan.length >= limits->max_work_bytes)
        return GDZ_COMPACT_LIMIT;
    size_t remaining = limits->max_work_bytes - plan.length;
    if (candidate.max_alloc_bytes > remaining) candidate.max_alloc_bytes = remaining;
    unsigned char *payload = malloc(plan.length);
    if (!payload) return GDZ_COMPACT_OOM;
    write_encoding(source, &plan, payload);
    result = gdz_compact_from_gdz1(payload, plan.length, &candidate, out);
    free(payload);
    return result;
}

GdzCompactResult gdz_compact_from_samples(
    uint64_t total_ticks, const GdzSample *samples, size_t count,
    const GdzCompactBuildLimits *limits, GdzCompact **out)
{
    GdzCompactBuildLimits selected;
    if (!out || *out || (!samples && count)) return GDZ_COMPACT_INVALID_ARGUMENT;
    GdzCompactResult result = select_limits(limits, &selected);
    if (result != GDZ_COMPACT_OK) return result;
    Source source = {samples, NULL, total_ticks, count, UNCHANGED, {0, 0}};
    return build(&source, &selected, out);
}

GdzCompactResult gdz_compact_encode_gdz1(
    const GdzCompact *compact, unsigned char *buffer, size_t capacity,
    size_t *required)
{
    GdzCompactInfo info;
    Plan plan;
    if (!compact || !required || (!buffer && capacity))
        return GDZ_COMPACT_INVALID_ARGUMENT;
    GdzCompactResult result = gdz_compact_info(compact, &info);
    if (result != GDZ_COMPACT_OK) return result;
    Source source = {NULL, compact, info.total_ticks, info.count, UNCHANGED, {0, 0}};
    result = plan_encoding(&source, &plan);
    if (result != GDZ_COMPACT_OK) return result;
    if (buffer && capacity < plan.length) {
        *required = plan.length;
        return GDZ_COMPACT_CAPACITY;
    }
    if (buffer) write_encoding(&source, &plan, buffer);
    *required = plan.length;
    return GDZ_COMPACT_OK;
}

static GdzCompactResult edit_base(
    const GdzCompact *base, uint64_t tick, uint64_t bits,
    const GdzCompactBuildLimits *limits, GdzCompact **out, Edit edit)
{
    GdzCompactBuildLimits selected;
    GdzCompactInfo info;
    uint64_t old_bits;
    if (!base || !out || *out) return GDZ_COMPACT_INVALID_ARGUMENT;
    GdzCompactResult result = select_limits(limits, &selected);
    if (result != GDZ_COMPACT_OK) return result;
    result = gdz_compact_info(base, &info);
    if (result != GDZ_COMPACT_OK) return result;
    result = gdz_compact_get(base, tick, &old_bits);
    if (edit == INSERT) {
        if (result == GDZ_COMPACT_OK) return GDZ_COMPACT_INVALID_ARGUMENT;
        if (result != GDZ_COMPACT_NOT_FOUND) return result;
        if (info.count == SIZE_MAX) return GDZ_COMPACT_OVERFLOW;
        ++info.count;
    } else if (result != GDZ_COMPACT_OK) return result;
    Source source = {NULL, base, info.total_ticks, info.count, edit, {tick, bits}};
    return build(&source, &selected, out);
}

GdzCompactResult gdz_compact_insert(
    const GdzCompact *base, uint64_t tick, uint64_t bits,
    const GdzCompactBuildLimits *limits, GdzCompact **out)
{
    return edit_base(base, tick, bits, limits, out, INSERT);
}

GdzCompactResult gdz_compact_update(
    const GdzCompact *base, uint64_t tick, uint64_t bits,
    const GdzCompactBuildLimits *limits, GdzCompact **out)
{
    return edit_base(base, tick, bits, limits, out, UPDATE);
}
