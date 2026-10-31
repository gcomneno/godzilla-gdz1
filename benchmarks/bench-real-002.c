/*
 * BENCH-REAL-002 — Real-world GDZ1 C11 benchmark.
 *
 * Dataset: UCI Individual Household Electric Power Consumption.
 * Input: extracted household_power_consumption.txt, kept outside Git.
 *
 * This is a functional and indicative performance experiment,
 * not a production certification.
 */
#include "godzilla_compact.h"
#include "godzilla_compact_build.h"

#include <errno.h>
#include <inttypes.h>
#include <locale.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    WINDOW_TICKS = 100000,
    EXPECTED_SAMPLES = 99992,
    EXPECTED_MISSING = 8,
    REPETITIONS = 5
};

static int load_samples(const char *path, GdzSample *samples,
                        size_t *count)
{
    FILE *file = fopen(path, "r");
    char line[512];
    size_t valid = 0;
    size_t missing = 0;

    if (file == NULL) {
        perror("open dataset");
        return 1;
    }

    if (fgets(line, sizeof line, file) == NULL) {
        fprintf(stderr, "Cannot read dataset header\n");
        fclose(file);
        return 1;
    }

    /* Accept LF and CRLF without weakening header validation. */
    line[strcspn(line, "\r\n")] = '\0';
    if (strcmp(line,
               "Date;Time;Global_active_power;Global_reactive_power;"
               "Voltage;Global_intensity;Sub_metering_1;"
               "Sub_metering_2;Sub_metering_3") != 0) {
        fprintf(stderr, "Unexpected dataset header\n");
        fclose(file);
        return 1;
    }

    for (size_t i = 0; i < WINDOW_TICKS; ++i) {
        char *first;
        char *second;
        char *value;
        char *end;
        double number;
        uint64_t bits;

        if (fgets(line, sizeof line, file) == NULL) {
            fprintf(stderr, "Dataset ended at tick %zu\n", i + 1);
            fclose(file);
            return 1;
        }

        first = strchr(line, ';');
        second = first == NULL ? NULL : strchr(first + 1, ';');
        value = second == NULL ? NULL : second + 1;

        if (value == NULL) {
            fprintf(stderr, "Malformed row at tick %zu\n", i + 1);
            fclose(file);
            return 1;
        }

        if (value[0] == '?' && value[1] == ';') {
            ++missing;
            continue;
        }

        errno = 0;
        number = strtod(value, &end);

        if (errno != 0 || end == value || *end != ';' ||
            !isfinite(number)) {
            fprintf(stderr, "Invalid numeric value at tick %zu\n",
                    i + 1);
            fclose(file);
            return 1;
        }

        /* Preserve the binary64 pattern, including its exact bits. */
        memcpy(&bits, &number, sizeof bits);
        samples[valid].tick = (uint64_t)i + 1;
        samples[valid].bits = bits;
        ++valid;
    }

    if (fclose(file) != 0) {
        perror("close dataset");
        return 1;
    }

    printf("WINDOW_TICKS=%d\n", WINDOW_TICKS);
    printf("PRESENT_SAMPLES=%zu\n", valid);
    printf("MISSING_SAMPLES=%zu\n", missing);

    if (valid != EXPECTED_SAMPLES || missing != EXPECTED_MISSING) {
        fprintf(stderr, "Dataset geometry mismatch\n");
        return 1;
    }

    *count = valid;
    return 0;
}

static double elapsed_ms(clock_t start, clock_t end)
{
    return 1000.0 * (double)(end - start) / CLOCKS_PER_SEC;
}

static int run_once(const GdzSample *samples, size_t count,
                    GdzSample *actual, unsigned repetition)
{
    GdzCompact *original = NULL;
    GdzCompact *decoded = NULL;
    unsigned char *encoded = NULL;
    GdzCompactInfo info;
    GdzCompactResult result;
    size_t required = 0;
    size_t written = 0;
    size_t recovered = 0;
    size_t encoded_bytes = 0;
    clock_t start;
    clock_t built;
    clock_t encoded_at;
    clock_t decoded_at;
    double build_ms;
    double encode_ms;
    double decode_ms;
    int failure = 1;

    start = clock();
    result = gdz_compact_from_samples(
        WINDOW_TICKS, samples, count, NULL, &original);
    built = clock();
    build_ms = elapsed_ms(start, built);

    if (result != GDZ_COMPACT_OK) {
        fprintf(stderr, "Build failed: %d\n", (int)result);
        goto cleanup;
    }

    result = gdz_compact_encode_gdz1(
        original, NULL, 0, &required);
    if (result != GDZ_COMPACT_OK || required == 0) {
        fprintf(stderr, "Encoding size query failed\n");
        goto cleanup;
    }

    encoded = malloc(required);
    if (encoded == NULL) {
        fprintf(stderr, "Encoding allocation failed\n");
        goto cleanup;
    }

    start = clock();
    result = gdz_compact_encode_gdz1(
        original, encoded, required, &written);
    encoded_at = clock();
    encode_ms = elapsed_ms(start, encoded_at);

    if (result != GDZ_COMPACT_OK || written != required) {
        fprintf(stderr, "Encode failed\n");
        goto cleanup;
    }

    encoded_bytes = written;
    start = clock();
    result = gdz_compact_from_gdz1(
        encoded, encoded_bytes, NULL, &decoded);
    decoded_at = clock();
    decode_ms = elapsed_ms(start, decoded_at);

    if (result != GDZ_COMPACT_OK) {
        fprintf(stderr, "Decode failed: %d\n", (int)result);
        goto cleanup;
    }

    result = gdz_compact_info(decoded, &info);
    if (result != GDZ_COMPACT_OK ||
        info.total_ticks != WINDOW_TICKS ||
        info.count != count) {
        fprintf(stderr, "Decoded geometry mismatch\n");
        goto cleanup;
    }

    result = gdz_compact_range(
        decoded, 1, WINDOW_TICKS, actual, count, &recovered);

    if (result != GDZ_COMPACT_OK || recovered != count) {
        fprintf(stderr, "Decoded range mismatch\n");
        goto cleanup;
    }

    for (size_t i = 0; i < count; ++i) {
        if (actual[i].tick != samples[i].tick ||
            actual[i].bits != samples[i].bits) {
            fprintf(stderr, "Mismatch at sample %zu\n", i);
            goto cleanup;
        }
    }

    /*
     * Identical geometry and ordered sample equality also establish
     * preservation of all absent ticks within the tested interval.
     */
    printf("RUN=%u GDZ1_BYTES=%zu RAW_PAIR_BYTES=%zu "
           "BUILD_MS=%.3f ENCODE_MS=%.3f DECODE_MS=%.3f "
           "EXACT=PASS\n",
           repetition, encoded_bytes, count * 16,
           build_ms, encode_ms, decode_ms);

    failure = 0;

cleanup:
    free(encoded);
    gdz_compact_close(&decoded);
    gdz_compact_close(&original);
    return failure;
}

int main(int argc, char **argv)
{
    GdzSample *samples = NULL;
    GdzSample *actual = NULL;
    size_t count = 0;
    int status = 1;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s DATASET.txt\n", argv[0]);
        return 1;
    }

    if (setlocale(LC_NUMERIC, "C") == NULL ||
        sizeof(double) != sizeof(uint64_t)) {
        fprintf(stderr, "Unsupported numeric environment\n");
        return 1;
    }

    samples = malloc(WINDOW_TICKS * sizeof *samples);
    actual = malloc(WINDOW_TICKS * sizeof *actual);

    if (samples == NULL || actual == NULL) {
        fprintf(stderr, "Sample allocation failed\n");
        goto cleanup;
    }

    if (load_samples(argv[1], samples, &count) != 0)
        goto cleanup;

    for (unsigned run = 1; run <= REPETITIONS; ++run) {
        if (run_once(samples, count, actual, run) != 0)
            goto cleanup;
    }

    puts("BENCH_REAL_002_ROUNDTRIP_GATE=PASS");
    status = 0;

cleanup:
    free(actual);
    free(samples);
    return status;
}
