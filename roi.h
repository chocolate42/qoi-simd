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

A ROI file has a 14 byte header, followed by any number of data "chunks" and an
8-byte end marker.

struct qoi_header_t {
	char     magic[4];   // magic bytes "roif"
	uint32_t width;      // image width in pixels (BE)
	uint32_t height;     // image height in pixels (BE)
	uint8_t  channels;   // 3 = RGB, 4 = RGBA
	uint8_t  colorspace; // 0 = sRGB with linear alpha, 1 = all channels linear
};

The colorspace byte in the header also has the second bit set if the file was
encoded without RLE. This is applied internally and allows for decode
optimisations.

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

LUMA op values are stored with a bias, for example a 3 bit value is in the range
-4..3 inclusive, which is stored as 0..7 by adding 4

QOI_OP_RUN: xxxxx111
	1 byte op defining a run of repeating pixels, x=0..29 indicates runs of 1..30
	respectively. x=30 and x=31 is reserved for use by QOI_OP_RGB and QOI_OP_RGBA

QOI_OP_LUMA232: bbrrggg0
  1 byte op that stores vg_r and vg_b in 2 bits, vg in 3 bits

QOI_OP_LUMA464: gggggg01 bbbbrrrr
  2 byte op that stores vg_r and vg_b in 4 bits, vg in 6 bits

QOI_OP_LUMA777: ggggg011 rrrrrrgg bbbbbbbr
  3 byte op that stores vg_r, vg_b and vg in 7 bits

QOI_OP_RGB: 11110111 gggggggg rrrrrrrr bbbbbbbb
  4 byte op that stores vg_r, vg_b and vg in 8 bits, without any bias

QOI_OP_RGBA: 11111111 aaaaaaaa
	2 byte op that stores the current alpha value. Always followed by an RGB op
	to fully define a pixel

The byte stream's end is marked with 7 0x00 bytes followed a single 0x01 byte.

Unlike most qoi-like formats roi stores values within ops in little endian.
This allows for optimisations on little-endian hardware, most hardware.

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

enum codepath {scalar, sse};
typedef struct{
	enum codepath path;
} options;

#ifndef QOI_NO_STDIO

/* Encode raw RGB or RGBA pixels into a QOI image and write it to the file
system. The qoi_desc struct must be filled with the image width, height,
number of channels (3 = RGB, 4 = RGBA) and the colorspace.

The function returns 0 on failure (invalid parameters, or fopen or malloc
failed) or the number of bytes written on success. */

int qoi_write(const char *filename, const void *data, const qoi_desc *desc, const options *opt);

/* Encode directly from a PPM file to a QOI file

The function returns 0 on failure (invalid parameters, or fopen or malloc
failed) or 1 on success. */

int qoi_write_from_ppm(const char *ppm_f, const char *qoi_f, const options *opt);

/* Read and decode a QOI image from the file system. If channels is 0, the
number of channels from the file header is used. If channels is 3 or 4 the
output format will be forced into this number of channels.

The function either returns NULL on failure (invalid data, or malloc or fopen
failed) or a pointer to the decoded pixels. On success, the qoi_desc struct
will be filled with the description from the file header.

The returned pixel data should be QOI_FREE()d after use. */

void *qoi_read(const char *filename, qoi_desc *desc, int channels);

/* Decode directly from a QOI file to a PPM file

The function returns 0 on failure (invalid parameters, or fopen or malloc
failed) or 1 on success. */

int qoi_read_to_ppm(const char *qoi_f, const char *ppm_f, const options *opt);

#endif /* QOI_NO_STDIO */

/* Encode raw RGB or RGBA pixels into a QOI image in memory.

The function either returns NULL on failure (invalid parameters or malloc
failed) or a pointer to the encoded data on success. On success the out_len
is set to the size in bytes of the encoded data.

The returned qoi data should be QOI_FREE()d after use. */

void *qoi_encode(const void *data, const qoi_desc *desc, int *out_len, const options *opt);

/* Decode a QOI image from memory.

The function either returns NULL on failure (invalid parameters or malloc
failed) or a pointer to the decoded pixels. On success, the qoi_desc struct
is filled with the description from the file header.

The returned pixel data should be QOI_FREE()d after use. */

void *qoi_decode(const void *data, int size, qoi_desc *desc, int channels);

#ifdef __cplusplus
}
#endif
#endif /* QOI_H */

/* -----------------------------------------------------------------------------
Implementation */

#ifdef QOI_IMPLEMENTATION
#include <inttypes.h>
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

#define QOI_OP_LUMA232 0x00 /* xxxxxxx0 */
#define QOI_OP_LUMA464 0x01 /* xxxxxx01 */
#define QOI_OP_LUMA777 0x03 /* xxxxx011 */
#define QOI_OP_RUN     0x07 /* xxxxx111 */
#define QOI_OP_RGB     0xf7 /* 11110111 */
#define QOI_OP_RGBA    0xff /* 11111111 */

#define QOI_OP_RUN30   0xef /* 11101111 */

#define QOI_MASK_1     0x01 /* 00000001 */
#define QOI_MASK_2     0x03 /* 00000011 */
#define QOI_MASK_3     0x07 /* 00000111 */

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
#define CHUNK 131072

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

static unsigned int qoi_read_32(const unsigned char *bytes, unsigned int *p) {
	unsigned int a = bytes[(*p)++];
	unsigned int b = bytes[(*p)++];
	unsigned int c = bytes[(*p)++];
	unsigned int d = bytes[(*p)++];
	return a << 24 | b << 16 | c << 8 | d;
}

static void qoi_encode_init(const qoi_desc *desc, unsigned char *bytes, int *p, qoi_rgba_t *px_prev, const options *opt) {
	qoi_write_32(bytes, p, QOI_MAGIC);
	qoi_write_32(bytes, p, desc->width);
	qoi_write_32(bytes, p, desc->height);
	bytes[(*p)++] = desc->channels;
	bytes[(*p)++] = desc->colorspace;
	px_prev->rgba.r = 0;
	px_prev->rgba.g = 0;
	px_prev->rgba.b = 0;
	px_prev->rgba.a = desc->channels==3?0:255;//to simplify ENC_READ_RGB
}

typedef struct{
	unsigned char *bytes, *pixels;
	qoi_rgba_t px;
	unsigned int b, b_limit, b_present, p, p_limit, px_pos, run, pixel_cnt, pixel_curr;
} dec_state;

#include "roi_optimised.c"

//pointers to optimised encode functions
#define ENC_ARR_INDEX ((opt->path<<1)|(desc->channels-3))
static void (*enc_chunk_arr[])(const unsigned char*, unsigned char*, int*, unsigned int, qoi_rgba_t*, int*)={
	qoi_encode_chunk3_scalar, qoi_encode_chunk4_scalar, qoi_encode_chunk3_sse, qoi_encode_chunk4_sse
};
static void (*enc_finish_arr[])(const unsigned char*, unsigned char*, int*, unsigned int, qoi_rgba_t*, int*)={
	qoi_encode_chunk3_scalar, qoi_encode_chunk4_scalar, qoi_encode_chunk3_scalar, qoi_encode_chunk4_scalar
};

#define DEC_ARR_INDEX (((desc->channels-3)<<1)|(channels-3))
static void (*dec_arr[])(dec_state*)={dec_in3out3, dec_in3out4, dec_in4out3, dec_in4out4};

void *qoi_encode(const void *data, const qoi_desc *desc, int *out_len, const options *opt) {
	int i, max_size, p=0, run=0;
	unsigned char *bytes;
	qoi_rgba_t px_prev;

	if (
		data == NULL || out_len == NULL || desc == NULL ||
		desc->width == 0 || desc->height == 0 ||
		desc->channels < 3 || desc->channels > 4 ||
		desc->colorspace > 1 ||
		desc->height >= QOI_PIXELS_MAX / desc->width ||
		opt->path>1
	)
		return NULL;

	max_size =
		desc->width * desc->height * (desc->channels + (desc->channels==4?2:1)) +
		QOI_HEADER_SIZE + sizeof(qoi_padding);

	if(!(bytes = (unsigned char *) QOI_MALLOC(max_size)))
		return NULL;

	qoi_encode_init(desc, bytes, &p, &px_prev, opt);
	if((desc->width * desc->height)/CHUNK)//encode most of the input as the largest multiple of chunk size for simd
		enc_chunk_arr[ENC_ARR_INDEX]((const unsigned char *)data, bytes, &p, (desc->width * desc->height)-((desc->width * desc->height)%CHUNK), &px_prev, &run);
	if((desc->width * desc->height)%CHUNK)//encode the trailing input scalar
		enc_finish_arr[ENC_ARR_INDEX]((const unsigned char *)data + (((desc->width * desc->height)-((desc->width * desc->height)%CHUNK))*desc->channels),
			bytes, &p, ((desc->width * desc->height)%CHUNK), &px_prev, &run);
	DUMP_RUN(run);
	for (i = 0; i < (int)sizeof(qoi_padding); i++)
		bytes[p++] = qoi_padding[i];
	*out_len = p;
	return bytes;
}

void *qoi_decode(const void *data, int size, qoi_desc *desc, int channels) {
	unsigned int header_magic;
	dec_state s={0};

	if (
		data == NULL || desc == NULL ||
		(channels != 0 && channels != 3 && channels != 4) ||
		size < QOI_HEADER_SIZE + (int)sizeof(qoi_padding)
	)
		return NULL;

	s.bytes=(unsigned char*)data;

	header_magic = qoi_read_32(s.bytes, &(s.b));
	desc->width = qoi_read_32(s.bytes, &(s.b));
	desc->height = qoi_read_32(s.bytes, &(s.b));
	desc->channels = s.bytes[s.b++];
	desc->colorspace = s.bytes[s.b++];

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

	s.pixel_cnt=desc->width * desc->height;
	s.p_limit=s.pixel_cnt*channels;
	if(!(s.pixels = QOI_MALLOC(s.p_limit)))
		return NULL;
	s.b_limit=size;
	s.b_present=size;
	s.px.rgba.a=255;

	dec_arr[DEC_ARR_INDEX](&s);

	return s.pixels;
}

#ifndef QOI_NO_STDIO
#include <stdio.h>

//decode to a format that contains raw pixels in RGB/A
static int qoi_read_to_file(FILE *fi, const char *out_f, char *head, int head_len, qoi_desc *desc, int channels, const options *opt){
	dec_state s={0};
	FILE *fo;
	unsigned int advancing;

	if(
		desc->width==0 || desc->height==0 ||
		desc->channels<3 || desc->channels>4 ||
		desc->colorspace>3 ||
		opt->path>1
	)
		goto BADEXIT0;

	if(!(fo=fopen(out_f, "wb")))
		goto BADEXIT0;

	if(head_len){
		if(head_len!=fwrite(head, 1, head_len, fo))
			goto BADEXIT1;
	}

	s.b_limit=CHUNK*(desc->channels==3?2:3);
	if(!(s.bytes=QOI_MALLOC(s.b_limit)))
		goto BADEXIT1;
	s.p_limit=CHUNK*channels;
	if(!(s.pixels=QOI_MALLOC(s.p_limit)))
		goto BADEXIT2;
	s.px.rgba.a=255;
	s.pixel_cnt=desc->width*desc->height;
	while(s.pixel_curr!=s.pixel_cnt){
		advancing=s.pixel_curr;
		s.b_present+=fread(s.bytes+s.b_present, 1, s.b_limit-s.b_present, fi);
		dec_arr[DEC_ARR_INDEX](&s);
		if(s.px_pos!=fwrite(s.pixels, 1, s.px_pos, fo))
			goto BADEXIT3;
		memmove(s.bytes, s.bytes+s.b, s.b_present-s.b);
		s.b_present-=s.b;
		s.b=0;
		s.px_pos=0;
		if(advancing==s.pixel_curr)//truncated input
			goto BADEXIT3;
	}
	QOI_FREE(s.pixels);
	QOI_FREE(s.bytes);
	fclose(fo);
	return 0;
	BADEXIT3:
	QOI_FREE(s.pixels);
	BADEXIT2:
	QOI_FREE(s.bytes);
	BADEXIT1:
	fclose(fo);
	BADEXIT0:
	return 1;
}

static int file_to_desc(FILE *fi, qoi_desc *desc){
	unsigned char head[14];
	if(14!=fread(head, 1, 14, fi))
		return 1;
	if(QOI_MAGIC!=(head[0] << 24 | head[1] << 16 | head[2] << 8 | head[3]))
		return 1;
	desc->width = head[4] << 24 | head[5] << 16 | head[6] << 8 | head[7];
	desc->height = head[8] << 24 | head[9] << 16 | head[10] << 8 | head[11];
	desc->channels = head[12];
	desc->colorspace = head[13];
	return 0;
}

int qoi_read_to_pam(const char *qoi_f, const char *pam_f, const options *opt) {
	char head[128];
	FILE *fi;
	qoi_desc desc;
	if(!(fi=fopen(qoi_f, "rb")))
		goto BADEXIT0;
	if(file_to_desc(fi, &desc))
		goto BADEXIT1;

	sprintf(head, "P7\nWIDTH %u\nHEIGHT %u\nDEPTH %u\nMAXVAL 255\nTUPLTYPE RGB%s\nENDHDR\n", desc.width, desc.height, desc.channels, desc.channels==3?"":"_ALPHA");

	if(qoi_read_to_file(fi, pam_f, head, strlen(head), &desc, desc.channels, opt))
		goto BADEXIT1;

	fclose(fi);
	return 0;
	BADEXIT1:
	fclose(fi);
	BADEXIT0:
	return 1;
}

int qoi_read_to_ppm(const char *qoi_f, const char *ppm_f, const options *opt) {
	char head[128];
	FILE *fi;
	qoi_desc desc;
	if(!(fi=fopen(qoi_f, "rb")))
		goto BADEXIT0;
	if(file_to_desc(fi, &desc))
		goto BADEXIT1;

	sprintf(head, "P6 %u %u 255\n", desc.width, desc.height);

	if(qoi_read_to_file(fi, ppm_f, head, strlen(head), &desc, 3, opt))
		goto BADEXIT1;

	fclose(fi);
	return 0;
	BADEXIT1:
	fclose(fi);
	BADEXIT0:
	return 1;
}

//process from an opened raw file directly
static inline int qoi_write_from_file(FILE *fi, const char *qoi_f, qoi_desc *desc, const options *opt){
	FILE *fo;
	int p=0, run=0;
	qoi_rgba_t px_prev;
	unsigned char *in, *out;
	unsigned int i, pixels;

	if(!(fo = fopen(qoi_f, "wb")))
		goto BADEXIT0;

	if(!(in=QOI_MALLOC((CHUNK*desc->channels)+1)))
		goto BADEXIT1;
	if(!(out=QOI_MALLOC(CHUNK*(desc->channels==3?4:6))))
		goto BADEXIT2;

	qoi_encode_init(desc, out, &p, &px_prev, opt);
	if(p!=fwrite(out, 1, p, fo))
		goto BADEXIT3;

	pixels=desc->width*desc->height;
	for(i=0;(i+CHUNK)<=pixels;i+=CHUNK){
		if((CHUNK*desc->channels)!=fread(in, 1, CHUNK*desc->channels, fi))
			goto BADEXIT3;
		p=0;
		enc_chunk_arr[ENC_ARR_INDEX](in, out, &p, CHUNK, &px_prev, &run);
		if(p!=fwrite(out, 1, p, fo))
			goto BADEXIT3;
	}
	if(i<pixels){
		if(((pixels-i)*desc->channels)!=fread(in, 1, (pixels-i)*desc->channels, fi))
			goto BADEXIT3;
		p=0;
		enc_finish_arr[ENC_ARR_INDEX](in, out, &p, (pixels-i), &px_prev, &run);
		if(p!=fwrite(out, 1, p, fo))
			goto BADEXIT3;
	}
	p=0;
	for(;run>=30;run-=30)
		out[p++]=QOI_OP_RUN30;
	if(run){
		out[p++] = QOI_OP_RUN | ((run - 1)<<3);
		if(p!=fwrite(out, 1, p, fo))
			goto BADEXIT3;
	}
	if(sizeof(qoi_padding)!=fwrite(qoi_padding, 1, sizeof(qoi_padding), fo))
		goto BADEXIT3;

	QOI_FREE(out);
	QOI_FREE(in);
	fclose(fo);
	return 0;
	BADEXIT3:
	QOI_FREE(out);
	BADEXIT2:
	QOI_FREE(in);
	BADEXIT1:
	fclose(fo);
	BADEXIT0:
	return 1;
}

#define isspace(num) (num==' '||((num>=0x09) && (num<=0x0d)))
#define isdigit(num) ((num>='0') && (num<='9'))

#define PAM_READ1 do{ \
	if(1!=fread(&t, 1, 1, fi)) \
		goto BADEXIT1; \
}while(0)

//Read a variable from a ppm header
#define PAM_SPACE_NUM(var) do{ \
	if(!isspace(t)) \
		goto BADEXIT1; \
	do { \
		PAM_READ1; \
	} while(isspace(t)); \
	if(!isdigit(t)) \
		goto BADEXIT1; \
	while(isdigit(t)){ \
		var*=10; \
		var+=(t-'0'); \
		PAM_READ1; \
	} \
}while(0);

#define PAM_EXPECT(val) do{ \
	PAM_READ1; \
	if(t!=val) \
		goto BADEXIT1; \
}while(0)

#define PAM_COMMENT do{ \
	while(t!='\n'){ \
		PAM_READ1; \
	} \
}while(0)

int qoi_write_from_pam(const char *pam_f, const char *qoi_f, const options *opt) {
	qoi_desc desc;
	char *token[]={"WIDTH", "HEIGHT", "DEPTH", "MAXVAL", "ENDHDR\n"};
	unsigned int hval[4]={0};
	unsigned char t;
	unsigned int i, j;
	FILE *fi;

	if(!(fi = fopen(pam_f, "rb")))
		goto BADEXIT0;

	PAM_EXPECT('P');
	PAM_EXPECT('7');
	PAM_EXPECT('\n');

	while(1){//read header line by line
		PAM_READ1;
		if(t=='\n')//empty line
			continue;
		if(t=='#'){//comment
			PAM_COMMENT;
			continue;
		}
		for(i=0;i<5;++i){
			if(t==token[i][0])
				break;
		}
		if(i==5){//irrelevant token
			PAM_COMMENT;
			continue;
		}
		for(j=1;token[i][j];++j){
			PAM_READ1;
			if(t!=token[i][j])
				break;
		}
		if(token[i][j]){
			PAM_COMMENT;
			continue;
		}
		if(i==4)//ENDHDR
			break;
		//WIDTH HEIGHT DEPTH MAXVAL
		if(hval[i])//there can be only one
			goto BADEXIT1;
		PAM_READ1;
		PAM_SPACE_NUM(hval[i]);
	}
	if(hval[0]==0 || hval[1]==0 || hval[2]<3 || hval[2]>4 || hval[3]>255 )
		goto BADEXIT1;
	desc.width=hval[0];
	desc.height=hval[1];
	desc.channels=hval[2];
	desc.colorspace=0;

	if(qoi_write_from_file(fi, qoi_f, &desc, opt))
		goto BADEXIT1;

	fclose(fi);
	return 0;
	BADEXIT1:
	fclose(fi);
	BADEXIT0:
	return 1;
}

int qoi_write_from_ppm(const char *ppm_f, const char *qoi_f, const options *opt) {
	qoi_desc desc={0};
	unsigned char t;
	unsigned int maxval=0;
	FILE *fi;

	if(!(fi = fopen(ppm_f, "rb")))
		goto BADEXIT0;

	PAM_EXPECT('P');
	PAM_EXPECT('6');
	PAM_READ1;
	PAM_SPACE_NUM(desc.width);
	PAM_SPACE_NUM(desc.height);
	PAM_SPACE_NUM(maxval);
	if(t=='#'){
		PAM_COMMENT;
	}
	if(!isspace(t))
		goto BADEXIT1;
	if(maxval>255)
		goto BADEXIT1;
	desc.channels=3;
	desc.colorspace=0;
	if(qoi_write_from_file(fi, qoi_f, &desc, opt))
		goto BADEXIT1;

	fclose(fi);
	return 0;
	BADEXIT1:
	fclose(fi);
	BADEXIT0:
	return 1;
}

int qoi_write(const char *filename, const void *data, const qoi_desc *desc, const options *opt) {
	FILE *f = fopen(filename, "wb");
	int size, err;
	void *encoded;

	if (!f)
		return 0;

	encoded = qoi_encode(data, desc, &size, opt);
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
