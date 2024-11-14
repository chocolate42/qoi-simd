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
#include "qoi.h"

#ifndef QOI_MLUT_EMBED
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#endif

#define STR_ENDS_WITH(S, E) (strcmp(S + strlen(S) - (sizeof(E)-1), E) == 0)

int main(int argc, char **argv) {
#ifndef QOI_MLUT_EMBED
#ifdef _WIN32
	HANDLE fd, file_mapping_object;
#endif
#endif
	options opt={0};
	if (argc < 3) {
		puts("Usage: "EXT_STR"conv [ops] <infile> <outfile>");
		puts("[ops]");
#ifdef ROI
		puts(" -mlut : Use mega-LUT to encode anything normally done with standard scalar");
#ifndef QOI_MLUT_EMBED
		puts(" -mlut-path file : File containing mega-LUT");
		puts(" -mlut-gen file: Generate mega-LUT");
#endif
#endif
		puts("Examples:");
		puts("  "EXT_STR"conv input.png output."EXT_STR"");
		puts("  "EXT_STR"conv input."EXT_STR" output.png");
		exit(1);
	}

	for(int i=1;i<argc;++i){
		if(0);
#ifdef ROI
		else if(strcmp(argv[i], "-mlut")==0)
			opt.mlut=1;
#ifndef QOI_MLUT_EMBED
		else if(strcmp(argv[i], "-mlut-path")==0){
#ifdef _WIN32
			fd = CreateFileA(argv[i+1], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if(fd==INVALID_HANDLE_VALUE)
				return fprintf(stderr, "CreateFileA failed\n");
			file_mapping_object = CreateFileMappingA(fd, NULL, PAGE_READONLY, 0, 0, NULL);
			if(NULL==file_mapping_object)
				return fprintf(stderr, "CreateFileMappingA failed\n");
			qoi_mlut=MapViewOfFile(file_mapping_object, FILE_MAP_READ, 0, 0, 0);
#else
			#include <fcntl.h>
			#include <sys/mman.h>
			#include <unistd.h>
			int fd = open(argv[i+1], O_RDONLY);
			if(-1==fd)
				return fprintf(stderr, "open() for mmap failed\n");
			qoi_mlut=mmap(NULL, 256*256*256*5, PROT_READ, MAP_SHARED, fd, 0);
			if(MAP_FAILED==qoi_mlut)
				return fprintf(stderr, "mmap failed\n");
			close(fd);
#endif
			++i;
		}
		else if(strcmp(argv[i], "-mlut-gen")==0)
			return gen_mlut(argv[i+1]);
#endif
#endif
		else if(i<(argc-2))
			return fprintf(stderr, "Unknown option '%s'\n", argv[i]);
	}

#ifdef ROI
	if(opt.mlut && !qoi_mlut)
		return fprintf(stderr, "mlut path requires mlut to be present (built into executable or defined with -mlut-path file)\n");
#endif
	if ((STR_ENDS_WITH(argv[argc-2], ".ppm")) && ((STR_ENDS_WITH(argv[argc-1], "."EXT_STR))||(0==strcmp(argv[argc-1], "-"))) )
		return qoi_write_from_ppm(argv[argc-2], argv[argc-1], &opt);
	else if ( ((STR_ENDS_WITH(argv[argc-2], "."EXT_STR))||(0==strcmp(argv[argc-2], "-"))) && (STR_ENDS_WITH(argv[argc-1], ".ppm")))
		return qoi_read_to_ppm(argv[argc-2], argv[argc-1], &opt);
	if ((STR_ENDS_WITH(argv[argc-2], ".pam")) && ((STR_ENDS_WITH(argv[argc-1], "."EXT_STR))||(0==strcmp(argv[argc-1], "-"))) )
		return qoi_write_from_pam(argv[argc-2], argv[argc-1], &opt);
	else if ( ((STR_ENDS_WITH(argv[argc-2], "."EXT_STR))||(0==strcmp(argv[argc-2], "-"))) && (STR_ENDS_WITH(argv[argc-1], ".pam")))
		return qoi_read_to_pam(argv[argc-2], argv[argc-1], &opt);
	else {

		void *pixels = NULL;
		int w, h, channels;
		if (STR_ENDS_WITH(argv[argc-2], ".png")) {
			if(!stbi_info(argv[argc-2], &w, &h, &channels)) {
				fprintf(stderr, "Couldn't read header %s\n", argv[argc-2]);
				exit(1);
			}

			if(channels != 3)// Force all odd encodings to be RGBA
				channels = 4;

			pixels = (void *)stbi_load(argv[argc-2], &w, &h, NULL, channels);
			pixels = realloc(pixels, (w*h*channels)+1);
		}
		else if (STR_ENDS_WITH(argv[argc-2], "."EXT_STR)) {
			qoi_desc desc;
			pixels = qoi_read(argv[argc-2], &desc, 0, &opt);
			channels = desc.channels;
			w = desc.width;
			h = desc.height;
		}

		if (pixels == NULL) {
			fprintf(stderr, "Couldn't load/decode %s\n", argv[argc-2]);
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
				.colorspace = QOI_SRGB
			}, &opt);
		}

		if (!encoded) {
			fprintf(stderr, "Couldn't write/encode %s\n", argv[argc-1]);
			exit(1);
		}

		if (STR_ENDS_WITH(argv[argc-2], "."EXT_STR))
			QOI_FREE(pixels);
		else
			free(pixels);
	}
#ifdef ROI
#ifndef QOI_MLUT_EMBED
	if(qoi_mlut){
#ifdef _WIN32
		UnmapViewOfFile(qoi_mlut);
		CloseHandle(file_mapping_object);
		CloseHandle(fd);
#endif
	//munmap TODO maybe, automatically done on exit anyway
	}
#endif
#endif
	return 0;
}
