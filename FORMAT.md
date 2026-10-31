# Implemented GDZ1 v1 wire format

This document describes the GDZ1 version 1 reader and writer implemented in
`src/godzilla_compact.c` and `src/godzilla_compact_build.c`. The public API and
ABI remain provisional. This description makes no production certification.

## Layout

Bytes are eight bits. Fixed-width integers are unsigned, big-endian. Bit fields
are read and written most significant bit first within each byte. Input need
not be aligned, and host endianness does not affect the wire layout.

Let `L` be the index length in bytes:

| Byte offset | Size | Contents |
| --- | --- | --- |
| 0 | 4 bytes | ASCII `GDZ1` (`47 44 5a 31` hexadecimal) |
| 4 | 1 byte | Version: `1` |
| 5 | 1 byte | Index mode: `0` bitmap, `1` sparse deltas |
| 6 | 8 bytes | Total tick extent `T` |
| 14 | 4 bytes | Index length `L` |
| 18 | `L` bytes | Index |
| `18 + L` | 4 bytes | Stored sample count `N` |
| `22 + L` | Variable | Value bitstream, padded to a byte boundary |

`T` must be positive and is limited to 100000 by this implementation. Ticks are
one-based in `1..T`; an unset tick has no sample. `N` must equal the index's
sample count and cannot exceed `T` or the selected sample limit. Values follow
the ascending tick order of the index. Zero samples are valid.

There is no separate value length, checksum, or trailing extension area. The
remaining bytes after the index contain the count and value stream, and must
have exactly the length consumed by those fields plus final bit padding.

## Index modes and canonical selection

For bitmap mode, `L = ceil(T / 8)`. Bit `tick - 1` marks presence, with tick 1
in the high bit of the first byte. Unused low bits in the last byte must be zero.

For sparse mode, the index is a concatenation of positive tick deltas. The
previous tick starts at zero; each decoded delta is added to it. Each delta
uses unsigned base-128 little-endian groups: the low seven bits of each byte
are data, and the high bit indicates another byte. Encodings must be minimal
(a multi-byte encoding cannot end with a zero data group). At most ten bytes
are allowed for a `uint64_t`; the tenth byte may only be `0` or `1`, with no
continuation. Overflow is rejected. Deltas must be nonzero and their cumulative
ticks must remain within `1..T`. The index ends after exactly `L` bytes, with
no partial delta. An empty sparse index has `L = 0`.

Let `B = ceil(T / 8)` and let `S` be the total byte length of the minimal sparse
delta encodings for the same ticks. Canonical mode selection is:

- Bitmap if `B <= S`, including a tie.
- Sparse if `S < B`.

The decoder enforces this selection and rejects a noncanonical index with
`GDZ_COMPACT_NONCANONICAL_INDEX`. Nonminimal delta encodings also produce that
result. Malformed geometry, padding, and invalid modes produce format errors;
incomplete input and integer overflow have separate result codes.

## Value stream (legacy GXOR64)

The value section starts with the 32-bit big-endian count `N`. The bitstream
immediately follows that count, without alignment between individual samples:

1. If `N = 0`, there are no value bits; the section is exactly four bytes.
2. For the first sample, store its complete 64-bit pattern.
3. For each subsequent sample, read a one-bit flag. Flag `0` repeats the previous
   value. Flag `1` is followed by a six-bit leading-zero count `l`, a six-bit
   width-minus-one field `m`, and then `w = m + 1` payload bits `p`.
4. For flag `1`, require `l + w <= 64`. Set `t = 64 - l - w` and reconstruct
   `value = previous_value XOR (p << t)` using unsigned 64-bit arithmetic.

Each flag `1` declares its own window; no previous-window reuse field exists.
After the last value, pad the final byte with zero low bits (zero to seven
bits). Nonzero padding or extra whole bytes are rejected. All 64-bit patterns
are preserved without conversion to floating point.

The decoder accepts the declared legacy window even when its payload is zero
or the window is wider than necessary. Such value encodings are accepted
despite being noncanonical; this does not relax index canonicality.

The encoder always writes canonical values: equal consecutive patterns use
flag `0`; otherwise it computes their XOR, strips all leading and trailing
zero bits, and writes flag `1` with that minimal window and its payload.
Thus decoding and re-encoding preserve tick/value pairs but need not preserve
legacy value bytes or hashes. No other window format is implemented here.

## Implementation ceilings

Default decoder ceilings are 100000 ticks, 100000 samples, 975021 input bytes,
and 1600256 requested owner allocation bytes. Owner allocation is
`sizeof(GdzCompact) + 2 * N * sizeof(uint64_t)`; allocator overhead is excluded.
The owner layout is private and is not part of the wire format.

Construction defaults to a 2575277-byte work ceiling for the temporary wire
payload plus candidate owner heap allocation. It excludes base-owner storage,
caller storage, allocator overhead, and runtime allocations. Custom limits
can only lower defaults; all ceilings must be positive except the sample
ceiling, which can be zero. These ceilings are implementation restrictions,
not additional wire fields. See the public headers for API lifetime and
output-storage requirements.
