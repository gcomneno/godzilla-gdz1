# BENCH-REAL-002 — Real-world GDZ1 C11 benchmark

Date: 2026-10-31
Status: FUNCTIONAL PASS — INDICATIVE PERFORMANCE ONLY

## Dataset

Source: UCI Machine Learning Repository, dataset 235.
Title: Individual Household Electric Power Consumption.
DOI: 10.24432/C58K54.
License: CC BY 4.0.

Archive SHA-256:
9f84b46ade8a2d8e1286ec4b2b6c2987a45a755c59f263be3b3b3d10dfbda3ff

The SHA-256 identifies the downloaded archive; it was
not compared with an independent official checksum.

The dataset is stored outside the Git repository.

## Workload

- First 100,000 chronological observations.
- Period: 2006-12-16 17:24 to 2007-02-24 04:03.
- Column: Global_active_power.
- Valid samples: 99,992.
- Missing samples: 8.
- Invalid numeric values: 0.
- Temporal discontinuities: 0.

Original CSV values are converted to C double, then
their binary64 patterns are stored in uint64_t.

Lossless verification applies to those binary64 patterns,
not to the original decimal text.

Missing observations preserve their tick positions.
The tested window fits the public API limits.

## Procedure

Public C11 GDZ1 implementation only.

For each of five repetitions:

1. Construct an independent compact owner.
2. Query the exact required encoding size.
3. Encode to GDZ1.
4. Decode into an independent owner.
5. Verify geometry and the complete ordered sample set.
6. Compare every tick and value pattern exactly.
7. Release all resources.

Dataset parsing and verification are outside the
reported construction, encoding and decoding timings.

Timing uses C clock() and reports CPU milliseconds.

## Observed results

| Run | GDZ1 bytes | Raw pair bytes | Build ms | Encode ms | Decode ms | Exact |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| 1 | 690653 | 1599872 | 38.748 | 19.771 | 9.190 | PASS |
| 2 | 690653 | 1599872 | 28.779 | 18.768 | 9.292 | PASS |
| 3 | 690653 | 1599872 | 28.093 | 18.692 | 9.333 | PASS |
| 4 | 690653 | 1599872 | 28.391 | 18.682 | 9.223 | PASS |
| 5 | 690653 | 1599872 | 28.098 | 18.731 | 9.205 | PASS |

Raw reference: 16 bytes per present sample.
Observed raw/GDZ1 ratio: approximately 2.32x.
Space reduction against raw sample pairs: approximately 56.8%.

Means of runs 2–5:
- Build: 28.340 ms.
- Encode: 18.718 ms.
- Decode: 9.263 ms.

Whole-process observations:
- Elapsed wall time: 0.38 seconds.
- Maximum resident set size: 8,356 KiB.
- Exact reconstruction: PASS, all five repetitions.
- Missing tick preservation: PASS.

Whole-process resources are not exclusively attributable
to the codec.

## Reproduction

Build with:

    gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
        -Iinclude src/godzilla_compact.c \
        src/godzilla_compact_build.c \
        benchmarks/bench-real-002.c \
        -lm -o bench-real-002

Run with the extracted UCI text file as the sole argument.

The dataset is intentionally not committed to Git.

## Evidence limits

- Five repetitions within one process.
- No independent process-run distribution.
- No controlled CPU frequency or system load.
- Measurement environment: Ubuntu 24.04.5 LTS,
  Linux 6.8.0-147-generic, x86_64.
- CPU: Intel Core i3-4330 @ 3.50 GHz;
  4 logical CPUs, 2 threads per core.
- Compiler: GCC 13.3.0, C11, -O2,
  -Wall -Wextra -Wpedantic -Werror.
- System load and CPU-frequency behavior were
  not controlled; cross-machine performance
  reproducibility is not established.
- No comparison with external codecs.
- No concurrent or crash-recovery workload.
- The publisher and transactional COMMIT are not tested.
- Production readiness remains NOT CERTIFIED.

ROAD-TEST-001 is an independent historical baseline
and must remain unchanged.
