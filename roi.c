/*

ROI:
Copyright (c) 2024, Matthew Ling


-- About

ROI is a simple byte format for storing lossless images. There are a handful
of ways it does this, at its core each pixel is diffed from the previous
pixel and stored in up to a 4 byte encoding for RGB or up to a 6 byte encoding
for RGBA.

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

#define QOI_OP_LUMA232 0x00 /* xxxxxxx0 */
#define QOI_OP_LUMA464 0x01 /* xxxxxx01 */
#define QOI_OP_LUMA777 0x03 /* xxxxx011 */
#define QOI_OP_RUN     0x07 /* xxxxx111 */
#define QOI_OP_RGB     0xf7 /* 11110111 */
#define QOI_OP_RGBA    0xff /* 11111111 */

#define QOI_OP_RUN_FULL 0xef /* 11101111 */
#define QOI_RUN_FULL_VAL (30)

#define QOI_MASK_1     0x01 /* 00000001 */
#define QOI_MASK_2     0x03 /* 00000011 */
#define QOI_MASK_3     0x07 /* 00000111 */

#define QOI_MAGIC \
	(((unsigned int)'r') << 24 | ((unsigned int)'o') << 16 | \
	 ((unsigned int)'i') <<  8 | ((unsigned int)'f'))

#define EXT_STR "roi"

#define QOI_PIXEL_WORST_CASE (desc->channels==4?6:4)

#define DUMP_RUN(rrr) do{ \
	for(;rrr>=QOI_RUN_FULL_VAL;rrr-=QOI_RUN_FULL_VAL) \
		s.bytes[s.b++] = QOI_OP_RUN_FULL; \
	if (rrr) { \
		s.bytes[s.b++] = QOI_OP_RUN | ((rrr - 1)<<3); \
		rrr = 0; \
	} \
}while(0)

//	px.rgba.r = pixels[px_pos + 0];
//	px.rgba.g = pixels[px_pos + 1];
//	px.rgba.b = pixels[px_pos + 2];
#define ENC_READ_RGB do{ \
	memcpy(&px, s.pixels+s.px_pos, 4); \
	px.v&=0x00FFFFFF; \
}while(0)

//optimised encode functions////////////////////////////////////////////////////

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
		s.bytes[s.b++]=QOI_OP_LUMA232|((vg_b+2)<<6)|((vg_r+2)<<4)|((vg+4)<<1);\
	} else if ( arb <  8 && ag  < 32 ) {\
		*(unsigned int*)(s.bytes+s.b)=QOI_OP_LUMA464|((vg_b+8)<<12)|((vg_r+8)<<8)|((vg+32)<<2); \
		s.b+=2; \
	} else if ( (arb|ag) < 64 ) {\
		*(unsigned int*)(s.bytes+s.b)=QOI_OP_LUMA777|((vg_b+64)<<17)|((vg_r+64)<<10)|((vg+64)<<3); \
		s.b+=3; \
	} else {\
		s.bytes[s.b++]=QOI_OP_RGB; \
		s.bytes[s.b++]=vg; \
		s.bytes[s.b++]=vg_r; \
		s.bytes[s.b++]=vg_b; \
	}\
}while(0)

typedef struct{
	unsigned char *bytes, *pixels, *pixels_alloc;
	unsigned int b, px_pos, run, pixel_cnt;
} enc_state;

int gen_mlut(const char *path){
	signed char vg, vg_r, vg_b;
	unsigned char ar, ag, ab, arb, *mlut, t;
	unsigned int val;
	FILE *fo;
	qoi_rgba_t d={0};
	mlut=malloc(256*256*256*5);
	memset(mlut, 0, 256*256*256*5);
	for(int rr=-128;rr<128;++rr){
	for(int gg=-128;gg<128;++gg){
	for(int bb=-128;bb<128;++bb){
		d.rgba.r=rr;
		d.rgba.g=gg;
		d.rgba.b=bb;
		vg=d.rgba.g;
		vg_r = d.rgba.r - vg;
		vg_b = d.rgba.b - vg;
		ar = (vg_r<0)?(-vg_r)-1:vg_r;
		ag = (vg<0)?(-vg)-1:vg;
		ab = (vg_b<0)?(-vg_b)-1:vg_b;
		arb = ar|ab;
		if ( arb < 2 && ag  < 4 ) {
			mlut[(5*d.v)]=1;
			mlut[(5*d.v)+1]=QOI_OP_LUMA232|((vg_b+2)<<6)|((vg_r+2)<<4)|((vg+4)<<1);
		} else if ( arb <  8 && ag  < 32 ) {
			mlut[(5*d.v)]=2;
			val=QOI_OP_LUMA464|((vg_b+8)<<12)|((vg_r+8)<<8)|((vg+32)<<2);
			mlut[(5*d.v)+1]=val&255;
			mlut[(5*d.v)+2]=(val>>8)&255;
		} else if ( (arb|ag) < 64 ) {
			mlut[(5*d.v)]=3;
			val=QOI_OP_LUMA777|((vg_b+64)<<17)|((vg_r+64)<<10)|((vg+64)<<3);
			mlut[(5*d.v)+1]=val&255;
			mlut[(5*d.v)+2]=(val>>8)&255;
			mlut[(5*d.v)+3]=(val>>16)&255;
		} else {
			mlut[(5*d.v)]=4;
			mlut[(5*d.v)+1]=QOI_OP_RGB;
			t=vg;
			mlut[(5*d.v)+2]=t;
			t=vg_r;
			mlut[(5*d.v)+3]=t;
			t=vg_b;
			mlut[(5*d.v)+4]=t;
		}
	}
	}
	}
	fo=fopen(path, "wb");
	fwrite(mlut, 1, 256*256*256*5, fo);
	fclose(fo);
	free(mlut);
	return 0;
}

#ifdef QOI_MLUT_EMBED
extern char _binary_roi_mlut_start[];
unsigned char *qoi_mlut=(unsigned char*)_binary_roi_mlut_start;
#else
unsigned char *qoi_mlut=NULL;
#endif

static enc_state qoi_encode_chunk3_mlut(enc_state s){
	qoi_rgba_t px, px_prev, diff={0};
	unsigned int px_end=(s.pixel_cnt-1)*3;
	memcpy(&px_prev, s.pixels+s.px_pos-3, 4);
	px_prev.v&=0x00FFFFFF;
	for (; s.px_pos <= px_end; s.px_pos += 3) {
		ENC_READ_RGB;
		while(px.v == px_prev.v) {
			++s.run;
			if(s.px_pos == px_end){
				for(;s.run>=QOI_RUN_FULL_VAL;s.run-=QOI_RUN_FULL_VAL)
					s.bytes[s.b++] = QOI_OP_RUN_FULL;
				s.px_pos+=3;
				return s;
			}
			s.px_pos+=3;
			ENC_READ_RGB;
		}
		DUMP_RUN(s.run);
		diff.rgba.r=px.rgba.r-px_prev.rgba.r;
		diff.rgba.g=px.rgba.g-px_prev.rgba.g;
		diff.rgba.b=px.rgba.b-px_prev.rgba.b;
		*(unsigned int*)(s.bytes+s.b)=*(unsigned int*)(qoi_mlut+(diff.v*5)+1);
		s.b+=qoi_mlut[diff.v*5];
		px_prev = px;
	}
	return s;
}

static enc_state qoi_encode_chunk4_mlut(enc_state s){
	qoi_rgba_t px, px_prev, diff={0};
	unsigned int px_end=(s.pixel_cnt-1)*4;
	memcpy(&px_prev, s.pixels+s.px_pos-4, 4);
	for (; s.px_pos <= px_end; s.px_pos += 4) {
		ENC_READ_RGBA;
		while(px.v == px_prev.v) {
			++s.run;
			if(s.px_pos == px_end) {
				for(;s.run>=QOI_RUN_FULL_VAL;s.run-=QOI_RUN_FULL_VAL)
					s.bytes[s.b++] = QOI_OP_RUN_FULL;
				s.px_pos+=4;
				return s;
			}
			s.px_pos+=4;
			ENC_READ_RGBA;
		}
		DUMP_RUN(s.run);
		if(px.rgba.a!=px_prev.rgba.a){
			s.bytes[s.b++] = QOI_OP_RGBA;
			s.bytes[s.b++] = px.rgba.a;
		}
		diff.rgba.r=px.rgba.r-px_prev.rgba.r;
		diff.rgba.g=px.rgba.g-px_prev.rgba.g;
		diff.rgba.b=px.rgba.b-px_prev.rgba.b;
		*(unsigned int*)(s.bytes+s.b)=*(unsigned int*)(qoi_mlut+(diff.v*5)+1);
		s.b+=qoi_mlut[diff.v*5];
		px_prev = px;
	}
	return s;
}

static enc_state qoi_encode_chunk3_scalar(enc_state s){
	qoi_rgba_t px, px_prev={0};
	unsigned int px_end=(s.pixel_cnt-1)*3;
	memcpy(&px_prev, s.pixels+s.px_pos-3, 4);
	px_prev.v&=0x00FFFFFF;
	for (; s.px_pos <= px_end; s.px_pos += 3) {
		ENC_READ_RGB;
		while(px.v == px_prev.v) {
			++s.run;
			if(s.px_pos == px_end){
				for(;s.run>=QOI_RUN_FULL_VAL;s.run-=QOI_RUN_FULL_VAL)
					s.bytes[s.b++] = QOI_OP_RUN_FULL;
				s.px_pos+=3;
				return s;
			}
			s.px_pos+=3;
			ENC_READ_RGB;
		}
		DUMP_RUN(s.run);
		RGB_ENC_SCALAR;
		px_prev = px;
	}
	return s;
}

static enc_state qoi_encode_chunk4_scalar(enc_state s){
	qoi_rgba_t px, px_prev;
	unsigned int px_end=(s.pixel_cnt-1)*4;
	memcpy(&px_prev, s.pixels+s.px_pos-4, 4);
	for (; s.px_pos <= px_end; s.px_pos += 4) {
		ENC_READ_RGBA;
		while(px.v == px_prev.v) {
			++s.run;
			if(s.px_pos == px_end) {
				for(;s.run>=QOI_RUN_FULL_VAL;s.run-=QOI_RUN_FULL_VAL)
					s.bytes[s.b++] = QOI_OP_RUN_FULL;
				s.px_pos+=4;
				return s;
			}
			s.px_pos+=4;
			ENC_READ_RGBA;
		}
		DUMP_RUN(s.run);
		if(px.rgba.a!=px_prev.rgba.a){
			s.bytes[s.b++] = QOI_OP_RGBA;
			s.bytes[s.b++] = px.rgba.a;
		}
		RGB_ENC_SCALAR;
		px_prev = px;
	}
	return s;
}

static enc_state (*enc_finish[])(enc_state)={qoi_encode_chunk3_scalar, qoi_encode_chunk4_scalar};

#ifdef QOI_SSE
//load the next 16 bytes, diff pixels
#define LOAD16(diff, offset, psize) do{ \
	w1=_mm_loadu_si128((__m128i const*)(s.pixels+s.px_pos+offset)); \
	diff=_mm_loadu_si128((__m128i const*)((s.pixels+s.px_pos+offset)-psize)); \
	diff=_mm_sub_epi8(w1, diff); \
}while(0)

//de-interleave one plane from 3 vectors containing RGB
#define PLANAR_SHUFFLE(plane, source1, source2, source3, shufflemask) do{ \
	plane=_mm_blendv_epi8(source1, source2, blend1); \
	plane=_mm_blendv_epi8(plane, source3, blend2); \
	plane=_mm_shuffle_epi8(plane, shufflemask); \
}while(0)

//do (x<0)?(-x)-1:x for a single plane
#define ABSOLUTER(plane, absolute) do{ \
	w1=_mm_adds_epi8(plane, _mm_set1_epi8(1)); \
	w1=_mm_abs_epi8(w1); \
	w2=_mm_cmpgt_epi8(_mm_setzero_si128(), plane); \
	absolute=_mm_blendv_epi8(plane, w1, w2); \
}while(0)

//the following 2 macros:
// normalise value depending on opcode
// shift value to where it is in the op
// combine into 4 result vectors
#define NORMALISE_SHIFT16_EMBIGGEN(plane, opmask, value, shift) do{ \
	w1=_mm_add_epi8(plane, value); \
	w1=_mm_and_si128(w1, opmask); \
	w2=_mm_unpacklo_epi8(w1, _mm_setzero_si128()); \
	w2=_mm_slli_epi16(w2, shift); \
	w3=_mm_unpacklo_epi16(w2, _mm_setzero_si128()); \
	res0=_mm_or_si128(w3, res0); \
	w3=_mm_unpackhi_epi16(w2, _mm_setzero_si128()); \
	res1=_mm_or_si128(w3, res1); \
	w2=_mm_unpackhi_epi8(w1, _mm_setzero_si128()); \
	w2=_mm_slli_epi16(w2, shift); \
	w3=_mm_unpacklo_epi16(w2, _mm_setzero_si128()); \
	res2=_mm_or_si128(w3, res2); \
	w3=_mm_unpackhi_epi16(w2, _mm_setzero_si128()); \
	res3=_mm_or_si128(w3, res3); \
}while(0)

#define NORMALISE_SHIFT32_EMBIGGEN(plane, opmask, value, shift) do{ \
	w1=_mm_add_epi8(plane, value); \
	w1=_mm_and_si128(w1, opmask); \
	w2=_mm_unpacklo_epi8(w1, _mm_setzero_si128()); \
	w3=_mm_unpacklo_epi16(w2, _mm_setzero_si128()); \
	w3=_mm_slli_epi32(w3, shift); \
	res0=_mm_or_si128(w3, res0); \
	w3=_mm_unpackhi_epi16(w2, _mm_setzero_si128()); \
	w3=_mm_slli_epi32(w3, shift); \
	res1=_mm_or_si128(w3, res1); \
	w2=_mm_unpackhi_epi8(w1, _mm_setzero_si128()); \
	w3=_mm_unpacklo_epi16(w2, _mm_setzero_si128()); \
	w3=_mm_slli_epi32(w3, shift); \
	res2=_mm_or_si128(w3, res2); \
	w3=_mm_unpackhi_epi16(w2, _mm_setzero_si128()); \
	w3=_mm_slli_epi32(w3, shift); \
	res3=_mm_or_si128(w3, res3); \
}while(0)

#define NORMALISE_INPLACE(plane, opmask, value) do{ \
	w1=_mm_add_epi8(plane, value); \
	plane=_mm_blendv_epi8(plane, w1, opmask); \
}while(0)

//sse lut
static const uint8_t writer_lut[4096] = {//shuffle used bytes in output vector to the left ready for writing
	0,4,8,12,0,0,0,0,0,0,0,0,0,0,0,0, 0,1,4,8,12,0,0,0,0,0,0,0,0,0,0,0, 0,1,2,4,8,12,0,0,0,0,0,0,0,0,0,0, 0,1,2,3,4,8,12,0,0,0,0,0,0,0,0,0,
	0,4,5,8,12,0,0,0,0,0,0,0,0,0,0,0, 0,1,4,5,8,12,0,0,0,0,0,0,0,0,0,0, 0,1,2,4,5,8,12,0,0,0,0,0,0,0,0,0, 0,1,2,3,4,5,8,12,0,0,0,0,0,0,0,0,
	0,4,5,6,8,12,0,0,0,0,0,0,0,0,0,0, 0,1,4,5,6,8,12,0,0,0,0,0,0,0,0,0, 0,1,2,4,5,6,8,12,0,0,0,0,0,0,0,0, 0,1,2,3,4,5,6,8,12,0,0,0,0,0,0,0,
	0,4,5,6,7,8,12,0,0,0,0,0,0,0,0,0, 0,1,4,5,6,7,8,12,0,0,0,0,0,0,0,0, 0,1,2,4,5,6,7,8,12,0,0,0,0,0,0,0, 0,1,2,3,4,5,6,7,8,12,0,0,0,0,0,0,
	0,4,8,9,12,0,0,0,0,0,0,0,0,0,0,0, 0,1,4,8,9,12,0,0,0,0,0,0,0,0,0,0, 0,1,2,4,8,9,12,0,0,0,0,0,0,0,0,0, 0,1,2,3,4,8,9,12,0,0,0,0,0,0,0,0,
	0,4,5,8,9,12,0,0,0,0,0,0,0,0,0,0, 0,1,4,5,8,9,12,0,0,0,0,0,0,0,0,0, 0,1,2,4,5,8,9,12,0,0,0,0,0,0,0,0, 0,1,2,3,4,5,8,9,12,0,0,0,0,0,0,0,
	0,4,5,6,8,9,12,0,0,0,0,0,0,0,0,0, 0,1,4,5,6,8,9,12,0,0,0,0,0,0,0,0, 0,1,2,4,5,6,8,9,12,0,0,0,0,0,0,0, 0,1,2,3,4,5,6,8,9,12,0,0,0,0,0,0,
	0,4,5,6,7,8,9,12,0,0,0,0,0,0,0,0, 0,1,4,5,6,7,8,9,12,0,0,0,0,0,0,0, 0,1,2,4,5,6,7,8,9,12,0,0,0,0,0,0, 0,1,2,3,4,5,6,7,8,9,12,0,0,0,0,0,
	0,4,8,9,10,12,0,0,0,0,0,0,0,0,0,0, 0,1,4,8,9,10,12,0,0,0,0,0,0,0,0,0, 0,1,2,4,8,9,10,12,0,0,0,0,0,0,0,0, 0,1,2,3,4,8,9,10,12,0,0,0,0,0,0,0,
	0,4,5,8,9,10,12,0,0,0,0,0,0,0,0,0, 0,1,4,5,8,9,10,12,0,0,0,0,0,0,0,0, 0,1,2,4,5,8,9,10,12,0,0,0,0,0,0,0, 0,1,2,3,4,5,8,9,10,12,0,0,0,0,0,0,
	0,4,5,6,8,9,10,12,0,0,0,0,0,0,0,0, 0,1,4,5,6,8,9,10,12,0,0,0,0,0,0,0, 0,1,2,4,5,6,8,9,10,12,0,0,0,0,0,0, 0,1,2,3,4,5,6,8,9,10,12,0,0,0,0,0,
	0,4,5,6,7,8,9,10,12,0,0,0,0,0,0,0, 0,1,4,5,6,7,8,9,10,12,0,0,0,0,0,0, 0,1,2,4,5,6,7,8,9,10,12,0,0,0,0,0, 0,1,2,3,4,5,6,7,8,9,10,12,0,0,0,0,
	0,4,8,9,10,11,12,0,0,0,0,0,0,0,0,0, 0,1,4,8,9,10,11,12,0,0,0,0,0,0,0,0, 0,1,2,4,8,9,10,11,12,0,0,0,0,0,0,0, 0,1,2,3,4,8,9,10,11,12,0,0,0,0,0,0,
	0,4,5,8,9,10,11,12,0,0,0,0,0,0,0,0, 0,1,4,5,8,9,10,11,12,0,0,0,0,0,0,0, 0,1,2,4,5,8,9,10,11,12,0,0,0,0,0,0, 0,1,2,3,4,5,8,9,10,11,12,0,0,0,0,0,
	0,4,5,6,8,9,10,11,12,0,0,0,0,0,0,0, 0,1,4,5,6,8,9,10,11,12,0,0,0,0,0,0, 0,1,2,4,5,6,8,9,10,11,12,0,0,0,0,0, 0,1,2,3,4,5,6,8,9,10,11,12,0,0,0,0,
	0,4,5,6,7,8,9,10,11,12,0,0,0,0,0,0, 0,1,4,5,6,7,8,9,10,11,12,0,0,0,0,0, 0,1,2,4,5,6,7,8,9,10,11,12,0,0,0,0, 0,1,2,3,4,5,6,7,8,9,10,11,12,0,0,0,
	0,4,8,12,13,0,0,0,0,0,0,0,0,0,0,0, 0,1,4,8,12,13,0,0,0,0,0,0,0,0,0,0, 0,1,2,4,8,12,13,0,0,0,0,0,0,0,0,0, 0,1,2,3,4,8,12,13,0,0,0,0,0,0,0,0,
	0,4,5,8,12,13,0,0,0,0,0,0,0,0,0,0, 0,1,4,5,8,12,13,0,0,0,0,0,0,0,0,0, 0,1,2,4,5,8,12,13,0,0,0,0,0,0,0,0, 0,1,2,3,4,5,8,12,13,0,0,0,0,0,0,0,
	0,4,5,6,8,12,13,0,0,0,0,0,0,0,0,0, 0,1,4,5,6,8,12,13,0,0,0,0,0,0,0,0, 0,1,2,4,5,6,8,12,13,0,0,0,0,0,0,0, 0,1,2,3,4,5,6,8,12,13,0,0,0,0,0,0,
	0,4,5,6,7,8,12,13,0,0,0,0,0,0,0,0, 0,1,4,5,6,7,8,12,13,0,0,0,0,0,0,0, 0,1,2,4,5,6,7,8,12,13,0,0,0,0,0,0, 0,1,2,3,4,5,6,7,8,12,13,0,0,0,0,0,
	0,4,8,9,12,13,0,0,0,0,0,0,0,0,0,0, 0,1,4,8,9,12,13,0,0,0,0,0,0,0,0,0, 0,1,2,4,8,9,12,13,0,0,0,0,0,0,0,0, 0,1,2,3,4,8,9,12,13,0,0,0,0,0,0,0,
	0,4,5,8,9,12,13,0,0,0,0,0,0,0,0,0, 0,1,4,5,8,9,12,13,0,0,0,0,0,0,0,0, 0,1,2,4,5,8,9,12,13,0,0,0,0,0,0,0, 0,1,2,3,4,5,8,9,12,13,0,0,0,0,0,0,
	0,4,5,6,8,9,12,13,0,0,0,0,0,0,0,0, 0,1,4,5,6,8,9,12,13,0,0,0,0,0,0,0, 0,1,2,4,5,6,8,9,12,13,0,0,0,0,0,0, 0,1,2,3,4,5,6,8,9,12,13,0,0,0,0,0,
	0,4,5,6,7,8,9,12,13,0,0,0,0,0,0,0, 0,1,4,5,6,7,8,9,12,13,0,0,0,0,0,0, 0,1,2,4,5,6,7,8,9,12,13,0,0,0,0,0, 0,1,2,3,4,5,6,7,8,9,12,13,0,0,0,0,
	0,4,8,9,10,12,13,0,0,0,0,0,0,0,0,0, 0,1,4,8,9,10,12,13,0,0,0,0,0,0,0,0, 0,1,2,4,8,9,10,12,13,0,0,0,0,0,0,0, 0,1,2,3,4,8,9,10,12,13,0,0,0,0,0,0,
	0,4,5,8,9,10,12,13,0,0,0,0,0,0,0,0, 0,1,4,5,8,9,10,12,13,0,0,0,0,0,0,0, 0,1,2,4,5,8,9,10,12,13,0,0,0,0,0,0, 0,1,2,3,4,5,8,9,10,12,13,0,0,0,0,0,
	0,4,5,6,8,9,10,12,13,0,0,0,0,0,0,0, 0,1,4,5,6,8,9,10,12,13,0,0,0,0,0,0, 0,1,2,4,5,6,8,9,10,12,13,0,0,0,0,0, 0,1,2,3,4,5,6,8,9,10,12,13,0,0,0,0,
	0,4,5,6,7,8,9,10,12,13,0,0,0,0,0,0, 0,1,4,5,6,7,8,9,10,12,13,0,0,0,0,0, 0,1,2,4,5,6,7,8,9,10,12,13,0,0,0,0, 0,1,2,3,4,5,6,7,8,9,10,12,13,0,0,0,
	0,4,8,9,10,11,12,13,0,0,0,0,0,0,0,0, 0,1,4,8,9,10,11,12,13,0,0,0,0,0,0,0, 0,1,2,4,8,9,10,11,12,13,0,0,0,0,0,0, 0,1,2,3,4,8,9,10,11,12,13,0,0,0,0,0,
	0,4,5,8,9,10,11,12,13,0,0,0,0,0,0,0, 0,1,4,5,8,9,10,11,12,13,0,0,0,0,0,0, 0,1,2,4,5,8,9,10,11,12,13,0,0,0,0,0, 0,1,2,3,4,5,8,9,10,11,12,13,0,0,0,0,
	0,4,5,6,8,9,10,11,12,13,0,0,0,0,0,0, 0,1,4,5,6,8,9,10,11,12,13,0,0,0,0,0, 0,1,2,4,5,6,8,9,10,11,12,13,0,0,0,0, 0,1,2,3,4,5,6,8,9,10,11,12,13,0,0,0,
	0,4,5,6,7,8,9,10,11,12,13,0,0,0,0,0, 0,1,4,5,6,7,8,9,10,11,12,13,0,0,0,0, 0,1,2,4,5,6,7,8,9,10,11,12,13,0,0,0, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,0,0,
	0,4,8,12,13,14,0,0,0,0,0,0,0,0,0,0, 0,1,4,8,12,13,14,0,0,0,0,0,0,0,0,0, 0,1,2,4,8,12,13,14,0,0,0,0,0,0,0,0, 0,1,2,3,4,8,12,13,14,0,0,0,0,0,0,0,
	0,4,5,8,12,13,14,0,0,0,0,0,0,0,0,0, 0,1,4,5,8,12,13,14,0,0,0,0,0,0,0,0, 0,1,2,4,5,8,12,13,14,0,0,0,0,0,0,0, 0,1,2,3,4,5,8,12,13,14,0,0,0,0,0,0,
	0,4,5,6,8,12,13,14,0,0,0,0,0,0,0,0, 0,1,4,5,6,8,12,13,14,0,0,0,0,0,0,0, 0,1,2,4,5,6,8,12,13,14,0,0,0,0,0,0, 0,1,2,3,4,5,6,8,12,13,14,0,0,0,0,0,
	0,4,5,6,7,8,12,13,14,0,0,0,0,0,0,0, 0,1,4,5,6,7,8,12,13,14,0,0,0,0,0,0, 0,1,2,4,5,6,7,8,12,13,14,0,0,0,0,0, 0,1,2,3,4,5,6,7,8,12,13,14,0,0,0,0,
	0,4,8,9,12,13,14,0,0,0,0,0,0,0,0,0, 0,1,4,8,9,12,13,14,0,0,0,0,0,0,0,0, 0,1,2,4,8,9,12,13,14,0,0,0,0,0,0,0, 0,1,2,3,4,8,9,12,13,14,0,0,0,0,0,0,
	0,4,5,8,9,12,13,14,0,0,0,0,0,0,0,0, 0,1,4,5,8,9,12,13,14,0,0,0,0,0,0,0, 0,1,2,4,5,8,9,12,13,14,0,0,0,0,0,0, 0,1,2,3,4,5,8,9,12,13,14,0,0,0,0,0,
	0,4,5,6,8,9,12,13,14,0,0,0,0,0,0,0, 0,1,4,5,6,8,9,12,13,14,0,0,0,0,0,0, 0,1,2,4,5,6,8,9,12,13,14,0,0,0,0,0, 0,1,2,3,4,5,6,8,9,12,13,14,0,0,0,0,
	0,4,5,6,7,8,9,12,13,14,0,0,0,0,0,0, 0,1,4,5,6,7,8,9,12,13,14,0,0,0,0,0, 0,1,2,4,5,6,7,8,9,12,13,14,0,0,0,0, 0,1,2,3,4,5,6,7,8,9,12,13,14,0,0,0,
	0,4,8,9,10,12,13,14,0,0,0,0,0,0,0,0, 0,1,4,8,9,10,12,13,14,0,0,0,0,0,0,0, 0,1,2,4,8,9,10,12,13,14,0,0,0,0,0,0, 0,1,2,3,4,8,9,10,12,13,14,0,0,0,0,0,
	0,4,5,8,9,10,12,13,14,0,0,0,0,0,0,0, 0,1,4,5,8,9,10,12,13,14,0,0,0,0,0,0, 0,1,2,4,5,8,9,10,12,13,14,0,0,0,0,0, 0,1,2,3,4,5,8,9,10,12,13,14,0,0,0,0,
	0,4,5,6,8,9,10,12,13,14,0,0,0,0,0,0, 0,1,4,5,6,8,9,10,12,13,14,0,0,0,0,0, 0,1,2,4,5,6,8,9,10,12,13,14,0,0,0,0, 0,1,2,3,4,5,6,8,9,10,12,13,14,0,0,0,
	0,4,5,6,7,8,9,10,12,13,14,0,0,0,0,0, 0,1,4,5,6,7,8,9,10,12,13,14,0,0,0,0, 0,1,2,4,5,6,7,8,9,10,12,13,14,0,0,0, 0,1,2,3,4,5,6,7,8,9,10,12,13,14,0,0,
	0,4,8,9,10,11,12,13,14,0,0,0,0,0,0,0, 0,1,4,8,9,10,11,12,13,14,0,0,0,0,0,0, 0,1,2,4,8,9,10,11,12,13,14,0,0,0,0,0, 0,1,2,3,4,8,9,10,11,12,13,14,0,0,0,0,
	0,4,5,8,9,10,11,12,13,14,0,0,0,0,0,0, 0,1,4,5,8,9,10,11,12,13,14,0,0,0,0,0, 0,1,2,4,5,8,9,10,11,12,13,14,0,0,0,0, 0,1,2,3,4,5,8,9,10,11,12,13,14,0,0,0,
	0,4,5,6,8,9,10,11,12,13,14,0,0,0,0,0, 0,1,4,5,6,8,9,10,11,12,13,14,0,0,0,0, 0,1,2,4,5,6,8,9,10,11,12,13,14,0,0,0, 0,1,2,3,4,5,6,8,9,10,11,12,13,14,0,0,
	0,4,5,6,7,8,9,10,11,12,13,14,0,0,0,0, 0,1,4,5,6,7,8,9,10,11,12,13,14,0,0,0, 0,1,2,4,5,6,7,8,9,10,11,12,13,14,0,0, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,0,
	0,4,8,12,13,14,15,0,0,0,0,0,0,0,0,0, 0,1,4,8,12,13,14,15,0,0,0,0,0,0,0,0, 0,1,2,4,8,12,13,14,15,0,0,0,0,0,0,0, 0,1,2,3,4,8,12,13,14,15,0,0,0,0,0,0,
	0,4,5,8,12,13,14,15,0,0,0,0,0,0,0,0, 0,1,4,5,8,12,13,14,15,0,0,0,0,0,0,0, 0,1,2,4,5,8,12,13,14,15,0,0,0,0,0,0, 0,1,2,3,4,5,8,12,13,14,15,0,0,0,0,0,
	0,4,5,6,8,12,13,14,15,0,0,0,0,0,0,0, 0,1,4,5,6,8,12,13,14,15,0,0,0,0,0,0, 0,1,2,4,5,6,8,12,13,14,15,0,0,0,0,0, 0,1,2,3,4,5,6,8,12,13,14,15,0,0,0,0,
	0,4,5,6,7,8,12,13,14,15,0,0,0,0,0,0, 0,1,4,5,6,7,8,12,13,14,15,0,0,0,0,0, 0,1,2,4,5,6,7,8,12,13,14,15,0,0,0,0, 0,1,2,3,4,5,6,7,8,12,13,14,15,0,0,0,
	0,4,8,9,12,13,14,15,0,0,0,0,0,0,0,0, 0,1,4,8,9,12,13,14,15,0,0,0,0,0,0,0, 0,1,2,4,8,9,12,13,14,15,0,0,0,0,0,0, 0,1,2,3,4,8,9,12,13,14,15,0,0,0,0,0,
	0,4,5,8,9,12,13,14,15,0,0,0,0,0,0,0, 0,1,4,5,8,9,12,13,14,15,0,0,0,0,0,0, 0,1,2,4,5,8,9,12,13,14,15,0,0,0,0,0, 0,1,2,3,4,5,8,9,12,13,14,15,0,0,0,0,
	0,4,5,6,8,9,12,13,14,15,0,0,0,0,0,0, 0,1,4,5,6,8,9,12,13,14,15,0,0,0,0,0, 0,1,2,4,5,6,8,9,12,13,14,15,0,0,0,0, 0,1,2,3,4,5,6,8,9,12,13,14,15,0,0,0,
	0,4,5,6,7,8,9,12,13,14,15,0,0,0,0,0, 0,1,4,5,6,7,8,9,12,13,14,15,0,0,0,0, 0,1,2,4,5,6,7,8,9,12,13,14,15,0,0,0, 0,1,2,3,4,5,6,7,8,9,12,13,14,15,0,0,
	0,4,8,9,10,12,13,14,15,0,0,0,0,0,0,0, 0,1,4,8,9,10,12,13,14,15,0,0,0,0,0,0, 0,1,2,4,8,9,10,12,13,14,15,0,0,0,0,0, 0,1,2,3,4,8,9,10,12,13,14,15,0,0,0,0,
	0,4,5,8,9,10,12,13,14,15,0,0,0,0,0,0, 0,1,4,5,8,9,10,12,13,14,15,0,0,0,0,0, 0,1,2,4,5,8,9,10,12,13,14,15,0,0,0,0, 0,1,2,3,4,5,8,9,10,12,13,14,15,0,0,0,
	0,4,5,6,8,9,10,12,13,14,15,0,0,0,0,0, 0,1,4,5,6,8,9,10,12,13,14,15,0,0,0,0, 0,1,2,4,5,6,8,9,10,12,13,14,15,0,0,0, 0,1,2,3,4,5,6,8,9,10,12,13,14,15,0,0,
	0,4,5,6,7,8,9,10,12,13,14,15,0,0,0,0, 0,1,4,5,6,7,8,9,10,12,13,14,15,0,0,0, 0,1,2,4,5,6,7,8,9,10,12,13,14,15,0,0, 0,1,2,3,4,5,6,7,8,9,10,12,13,14,15,0,
	0,4,8,9,10,11,12,13,14,15,0,0,0,0,0,0, 0,1,4,8,9,10,11,12,13,14,15,0,0,0,0,0, 0,1,2,4,8,9,10,11,12,13,14,15,0,0,0,0, 0,1,2,3,4,8,9,10,11,12,13,14,15,0,0,0,
	0,4,5,8,9,10,11,12,13,14,15,0,0,0,0,0, 0,1,4,5,8,9,10,11,12,13,14,15,0,0,0,0, 0,1,2,4,5,8,9,10,11,12,13,14,15,0,0,0, 0,1,2,3,4,5,8,9,10,11,12,13,14,15,0,0,
	0,4,5,6,8,9,10,11,12,13,14,15,0,0,0,0, 0,1,4,5,6,8,9,10,11,12,13,14,15,0,0,0, 0,1,2,4,5,6,8,9,10,11,12,13,14,15,0,0, 0,1,2,3,4,5,6,8,9,10,11,12,13,14,15,0,
	0,4,5,6,7,8,9,10,11,12,13,14,15,0,0,0, 0,1,4,5,6,7,8,9,10,11,12,13,14,15,0,0, 0,1,2,4,5,6,7,8,9,10,11,12,13,14,15,0, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
};
static const uint8_t writer_len[256] = {
	4, 5, 6, 7, 5, 6, 7, 8, 6, 7, 8, 9, 7, 8, 9, 10, 5, 6, 7, 8, 6, 7, 8, 9, 7, 8, 9, 10, 8, 9, 10, 11,
	6, 7, 8, 9, 7, 8, 9, 10, 8, 9, 10, 11, 9, 10, 11, 12,	7, 8, 9, 10, 8, 9, 10, 11, 9, 10, 11, 12, 10, 11, 12, 13,
	5, 6, 7, 8, 6, 7, 8, 9, 7, 8, 9, 10, 8, 9, 10, 11, 6, 7, 8, 9, 7, 8, 9, 10, 8, 9, 10, 11, 9, 10, 11, 12,
	7, 8, 9, 10, 8, 9, 10, 11, 9, 10, 11, 12, 10, 11, 12, 13, 8, 9, 10, 11, 9, 10, 11, 12, 10, 11, 12, 13, 11, 12, 13, 14,
	6, 7, 8, 9, 7, 8, 9, 10, 8, 9, 10, 11, 9, 10, 11, 12, 7, 8, 9, 10, 8, 9, 10, 11, 9, 10, 11, 12, 10, 11, 12, 13,
	8, 9, 10, 11, 9, 10, 11, 12, 10, 11, 12, 13, 11, 12, 13, 14, 9, 10, 11, 12, 10, 11, 12, 13, 11, 12, 13, 14, 12, 13, 14, 15,
	7, 8, 9, 10, 8, 9, 10, 11, 9, 10, 11, 12, 10, 11, 12, 13, 8, 9, 10, 11, 9, 10, 11, 12, 10, 11, 12, 13, 11, 12, 13, 14,
	9, 10, 11, 12, 10, 11, 12, 13, 11, 12, 13, 14, 12, 13, 14, 15, 10, 11, 12, 13, 11, 12, 13, 14, 12, 13, 14, 15, 13, 14, 15, 16
};

//SSE to the point that we have 4 output vectors ready to write
#define SSE_COMMON do{ \
	/*convert vr, vb to vg_r, vg_b respectively*/ \
	r=_mm_sub_epi8(r, g); \
	b=_mm_sub_epi8(b, g); \
	/*generate absolute vectors for each of r, g, b, (vg<0)?(-vg)-1:vg;*/ \
	ABSOLUTER(r, ar); \
	ABSOLUTER(g, ag); \
	ABSOLUTER(b, ab); \
	/*determine how to store pixels*/ \
	/* 1 byte if arb<2, ag<4*/ \
	/* 2 byte if arb<8, ag<32*/ \
	/* 3 byte if argb<64*/ \
	/* 4 byte otherwise*/ \
	arb=_mm_or_si128(ar, ab); \
	op1=_mm_subs_epu8(ag, _mm_set1_epi8(2)); \
	op1=_mm_or_si128(op1, arb); \
	op1=_mm_cmpgt_epi8(_mm_set1_epi8(2), op1);/*op1*/ \
	op2=_mm_subs_epu8(ag, _mm_set1_epi8(24)); \
	op2=_mm_or_si128(op2, arb); \
	op2=_mm_cmpgt_epi8(_mm_set1_epi8(8), op2);/*op1|op2*/ \
	op3=_mm_cmpgt_epi8(_mm_set1_epi8(64), _mm_or_si128(arb, ag));/*op1|op2|op3*/ \
	op4=_mm_andnot_si128(op3, _mm_set1_epi8(-1));/*op4*/ \
	op3=_mm_sub_epi8(op3, op2);/*op3*/ \
	op2=_mm_sub_epi8(op2, op1);/*op2*/ \
	res0=_mm_setzero_si128(); \
	res1=_mm_setzero_si128(); \
	res2=_mm_setzero_si128(); \
	res3=_mm_setzero_si128(); \
	/*build opcode vector*/ \
	opuse=_mm_and_si128(op2, _mm_set1_epi8(1)); \
	opuse=_mm_or_si128(opuse, _mm_and_si128(op3, _mm_set1_epi8(3))); \
	opuse=_mm_or_si128(opuse, _mm_and_si128(op4, _mm_set1_epi8(-9))); \
	/*apply opcodes to output*/ \
	w1=_mm_unpacklo_epi8(opuse, _mm_setzero_si128()); \
	w2=_mm_unpacklo_epi16(w1, _mm_setzero_si128()); \
	res0=_mm_or_si128(w2, res0); \
	w2=_mm_unpackhi_epi16(w1, _mm_setzero_si128()); \
	res1=_mm_or_si128(w2, res1); \
	w1=_mm_unpackhi_epi8(opuse, _mm_setzero_si128()); \
	w2=_mm_unpacklo_epi16(w1, _mm_setzero_si128()); \
	res2=_mm_or_si128(w2, res2); \
	w2=_mm_unpackhi_epi16(w1, _mm_setzero_si128()); \
	res3=_mm_or_si128(w2, res3); \
	/*bbrrggg0*/ \
	NORMALISE_SHIFT16_EMBIGGEN(g, op1, _mm_set1_epi8(4), 1); \
	NORMALISE_SHIFT16_EMBIGGEN(r, op1, _mm_set1_epi8(2), 4); \
	NORMALISE_SHIFT16_EMBIGGEN(b, op1, _mm_set1_epi8(2), 6); \
	/*bbbbrrrr gggggg01*/ \
	NORMALISE_SHIFT16_EMBIGGEN(g, op2, _mm_set1_epi8(32), 2); \
	NORMALISE_SHIFT16_EMBIGGEN(r, op2, _mm_set1_epi8(8), 8); \
	NORMALISE_SHIFT16_EMBIGGEN(b, op2, _mm_set1_epi8(8), 12); \
	/*bbbbbbbr rrrrrrgg ggggg011*/ \
	NORMALISE_SHIFT16_EMBIGGEN(g, op3, _mm_set1_epi8(64), 3); \
	NORMALISE_SHIFT32_EMBIGGEN(r, op3, _mm_set1_epi8(64), 10); \
	NORMALISE_SHIFT32_EMBIGGEN(b, op3, _mm_set1_epi8(64), 17); \
	/*bbbbbbbb rrrrrrrr gggggggg 11110111*/ \
	/*shift op4 g*/ \
	w1=_mm_and_si128(g, op4); \
	w2=_mm_unpacklo_epi8(_mm_setzero_si128(), w1);/*switched to end up at 2nd byte position*/ \
	w3=_mm_unpacklo_epi16(w2, _mm_setzero_si128()); \
	res0=_mm_or_si128(w3, res0); \
	w3=_mm_unpackhi_epi16(w2, _mm_setzero_si128()); \
	res1=_mm_or_si128(w3, res1); \
	w2=_mm_unpackhi_epi8(_mm_setzero_si128(), w1);/*switched*/ \
	w3=_mm_unpacklo_epi16(w2, _mm_setzero_si128()); \
	res2=_mm_or_si128(w3, res2); \
	w3=_mm_unpackhi_epi16(w2, _mm_setzero_si128()); \
	res3=_mm_or_si128(w3, res3); \
	/*shift op4 r*/ \
	w1=_mm_and_si128(r, op4); \
	w2=_mm_unpacklo_epi8(w1, _mm_setzero_si128()); \
	w3=_mm_unpacklo_epi16(_mm_setzero_si128(), w2);/*switch*/ \
	res0=_mm_or_si128(w3, res0); \
	w3=_mm_unpackhi_epi16(_mm_setzero_si128(), w2);/*switch*/ \
	res1=_mm_or_si128(w3, res1); \
	w2=_mm_unpackhi_epi8(w1, _mm_setzero_si128()); \
	w3=_mm_unpacklo_epi16(_mm_setzero_si128(), w2);/*switch*/ \
	res2=_mm_or_si128(w3, res2); \
	w3=_mm_unpackhi_epi16(_mm_setzero_si128(), w2);/*switch*/ \
	res3=_mm_or_si128(w3, res3); \
	/*shift op4 b*/ \
	w1=_mm_and_si128(b, op4); \
	w2=_mm_unpacklo_epi8(_mm_setzero_si128(), w1);/*switch*/ \
	w3=_mm_unpacklo_epi16(_mm_setzero_si128(), w2);/*switch*/ \
	res0=_mm_or_si128(w3, res0); \
	w3=_mm_unpackhi_epi16(_mm_setzero_si128(), w2);/*switch*/ \
	res1=_mm_or_si128(w3, res1); \
	w2=_mm_unpackhi_epi8(_mm_setzero_si128(), w1);/*switch*/ \
	w3=_mm_unpacklo_epi16(_mm_setzero_si128(), w2);/*switch*/ \
	res2=_mm_or_si128(w3, res2); \
	w3=_mm_unpackhi_epi16(_mm_setzero_si128(), w2);/*switch*/ \
	res3=_mm_or_si128(w3, res3); \
}while(0)

//write output vector scalar, RLE bytes are involved
#define QOI_SSE_WRITE_SAFE(data, len) do{ \
	_mm_storeu_si128((__m128i*)dump, data); \
	if(dump[0]==0xA8) \
		++s.run; \
	else{ \
		DUMP_RUN(s.run); \
		memcpy(s.bytes+s.b, dump+0, len[0]); \
		s.b+=len[0]; \
	} \
	if(dump[4]==0xA8) \
		++s.run; \
	else{ \
		DUMP_RUN(s.run); \
		memcpy(s.bytes+s.b, dump+4, len[1]); \
		s.b+=len[1]; \
	} \
	if(dump[8]==0xA8) \
		++s.run; \
	else{ \
		DUMP_RUN(s.run); \
		memcpy(s.bytes+s.b, dump+8, len[2]); \
		s.b+=len[2]; \
	} \
	if(dump[12]==0xA8) \
		++s.run; \
	else{ \
		DUMP_RUN(s.run); \
		memcpy(s.bytes+s.b, dump+12, len[3]); \
		s.b+=len[3]; \
	} \
}while(0)

//write output vector with SSE, RLE bytes not involved
//DUMP_RUN(s.run) not here, handled elsewhere because multiple paths
#define QOI_SSE_WRITE_QUICK(data, lut_offset) do{ \
	/*write first vec*/ \
	w1=_mm_loadu_si128((__m128i const*)(writer_lut)+((lut_index>>lut_offset)&255)); \
	w1=_mm_shuffle_epi8(data, w1); \
	_mm_storeu_si128((__m128i*)(s.bytes+s.b), w1); \
	s.b+=writer_len[(lut_index>>lut_offset)&255]; \
}while(0)

#define QOI_SSE_GETLUT0 do{ \
	/*get lut for first 8 pixels*/ \
	w2=_mm_unpacklo_epi8(op2, _mm_setzero_si128()); \
	w1=_mm_unpacklo_epi8(_mm_setzero_si128(), op3); \
	w2=_mm_or_si128(w2, w1); \
	w1=_mm_unpacklo_epi8(op4, op4); \
	w2=_mm_or_si128(w2, w1); \
	lut_index=_mm_movemask_epi8(w2); \
}while(0)

#define QOI_SSE_GETLUT1 do{ \
	/*get lut for next 8 pixels*/ \
	w2=_mm_unpackhi_epi8(op2, _mm_setzero_si128()); \
	w1=_mm_unpackhi_epi8(_mm_setzero_si128(), op3); \
	w2=_mm_or_si128(w2, w1); \
	w1=_mm_unpackhi_epi8(op4, op4); \
	w2=_mm_or_si128(w2, w1); \
	lut_index=_mm_movemask_epi8(w2); \
}while(0)

//RLE not involved, write all 4 output vectors with SSE branchless
#define QOI_SSE_QUICK do{ \
	DUMP_RUN(s.run); \
	QOI_SSE_GETLUT0; \
	QOI_SSE_WRITE_QUICK(res0, 0); \
	QOI_SSE_WRITE_QUICK(res1, 8); \
	QOI_SSE_GETLUT1; \
	QOI_SSE_WRITE_QUICK(res2, 0); \
	QOI_SSE_WRITE_QUICK(res3, 8); \
}while(0)

//RLE involved, branch to do scalar/sse output as necessary per vector
#define SSE_CAREFUL do{ \
	/*get op lengths*/ \
	opuse=_mm_and_si128(op1, _mm_set1_epi8(1)); \
	opuse=_mm_or_si128(opuse, _mm_and_si128(op2, _mm_set1_epi8(2))); \
	opuse=_mm_or_si128(opuse, _mm_and_si128(op3, _mm_set1_epi8(3))); \
	opuse=_mm_or_si128(opuse, _mm_and_si128(op4, _mm_set1_epi8(4))); \
	_mm_storeu_si128((__m128i*)opuse_, opuse); \
	if(!rle_status[0]||!rle_status[1]){ \
		QOI_SSE_GETLUT0; \
	} \
	if(!rle_status[0]){ \
		DUMP_RUN(s.run); \
		QOI_SSE_WRITE_QUICK(res0, 0); \
	} \
	else{ \
		QOI_SSE_WRITE_SAFE(res0, (opuse_+0)); \
	} \
	if(!rle_status[1]){ \
		DUMP_RUN(s.run); \
		QOI_SSE_WRITE_QUICK(res1, 8); \
	} \
	else{ \
		QOI_SSE_WRITE_SAFE(res1, (opuse_+4)); \
	} \
	if(!rle_status[2]||!rle_status[3]){ \
		QOI_SSE_GETLUT1; \
	} \
	if(!rle_status[2]){ \
		DUMP_RUN(s.run); \
		QOI_SSE_WRITE_QUICK(res2, 0); \
	} \
	else{ \
		QOI_SSE_WRITE_SAFE(res2, (opuse_+8)); \
	} \
	if(!rle_status[3]){ \
		DUMP_RUN(s.run); \
		QOI_SSE_WRITE_QUICK(res3, 8); \
	} \
	else{ \
		QOI_SSE_WRITE_SAFE(res3, (opuse_+12)); \
	} \
}while(0)

static enc_state qoi_encode_chunk4_sse(enc_state s){
	__m128i da, db, dc, dd, r, g, b, a, ar, ag, ab, arb, w1, w2, w3, w4, w5, w6;
	__m128i gshuf, shuf1, shuf2, blend;
	__m128i op1, op2, op3, op4, opuse, res0, res1, res2, res3;
	int lut_index=0;
	unsigned char dump[16], opuse_[16];
	unsigned int rle_status[4];

	//constants
	shuf1=_mm_setr_epi8(0,4,8,12,1,5,9,13,2,6,10,14,3,7,11,15);
	shuf2=_mm_setr_epi8(1,5,9,13,0,4,8,12,3,7,11,15,2,6,10,14);
	gshuf=_mm_setr_epi8(8,9,10,11,12,13,14,15,0,1,2,3,4,5,6,7);
	blend=_mm_setr_epi8(0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1, -1, -1);

	for (; s.px_pos < s.pixel_cnt*4; s.px_pos += 64) {
		//load pixels
		LOAD16(da,  0, 4);
		LOAD16(db, 16, 4);
		LOAD16(dc, 32, 4);
		LOAD16(dd, 48, 4);

		if(_mm_test_all_zeros( _mm_or_si128(_mm_or_si128(da, db), _mm_or_si128(dc, dd)), _mm_set1_epi8(-1))){//all RLE
			s.run+=16;
			continue;
		}

		//unpack into rgba planes
		w1=_mm_shuffle_epi8(da, shuf1);//r4g4b4a4
		w2=_mm_shuffle_epi8(db, shuf1);//r4g4b4a4
		w3=_mm_shuffle_epi8(dc, shuf2);//g4r4a4b4
		w4=_mm_shuffle_epi8(dd, shuf2);//g4r4a4b4
		w5=_mm_unpackhi_epi32(w1, w2);//b8a8
		w6=_mm_unpackhi_epi32(w3, w4);//a8b8
		a=_mm_blendv_epi8(w6, w5, blend);//out of order, irrelevant
		if(!_mm_test_all_zeros(a, _mm_set1_epi8(-1))){//alpha present, scalar this iteration
			//TODO ditch this scalar code, make a parallel alpha op vector and zip together with rgb vector
			unsigned int pixel_cnt_store=s.pixel_cnt;
			s.pixel_cnt=(s.px_pos/4)+16;
			s=enc_finish[1](s);
			s.px_pos-=64;
			s.pixel_cnt=pixel_cnt_store;
			continue;
		}
		//no alpha, finish extracting planes then re-use rgb sse implementation
		b=_mm_blendv_epi8(w5, w6, blend);
		w1=_mm_unpacklo_epi32(w1, w2);//r8g8
		w2=_mm_unpacklo_epi32(w3, w4);//g8r8
		r=_mm_blendv_epi8(w1, w2, blend);
		g=_mm_blendv_epi8(w2, w1, blend);//out of order
		g=_mm_shuffle_epi8(g, gshuf);//in order

		SSE_COMMON;
		w1=_mm_cmpeq_epi8(_mm_or_si128(r, _mm_or_si128(g, b)), _mm_set1_epi8(0));
		if(!_mm_testz_si128(w1, w1)){//RLE, tread lightly
			_mm_storeu_si128((__m128i*)rle_status, w1);
			SSE_CAREFUL;
		}
		else{
			QOI_SSE_QUICK;
		}
	}
	return s;
}

static enc_state qoi_encode_chunk3_sse(enc_state s){
	__m128i da, db, dc, r, g, b, ar, ag, ab, arb, w1, w2, w3;
	__m128i rshuf, gshuf, bshuf, blend1, blend2;
	__m128i op1, op2, op3, op4, opuse, res0, res1, res2, res3;
	int lut_index=0;
	unsigned char dump[16], opuse_[16];
	unsigned int rle_status[4];

	//constants
	rshuf=_mm_setr_epi8(0,3,6,9,12,15, 2,5,8,11,14, 1,4,7,10,13);
	gshuf=_mm_setr_epi8(1,4,7,10,13, 0,3,6,9,12,15, 2,5,8,11,14);
	bshuf=_mm_setr_epi8(2,5,8,11,14, 1,4,7,10,13, 0,3,6,9,12,15);
	blend1=_mm_setr_epi8(0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0);
	blend2=_mm_setr_epi8(0,-1,0,0,-1,0,0,-1,0,0,-1,0,0,-1,0,0);

	for (; s.px_pos < s.pixel_cnt*3; s.px_pos += 48) {
		//load and diff next 16 pixels
		LOAD16(da, 0, 3);
		LOAD16(db, 16, 3);
		LOAD16(dc, 32, 3);

		if(_mm_test_all_zeros( _mm_or_si128(da, _mm_or_si128(db, dc)), _mm_set1_epi8(-1))){//all RLE
			s.run+=16;
			continue;
		}

		/*convert to rgb vectors*/
		PLANAR_SHUFFLE(r, da, db, dc, rshuf);
		PLANAR_SHUFFLE(g, db, dc, da, gshuf);
		PLANAR_SHUFFLE(b, dc, da, db, bshuf);

		SSE_COMMON;
		w1=_mm_cmpeq_epi8(_mm_or_si128(r, _mm_or_si128(g, b)), _mm_set1_epi8(0));
		if(!_mm_testz_si128(w1, w1)){//RLE, tread lightly
			_mm_storeu_si128((__m128i*)rle_status, w1);
			SSE_CAREFUL;
		}
		else{
			QOI_SSE_QUICK;
		}
	}
	return s;
}
#endif

//pointers to optimised functions
static enc_state (*enc_bulk[])(enc_state)={
#ifdef QOI_SCALAR
	qoi_encode_chunk3_scalar, qoi_encode_chunk4_scalar
#elif defined QOI_SSE
	qoi_encode_chunk3_sse, qoi_encode_chunk4_sse
#elif defined QOI_AVX2
	qoi_encode_chunk3_avx2, qoi_encode_chunk4_avx2
#elif defined QOI_AVX512
	qoi_encode_chunk3_avx512, qoi_encode_chunk4_avx512
#else
#error "Must define instruction set at compile time. One of: QOI_SCALAR QOI_SSE"
#endif
};

//Optimised decode functions////////////////////////////////////////////////////
typedef struct{
	unsigned char *bytes, *pixels;
	qoi_rgba_t px;
	unsigned int b, b_limit, b_present, p, p_limit, px_pos, run, pixel_cnt, pixel_curr;
} dec_state;

#define QOI_DECODE_COMMON \
	;int b1 = s.bytes[s.b++]; \
	if ((b1 & QOI_MASK_1) == QOI_OP_LUMA232) { \
		int vg = ((b1>>1)&7) - 6; \
		s.px.rgba.r += vg + ((b1 >> 4) & 3); \
		s.px.rgba.g += vg + 2; \
		s.px.rgba.b += vg + ((b1 >> 6) & 3); \
	} \
	else if ((b1 & QOI_MASK_2) == QOI_OP_LUMA464) { \
		int b2=s.bytes[s.b++]; \
		int vg = ((b1>>2)&63) - 40; \
		s.px.rgba.r += vg + ((b2     ) & 0x0f); \
		s.px.rgba.g += vg + 8; \
		s.px.rgba.b += vg + ((b2 >>4) & 0x0f); \
	} \
	else if ((b1 & QOI_MASK_3) == QOI_OP_LUMA777) { \
		int b2=s.bytes[s.b++]; \
		int b3=s.bytes[s.b++]; \
		int vg = (((b2&3)<<5)|((b1>>3)&31))-128; \
		s.px.rgba.r += vg + (((b3&1)<<6)|((b2>>2)&63)); \
		s.px.rgba.g += vg + 64; \
		s.px.rgba.b += vg + ((b3>>1)&127); \
	} \
	else if (b1 == QOI_OP_RGB) { \
		signed char vg=s.bytes[s.b++]; \
		signed char b3=s.bytes[s.b++]; \
		signed char b4=s.bytes[s.b++]; \
		s.px.rgba.r += vg + b3; \
		s.px.rgba.g += vg; \
		s.px.rgba.b += vg + b4; \
	}

static dec_state dec_in4out4(dec_state s){
	while( ((s.b+6)<s.b_present) && ((s.px_pos+4)<=s.p_limit) && (s.pixel_cnt!=s.pixel_curr) ){
		if (s.run)
			s.run--;
		else{
			OP_RGBA_GOTO:
			QOI_DECODE_COMMON
			else if (b1 == QOI_OP_RGBA) {
				s.px.rgba.a = s.bytes[s.b++];
				goto OP_RGBA_GOTO;
			}
			else// if ((b1 & QOI_MASK_3) == QOI_OP_RUN)
				s.run = ((b1>>3) & 0x1f);
		}
		s.pixels[s.px_pos + 0] = s.px.rgba.r;
		s.pixels[s.px_pos + 1] = s.px.rgba.g;
		s.pixels[s.px_pos + 2] = s.px.rgba.b;
		s.pixels[s.px_pos + 3] = s.px.rgba.a;
		s.px_pos+=4;
		s.pixel_curr++;
	}
	return s;
}

static dec_state dec_in4out3(dec_state s){
	while( ((s.b+6)<s.b_present) && ((s.px_pos+3)<=s.p_limit) && (s.pixel_cnt!=s.pixel_curr) ){
		if (s.run)
			s.run--;
		else{
			OP_RGBA_GOTO:
			QOI_DECODE_COMMON
			else if (b1 == QOI_OP_RGBA) {
				s.px.rgba.a = s.bytes[s.b++];
				goto OP_RGBA_GOTO;
			}
			else// if ((b1 & QOI_MASK_3) == QOI_OP_RUN)
				s.run = ((b1>>3) & 0x1f);
		}
		s.pixels[s.px_pos + 0] = s.px.rgba.r;
		s.pixels[s.px_pos + 1] = s.px.rgba.g;
		s.pixels[s.px_pos + 2] = s.px.rgba.b;
		s.px_pos+=3;
		s.pixel_curr++;
	}
	return s;
}

static dec_state dec_in3out4(dec_state s){
	while( ((s.b+6)<s.b_present) && ((s.px_pos+4)<=s.p_limit) && (s.pixel_cnt!=s.pixel_curr) ){
		if (s.run)
			s.run--;
		else{
			QOI_DECODE_COMMON
			else// if ((b1 & QOI_MASK_3) == QOI_OP_RUN)
				s.run = ((b1>>3) & 0x1f);
		}
		s.pixels[s.px_pos + 0] = s.px.rgba.r;
		s.pixels[s.px_pos + 1] = s.px.rgba.g;
		s.pixels[s.px_pos + 2] = s.px.rgba.b;
		s.pixels[s.px_pos + 3] = s.px.rgba.a;
		s.px_pos+=4;
		s.pixel_curr++;
	}
	return s;
}

static dec_state dec_in3out3(dec_state s){
	while( ((s.b+6)<s.b_present) && ((s.px_pos+3)<=s.p_limit) && (s.pixel_cnt!=s.pixel_curr) ){
		if (s.run)
			s.run--;
		else{
			QOI_DECODE_COMMON
			else// if ((b1 & QOI_MASK_3) == QOI_OP_RUN)
				s.run = ((b1>>3) & 0x1f);
		}
		s.pixels[s.px_pos + 0] = s.px.rgba.r;
		s.pixels[s.px_pos + 1] = s.px.rgba.g;
		s.pixels[s.px_pos + 2] = s.px.rgba.b;
		s.px_pos+=3;
		s.pixel_curr++;
	}
	return s;
}

#define DEC_ARR_INDEX (((desc->channels-3)<<1)|(channels-3))
static dec_state (*dec_arr[])(dec_state)={dec_in3out3, dec_in3out4, dec_in4out3, dec_in4out4};
