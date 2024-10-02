/*

ROI:
Copyright (c) 2024, Matthew Ling
SPDX-License-Identifier: MIT

Adapted from QOI - The "Quite OK Image" format
Copyright (c) 2021, Dominic Szablewski - https://phoboslab.org


-- About

ROI is a simple byte format for storing lossless images. There are a handful
of ways it does this, at its core each pixel is diffed from the previous
pixel and stored in up to a 4 byte encoding for RGB or up to a 6 byte encoding
for RGBA.


-- Synopsis

// Define `QOI_IMPLEMENTATION` in *one* C/C++ file before including this
// library to create the implementation.

#define QOI_IMPLEMENTATION
#include "roi.h"

// Encode and store an RGBA buffer to the file system. The qoi_desc describes
// the input pixel data.
qoi_write("image_new.qoi", rgba_pixels, &(qoi_desc){
	.width = 1920,
	.height = 1080,
	.channels = 4,
	.colorspace = QOI_SRGB
});

// Load and decode a QOI image from the file system into a 32bbp RGBA buffer.
// The qoi_desc struct will be filled with the width, height, number of channels
// and colorspace read from the file header.
qoi_desc desc;
void *rgba_pixels = qoi_read("image.qoi", &desc, 4);


-- Documentation

This library provides the following functions;
- qoi_read: Read and decode a QOI file to memory
- qoi_decode: Decode an in-memory QOI image to memory
- qoi_write: Encode and write a QOI file from memory
- qoi_write_from_ppm: Directly encode and write from a PPM file to a QOI file
- qoi_encode: Encode an rgb/a buffer into a QOI image in memory

See the function declaration below for the signature and more information.

If you don't want/need the qoi_read and qoi_write functions, you can define
QOI_NO_STDIO before including this library.

This library uses malloc() and free(). To supply your own malloc implementation
you can define QOI_MALLOC and QOI_FREE before including this library.


-- Data Format

A QOI file has a 14 byte header, followed by any number of data "chunks" and an
8-byte end marker.

struct qoi_header_t {
	char     magic[4];   // magic bytes "roif"
	uint32_t width;      // image width in pixels (BE)
	uint32_t height;     // image height in pixels (BE)
	uint8_t  channels;   // 3 = RGB, 4 = RGBA
	uint8_t  colorspace; // 0 = sRGB with linear alpha, 1 = all channels linear
};

Images are encoded row by row, left to right, top to bottom. The decoder and
encoder start with {r: 0, g: 0, b: 0, a: 255} as the previous pixel value. An
image is complete when all pixels specified by width * height have been covered.

The color channels are assumed to not be premultiplied with the alpha channel
("un-premultiplied alpha").

The format has 6 ops defined:
* QOI_OP_LUMA232, QOI_OP_LUMA464, QOI_OP_LUMA777, QOI_OP_RGB: RGB ops, encode in
  1/2/3/4 bytes respectively
* QOI_OP_RUN: 1 byte RLE repeating the previous pixel 1..30 times
* QOI_OP_RGBA: 2 byte encoding used whenever alpha changes, followed by an RGB
  op to encode the RGB elements

In detail:

vr, vg, vb are red green blue diffed from the previous pixel respectively

vg_r, vg_b are vr and vb respectively diffed from vg

Values are stored with a bias, for example a 3 bit value is in the range -4..3
inclusive, which is stored as 0..7 by adding 4

QOI_OP_RUN: 111xxxxx
	1 byte op defining a run of repeating pixels, x=0..29 indicates runs of 1..30
	respectively. x=30 and x=31 is reserved for use by QOI_OP_RGB and QOI_OP_RGBA

QOI_OP_LUMA232: 0rrbbggg
  1 byte op that stores vg_r and vg_b in 2 bits, vg in 3 bits

QOI_OP_LUMA464: 10gggggg rrrrbbbb
  2 byte op that stores vg_r and vg_b in 4 bits, vg in 6 bits

QOI_OP_LUMA777: 110bbbbb bbrrrrrr rggggggg
  3 byte op that stores vg_r, vg_b and vg in 7 bits

QOI_OP_RGB: 11111110 rrrrrrrr gggggggg bbbbbbbb
  4 byte op that stores vg_r, vg_b and vg in 8 bits

QOI_OP_RGBA: 11111111 aaaaaaaa
	2 byte op that stores the current alpha value. Always followed by an RGB op
	to fully define a pixel

The byte stream's end is marked with 7 0x00 bytes followed a single 0x01 byte.

*/

/* -----------------------------------------------------------------------------
Header - Public functions */

#ifndef QOI_H
#define QOI_H

#ifdef __cplusplus
extern "C" {
#endif

/* A pointer to a qoi_desc struct has to be supplied to all of qoi's functions.
It describes either the input format (for qoi_write and qoi_encode), or is
filled with the description read from the file header (for qoi_read and
qoi_decode).

The colorspace in this qoi_desc is an enum where
	0 = sRGB, i.e. gamma scaled RGB channels and a linear alpha channel
	1 = all channels are linear
You may use the constants QOI_SRGB or QOI_LINEAR. The colorspace is purely
informative. It will be saved to the file header, but does not affect
how chunks are en-/decoded. */

#define QOI_SRGB   0
#define QOI_LINEAR 1

typedef struct {
	unsigned int width;
	unsigned int height;
	unsigned char channels;
	unsigned char colorspace;
} qoi_desc;

#ifndef QOI_NO_STDIO

/* Encode raw RGB or RGBA pixels into a QOI image and write it to the file
system. The qoi_desc struct must be filled with the image width, height,
number of channels (3 = RGB, 4 = RGBA) and the colorspace.

The function returns 0 on failure (invalid parameters, or fopen or malloc
failed) or the number of bytes written on success. */

int qoi_write(const char *filename, const void *data, const qoi_desc *desc);

/* Encode directly from a PPM file to a QOI file

The function returns 0 on failure (invalid parameters, or fopen or malloc
failed) or 1 on success. */

int qoi_write_from_ppm(const char *ppm_f, const char *qoi_f, int norle);

/* Read and decode a QOI image from the file system. If channels is 0, the
number of channels from the file header is used. If channels is 3 or 4 the
output format will be forced into this number of channels.

The function either returns NULL on failure (invalid data, or malloc or fopen
failed) or a pointer to the decoded pixels. On success, the qoi_desc struct
will be filled with the description from the file header.

The returned pixel data should be free()d after use. */

void *qoi_read(const char *filename, qoi_desc *desc, int channels);

#endif /* QOI_NO_STDIO */

/* Encode raw RGB or RGBA pixels into a QOI image in memory.

The function either returns NULL on failure (invalid parameters or malloc
failed) or a pointer to the encoded data on success. On success the out_len
is set to the size in bytes of the encoded data.

The returned qoi data should be free()d after use. */

void *qoi_encode(const void *data, const qoi_desc *desc, int *out_len);

/* Decode a QOI image from memory.

The function either returns NULL on failure (invalid parameters or malloc
failed) or a pointer to the decoded pixels. On success, the qoi_desc struct
is filled with the description from the file header.

The returned pixel data should be free()d after use. */

void *qoi_decode(const void *data, int size, qoi_desc *desc, int channels);

#ifdef __cplusplus
}
#endif
#endif /* QOI_H */

/* -----------------------------------------------------------------------------
Implementation */

#ifdef QOI_IMPLEMENTATION
#include <stdlib.h>
#include <string.h>
#ifdef QOI_SSE
#include <immintrin.h>
#endif

#ifndef QOI_MALLOC
	#ifdef QOI_SSE
		#define QOI_MALLOC(sz) _mm_malloc(sz, 64)
		#define QOI_FREE(p)    _mm_free(p)
	#else
		#define QOI_MALLOC(sz) malloc(sz)
		#define QOI_FREE(p)    free(p)
	#endif
#endif
#ifndef QOI_ZEROARR
	#define QOI_ZEROARR(a) memset((a),0,sizeof(a))
#endif

#define QOI_OP_LUMA232 0x00 /* 0xxxxxxx */
#define QOI_OP_LUMA464 0x80 /* 10xxxxxx */
#define QOI_OP_LUMA777 0xc0 /* 110xxxxx */
#define QOI_OP_RUN     0xe0 /* 111xxxxx */
#define QOI_OP_RGB     0xfe /* 11111110 */
#define QOI_OP_RGBA    0xff /* 11111111 */

#define QOI_MASK_1     0x80 /* 10000000 */
#define QOI_MASK_2     0xc0 /* 11000000 */
#define QOI_MASK_3     0xe0 /* 11100000 */

#define QOI_MAGIC \
	(((unsigned int)'r') << 24 | ((unsigned int)'o') << 16 | \
	 ((unsigned int)'i') <<  8 | ((unsigned int)'f'))
#define QOI_HEADER_SIZE 14

/* 2GB is the max file size that this implementation can safely handle. We guard
against anything larger than that, assuming the worst case with 5 bytes per
pixel, rounded down to a nice clean value. 400 million pixels ought to be
enough for anybody. */
#define QOI_PIXELS_MAX ((unsigned int)400000000)

//the number of pixels to process per chunk when chunk processing
//must be a multiple of 64 for simd alignment
//65536 chosen by scalar experimentation on Ryzen 7840u
#define CHUNK 65536

typedef union {
	struct { unsigned char r, g, b, a; } rgba;
	unsigned int v;
} qoi_rgba_t;

static const unsigned char qoi_padding[8] = {0,0,0,0,0,0,0,1};

static void qoi_write_32(unsigned char *bytes, int *p, unsigned int v) {
	bytes[(*p)++] = (0xff000000 & v) >> 24;
	bytes[(*p)++] = (0x00ff0000 & v) >> 16;
	bytes[(*p)++] = (0x0000ff00 & v) >> 8;
	bytes[(*p)++] = (0x000000ff & v);
}

static unsigned int qoi_read_32(const unsigned char *bytes, int *p) {
	unsigned int a = bytes[(*p)++];
	unsigned int b = bytes[(*p)++];
	unsigned int c = bytes[(*p)++];
	unsigned int d = bytes[(*p)++];
	return a << 24 | b << 16 | c << 8 | d;
}

#define RGB_ENC_SCALAR do{\
	signed char vr = px.rgba.r - px_prev.rgba.r;\
	signed char vg = px.rgba.g - px_prev.rgba.g;\
	signed char vb = px.rgba.b - px_prev.rgba.b;\
	signed char vg_r = vr - vg;\
	signed char vg_b = vb - vg;\
	unsigned char ar = (vg_r<0)?(-vg_r)-1:vg_r;\
	unsigned char ag = (vg<0)?(-vg)-1:vg;\
	unsigned char ab = (vg_b<0)?(-vg_b)-1:vg_b;\
	unsigned char arb = ar|ab;\
	if ( arb < 2 && ag  < 4 ) {\
		bytes[p++] = QOI_OP_LUMA232 | ((vg_r + 2) << 5) | ((vg_b + 2) << 3) | (vg + 4);\
	} else if ( arb <  8 && ag  < 32 ) {\
		bytes[p++] = QOI_OP_LUMA464    | (vg   + 32);\
		bytes[p++] = ((vg_r + 8) << 4) | (vg_b +  8);\
	} else if ( (arb|ag) < 64 ) {\
		bytes[p++] = QOI_OP_LUMA777     | ((vg_b + 64)>>2);\
		bytes[p++] = (((vg_b+64)&3)<<6) | ((vg_r + 64)>>1);\
		bytes[p++] = (((vg_r+64)&1)<<7) | vg+64;\
	} else {\
		bytes[p++] = QOI_OP_RGB;\
		bytes[p++] = vg_r+128;\
		bytes[p++] = vg+128;\
		bytes[p++] = vg_b+128;\
	}\
}while(0)

#ifdef QOI_SSE
	//TODO SSE implementation of qoi_encode_chunk3
	qoi_encode_chunk3_sse()...
#endif

void qoi_encode_chunk3_scalar(const unsigned char *pixels, unsigned char *bytes, int *pp, unsigned int pixel_cnt, qoi_rgba_t *pixel_prev, int *r){
	int p=*pp;
	int run=*r;
	qoi_rgba_t px, px_prev=*pixel_prev;
	unsigned int px_pos, px_end=(pixel_cnt-1)*3;
	px.rgba.a=255;
	for (px_pos = 0; px_pos <= px_end; px_pos += 3) {
		px.rgba.r = pixels[px_pos + 0];
		px.rgba.g = pixels[px_pos + 1];
		px.rgba.b = pixels[px_pos + 2];
		while(px.v == px_prev.v) {
			++run;
			if(px_pos == px_end){
				for(;run>=30;run-=30)
					bytes[p++] = 0xFD;
				goto DONE;
			}
			px_pos+=3;
			px.rgba.r = pixels[px_pos + 0];
			px.rgba.g = pixels[px_pos + 1];
			px.rgba.b = pixels[px_pos + 2];
		}
		for(;run>=30;run-=30)
			bytes[p++] = 0xFD;
		if (run) {
			bytes[p++] = QOI_OP_RUN | (run - 1);
			run = 0;
		}
		RGB_ENC_SCALAR;
		px_prev = px;
	}
	DONE:
	*pixel_prev=px_prev;
	*r=run;
	*pp=p;
}

void qoi_encode_chunk3_scalar_norle(const unsigned char *pixels, unsigned char *bytes, int *pp, unsigned int pixel_cnt, qoi_rgba_t *pixel_prev, int *r){
	int p=*pp;
	qoi_rgba_t px, px_prev=*pixel_prev;
	unsigned int px_pos, px_end=(pixel_cnt-1)*3;
	px.rgba.a=255;
	for (px_pos = 0; px_pos <= px_end; px_pos += 3) {
		px.rgba.r = pixels[px_pos + 0];
		px.rgba.g = pixels[px_pos + 1];
		px.rgba.b = pixels[px_pos + 2];
		RGB_ENC_SCALAR;
		px_prev = px;
	}
	*pixel_prev=px_prev;
	*pp=p;
}

void qoi_encode_chunk4_scalar(const unsigned char *pixels, unsigned char *bytes, int *pp, unsigned int pixel_cnt, qoi_rgba_t *pixel_prev, int *r){
	int p=*pp, run=*r;
	qoi_rgba_t px, px_prev=*pixel_prev;
	unsigned int px_pos, px_end=(pixel_cnt-1)*4;
	for (px_pos = 0; px_pos <= px_end; px_pos += 4) {
		px.rgba.r = pixels[px_pos + 0];
		px.rgba.g = pixels[px_pos + 1];
		px.rgba.b = pixels[px_pos + 2];
		px.rgba.a = pixels[px_pos + 3];

		while(px.v == px_prev.v) {
			++run;
			if(px_pos == px_end) {
				for(;run>=30;run-=30)
					bytes[p++] = 0xFD;
				goto DONE;
			}
			px_pos+=4;
			px.rgba.r = pixels[px_pos + 0];
			px.rgba.g = pixels[px_pos + 1];
			px.rgba.b = pixels[px_pos + 2];
			px.rgba.a = pixels[px_pos + 3];
		}
		for(;run>=30;run-=30)
			bytes[p++] = 0xFD;
		if (run) {
			bytes[p++] = QOI_OP_RUN | (run - 1);
			run = 0;
		}
		if(px.rgba.a!=px_prev.rgba.a){
			bytes[p++] = QOI_OP_RGBA;
			bytes[p++] = px.rgba.a;
		}
		RGB_ENC_SCALAR;
		px_prev = px;
	}
	DONE:
	*pixel_prev=px_prev;
	*r=run;
	*pp=p;
}

void qoi_encode_chunk4_scalar_norle(const unsigned char *pixels, unsigned char *bytes, int *pp, unsigned int pixel_cnt, qoi_rgba_t *pixel_prev, int *r){
	int p=*pp;
	qoi_rgba_t px, px_prev=*pixel_prev;
	unsigned int px_pos, px_end=(pixel_cnt-1)*4;
	for (px_pos = 0; px_pos <= px_end; px_pos += 4) {
		px.rgba.r = pixels[px_pos + 0];
		px.rgba.g = pixels[px_pos + 1];
		px.rgba.b = pixels[px_pos + 2];
		px.rgba.a = pixels[px_pos + 3];
		if(px.rgba.a!=px_prev.rgba.a){
			bytes[p++] = QOI_OP_RGBA;
			bytes[p++] = px.rgba.a;
		}
		RGB_ENC_SCALAR;
		px_prev = px;
	}
	*pixel_prev=px_prev;
	*pp=p;
}

void *qoi_encode_init(const qoi_desc *desc, unsigned char *bytes, int *p, qoi_rgba_t *px_prev) {
	qoi_write_32(bytes, p, QOI_MAGIC);
	qoi_write_32(bytes, p, desc->width);
	qoi_write_32(bytes, p, desc->height);
	bytes[(*p)++] = desc->channels;
	bytes[(*p)++] = desc->colorspace;

	px_prev->rgba.r = 0;
	px_prev->rgba.g = 0;
	px_prev->rgba.b = 0;
	px_prev->rgba.a = 255;
}

void *qoi_encode(const void *data, const qoi_desc *desc, int *out_len) {
	int i, max_size, norle=(desc->colorspace>>1)&1, p=0, run=0;
	unsigned char *bytes;
	qoi_rgba_t px_prev;
	void (*enc_chunk)(const unsigned char*, unsigned char*, int*, unsigned int, qoi_rgba_t*, int*);
	void (*enc_finish)(const unsigned char*, unsigned char*, int*, unsigned int, qoi_rgba_t*, int*);

	if (
		data == NULL || out_len == NULL || desc == NULL ||
		desc->width == 0 || desc->height == 0 ||
		desc->channels < 3 || desc->channels > 4 ||
		desc->colorspace > 3 ||
		desc->height >= QOI_PIXELS_MAX / desc->width
	)
		return NULL;

	//use optimised encode functions depending on input and settings
#ifdef QOI_SSE
	if(desc->channels==3)
		enc_chunk=qoi_encode_chunk3_sse;
	else if(norle)
		enc_chunk=qoi_encode_chunk4_scalar_norle;
	else
		enc_chunk=qoi_encode_chunk4_scalar;
#else
	if(norle && desc->channels==3)
		enc_chunk=qoi_encode_chunk3_scalar_norle;
	else if(norle && desc->channels==4)
		enc_chunk=qoi_encode_chunk4_scalar_norle;
	else if(desc->channels==3)
		enc_chunk=qoi_encode_chunk3_scalar;
	else
		enc_chunk=qoi_encode_chunk4_scalar;
#endif
	if(norle && desc->channels==3)
		enc_finish=qoi_encode_chunk3_scalar_norle;
	else if(norle && desc->channels==4)
		enc_finish=qoi_encode_chunk4_scalar_norle;
	else if(desc->channels==3)
		enc_finish=qoi_encode_chunk3_scalar;
	else
		enc_finish=qoi_encode_chunk4_scalar;

	max_size =
		desc->width * desc->height * (desc->channels + (desc->channels==4?2:1)) +
		QOI_HEADER_SIZE + sizeof(qoi_padding);

	if(!(bytes = (unsigned char *) QOI_MALLOC(max_size)))
		return NULL;

	qoi_encode_init(desc, bytes, &p, &px_prev);
	if((desc->width * desc->height)/CHUNK)//encode most of the input as the largest multiple of chunk size for simd
		enc_chunk((const unsigned char *)data, bytes, &p, (desc->width * desc->height)-((desc->width * desc->height)%CHUNK), &px_prev, &run);
	if((desc->width * desc->height)%CHUNK)//encode the trailing input scalar
		enc_finish((const unsigned char *)data + (((desc->width * desc->height)-((desc->width * desc->height)%CHUNK))*desc->channels), 
			bytes, &p, ((desc->width * desc->height)%CHUNK), &px_prev, &run);

	if (run)
		bytes[p++] = QOI_OP_RUN | (run - 1);
	for (i = 0; i < (int)sizeof(qoi_padding); i++)
		bytes[p++] = qoi_padding[i];

	*out_len = p;
	return bytes;
}

#define QOI_DECODE_COMMON \
	int b1 = bytes[p++]; \
	if ((b1 & QOI_MASK_1) == QOI_OP_LUMA232) { \
		int vg = (b1 & 7) - 4; \
		px.rgba.r += vg - 2 + ((b1 >> 5) & 3); \
		px.rgba.g += vg; \
		px.rgba.b += vg - 2 + ((b1 >> 3) & 3); \
	} \
	else if ((b1 & QOI_MASK_2) == QOI_OP_LUMA464) { \
		int b2 = bytes[p++]; \
		int vg = (b1 & 0x3f) - 32; \
		px.rgba.r += vg - 8 + ((b2 >> 4) & 0x0f); \
		px.rgba.g += vg; \
		px.rgba.b += vg - 8 +  (b2       & 0x0f); \
	} \
	else if ((b1 & QOI_MASK_3) == QOI_OP_LUMA777) { \
		int b2 = bytes[p++]; \
		int b3 = bytes[p++]; \
		int vg = (b3 & 0x7f) - 64; \
		px.rgba.r += vg - 64 + ((b2 & 0x3f)<<1) + (b3>>7); \
		px.rgba.g += vg; \
		px.rgba.b += vg - 64 + ((b1 & 0x1f)<<2) + (b2>>6); \
	} \
	else if (b1 == QOI_OP_RGB) { \
		int b2 = bytes[p++]; \
		int vg = bytes[p++]; \
		vg-=128; \
		int b3 = bytes[p++]; \
		px.rgba.r += vg - 128 + b2; \
		px.rgba.g += vg; \
		px.rgba.b += vg - 128 + b3; \
	}

void *qoi_decode(const void *data, int size, qoi_desc *desc, int channels) {
	const unsigned char *bytes;
	unsigned int header_magic;
	unsigned char *pixels;
	qoi_rgba_t px;
	int px_len, chunks_len, px_pos;
	int p = 0, run = 0;

	if (
		data == NULL || desc == NULL ||
		(channels != 0 && channels != 3 && channels != 4) ||
		size < QOI_HEADER_SIZE + (int)sizeof(qoi_padding)
	)
		return NULL;

	bytes = (const unsigned char *)data;

	header_magic = qoi_read_32(bytes, &p);
	desc->width = qoi_read_32(bytes, &p);
	desc->height = qoi_read_32(bytes, &p);
	desc->channels = bytes[p++];
	desc->colorspace = bytes[p++];

	if (
		desc->width == 0 || desc->height == 0 ||
		desc->channels < 3 || desc->channels > 4 ||
		desc->colorspace > 3 ||
		header_magic != QOI_MAGIC ||
		desc->height >= QOI_PIXELS_MAX / desc->width
	)
		return NULL;

	if (channels == 0)
		channels = desc->channels;

	px_len = desc->width * desc->height * channels;
	pixels = (unsigned char *) QOI_MALLOC(px_len);
	if (!pixels)
		return NULL;

	px.rgba.r = 0;
	px.rgba.g = 0;
	px.rgba.b = 0;
	px.rgba.a = 255;

	chunks_len = size - (int)sizeof(qoi_padding);
	if(desc->channels==4){//no shortcuts
		for (px_pos = 0; px_pos < px_len; px_pos += 4) {
			if (run > 0)
				run--;
			else if (p < chunks_len) {
				OP_RGBA_GOTO:
				QOI_DECODE_COMMON
				else if (b1 == QOI_OP_RGBA) {
					px.rgba.a = bytes[p++];
					goto OP_RGBA_GOTO;
				}
				else if ((b1 & QOI_MASK_3) == QOI_OP_RUN)
					run = (b1 & 0x1f);
			}
			pixels[px_pos + 0] = px.rgba.r;
			pixels[px_pos + 1] = px.rgba.g;
			pixels[px_pos + 2] = px.rgba.b;
			if(channels==4)
				pixels[px_pos + 3] = px.rgba.a;
		}
	}
	else if(channels==3){//alpha shortcut, rgb out
		for (px_pos = 0; px_pos < px_len; px_pos += 3) {
			if (run > 0)
				run--;
			else if (p < chunks_len) {
				QOI_DECODE_COMMON
				else if ((b1 & QOI_MASK_3) == QOI_OP_RUN)
					run = (b1 & 0x1f);
			}
			pixels[px_pos + 0] = px.rgba.r;
			pixels[px_pos + 1] = px.rgba.g;
			pixels[px_pos + 2] = px.rgba.b;
		}
	}
	else{//alpha shortcut, rgba out
		for (px_pos = 0; px_pos < px_len; px_pos += 4) {
			if (run > 0)
				run--;
			else if (p < chunks_len) {
				QOI_DECODE_COMMON
				else if ((b1 & QOI_MASK_3) == QOI_OP_RUN)
					run = (b1 & 0x1f);
			}
			pixels[px_pos + 0] = px.rgba.r;
			pixels[px_pos + 1] = px.rgba.g;
			pixels[px_pos + 2] = px.rgba.b;
			pixels[px_pos + 3] = px.rgba.a;
		}
	}
	return pixels;
}

#ifndef QOI_NO_STDIO
#include <stdio.h>

int qoi_write_from_ppm(const char *ppm_f, const char *qoi_f, int norle) {
	void (*enc_chunk)(const unsigned char*, unsigned char*, int*, unsigned int, qoi_rgba_t*, int*);
	void (*enc_finish)(const unsigned char*, unsigned char*, int*, unsigned int, qoi_rgba_t*, int*);
	int p=0, run=0;
	qoi_desc desc;
	unsigned char t, *in, *out;
	unsigned int height=0, i, maxval=0, pixels, width=0;
	qoi_rgba_t px_prev;
	FILE *fi = fopen(ppm_f, "rb"), *fo=fopen(qoi_f, "wb");

#ifdef QOI_SSE
	enc_chunk=qoi_encode_chunk3_sse;
#else
	if(norle)
		enc_chunk=qoi_encode_chunk3_scalar_norle;
	else
		enc_chunk=qoi_encode_chunk3_scalar;
#endif
	if(norle)
		enc_finish=qoi_encode_chunk3_scalar_norle;
	else
		enc_finish=qoi_encode_chunk3_scalar;

	fread(&t, 1, 1, fi);
	if(t!='P')
		return 0;
	fread(&t, 1, 1, fi);
	if(t!='6')
		return 0;
	do fread(&t, 1, 1, fi); while((t==' ')||(t=='\t')||(t=='\n')||(t=='\r'));
	if((t<'0')||(t>'9'))
		return 0;
	while((t>='0')&&(t<='9')){
		width*=10;
		width+=(t-'0');
		fread(&t, 1, 1, fi);
	}
	if((t!=' ')&&(t!='\t')&&(t!='\n')&&(t!='\r'))
		return 0;
	while((t==' ')||(t=='\t')||(t=='\n')||(t=='\r'))
		fread(&t, 1, 1, fi);
	if((t<'0')||(t>'9'))
		return 0;
	while((t>='0')&&(t<='9')){
		height*=10;
		height+=(t-'0');
		fread(&t, 1, 1, fi);
	}
	if((t!=' ')&&(t!='\t')&&(t!='\n')&&(t!='\r'))
		return 0;
	while((t==' ')||(t=='\t')||(t=='\n')||(t=='\r'))
		fread(&t, 1, 1, fi);
	if((t<'0')||(t>'9'))
		return 0;
	while((t>='0')&&(t<='9')){
		maxval*=10;
		maxval+=(t-'0');
		fread(&t, 1, 1, fi);
	}
	if((t!=' ')&&(t!='\t')&&(t!='\n')&&(t!='\r'))
		return 0;
	if(maxval>255)//multi-byte not supported
		return 0;
	desc.width=width;
	desc.height=height;
	desc.channels=3;
	desc.colorspace=norle?2:0;

	in=QOI_MALLOC(CHUNK*3);
	out=QOI_MALLOC(CHUNK*4);
	qoi_encode_init(&desc, out, &p, &px_prev);
	fwrite(out, 1, p, fo);
	pixels=width*height;
	for(i=0;(i+CHUNK)<=pixels;i+=CHUNK){
		fread(in, 1, CHUNK*3, fi);
		p=0;
		enc_chunk(in, out, &p, CHUNK, &px_prev, &run);
		fwrite(out, 1, p, fo);
	}
	if(i<pixels){
		fread(in, 1, (pixels-i)*3, fi);
		p=0;
		enc_finish(in, out, &p, (pixels-i), &px_prev, &run);
		fwrite(out, 1, p, fo);
	}
	if(run)
		out[0] = QOI_OP_RUN | (run - 1);

	fwrite(out, 1, 1, fo);
	fwrite(qoi_padding, 1, sizeof(qoi_padding), fo);
	fclose(fo);
	return 1;
}

int qoi_write(const char *filename, const void *data, const qoi_desc *desc) {
	FILE *f = fopen(filename, "wb");
	int size, err;
	void *encoded;

	if (!f)
		return 0;

	encoded = qoi_encode(data, desc, &size);
	if (!encoded) {
		fclose(f);
		return 0;
	}

	fwrite(encoded, 1, size, f);
	fflush(f);
	err = ferror(f);
	fclose(f);

	QOI_FREE(encoded);
	return err ? 0 : size;
}

void *qoi_read(const char *filename, qoi_desc *desc, int channels) {
	FILE *f = fopen(filename, "rb");
	int size, bytes_read;
	void *pixels, *data;

	if (!f)
		return NULL;

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	if (size <= 0 || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return NULL;
	}

	if (!(data = QOI_MALLOC(size))) {
		fclose(f);
		return NULL;
	}

	bytes_read = fread(data, 1, size, f);
	fclose(f);
	pixels = (bytes_read != size) ? NULL : qoi_decode(data, bytes_read, desc, channels);
	QOI_FREE(data);
	return pixels;
}

#endif /* QOI_NO_STDIO */
#endif /* QOI_IMPLEMENTATION */
