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
* Has a 3-byte RGB op, qoi only has 1/2/4 byte RGB ops
* Takes the concept of encoding red and blue relative to green from qoi's 2-byte QOI_OP_LUMA op (poor man's colour transform), and applies it also to the 1-byte and 3-byte ops
* There is no indexing op. Indexing is not simd-friendly, is detrimental when the image is further compressed by a generic compressor, and using the opcode space for other ops on average reduces the encoded size even when not compressed further
* Maximum run length stored in a single byte reduced to 30 from 62. The opcode space is better spent elsewhere
* The alpha op is a 2 byte alpha followed by an RGB op (QOI_OP_LUMA232/QOI_OP_LUMA464/QOI_OP_LUMA777/QOI_OP_RGB), so a pixel with an alpha change is stored in 3-6 bytes (qoi always consumes 5 bytes with an alpha change)
* Values are stored little-endian, which is friendlier to simd as well as being slightly more efficient on little-endian hardware generally

## Benchmarks (single thread, 64 bit, Linux)

```
LPCB benchmark
           size       ratio encode   decode   codepath    Notes
ppm        3462571880
qoi        1993357658 0.576 0m7.336s 0m6.785s Scalar
roi        1840638105 0.532 0m5.583s 0m5.361s Scalar
           1840638105 0.532 0m3.113s          Scalar mlut Identical output, uses 80MiB LUT to encode
           1840638105 0.532 0m2.677s          SSE         Identical output
```

