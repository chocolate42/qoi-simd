#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void write_u8(uint8_t *arr, int len, char *name){
	int q, line=0, spacer=0;
	line+=printf("%s[%d] = {", name, len);
	for(q=0;q<len;++q){
		if(!spacer && line>76){
			printf(" ");
			spacer=1;
		}
		if(line>158){
			printf("\n\t");
			line=2;
			spacer=0;
		}
		line+=printf("%u,", arr[q]);
	}
	printf("};\n");
}

/* Roi SSE implementation overview
	* An output vector contains 4 pixels of output, aligned to epi32
	* sse_runwriter_* constants are used to branchlessly prepare an output vector for writing
	* pre/post finish/start runs either side of the written data respectively
	* blenddata/blendmask put any mid-data runs into the data to be written, selected by mid
	* shuffle moves all used bytes to the left
	* len is the number of bytes to write
	* gen_runwriter_data() generates all of these constants
*/

/* Run config, 0=run 1=pixel stored as an rgb op
	0000 never hit as this case has to be avoided with a branch
	0001 pre=3
	0010 pre=2,        post=1
	0011 pre=2
	0100 pre=1         post=2
	0101 pre=1 mid=2.1
	0110 pre=1         post=1
	0111 pre=1
	1000               post=3
	1001       mid=1.2
	1010       mid=1.1 post=1
	1011       mid=1.1
	1100               post=2
	1101       mid=2.1
	1110               post=1
	1111 */

//                                       0                                mid=1.1                            mid=1.2                            mid=2.1
uint8_t sse_runwriter_blenddata_lut[64]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,  7,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0, 15,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 7,0,0,0,0,0,0,0};
uint8_t sse_runwriter_blendmask_lut[64]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,255,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,255,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,255,0,0,0,0,0,0,0};
void gen_runwriter_data(){
	uint8_t shuf[641<<4]={0}, tmp[16];
	uint8_t pre[641]={0}, mid[641]={0}, post[641]={0}, len[641]={0};
	uint32_t a, b, c, d, i, index, x, written, loc;
	//0=run, 1..4=rgb op of that len
	for(a=0;a<5;++a){for(b=0;b<5;++b){for(c=0;c<5;++c){for(d=0;d<5;++d){
		if(!(a+b+c+d))
			continue;//skip all run, has to be handled differently as it doesn't invoke DUMP_RUN
		i=(d<<24)|(c<<16)|(b<<8)|(a);
		index=i%641;
		if(!a){++pre[index];if(!b){++pre[index];if(!c)++pre[index];}}
		if(!d){++post[index];if(!c){++post[index];if(!b)++post[index];}}
		if(b&&!c&&d)
			mid[index]=48;
		if(a&&!b&&c)
			mid[index]=16;
		if(a&&!b&&!c&&d)
			mid[index]=32;

		//build shuffle vector
		memset(tmp, 0, 16);
		loc=0;written=0;
		for(x=0;x<a;++x)
			tmp[written++]=loc++;//mark first pixel

		loc=4;
		for(x=0;x<b;++x)
			tmp[written++]=loc++;//mark 2nd pixel
		if((mid[index]==16)||(mid[index]==32))
			tmp[written++]=loc++;//mark mid=1.1 and mid=1.2
		if(b)
			assert((mid[index]!=16) && (mid[index]!=32));

		loc=8;
		for(x=0;x<c;++x)
			tmp[written++]=loc++;//mark 3rd pixel
		if(mid[index]==48)
			tmp[written++]=loc++;//mark mid=2.1
		if(c)
			assert(mid[index]!=48);

		loc=12;
		for(x=0;x<d;++x)
			tmp[written++]=loc++;//mark 4th pixel
		memcpy(shuf+(index<<4), tmp, 16);
		len[index]=written;
	}}}}

	write_u8(sse_runwriter_blenddata_lut, 64, "static const uint8_t sse_runwriter_blenddata_lut");
	write_u8(sse_runwriter_blendmask_lut, 64, "static const uint8_t sse_runwriter_blendmask_lut");
	write_u8(len, 641, "static const uint8_t sse_runwriter_len_lut");
	write_u8(mid, 641, "static const uint8_t sse_runwriter_mid_lut");
	write_u8(pre, 641, "static const uint8_t sse_runwriter_pre_lut");
	write_u8(post, 641, "static const uint8_t sse_runwriter_post_lut");
	write_u8(shuf, 641<<4, "static const uint8_t sse_runwriter_shuffle_lut");
}

int main(){
	gen_runwriter_data();
}
