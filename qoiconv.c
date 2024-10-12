/*

Copyright (c) 2021, Dominic Szablewski - https://phoboslab.org
SPDX-License-Identifier: MIT


Command line tool to convert between png <> qoi format

Requires:
	-"stb_image.h" (https://github.com/nothings/stb/blob/master/stb_image.h)
	-"stb_image_write.h" (https://github.com/nothings/stb/blob/master/stb_image_write.h)
	-"qoi.h" (https://github.com/phoboslab/qoi/blob/master/qoi.h)

Compile with: 
	gcc qoiconv.c -std=c99 -O3 -o qoiconv

*/


#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define QOI_IMPLEMENTATION
#ifdef ROI
#define EXT_STR "roi"
#include "roi.h"
#elif defined SOI
#define EXT_STR "soi"
#include "soi.h"
#else
#define EXT_STR "qoi"
#include "qoi.h"
#endif


#define STR_ENDS_WITH(S, E) (strcmp(S + strlen(S) - (sizeof(E)-1), E) == 0)

int main(int argc, char **argv) {
	int norle=0;
	options opt={0};
	if (argc < 3) {
		puts("Usage: "EXT_STR"conv [ops] <infile> <outfile>");
		puts("[ops]");
		puts(" -rle : Enable RLE (disabled by default)");
		puts(" -scalar : Use scalar instructions");
		puts(" -sse : Use SSE instructions (if possible)");
		puts("Defaults to fastest implemented instruction set");
		puts("Examples:");
		puts("  "EXT_STR"conv input.png output."EXT_STR"");
		puts("  "EXT_STR"conv input."EXT_STR" output.png");
		exit(1);
	}

	for(int i=1;i<(argc-2);++i){
		if(strcmp(argv[i], "-rle")==0)
			opt.rle=1;
		else if(strcmp(argv[i], "-scalar")==0)
			opt.path=scalar;
		else if(strcmp(argv[i], "-sse")==0)
			opt.path=sse;
		else
			return printf("Unknown option '%s'\n", argv[i]);
	}

#ifdef ROI
	if ((STR_ENDS_WITH(argv[argc-2], ".ppm")) && (STR_ENDS_WITH(argv[argc-1], ".roi")))
		return qoi_write_from_ppm(argv[argc-2], argv[argc-1], &opt);
	else if ((STR_ENDS_WITH(argv[argc-2], ".roi")) && (STR_ENDS_WITH(argv[argc-1], ".ppm")))
		return qoi_read_to_ppm(argv[argc-2], argv[argc-1], &opt);
	if ((STR_ENDS_WITH(argv[argc-2], ".pam")) && (STR_ENDS_WITH(argv[argc-1], ".roi")))
		return qoi_write_from_pam(argv[argc-2], argv[argc-1], &opt);
	else if ((STR_ENDS_WITH(argv[argc-2], ".roi")) && (STR_ENDS_WITH(argv[argc-1], ".pam")))
		return qoi_read_to_pam(argv[argc-2], argv[argc-1], &opt);
	else {
#endif

	void *pixels = NULL;
	int w, h, channels;
	if (STR_ENDS_WITH(argv[argc-2], ".png")) {
		if(!stbi_info(argv[argc-2], &w, &h, &channels)) {
			printf("Couldn't read header %s\n", argv[argc-2]);
			exit(1);
		}

		if(channels != 3)// Force all odd encodings to be RGBA
			channels = 4;

		pixels = (void *)stbi_load(argv[argc-2], &w, &h, NULL, channels);
	}
	else if (STR_ENDS_WITH(argv[argc-2], "."EXT_STR)) {
		qoi_desc desc;
		pixels = qoi_read(argv[argc-2], &desc, 0);
		channels = desc.channels;
		w = desc.width;
		h = desc.height;
	}

	if (pixels == NULL) {
		printf("Couldn't load/decode %s\n", argv[argc-2]);
		exit(1);
	}

	int encoded = 0;
	if (STR_ENDS_WITH(argv[argc-1], ".png")) {
		encoded = stbi_write_png(argv[argc-1], w, h, channels, pixels, 0);
	}
	else if (STR_ENDS_WITH(argv[argc-1], "."EXT_STR)) {
		encoded = qoi_write(argv[argc-1], pixels, &(qoi_desc){
			.width = w,
			.height = h, 
			.channels = channels,
			.colorspace = (norle<<1)|QOI_SRGB
		}, &opt);
	}
	else if (STR_ENDS_WITH(argv[argc-1], ".ppm")) {
		char header[512];
		FILE *fo = fopen(argv[argc-1], "wb");
		int headerlen;
		headerlen=sprintf(header, "P6 %u %u 255\n", w, h);
		fwrite(header, 1, headerlen, fo);
		fwrite(pixels, 1, w*h*3, fo);
		fclose(fo);
		encoded=1;
	}

	if (!encoded) {
		printf("Couldn't write/encode %s\n", argv[argc-1]);
		exit(1);
	}

	if (STR_ENDS_WITH(argv[argc-2], "."EXT_STR))
		QOI_FREE(pixels);
	else
		free(pixels);
#ifdef ROI
	}
#endif
	return 0;
}
