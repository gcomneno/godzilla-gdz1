# ROAD TEST 001 — Public GDZ1 C11 preview

Date: 2026-10-31

## Purpose

Independent functional and indicative performance test of the
published `v0.1.1-preview.1` distribution.

This is a historical measurement baseline, not a production
certification or a statistically qualified benchmark.

## Provenance

- Repository: `gcomneno/godzilla-gdz1`
- Public baseline commit: `cc64c1228f370bdeeaad3b442c7d1661ab53a330`
- Release: `v0.1.1-preview.1`
- Asset: `godzilla-gdz1-c11-preview-0.1.1.zip`
- Asset SHA-256:
  `4dbdfb0af17ca6392bca16aac682474a8ad73c07cd4f349681677d967b5bfdfa`
- Test source: `benchmarks/road-test-001.c`
- Test source SHA-256:
  `ab768265be12d96a6105b53be022f83a54d57562f6142f238a3cb616380b95d3`

The test used only C11 sources extracted from the public
GitHub release. No private repository sources were used.

## Environment

- Platform: Ubuntu 24.04 (reported GCC package environment)
- Compiler: GCC 13.3.0
- Language: C11
- Flags: `-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror`
- Timing: C `clock()`, reported as milliseconds
- Process measurement: `/usr/bin/time`

CPU model, operating-system load and clock configuration
were not recorded.

## Workload

Four deterministic synthetic profiles, each containing
50,000 samples with consecutive one-based ticks:

1. Constant: identical 64-bit patterns.
2. Regular: a repeating low-variation sequence.
3. Noisy: deterministic pseudorandom 64-bit patterns.
4. Mixed: mostly structured values with noisy segments.

Total verified sample pairs across profiles: 200,000.

The source file is the authoritative workload definition,
including generator, seeds and bit patterns.

## Procedure

For each profile:

1. Generate 50,000 `(tick, bits)` samples.
2. Construct a native compact owner.
3. Query the required GDZ1 encoding size.
4. Encode into an exact-size byte buffer.
5. Decode the generated GDZ1 data into a separate owner.
6. Query all decoded samples through the public range API.
7. Compare every tick and 64-bit value exactly.
8. Verify the decoded owner geometry and release resources.

Encode and decode timings exclude the construction step,
dataset generation, result verification and cleanup.

The baseline is one observed execution, not a distribution
of repeated benchmark runs.

## Observed results

| Profile | Samples | GDZ1 bytes | Raw/GDZ1 | Encode (ms) | Decode (ms) | Exact |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Constant | 50,000 | 12,530 | 63.85x | 0.644 | 0.704 | PASS |
| Regular | 50,000 | 100,028 | 8.00x | 7.052 | 3.879 | PASS |
| Noisy | 50,000 | 475,072 | 1.68x | 19.569 | 8.448 | PASS |
| Mixed | 50,000 | 109,630 | 7.30x | 3.440 | 1.660 | PASS |

The uncompressed reference is 16 bytes per sample:
an 8-byte tick plus an 8-byte value pattern.

Whole-process observations:

- Elapsed wall time: 0.10 seconds.
- Maximum resident set size: 5,232 KiB.
- Exact reconstruction: PASS, 200,000/200,000 samples.
- Public-source-only build: PASS.
- Private repository isolation: PASS.

Whole-process resource observations must not be attributed
exclusively to the codec.

## Reproduction

From the root of a checkout of the public release source:

```sh
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
    -Iinclude \
    src/godzilla_compact.c \
    src/godzilla_compact_build.c \
    benchmarks/road-test-001.c \
    -o road-test-001

/usr/bin/time -f 'PROCESS_ELAPSED_SECONDS=%e MAX_RSS_KB=%M' \
    ./road-test-001
```

Run from a source revision with a compatible public API.
Do not assume later revisions preserve binary or timing
characteristics.

## Comparison rules for future tests

Keep this original source and result record unchanged.
Run the same workload with the same compiler flags and
measurement definitions, recording the exact commit,
release, compiler, CPU, environment and resulting outputs.

Use separate reports for new observations. Compare
compression ratios directly only when workload and wire
representation match. Treat timing differences cautiously
unless the runtime environment is controlled and multiple
measurements establish repeatability.

## Limits of evidence

- Synthetic profiles, not operational telemetry.
- Single timing observation per profile.
- No controlled CPU-frequency or system-load conditions.
- No comparison against Zstd or other external codecs.
- No long-running, concurrent or crash-recovery workload.
- This release does not include the GODZILLA production
  publisher or transactional COMMIT.
- Production readiness remains NOT CERTIFIED.
