/*

Copyright (c) 2021, Dominic Szablewski - https://phoboslab.org
SPDX-License-Identifier: MIT


Simple benchmark suite for png, stbi and qoi

Requires libpng, "stb_image.h" and "stb_image_write.h"
Compile with: 
	gcc qoibench.c -std=gnu99 -lpng -O3 -o qoibench 

*/

#include <stdio.h>
#include <dirent.h>
#include <png.h>
#include "lz4.h"
#include "zstd.h"

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




// -----------------------------------------------------------------------------
// Cross platform high resolution timer
// From https://gist.github.com/ForeverZer0/0a4f80fc02b96e19380ebb7a3debbee5

#include <stdint.h>
#if defined(__linux)
	#define HAVE_POSIX_TIMER
	#include <time.h>
	#ifdef CLOCK_MONOTONIC
		#define CLOCKID CLOCK_MONOTONIC
	#else
		#define CLOCKID CLOCK_REALTIME
	#endif
#elif defined(__APPLE__)
	#define HAVE_MACH_TIMER
	#include <mach/mach_time.h>
#elif defined(_WIN32)
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#endif

static uint64_t ns() {
	static uint64_t is_init = 0;
#if defined(__APPLE__)
		static mach_timebase_info_data_t info;
		if (0 == is_init) {
			mach_timebase_info(&info);
			is_init = 1;
		}
		uint64_t now;
		now = mach_absolute_time();
		now *= info.numer;
		now /= info.denom;
		return now;
#elif defined(__linux)
		static struct timespec linux_rate;
		if (0 == is_init) {
			clock_getres(CLOCKID, &linux_rate);
			is_init = 1;
		}
		uint64_t now;
		struct timespec spec;
		clock_gettime(CLOCKID, &spec);
		now = spec.tv_sec * 1.0e9 + spec.tv_nsec;
		return now;
#elif defined(_WIN32)
		static LARGE_INTEGER win_frequency;
		if (0 == is_init) {
			QueryPerformanceFrequency(&win_frequency);
			is_init = 1;
		}
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		return (uint64_t) ((1e9 * now.QuadPart)	/ win_frequency.QuadPart);
#endif
}

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define ERROR(...) printf("abort at line " TOSTRING(__LINE__) ": " __VA_ARGS__); printf("\n"); exit(1)


// -----------------------------------------------------------------------------
// libpng encode/decode wrappers
// Seriously, who thought this was a good abstraction for an API to read/write
// images?

typedef struct {
	int size;
	int capacity;
	unsigned char *data;
} libpng_write_t;

void libpng_encode_callback(png_structp png_ptr, png_bytep data, png_size_t length) {
	libpng_write_t *write_data = (libpng_write_t*)png_get_io_ptr(png_ptr);
	if (write_data->size + length >= write_data->capacity) {
		ERROR("PNG write");
	}
	memcpy(write_data->data + write_data->size, data, length);
	write_data->size += length;
}

void *libpng_encode(void *pixels, int w, int h, int channels, int *out_len) {
	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png) {
		ERROR("png_create_write_struct");
	}

	png_infop info = png_create_info_struct(png);
	if (!info) {
		ERROR("png_create_info_struct");
	}

	if (setjmp(png_jmpbuf(png))) {
		ERROR("png_jmpbuf");
	}

	// Output is 8bit depth, RGBA format.
	png_set_IHDR(
		png,
		info,
		w, h,
		8,
		channels == 3 ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_RGBA,
		PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_DEFAULT,
		PNG_FILTER_TYPE_DEFAULT
	);

	png_bytep row_pointers[h];
	for(int y = 0; y < h; y++){
		row_pointers[y] = ((unsigned char *)pixels + y * w * channels);
	}

	libpng_write_t write_data = {
		.size = 0,
		.capacity = w * h * channels,
		.data = malloc(w * h * channels)
	};

	png_set_rows(png, info, row_pointers);
	png_set_write_fn(png, &write_data, libpng_encode_callback, NULL);
	png_write_png(png, info, PNG_TRANSFORM_IDENTITY, NULL);

	png_destroy_write_struct(&png, &info);

	*out_len = write_data.size;
	return write_data.data;
}


typedef struct {
	int pos;
	int size;
	unsigned char *data;
} libpng_read_t;

void png_decode_callback(png_structp png, png_bytep data, png_size_t length) {
	libpng_read_t *read_data = (libpng_read_t*)png_get_io_ptr(png);
	if (read_data->pos + length > read_data->size) {
		ERROR("PNG read %ld bytes at pos %d (size: %d)", length, read_data->pos, read_data->size);
	}
	memcpy(data, read_data->data + read_data->pos, length);
	read_data->pos += length;
}

void png_warning_callback(png_structp png_ptr, png_const_charp warning_msg) {
	// Ignore warnings about sRGB profiles and such.
}

void *libpng_decode(void *data, int size, int *out_w, int *out_h) {	
	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, png_warning_callback);
	if (!png) {
		ERROR("png_create_read_struct");
	}

	png_infop info = png_create_info_struct(png);
	if (!info) {
		ERROR("png_create_info_struct");
	}

	libpng_read_t read_data = {
		.pos = 0,
		.size = size,
		.data = data
	};
	
	png_set_read_fn(png, &read_data, png_decode_callback);
	png_set_sig_bytes(png, 0);
	png_read_info(png, info);
	
	png_uint_32 w, h;
	int bitDepth, colorType, interlaceType;
	png_get_IHDR(png, info, &w, &h, &bitDepth, &colorType, &interlaceType, NULL, NULL);
	
	// 16 bit -> 8 bit
	png_set_strip_16(png);
	
	// 1, 2, 4 bit -> 8 bit
	if (bitDepth < 8) {
		png_set_packing(png);
	}

	if (colorType & PNG_COLOR_MASK_PALETTE) {
		png_set_expand(png);
	}
	
	if (!(colorType & PNG_COLOR_MASK_COLOR)) {
		png_set_gray_to_rgb(png);
	}

	// set paletted or RGB images with transparency to full alpha so we get RGBA
	if (png_get_valid(png, info, PNG_INFO_tRNS)) {
		png_set_tRNS_to_alpha(png);
	}
	
	// make sure every pixel has an alpha value
	if (!(colorType & PNG_COLOR_MASK_ALPHA)) {
		png_set_filler(png, 255, PNG_FILLER_AFTER);
	}
	
	png_read_update_info(png, info);

	unsigned char* out = malloc(w * h * 4);
	*out_w = w;
	*out_h = h;
	
	// png_uint_32 rowBytes = png_get_rowbytes(png, info);
	png_bytep row_pointers[h];
	for (png_uint_32 row = 0; row < h; row++ ) {
		row_pointers[row] = (png_bytep)(out + (row * w * 4));
	}
	
	png_read_image(png, row_pointers);
	png_read_end(png, info);
	png_destroy_read_struct( &png, &info, NULL);
	
	return out;
}


// -----------------------------------------------------------------------------
// stb_image encode callback

void stbi_write_callback(void *context, void *data, int size) {
	int *encoded_size = (int *)context;
	*encoded_size += size;
	// In theory we'd need to do another malloc(), memcpy() and free() here to 
	// be fair to the other decode functions...
}


// -----------------------------------------------------------------------------
// function to load a whole file into memory

void *fload(const char *path, int *out_size) {
	FILE *fh = fopen(path, "rb");
	if (!fh) {
		ERROR("Can't open file");
	}

	fseek(fh, 0, SEEK_END);
	int size = ftell(fh);
	fseek(fh, 0, SEEK_SET);

	void *buffer = malloc(size);
	if (!buffer) {
		ERROR("Malloc for %d bytes failed", size);
	}

	if (!fread(buffer, size, 1, fh)) {
		ERROR("Can't read file %s", path);
	}
	fclose(fh);

	*out_size = size;
	return buffer;
}


// -----------------------------------------------------------------------------
// benchmark runner


int opt_runs = 1;
int opt_nopng = 0;
int opt_nowarmup = 0;
int opt_noverify = 0;
int opt_nodecode = 0;
int opt_noencode = 0;
int opt_norecurse = 0;
int opt_onlytotals = 0;
int opt_nolz4 = 0;
int opt_nozstd1 = 0;
int opt_nozstd3 = 0;
int opt_nozstd9 = 0;
int opt_nozstd19 = 0;
options opt={0};

enum {
	LIBPNG,
	STBI,
	QOI,
	LZ4,
	ZSTD1,
	ZSTD3,
	ZSTD9,
	ZSTD19,
	BENCH_COUNT /* must be the last element */
};
static const char *const lib_names[BENCH_COUNT] = {
	// NOTE: pad with spaces so everything lines up properly
	[LIBPNG] =  "libpng:     ",
	[STBI]   =  "stbi:       ",
	[QOI]    =  EXT_STR":        ",
	[LZ4]    =  EXT_STR".lz4:    ",
	[ZSTD1]    =  EXT_STR".zstd1:  ",
	[ZSTD3]    =  EXT_STR".zstd3:  ",
	[ZSTD9]    =  EXT_STR".zstd9:  ",
	[ZSTD19]    =  EXT_STR".zstd19: "
};

typedef struct {
	uint64_t size;
	uint64_t encode_time;
	uint64_t decode_time;
} benchmark_lib_result_t;

typedef struct {
	int count;
	uint64_t raw_size;
	uint64_t px;
	int w;
	int h;
	benchmark_lib_result_t libs[BENCH_COUNT];
} benchmark_result_t;


void benchmark_print_result(benchmark_result_t res) {
	res.px /= res.count;
	res.raw_size /= res.count;

	double px = res.px;
	printf("              decode ms   encode ms   decode mpps   encode mpps   size kb    rate\n");
	for (int i = 0; i < BENCH_COUNT; ++i) {
		if (opt_nopng && (i == LIBPNG || i == STBI))
			continue;
		if(opt_nolz4 && (i == LZ4) )
			continue;
		if(opt_nozstd1 && (i == ZSTD1) )
			continue;
		if(opt_nozstd3 && (i == ZSTD3) )
			continue;
		if(opt_nozstd9 && (i == ZSTD9) )
			continue;
		if(opt_nozstd19 && (i == ZSTD19) )
			continue;
		res.libs[i].encode_time /= res.count;
		res.libs[i].decode_time /= res.count;
		res.libs[i].size /= res.count;
		printf(
			"%s   %8.1f    %8.1f      %8.2f      %8.2f  %8ld   %4.1f%%\n",
			lib_names[i],
			(double)res.libs[i].decode_time/1000000.0,
			(double)res.libs[i].encode_time/1000000.0,
			(res.libs[i].decode_time > 0 ? px / ((double)res.libs[i].decode_time/1000.0) : 0),
			(res.libs[i].encode_time > 0 ? px / ((double)res.libs[i].encode_time/1000.0) : 0),
			res.libs[i].size/1024,
			((double)res.libs[i].size/(double)res.raw_size) * 100.0
		);
	}
	printf("\n");
}

// Run __VA_ARGS__ a number of times and measure the time taken. The first
// run is ignored.
#define BENCHMARK_FN(NOWARMUP, RUNS, AVG_TIME, ...) \
	do { \
		uint64_t time = 0; \
		for (int i = NOWARMUP; i <= RUNS; i++) { \
			uint64_t time_start = ns(); \
			__VA_ARGS__ \
			uint64_t time_end = ns(); \
			if (i > 0) { \
				time += time_end - time_start; \
			} \
		} \
		AVG_TIME = time / RUNS; \
	} while (0)


benchmark_result_t benchmark_image(const char *path) {
	int encoded_png_size;
	int encoded_qoi_size;
	int encoded_qoi_lz4_size=0;
	int encoded_qoi_zstd1_size=0;
	int encoded_qoi_zstd3_size=0;
	int encoded_qoi_zstd9_size=0;
	int encoded_qoi_zstd19_size=0;
	int w;
	int h;
	int channels;

	// Load the encoded PNG, encoded QOI and raw pixels into memory
	if(!stbi_info(path, &w, &h, &channels)) {
		ERROR("Error decoding header %s", path);
	}

	if (channels != 3)
		channels = 4;

	void *pixels = (void *)stbi_load(path, &w, &h, NULL, channels);
	void *encoded_png = fload(path, &encoded_png_size);
	void *encoded_qoi = qoi_encode(pixels, &(qoi_desc){
			.width = w,
			.height = h, 
			.channels = channels,
			.colorspace = QOI_SRGB
		}, &encoded_qoi_size, &opt);
	void *encoded_qoi_lz4=NULL;
	void *encoded_qoi_zstd1=NULL;
	void *encoded_qoi_zstd3=NULL;
	void *encoded_qoi_zstd9=NULL;
	void *encoded_qoi_zstd19=NULL;
	if(!opt_nolz4) {
		encoded_qoi_lz4 = malloc(LZ4_compressBound(encoded_qoi_size));
		encoded_qoi_lz4_size = LZ4_compress_default(encoded_qoi, encoded_qoi_lz4, encoded_qoi_size, LZ4_compressBound(encoded_qoi_size));
	}
	if(!opt_nozstd1) {
		encoded_qoi_zstd1 = malloc(ZSTD_compressBound(encoded_qoi_size));
		encoded_qoi_zstd1_size = ZSTD_compress(encoded_qoi_zstd1, ZSTD_compressBound(encoded_qoi_size), encoded_qoi, encoded_qoi_size, 1);
	}
	if(!opt_nozstd3) {
		encoded_qoi_zstd3 = malloc(ZSTD_compressBound(encoded_qoi_size));
		encoded_qoi_zstd3_size = ZSTD_compress(encoded_qoi_zstd3, ZSTD_compressBound(encoded_qoi_size), encoded_qoi, encoded_qoi_size, 3);
	}
	if(!opt_nozstd9) {
		encoded_qoi_zstd9 = malloc(ZSTD_compressBound(encoded_qoi_size));
		encoded_qoi_zstd9_size = ZSTD_compress(encoded_qoi_zstd9, ZSTD_compressBound(encoded_qoi_size), encoded_qoi, encoded_qoi_size, 9);
	}
	if(!opt_nozstd19) {
		encoded_qoi_zstd19 = malloc(ZSTD_compressBound(encoded_qoi_size));
		encoded_qoi_zstd19_size = ZSTD_compress(encoded_qoi_zstd19, ZSTD_compressBound(encoded_qoi_size), encoded_qoi, encoded_qoi_size, 19);
	}

	if (!pixels || !encoded_qoi || !encoded_png) {
		ERROR("Error encoding %s", path);
	}

	// Verify QOI Output

	if (!opt_noverify) {
		qoi_desc dc;
		void *pixels_qoi = qoi_decode(encoded_qoi, encoded_qoi_size, &dc, channels);
		if (memcmp(pixels, pixels_qoi, w * h * channels) != 0) {
			ERROR(EXT_STR" roundtrip pixel mismatch for %s", path);
		}
		free(pixels_qoi);
	}

	benchmark_result_t res = {0};
	res.count = 1;
	res.raw_size = w * h * channels;
	res.px = w * h;
	res.w = w;
	res.h = h;

	// Decoding
	if (!opt_nodecode) {
		if (!opt_nopng) {
			BENCHMARK_FN(opt_nowarmup, opt_runs, res.libs[LIBPNG].decode_time, {
				int dec_w, dec_h;
				void *dec_p = libpng_decode(encoded_png, encoded_png_size, &dec_w, &dec_h);
				free(dec_p);
			});

			BENCHMARK_FN(opt_nowarmup, opt_runs, res.libs[STBI].decode_time, {
				int dec_w, dec_h, dec_channels;
				void *dec_p = stbi_load_from_memory(encoded_png, encoded_png_size, &dec_w, &dec_h, &dec_channels, 4);
				free(dec_p);
			});
		}

		BENCHMARK_FN(opt_nowarmup, opt_runs, res.libs[QOI].decode_time, {
			qoi_desc desc;
			void *dec_p = qoi_decode(encoded_qoi, encoded_qoi_size, &desc, channels);
			free(dec_p);
		});

		if (!opt_nolz4) {
			BENCHMARK_FN(opt_nowarmup, opt_runs, res.libs[LZ4].decode_time, {
				qoi_desc desc;
				void *dec_lz4=malloc(encoded_qoi_size);
				LZ4_decompress_safe(encoded_qoi_lz4, dec_lz4, encoded_qoi_lz4_size, encoded_qoi_size);
				void *dec_p = qoi_decode(dec_lz4, encoded_qoi_size, &desc, channels);
				free(dec_p);
				free(dec_lz4);
			});
		}

		if (!opt_nozstd1) {
			BENCHMARK_FN(opt_nowarmup, opt_runs, res.libs[ZSTD1].decode_time, {
				qoi_desc desc;
				void *dec=malloc(encoded_qoi_size);
				ZSTD_decompress(dec, encoded_qoi_size, encoded_qoi_zstd1, encoded_qoi_zstd1_size);
				void *dec_p = qoi_decode(dec, encoded_qoi_size, &desc, channels);
				free(dec_p);
				free(dec);
			});
		}

		if (!opt_nozstd3) {
			BENCHMARK_FN(opt_nowarmup, opt_runs, res.libs[ZSTD3].decode_time, {
				qoi_desc desc;
				void *dec=malloc(encoded_qoi_size);
				ZSTD_decompress(dec, encoded_qoi_size, encoded_qoi_zstd3, encoded_qoi_zstd3_size);
				void *dec_p = qoi_decode(dec, encoded_qoi_size, &desc, channels);
				free(dec_p);
				free(dec);
			});
		}

		if (!opt_nozstd9) {
			BENCHMARK_FN(opt_nowarmup, opt_runs, res.libs[ZSTD9].decode_time, {
				qoi_desc desc;
				void *dec=malloc(encoded_qoi_size);
				ZSTD_decompress(dec, encoded_qoi_size, encoded_qoi_zstd9, encoded_qoi_zstd9_size);
				void *dec_p = qoi_decode(dec, encoded_qoi_size, &desc, channels);
				free(dec_p);
				free(dec);
			});
		}

		if (!opt_nozstd19) {
			BENCHMARK_FN(opt_nowarmup, opt_runs, res.libs[ZSTD19].decode_time, {
				qoi_desc desc;
				void *dec=malloc(encoded_qoi_size);
				ZSTD_decompress(dec, encoded_qoi_size, encoded_qoi_zstd19, encoded_qoi_zstd19_size);
				void *dec_p = qoi_decode(dec, encoded_qoi_size, &desc, channels);
				free(dec_p);
				free(dec);
			});
		}
	}

	// Encoding
	if (!opt_noencode) {
		if (!opt_nopng) {
			BENCHMARK_FN(opt_nowarmup, opt_runs, res.libs[LIBPNG].encode_time, {
				int enc_size;
				void *enc_p = libpng_encode(pixels, w, h, channels, &enc_size);
				res.libs[LIBPNG].size = enc_size;
				free(enc_p);
			});

			BENCHMARK_FN(opt_nowarmup, opt_runs, res.libs[STBI].encode_time, {
				int enc_size = 0;
				stbi_write_png_to_func(stbi_write_callback, &enc_size, w, h, channels, pixels, 0);
				res.libs[STBI].size = enc_size;
			});
		}

		BENCHMARK_FN(opt_nowarmup, opt_runs, res.libs[QOI].encode_time, {
			int enc_size;
			void *enc_p = qoi_encode(pixels, &(qoi_desc){
				.width = w,
				.height = h, 
				.channels = channels,
					.colorspace = QOI_SRGB
			}, &enc_size, &opt);
			res.libs[QOI].size = enc_size;
			free(enc_p);
		});

		if (!opt_nolz4) {
			BENCHMARK_FN(opt_nowarmup, opt_runs, res.libs[LZ4].encode_time, {
				int enc_size;
				void *enc;
				void *enc_p = qoi_encode(pixels, &(qoi_desc){
					.width = w,
					.height = h, 
					.channels = channels,
					.colorspace = QOI_SRGB
				}, &enc_size, &opt);
				enc = malloc(LZ4_compressBound(enc_size));
				res.libs[LZ4].size = LZ4_compress_default(enc_p, enc, enc_size, LZ4_compressBound(enc_size));
				free(enc_p);
				free(enc);
			});
		}

		if (!opt_nozstd1) {
			BENCHMARK_FN(opt_nowarmup, opt_runs, res.libs[ZSTD1].encode_time, {
				int enc_size;
				void *enc;
				void *enc_p = qoi_encode(pixels, &(qoi_desc){
					.width = w,
					.height = h, 
					.channels = channels,
					.colorspace = QOI_SRGB
				}, &enc_size, &opt);
				enc = malloc(ZSTD_compressBound(enc_size));
				res.libs[ZSTD1].size = ZSTD_compress(enc, ZSTD_compressBound(enc_size), enc_p, enc_size, 1);
				free(enc_p);
				free(enc);
			});
		}

		if (!opt_nozstd3) {
			BENCHMARK_FN(opt_nowarmup, opt_runs, res.libs[ZSTD3].encode_time, {
				int enc_size;
				void *enc;
				void *enc_p = qoi_encode(pixels, &(qoi_desc){
					.width = w,
					.height = h, 
					.channels = channels,
					.colorspace = QOI_SRGB
				}, &enc_size, &opt);
				enc = malloc(ZSTD_compressBound(enc_size));
				res.libs[ZSTD3].size = ZSTD_compress(enc, ZSTD_compressBound(enc_size), enc_p, enc_size, 3);
				free(enc_p);
				free(enc);
			});
		}

		if (!opt_nozstd9) {
			BENCHMARK_FN(opt_nowarmup, opt_runs, res.libs[ZSTD9].encode_time, {
				int enc_size;
				void *enc;
				void *enc_p = qoi_encode(pixels, &(qoi_desc){
					.width = w,
					.height = h, 
					.channels = channels,
					.colorspace = QOI_SRGB
				}, &enc_size, &opt);
				enc = malloc(ZSTD_compressBound(enc_size));
				res.libs[ZSTD9].size = ZSTD_compress(enc, ZSTD_compressBound(enc_size), enc_p, enc_size, 9);
				free(enc_p);
				free(enc);
			});
		}

		if (!opt_nozstd19) {
			BENCHMARK_FN(opt_nowarmup, opt_runs, res.libs[ZSTD19].encode_time, {
				int enc_size;
				void *enc;
				void *enc_p = qoi_encode(pixels, &(qoi_desc){
					.width = w,
					.height = h, 
					.channels = channels,
					.colorspace = QOI_SRGB
				}, &enc_size, &opt);
				enc = malloc(ZSTD_compressBound(enc_size));
				res.libs[ZSTD19].size = ZSTD_compress(enc, ZSTD_compressBound(enc_size), enc_p, enc_size, 19);
				free(enc_p);
				free(enc);
			});
		}
	}

	free(pixels);
	free(encoded_png);
	free(encoded_qoi);
	free(encoded_qoi_lz4);
	free(encoded_qoi_zstd1);
	free(encoded_qoi_zstd3);
	free(encoded_qoi_zstd9);
	free(encoded_qoi_zstd19);

	return res;
}

void benchmark_directory(const char *path, benchmark_result_t *grand_total) {
	DIR *dir = opendir(path);
	if (!dir) {
		ERROR("Couldn't open directory %s", path);
	}

	struct dirent *file;

	if (!opt_norecurse) {
		for (int i = 0; (file = readdir(dir)) != NULL; i++) {
			if (
				file->d_type & DT_DIR &&
				strcmp(file->d_name, ".") != 0 &&
				strcmp(file->d_name, "..") != 0
			) {
				char subpath[1024];
				snprintf(subpath, 1024, "%s/%s", path, file->d_name);
				benchmark_directory(subpath, grand_total);
			}
		}
		rewinddir(dir);
	}

	benchmark_result_t dir_total = {0};
	
	int has_shown_head = 0;
	for (int i = 0; (file = readdir(dir)) != NULL; i++) {
		if (strcmp(file->d_name + strlen(file->d_name) - 4, ".png") != 0) {
			continue;
		}

		if (!has_shown_head) {
			has_shown_head = 1;
			printf("## Benchmarking %s/*.png -- %d runs\n\n", path, opt_runs);
		}

		char *file_path = malloc(strlen(file->d_name) + strlen(path)+8);
		sprintf(file_path, "%s/%s", path, file->d_name);
		
		benchmark_result_t res = benchmark_image(file_path);

		if (!opt_onlytotals) {
			printf("## %s size: %dx%d\n", file_path, res.w, res.h);
			benchmark_print_result(res);
		}

		free(file_path);
		
		dir_total.count++;
		dir_total.raw_size += res.raw_size;
		dir_total.px += res.px;
		for (int i = 0; i < BENCH_COUNT; ++i) {
			dir_total.libs[i].encode_time += res.libs[i].encode_time;
			dir_total.libs[i].decode_time += res.libs[i].decode_time;
			dir_total.libs[i].size += res.libs[i].size;
		}

		grand_total->count++;
		grand_total->raw_size += res.raw_size;
		grand_total->px += res.px;
		for (int i = 0; i < BENCH_COUNT; ++i) {
			grand_total->libs[i].encode_time += res.libs[i].encode_time;
			grand_total->libs[i].decode_time += res.libs[i].decode_time;
			grand_total->libs[i].size += res.libs[i].size;
		}
	}
	closedir(dir);

	if (dir_total.count > 0) {
		printf("## Total for %s\n", path);
		benchmark_print_result(dir_total);
	}
}

int main(int argc, char **argv) {
	if (argc < 3) {
		printf("Usage: "EXT_STR"bench <iterations> <directory> [options]\n");
		printf("Options:\n");
		printf("    --nowarmup ... don't perform a warmup run\n");
		printf("    --nopng ...... don't run png encode/decode\n");
		printf("    --noverify ... don't verify "EXT_STR" roundtrip\n");
		printf("    --noencode ... don't run encoders\n");
		printf("    --nodecode ... don't run decoders\n");
		printf("    --norecurse .. don't descend into directories\n");
		printf("    --onlytotals . don't print individual image results\n");
		printf("    --nolz4 ...... don't benchmark chained lz4 compression\n");
		printf("    --nozstd1 .... don't benchmark chained zstd compression level 1\n");
		printf("    --nozstd3 .... don't benchmark chained zstd compression level 3\n");
		printf("    --nozstd9 .... don't benchmark chained zstd compression level 9\n");
		printf("    --nozstd19 ... don't benchmark chained zstd compression level 19\n");
		printf("    --rle ........ enable RLE on "EXT_STR" encode, default disabled if possible\n");
		printf("    --scalar ..... use scalar encode path\n");
		printf("    --sse ........ use SSE encode path (if possible)\n");
		printf("Examples\n");
		printf("    "EXT_STR"bench 10 images/textures/\n");
		printf("    "EXT_STR"bench 1 images/textures/ --nopng --nowarmup\n");
		exit(1);
	}

	for (int i = 3; i < argc; i++) {
		if (strcmp(argv[i], "--nowarmup") == 0) { opt_nowarmup = 1; }
		else if (strcmp(argv[i], "--nopng") == 0) { opt_nopng = 1; }
		else if (strcmp(argv[i], "--noverify") == 0) { opt_noverify = 1; }
		else if (strcmp(argv[i], "--noencode") == 0) { opt_noencode = 1; }
		else if (strcmp(argv[i], "--nodecode") == 0) { opt_nodecode = 1; }
		else if (strcmp(argv[i], "--norecurse") == 0) { opt_norecurse = 1; }
		else if (strcmp(argv[i], "--onlytotals") == 0) { opt_onlytotals = 1; }
		else if (strcmp(argv[i], "--nolz4") == 0) { opt_nolz4 = 1; }
		else if (strcmp(argv[i], "--nozstd1") == 0) { opt_nozstd1 = 1; }
		else if (strcmp(argv[i], "--nozstd3") == 0) { opt_nozstd3 = 1; }
		else if (strcmp(argv[i], "--nozstd9") == 0) { opt_nozstd9 = 1; }
		else if (strcmp(argv[i], "--nozstd19") == 0) { opt_nozstd19 = 1; }
		else if (strcmp(argv[i], "--rle") == 0) { opt.rle = 1; }
		else if (strcmp(argv[i], "--scalar") == 0) { opt.path = scalar; }
		else if (strcmp(argv[i], "--sse") == 0) { opt.path = sse; }
		else { ERROR("Unknown option %s", argv[i]); }
	}

	opt_runs = atoi(argv[1]);
	if (opt_runs <=0) {
		ERROR("Invalid number of runs %d", opt_runs);
	}

	benchmark_result_t grand_total = {0};
	benchmark_directory(argv[2], &grand_total);

	if (grand_total.count > 0) {
		printf("# Grand total for %s\n", argv[2]);
		benchmark_print_result(grand_total);
	}
	else {
		printf("No images found in %s\n", argv[2]);
	}

	return 0;
}
