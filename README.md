# qoi-simd
qoi and qoi-like implementations optionally using simd

## roi
roi is a qoi-like format  using the following ops:

```
#define QOI_OP_LUMA232 0x00 /* 0xxxxxxx */
#define QOI_OP_LUMA464 0x80 /* 10xxxxxx */
#define QOI_OP_LUMA777 0xc0 /* 110xxxxx */
#define QOI_OP_RUN     0xe0 /* 111xxxxx */
#define QOI_OP_RGB     0xfe /* 11111110 */
#define QOI_OP_RGBA    0xff /* 11111111 */
```

* It takes the concept of encoding red and blue relative to green from qoi's 2-byte QOI_OP_LUMA op, and applies it also to the 1-byte and 3-byte ops
* There is no indexing op unlike qoi. Indexing is not simd-friendly, it also is detrimental on average to space-efficiency if the image is further compressed with a generic compressor

## soi
soi is a qoi-like format using the following ops:

```
#define QOI_OP_LUMA555 0x00 /* 0xxxxxxx */
#define QOI_OP_LUMA222 0x80 /* 10xxxxxx */
#define QOI_OP_LUMA777 0xc0 /* 110xxxxx */
#define QOI_OP_RUN     0xe0 /* 111xxxxx */
#define QOI_OP_RGB     0xfe /* 11111110 */
#define QOI_OP_RGBA    0xff /* 11111111 */
```

Slightly less space-efficient than roi on average, but cheaper to compute because the rgb values that each LUMA op can take are matched.

## Scalar Benchmarks

```
# Grand total for /home/f40/Pictures/Screenshots
          decode ms   encode ms   decode mpps   encode mpps   size kb    rate
qoi:            4.5         4.2        437.49        464.37       720    9.5%
roi:            3.5         3.8        552.92        520.60       751   10.0%
soi:            3.2         3.2        614.92        616.80       791   10.5%
```
