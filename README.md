# qoi-simd
qoi and qoi-like implementations optionally using simd

## roi
roi is a qoi-like format using the following ops:

```
#define QOI_OP_LUMA232
#define QOI_OP_LUMA464
#define QOI_OP_LUMA777
#define QOI_OP_RUN
#define QOI_OP_RGB
#define QOI_OP_RGBA
```

### Differences from qoi
* Takes the concept of encoding red and blue relative to green from qoi's 2-byte QOI_OP_LUMA op (poor man's colour transform), and applies it also to the 1-byte and 3-byte ops
* There is no indexing op. Indexing is not simd-friendly, it also is detrimental on average to space-efficiency if the image is further compressed with a generic compressor
* Values are stored little-endian, which is friendlier to simd as well as being slightly more efficient on little-endian hardware generally
* Runs 1..30 can be stored in a single byte (vs qoi 1..62 in a single byte), the opcode space saved is better spent on other ops

## Benchmarks (single thread, 64 bit, Linux)

```
# Grand total for LPCB (lossless photo compression benchmark, RGB)
     decode_ms  encode_ms  decode_mpps  encode_mpps  size_kb   rate  Description
qoi:  64.6        64.7        166.86        166.68    18192   57.6%  Scalar codepath, RLE
roi:  37.8        19.3        285.33        558.16    17324   54.8%  SSE codepath on encode, no RLE
roi:  37.6        49.7        286.97        216.83    17324   54.8%  Scalar codepath on encode, no RLE
roi:  39.8        56.5        271.23        190.95    16799   53.2%  Scalar codepath, RLE
```

