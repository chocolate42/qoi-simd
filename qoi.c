
#define QOI_OP_INDEX  0x00 /* 00xxxxxx */
#define QOI_OP_DIFF   0x40 /* 01xxxxxx */
#define QOI_OP_LUMA   0x80 /* 10xxxxxx */
#define QOI_OP_RUN    0xc0 /* 11xxxxxx */
#define QOI_OP_RGB    0xfe /* 11111110 */
#define QOI_OP_RGBA   0xff /* 11111111 */

#define QOI_OP_RUN_FULL 0xfd /* 11111101 */
#define QOI_RUN_FULL_VAL (62)

#define QOI_MASK_2    0xc0 /* 11000000 */

#define QOI_COLOR_HASH(C) (C.rgba.r*3 + C.rgba.g*5 + C.rgba.b*7 + C.rgba.a*11)
#define QOI_MAGIC \
	(((unsigned int)'q') << 24 | ((unsigned int)'o') << 16 | \
	 ((unsigned int)'i') <<  8 | ((unsigned int)'f'))

#define EXT_STR "qoi"

#define QOI_PIXEL_WORST_CASE (desc->channels==4?5:4)

#define DUMP_RUN(rrr) do{ \
	for(;rrr>=QOI_RUN_FULL_VAL;rrr-=QOI_RUN_FULL_VAL) \
		s.bytes[s.b++] = QOI_OP_RUN_FULL; \
	if (rrr) { \
		s.bytes[s.b++] = QOI_OP_RUN | (rrr - 1); \
		rrr = 0; \
	} \
}while(0)

//s.px.rgba.r = s.pixels[s.px_pos + 0];
//s.px.rgba.g = s.pixels[s.px_pos + 1];
//s.px.rgba.b = s.pixels[s.px_pos + 2];
//s.px.v=(s.px.v&0xFF000000)|(*(unsigned int*)(s.pixels+s.px_pos)&0x00FFFFFF);
#define ENC_READ_RGB do{ \
	s.px.rgba.r = s.pixels[s.px_pos + 0]; \
	s.px.rgba.g = s.pixels[s.px_pos + 1]; \
	s.px.rgba.b = s.pixels[s.px_pos + 2]; \
}while(0)

//optimised encode functions////////////////////////////////////////////////////

#define RGB_ENC_SCALAR do{\
	signed char vr = s.px.rgba.r - px_prev.rgba.r;\
	signed char vg = s.px.rgba.g - px_prev.rgba.g;\
	signed char vb = s.px.rgba.b - px_prev.rgba.b;\
	signed char vg_r = vr - vg;\
	signed char vg_b = vb - vg;\
	unsigned char ag = (vg<0)?(-vg)-1:vg;\
	unsigned char d = ((vr<0)?(-vr)-1:vr) | ((vb<0)?(-vb)-1:vb);\
	unsigned char l = ((vg_b<0)?(-vg_b)-1:vg_b) | ((vg_r<0)?(-vg_r)-1:vg_r);\
	if (\
		d < 2 &&\
		ag < 2\
	) {\
		s.bytes[s.b++] = QOI_OP_DIFF | (vr + 2) << 4 | (vg + 2) << 2 | (vb + 2);\
	}\
	else if (\
		l < 8 &&\
		ag < 32\
	) {\
		s.bytes[s.b++] = QOI_OP_LUMA     | (vg   + 32);\
		s.bytes[s.b++] = (vg_r + 8) << 4 | (vg_b +  8);\
	}\
	else {\
		s.bytes[s.b++] = QOI_OP_RGB;\
		s.bytes[s.b++] = s.px.rgba.r;\
		s.bytes[s.b++] = s.px.rgba.g;\
		s.bytes[s.b++] = s.px.rgba.b;\
	}\
}while(0)

typedef struct{
	unsigned char *bytes, *pixels;
	qoi_rgba_t px, index[64];
	unsigned int b, px_pos, run, pixel_cnt;
} enc_state;

static enc_state qoi_encode_chunk3_scalar(enc_state s){
	qoi_rgba_t px_prev=s.px;
	unsigned int px_end=(s.pixel_cnt-1)*3;
	for (; s.px_pos <= px_end; s.px_pos += 3) {
		ENC_READ_RGB;
		while(s.px.v == px_prev.v) {
			++s.run;
			if(s.px_pos == px_end) {
				for(;s.run>=QOI_RUN_FULL_VAL;s.run-=QOI_RUN_FULL_VAL)
					s.bytes[s.b++] = QOI_OP_RUN_FULL;
				s.px_pos+=3;
				return s;
			}
			s.px_pos+=3;
			ENC_READ_RGB;
		}
		DUMP_RUN(s.run);
		int index_pos = QOI_COLOR_HASH(s.px) & 63;
		if(s.index[index_pos].v == s.px.v) {
			s.bytes[s.b++] = QOI_OP_INDEX | index_pos;
			px_prev = s.px;
			continue;
		}
		s.index[index_pos] = s.px;

		RGB_ENC_SCALAR;
		px_prev = s.px;
	}
	return s;
}

static enc_state qoi_encode_chunk4_scalar(enc_state s){
	qoi_rgba_t px_prev=s.px;
	unsigned int px_end=(s.pixel_cnt-1)*4;
	for (; s.px_pos <= px_end; s.px_pos += 4) {
		ENC_READ_RGBA;
		while(s.px.v == px_prev.v) {
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
		int index_pos = QOI_COLOR_HASH(s.px) & 63;
		if(s.index[index_pos].v == s.px.v) {
			s.bytes[s.b++] = QOI_OP_INDEX | index_pos;
			px_prev = s.px;
			continue;
		}
		s.index[index_pos] = s.px;

		if(s.px.rgba.a!=px_prev.rgba.a){
			s.bytes[s.b++] = QOI_OP_RGBA;
			s.bytes[s.b++] = s.px.rgba.r;
			s.bytes[s.b++] = s.px.rgba.g;
			s.bytes[s.b++] = s.px.rgba.b;
			s.bytes[s.b++] = s.px.rgba.a;
			px_prev = s.px;
			continue;
		}
		RGB_ENC_SCALAR;
		px_prev = s.px;
	}
	return s;
}

//Optimised decode functions////////////////////////////////////////////////////

typedef struct{
	unsigned char *bytes, *pixels;
	qoi_rgba_t px, index[64];
	unsigned int b, b_limit, b_present, p, p_limit, px_pos, run, pixel_cnt, pixel_curr;
} dec_state;

#define QOI_DECODE_COMMON \
	;int b1 = s.bytes[s.b++]; \
	if ((b1 & QOI_MASK_2) == QOI_OP_INDEX) { \
		s.px = s.index[b1]; \
	} \
	else if ((b1 & QOI_MASK_2) == QOI_OP_DIFF) { \
		s.px.rgba.r += ((b1 >> 4) & 0x03) - 2; \
		s.px.rgba.g += ((b1 >> 2) & 0x03) - 2; \
		s.px.rgba.b += ( b1       & 0x03) - 2; \
	} \
	else if ((b1 & QOI_MASK_2) == QOI_OP_LUMA) { \
		int b2 = s.bytes[s.b++]; \
		int vg = (b1 & 0x3f) - 32; \
		s.px.rgba.r += vg - 8 + ((b2 >> 4) & 0x0f); \
		s.px.rgba.g += vg; \
		s.px.rgba.b += vg - 8 +  (b2       & 0x0f); \
	} \
	else if (b1 == QOI_OP_RGB) { \
		s.px.rgba.r = s.bytes[s.b++]; \
		s.px.rgba.g = s.bytes[s.b++]; \
		s.px.rgba.b = s.bytes[s.b++]; \
	}

static dec_state dec_in4out4(dec_state s){
	while( ((s.b+5)<s.b_present) && ((s.px_pos+4)<=s.p_limit) && (s.pixel_cnt!=s.pixel_curr) ){
		if (s.run)
			s.run--;
		else{
			QOI_DECODE_COMMON
			else if (b1 == QOI_OP_RGBA) {
				s.px.rgba.r = s.bytes[s.b++];
				s.px.rgba.g = s.bytes[s.b++];
				s.px.rgba.b = s.bytes[s.b++];
				s.px.rgba.a = s.bytes[s.b++];
			}
			else
				s.run = (b1 & 0x3f);
			s.index[QOI_COLOR_HASH(s.px) & 63] = s.px;
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
	while( ((s.b+5)<s.b_present) && ((s.px_pos+3)<=s.p_limit) && (s.pixel_cnt!=s.pixel_curr) ){
		if (s.run)
			s.run--;
		else{
			QOI_DECODE_COMMON
			else if (b1 == QOI_OP_RGBA) {
				s.px.rgba.r = s.bytes[s.b++];
				s.px.rgba.g = s.bytes[s.b++];
				s.px.rgba.b = s.bytes[s.b++];
				s.px.rgba.a = s.bytes[s.b++];
			}
			else
				s.run = (b1 & 0x3f);
			s.index[QOI_COLOR_HASH(s.px) & 63] = s.px;
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
	while( ((s.b+5)<s.b_present) && ((s.px_pos+4)<=s.p_limit) && (s.pixel_cnt!=s.pixel_curr) ){
		if (s.run)
			s.run--;
		else{
			QOI_DECODE_COMMON
			else
				s.run = (b1 & 0x3f);
			s.index[QOI_COLOR_HASH(s.px) & 63] = s.px;
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
	while( ((s.b+5)<s.b_present) && ((s.px_pos+3)<=s.p_limit) && (s.pixel_cnt!=s.pixel_curr) ){
		if (s.run)
			s.run--;
		else{
			QOI_DECODE_COMMON
			else
				s.run = (b1 & 0x3f);
			s.index[QOI_COLOR_HASH(s.px) & 63] = s.px;
		}
		s.pixels[s.px_pos + 0] = s.px.rgba.r;
		s.pixels[s.px_pos + 1] = s.px.rgba.g;
		s.pixels[s.px_pos + 2] = s.px.rgba.b;
		s.px_pos+=3;
		s.pixel_curr++;
	}
	return s;
}

//pointers to optimised functions
#define ENC_ARR_INDEX (desc->channels-3)
static enc_state (*enc_chunk_arr[])(enc_state)={
	qoi_encode_chunk3_scalar, qoi_encode_chunk4_scalar
};
static enc_state (*enc_finish_arr[])(enc_state)={
	qoi_encode_chunk3_scalar, qoi_encode_chunk4_scalar
};

#define DEC_ARR_INDEX (((desc->channels-3)<<1)|(channels-3))
static dec_state (*dec_arr[])(dec_state)={dec_in3out3, dec_in3out4, dec_in4out3, dec_in4out4};
