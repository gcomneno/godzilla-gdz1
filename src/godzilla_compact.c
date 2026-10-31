#include "godzilla_compact.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* These requirements also make every uint32 wire length/count representable.
 * Parsing uses byte loads: no host endianness or input alignment assumption. */
_Static_assert(CHAR_BIT == 8, "GDZ1 requires eight-bit bytes");
_Static_assert(sizeof(uint64_t) == 8, "GDZ1 requires exact uint64_t");
_Static_assert(sizeof(uint32_t) == 4, "GDZ1 requires exact uint32_t");
_Static_assert(SIZE_MAX >= UINT32_MAX, "GDZ1 requires at least 32-bit size_t");

/* Private definition: keep identical to the completion in godzilla_compact_build.c. */
struct GdzCompact {
    uint64_t total_ticks;
    size_t count;
    uint64_t *ticks;
    uint64_t *values;
};

_Static_assert(sizeof(GdzCompact) <= 256, "owner exceeds A budget");

typedef struct {
    const unsigned char *data;
    size_t bits;
    size_t position;
} BitReader;

static uint64_t big_endian(const unsigned char *data, unsigned bytes)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i)
        value = (value << 8) | data[i];
    return value;
}

static GdzCompactResult read_bits(BitReader *reader, unsigned width,
                                  uint64_t *out)
{
    uint64_t value = 0;
    if (width > 64) return GDZ_COMPACT_OVERFLOW;
    if (reader->position > reader->bits ||
        width > reader->bits - reader->position) return GDZ_COMPACT_TRUNCATED;
    /* A NULL output validates the same extent without extracting payload bits. */
    if (!out) {
        reader->position += width;
        return GDZ_COMPACT_OK;
    }
    for (unsigned i = 0; i < width; ++i) {
        size_t bit = reader->position++;
        value = (value << 1) |
            ((uint64_t)(reader->data[bit / 8] >> (7 - bit % 8)) & 1u);
    }
    *out = value;
    return GDZ_COMPACT_OK;
}

static GdzCompactResult read_delta(const unsigned char *data, size_t length,
                                   size_t *position, uint64_t *out)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 10; ++i) {
        unsigned byte;
        if (*position == length) return GDZ_COMPACT_TRUNCATED;
        byte = data[(*position)++];
        /* Check the last byte before shifting/accumulating; continuation at
         * byte ten is already an overlong uint64, even without byte eleven. */
        if (i == 9 && byte > 1) return GDZ_COMPACT_OVERFLOW;
        value |= (uint64_t)(byte & 127u) << (7 * i);
        if (!(byte & 128u)) {
            if (i != 0 && byte == 0) return GDZ_COMPACT_NONCANONICAL_INDEX;
            *out = value;
            return GDZ_COMPACT_OK;
        }
    }
    return GDZ_COMPACT_OVERFLOW;
}

/* One bounded index pass serves validation/count and later direct model fill.
 * Extent <= 100000 bounds tick loops, count and sparse-byte arithmetic. */
static GdzCompactResult parse_index(const unsigned char *data, size_t length,
                                    unsigned mode, uint64_t total,
                                    uint64_t *ticks, size_t *out_count)
{
    size_t count = 0, sparse_bytes = 0;
    size_t bitmap_bytes = (size_t)(total / 8 + (total % 8 != 0));
    uint64_t previous = 0;
    if (mode == 0) {
        if (length != bitmap_bytes) return GDZ_COMPACT_FORMAT;
        for (uint64_t tick = 1; tick <= total; ++tick) {
            size_t bit = (size_t)(tick - 1);
            if (data[bit / 8] & (1u << (7 - bit % 8))) {
                uint64_t delta = tick - previous;
                if (ticks) ticks[count] = tick;
                ++count;
                do { ++sparse_bytes; delta >>= 7; } while (delta);
                previous = tick;
            }
        }
        unsigned unused = (unsigned)((8 - total % 8) % 8);
        if (unused && (data[length - 1] & ((1u << unused) - 1u)))
            return GDZ_COMPACT_FORMAT;
        if (length > sparse_bytes) return GDZ_COMPACT_NONCANONICAL_INDEX;
    } else {
        size_t position = 0;
        while (position < length) {
            uint64_t delta;
            GdzCompactResult result = read_delta(data, length, &position, &delta);
            if (result != GDZ_COMPACT_OK) return result;
            if (delta > UINT64_MAX - previous) return GDZ_COMPACT_OVERFLOW;
            if (!delta || delta > total - previous) return GDZ_COMPACT_FORMAT;
            previous += delta;
            if (ticks) ticks[count] = previous;
            ++count;
        }
        if (length >= bitmap_bytes) return GDZ_COMPACT_NONCANONICAL_INDEX;
    }
    *out_count = count;
    return GDZ_COMPACT_OK;
}

/* Accept the legacy window as declared, including zero/nonminimal payloads.
 * Never convert bits to floating point or import window-v2 canonicality. */
static GdzCompactResult parse_values(const unsigned char *data, size_t length,
                                     size_t count, uint64_t *values)
{
    BitReader reader;
    uint64_t previous = 0, field;
    GdzCompactResult result;
    if (length > SIZE_MAX / 8) return GDZ_COMPACT_OVERFLOW;
    reader = (BitReader){data, length * 8, 32};
    for (size_t i = 0; i < count; ++i) {
        if (i == 0) {
            result = read_bits(&reader, 64, values ? &previous : NULL);
            if (result != GDZ_COMPACT_OK) return result;
        } else {
            result = read_bits(&reader, 1, &field);
            if (result != GDZ_COMPACT_OK) return result;
            if (field) {
                uint64_t leading, width_minus_one, payload;
                unsigned width;
                result = read_bits(&reader, 6, &leading);
                if (result != GDZ_COMPACT_OK) return result;
                result = read_bits(&reader, 6, &width_minus_one);
                if (result != GDZ_COMPACT_OK) return result;
                width = (unsigned)width_minus_one + 1u;
                if (leading > 64u - width) return GDZ_COMPACT_FORMAT;
                result = read_bits(&reader, width, values ? &payload : NULL);
                if (result != GDZ_COMPACT_OK) return result;
                if (values) {
                    unsigned trailing = 64u - (unsigned)leading - width;
                    previous ^= payload << trailing;
                }
            }
        }
        if (values) values[i] = previous;
    }
    /* Round without position+7, then examine at most seven padding bits. */
    if (length != reader.position / 8 + (reader.position % 8 != 0))
        return GDZ_COMPACT_FORMAT;
    while (reader.position < reader.bits) {
        result = read_bits(&reader, 1, &field);
        if (result != GDZ_COMPACT_OK) return result;
        if (field) return GDZ_COMPACT_FORMAT;
    }
    return GDZ_COMPACT_OK;
}

static void destroy(GdzCompact *compact)
{
    free(compact->values);
    free(compact->ticks);
    free(compact);
}

GdzCompactResult gdz_compact_from_gdz1(
    const unsigned char *data, size_t length,
    const GdzCompactLimits *limits, GdzCompact **out)
{
    GdzCompactLimits selected = {100000, 100000, 975021, 1600256};
    GdzCompactResult result;
    GdzCompact *compact;
    uint64_t total, encoded_count;
    size_t index_length, index_end, count, array_bytes, requested;
    unsigned mode;
    if (!out || *out || (!data && length)) return GDZ_COMPACT_INVALID_ARGUMENT;
    if (limits) selected = *limits;
    if (!selected.max_ticks || selected.max_ticks > 100000 ||
        selected.max_samples > 100000 || !selected.max_input_bytes ||
        selected.max_input_bytes > 975021 || !selected.max_alloc_bytes ||
        selected.max_alloc_bytes > 1600256) return GDZ_COMPACT_INVALID_ARGUMENT;
    if (length > selected.max_input_bytes) return GDZ_COMPACT_LIMIT;
    if (length < 18) return GDZ_COMPACT_TRUNCATED;
    mode = data[5];
    if (memcmp(data, "GDZ1", 4) || data[4] != 1 || mode > 1)
        return GDZ_COMPACT_FORMAT;
    total = big_endian(data + 6, 8);
    if (!total) return GDZ_COMPACT_FORMAT;
    if (total > selected.max_ticks) return GDZ_COMPACT_LIMIT;
    index_length = (size_t)big_endian(data + 14, 4);
    if (index_length > SIZE_MAX - 18) return GDZ_COMPACT_OVERFLOW;
    if (index_length > length - 18) return GDZ_COMPACT_TRUNCATED;
    index_end = 18 + index_length;
    result = parse_index(data + 18, index_length, mode, total, NULL, &count);
    if (result != GDZ_COMPACT_OK) return result;
    if (length - index_end < 4) return GDZ_COMPACT_TRUNCATED;
    encoded_count = big_endian(data + index_end, 4);
    if (encoded_count > total) return GDZ_COMPACT_FORMAT;
    if (encoded_count > selected.max_samples) return GDZ_COMPACT_LIMIT;
    if ((size_t)encoded_count != count) return GDZ_COMPACT_FORMAT;
    result = parse_values(data + index_end, length - index_end, count, NULL);
    if (result != GDZ_COMPACT_OK) return result;
    if (count > SIZE_MAX / sizeof(uint64_t)) return GDZ_COMPACT_OVERFLOW;
    array_bytes = count * sizeof(uint64_t);
    if (array_bytes > (SIZE_MAX - sizeof(GdzCompact)) / 2)
        return GDZ_COMPACT_OVERFLOW;
    requested = sizeof(GdzCompact) + 2 * array_bytes;
    if (requested > selected.max_alloc_bytes) return GDZ_COMPACT_LIMIT;
    compact = malloc(sizeof *compact);
    if (!compact) return GDZ_COMPACT_OOM;
    *compact = (GdzCompact){total, count, NULL, NULL};
    if (count) {
        compact->ticks = malloc(array_bytes);
        if (!compact->ticks) { destroy(compact); return GDZ_COMPACT_OOM; }
        compact->values = malloc(array_bytes);
        if (!compact->values) { destroy(compact); return GDZ_COMPACT_OOM; }
    }
    result = parse_index(data + 18, index_length, mode, total,
                         compact->ticks, &count);
    if (result == GDZ_COMPACT_OK)
        result = parse_values(data + index_end, length - index_end,
                              count, compact->values);
    if (result != GDZ_COMPACT_OK) { destroy(compact); return result; }
    *out = compact;
    return GDZ_COMPACT_OK;
}

GdzCompactResult gdz_compact_info(const GdzCompact *compact, GdzCompactInfo *out)
{
    if (!compact || !out) return GDZ_COMPACT_INVALID_ARGUMENT;
    *out = (GdzCompactInfo){compact->total_ticks, compact->count};
    return GDZ_COMPACT_OK;
}

/* Lower/upper bound without endpoint+1; mid stays within [low,high). */
static size_t bound(const GdzCompact *compact, uint64_t tick, int upper)
{
    size_t low = 0, high = compact->count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (compact->ticks[mid] < tick ||
            (upper && compact->ticks[mid] == tick)) low = mid + 1;
        else high = mid;
    }
    return low;
}

GdzCompactResult gdz_compact_get(const GdzCompact *compact, uint64_t tick,
                                uint64_t *bits)
{
    size_t position;
    if (!compact || !bits) return GDZ_COMPACT_INVALID_ARGUMENT;
    if (!tick || tick > compact->total_ticks) return GDZ_COMPACT_INVALID_RANGE;
    position = bound(compact, tick, 0);
    if (position == compact->count || compact->ticks[position] != tick)
        return GDZ_COMPACT_NOT_FOUND;
    *bits = compact->values[position];
    return GDZ_COMPACT_OK;
}

GdzCompactResult gdz_compact_range(
    const GdzCompact *compact, uint64_t first, uint64_t last,
    GdzSample *buffer, size_t capacity, size_t *required)
{
    size_t start, count;
    if (!compact || !required || (!buffer && capacity))
        return GDZ_COMPACT_INVALID_ARGUMENT;
    if (!first || first > last || last > compact->total_ticks)
        return GDZ_COMPACT_INVALID_RANGE;
    if (buffer && capacity > SIZE_MAX / sizeof(GdzSample))
        return GDZ_COMPACT_OVERFLOW;
    start = bound(compact, first, 0);
    count = bound(compact, last, 1) - start;
    if (buffer && capacity < count) {
        *required = count;
        return GDZ_COMPACT_CAPACITY;
    }
    if (buffer) {
        for (size_t i = 0; i < count; ++i)
            buffer[i] = (GdzSample){compact->ticks[start + i],
                                    compact->values[start + i]};
    }
    *required = count;
    return GDZ_COMPACT_OK;
}

GdzCompactResult gdz_compact_close(GdzCompact **owner)
{
    if (!owner) return GDZ_COMPACT_INVALID_ARGUMENT;
    if (*owner) destroy(*owner);
    *owner = NULL;
    return GDZ_COMPACT_OK;
}
