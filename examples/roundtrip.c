#include "godzilla_compact.h"
#include "godzilla_compact_build.h"

#include <stdio.h>
#include <stdlib.h>

static int check_result(GdzCompactResult result, const char *operation)
{
    if (result == GDZ_COMPACT_OK) return 1;
    fprintf(stderr, "%s failed (result %d)\n", operation, (int)result);
    return 0;
}

static int verify(const GdzCompact *owner, const GdzSample expected[4],
                  uint64_t total_ticks)
{
    GdzCompactInfo info;
    GdzSample actual[4];
    size_t required = 0;

    if (!check_result(gdz_compact_info(owner, &info), "info")) return 0;
    if (info.total_ticks != total_ticks || info.count != 4) {
        fputs("Owner geometry mismatch\n", stderr);
        return 0;
    }
    if (!check_result(gdz_compact_range(owner, 1, total_ticks, NULL, 0,
                                        &required), "range size query"))
        return 0;
    if (required != 4) {
        fputs("Range count mismatch\n", stderr);
        return 0;
    }
    if (!check_result(gdz_compact_range(owner, 1, total_ticks, actual, 4,
                                        &required), "range"))
        return 0;
    if (required != 4) {
        fputs("Returned range count mismatch\n", stderr);
        return 0;
    }
    for (size_t i = 0; i < 4; ++i) {
        uint64_t bits = 0;
        if (actual[i].tick != expected[i].tick ||
            actual[i].bits != expected[i].bits) {
            fputs("Tick/value pair mismatch\n", stderr);
            return 0;
        }
        if (!check_result(gdz_compact_get(owner, expected[i].tick, &bits),
                          "get")) return 0;
        if (bits != expected[i].bits) {
            fputs("Point value mismatch\n", stderr);
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    const uint64_t total_ticks = 1000;
    const GdzSample samples[4] = {
        {UINT64_C(1), UINT64_C(0x0000000000000000)},
        {UINT64_C(2), UINT64_C(0x0000000000000000)},
        {UINT64_C(128), UINT64_C(0x8000000000000001)},
        {UINT64_C(1000), UINT64_C(0xffffffffffffffff)}
    };
    GdzCompact *owner = NULL;
    GdzCompact *decoded = NULL;
    unsigned char *buffer = NULL;
    size_t required = 0;
    size_t written = 0;
    int status = EXIT_FAILURE;

    if (!check_result(gdz_compact_from_samples(total_ticks, samples, 4, NULL,
                                               &owner), "from_samples"))
        goto cleanup;
    if (!verify(owner, samples, total_ticks)) goto cleanup;
    if (!check_result(gdz_compact_encode_gdz1(owner, NULL, 0, &required),
                      "encoded size query")) goto cleanup;
    if (required == 0) {
        fputs("Unexpected zero encoded size\n", stderr);
        goto cleanup;
    }
    buffer = malloc(required);
    if (buffer == NULL) {
        fputs("Buffer allocation failed\n", stderr);
        goto cleanup;
    }
    if (!check_result(gdz_compact_encode_gdz1(owner, buffer, required, &written),
                      "encode")) goto cleanup;
    if (written != required) {
        fputs("Encoded size mismatch\n", stderr);
        goto cleanup;
    }
    if (!check_result(gdz_compact_from_gdz1(buffer, written, NULL, &decoded),
                      "decode")) goto cleanup;
    if (!verify(decoded, samples, total_ticks)) goto cleanup;
    status = EXIT_SUCCESS;

cleanup:
    if (!check_result(gdz_compact_close(&decoded), "close decoded"))
        status = EXIT_FAILURE;
    if (!check_result(gdz_compact_close(&owner), "close owner"))
        status = EXIT_FAILURE;
    free(buffer);
    return status;
}
