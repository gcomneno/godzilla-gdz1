#include "godzilla_compact.h"
#include "godzilla_compact_build.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N 50000u

static uint64_t random64(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * UINT64_C(2685821657736338717);
}

static uint64_t sample_value(size_t i, unsigned profile,
                             uint64_t *state)
{
    switch (profile) {
    case 0:
        return UINT64_C(0x3ff0000000000000);
    case 1:
        return UINT64_C(0x3ff0000000000000)
               + (uint64_t)(i % 1000u);
    case 2:
        return random64(state);
    default:
        if (i % 100u < 80u)
            return UINT64_C(0x4000000000000000)
                   + (uint64_t)(i / 100u);
        return random64(state);
    }
}

static double elapsed(clock_t start, clock_t end)
{
    return 1000.0 * (double)(end - start) / CLOCKS_PER_SEC;
}

static int run_profile(unsigned profile, const char *name)
{
    GdzSample *samples = NULL;
    GdzSample *actual = NULL;
    unsigned char *encoded = NULL;
    GdzCompact *original = NULL;
    GdzCompact *decoded = NULL;
    size_t required = 0;
    size_t written = 0;
    size_t count = 0;
    uint64_t seed = UINT64_C(0x123456789abcdef0);
    clock_t t0, t1, t2, t3;
    int ok = 0;

    samples = malloc(N * sizeof(*samples));
    actual = malloc(N * sizeof(*actual));
    if (!samples || !actual) goto cleanup;

    for (size_t i = 0; i < N; ++i) {
        samples[i].tick = (uint64_t)i + 1u;
        samples[i].bits = sample_value(i, profile, &seed);
    }

    if (gdz_compact_from_samples(N, samples, N, NULL,
                                 &original) != GDZ_COMPACT_OK)
        goto cleanup;

    if (gdz_compact_encode_gdz1(original, NULL, 0,
                                &required) != GDZ_COMPACT_OK)
        goto cleanup;

    encoded = malloc(required);
    if (!encoded) goto cleanup;

    t0 = clock();

    if (gdz_compact_encode_gdz1(original, encoded, required,
                                &written) != GDZ_COMPACT_OK)
        goto cleanup;

    t1 = clock();

    if (written != required) goto cleanup;

    t2 = clock();

    if (gdz_compact_from_gdz1(encoded, written, NULL,
                              &decoded) != GDZ_COMPACT_OK)
        goto cleanup;

    t3 = clock();

    if (gdz_compact_range(decoded, 1, N, actual, N,
                          &count) != GDZ_COMPACT_OK)
        goto cleanup;

    if (count != N) goto cleanup;

    for (size_t i = 0; i < N; ++i) {
        if (actual[i].tick != samples[i].tick ||
            actual[i].bits != samples[i].bits)
            goto cleanup;
    }

    {
        GdzCompactInfo info;
        if (gdz_compact_info(decoded, &info) != GDZ_COMPACT_OK)
            goto cleanup;
        if (info.count != N || info.total_ticks != N)
            goto cleanup;
    }

    printf("%-10s | %6zu | %8zu | %6.2fx | %9.3f | %9.3f | PASS\n",
           name, (size_t)N, written,
           (double)(N * sizeof(*samples)) / (double)written,
           elapsed(t0, t1), elapsed(t2, t3));

    ok = 1;

cleanup:
    if (gdz_compact_close(&decoded) != GDZ_COMPACT_OK)
        ok = 0;
    if (gdz_compact_close(&original) != GDZ_COMPACT_OK)
        ok = 0;

    free(encoded);
    free(actual);
    free(samples);

    if (!ok)
        fprintf(stderr, "ROAD_TEST_PROFILE_FAIL=%s\n", name);

    return ok;
}

int main(void)
{
    const char *names[] = {
        "constant", "regular", "noisy", "mixed"
    };

    puts("PROFILE    | SAMPLES | GDZ1 BYTES | RAW/GDZ1 | ENCODE ms | DECODE ms | EXACT");
    puts("-----------+---------+------------+----------+-----------+-----------+------");

    for (unsigned p = 0; p < 4; ++p) {
        if (!run_profile(p, names[p]))
            return EXIT_FAILURE;
    }

    puts("TOTAL_SAMPLES=200000");
    puts("EXACT_RECONSTRUCTION_GATE=PASS");
    return EXIT_SUCCESS;
}
