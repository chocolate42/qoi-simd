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
