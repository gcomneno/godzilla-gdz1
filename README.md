# GODZILLA GDZ1 C11 — Experimental Preview v0.1

This experimental C11 library stores sparse tick/value pairs in an immutable
in-memory owner and reads and writes the GDZ1 v1 wire format. Values are opaque
64-bit patterns: the library does not interpret them as floating-point numbers.
The public API and ABI are provisional. Production readiness is not certified.
This preview provides no database, transaction commit, integrity certification,
or authorization facility.

## Requirements and GCC usage

Use a C11 compiler and the standard C library. The implementation requires
eight-bit bytes, exact `uint32_t` and `uint64_t` types, and a `size_t` able to
represent all `uint32_t` values. No third-party runtime dependencies are needed.
Python and SQLite are not required.

From the project directory, the example can be built with GCC:

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic -Iinclude \
    src/godzilla_compact.c src/godzilla_compact_build.c \
    examples/roundtrip.c -o roundtrip
./roundtrip
```

The example constructs an owner from four samples, queries the exact encoded
size, allocates a buffer, encodes, and decodes into a separate owner. It checks
the extent, sample count, and every tick/value pair through range and point
queries, then releases both owners and the buffer. Success returns zero;
failure reports a diagnostic and returns nonzero.

## Public API

Include `godzilla_compact.h` for decoding and immutable access:

- `gdz_compact_from_gdz1`: validate and decode a complete payload into an owner.
- `gdz_compact_info`: read the total tick extent and stored sample count.
- `gdz_compact_get`: retrieve a stored value at an in-domain tick; an absent
  sample returns `GDZ_COMPACT_NOT_FOUND`.
- `gdz_compact_range`: copy samples in an inclusive tick interval, in ascending
  order. A `NULL` buffer with zero capacity queries the exact sample count.
- `gdz_compact_close`: free an owner and set its owner slot to `NULL`.

Include `godzilla_compact_build.h` for construction and encoding:

- `gdz_compact_from_samples`: build an independent owner from strictly ascending,
  unique samples with ticks in `1..total_ticks`.
- `gdz_compact_encode_gdz1`: write canonical GDZ1 v1 bytes; a `NULL` buffer with
  zero capacity queries the exact required byte count without allocation.
- `gdz_compact_insert`: create an independent owner with a previously absent tick.
- `gdz_compact_update`: create an independent owner with a present tick changed.
  Even an update with identical bits returns an independent owner.

Functions return `GdzCompactResult`; check every result. Owner-producing calls
require an output slot initially set to `NULL`. Inputs are borrowed only until
return; a successful owner is independent of them. Required output storage must
be live, aligned, and disjoint from other outputs, inputs, and the model, as
specified in the headers. Range and encoding capacities use samples and bytes,
respectively. `GDZ_COMPACT_CAPACITY` publishes the required size and leaves the
entire buffer unchanged. Other errors preserve their outputs; failed owner
construction frees partial state and leaves the owner slot `NULL`.

Immutable reads may run concurrently with disjoint caller outputs. Callers must
synchronize destruction with all reads, edits, encoding, and aliases. Closing
an owner invalidates its aliases; closing an already `NULL` owner slot succeeds.

## Limits

The supported tick extent is `1..100000`, with at most one sample per tick.
An empty sample set is valid. Default `GdzCompactLimits` ceilings are:

| Field | Default ceiling |
| --- | ---: |
| `max_ticks` | 100000 |
| `max_samples` | 100000 |
| `max_input_bytes` | 975021 |
| `max_alloc_bytes` | 1600256 |

`GdzCompactBuildLimits` adds `max_work_bytes`, defaulting to 2575277.
Supplied ceilings may only restrict these defaults; all must be positive except
`max_samples`, which may be zero. Allocation accounting covers the owner and its
two sample arrays. Build work accounting covers the temporary encoded payload
plus candidate owner allocation, excluding the base owner, caller storage,
allocator overhead, and runtime allocations. Allocation can still fail.

Encoding and read/close operations allocate no heap memory. Construction and
edits allocate a new owner; edits leave the base immutable. There is no deletion
API or streaming decoder. Payloads must be complete, within the configured
limits, and follow the index canonicality rules. Legacy XOR windows accepted by
the decoder may change bytes on canonical re-encoding, so original byte hashes
are not preserved. See [FORMAT.md](FORMAT.md) for the implemented wire format
and [LICENSE](LICENSE) for the MIT license.
