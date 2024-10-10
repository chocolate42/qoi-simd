CC ?= gcc

all: roiconv roiconv_exe roibench

roiconv:
	$(CC) -O3 -DROI -DQOI_SSE -msse -msse2 -msse3 -msse4 -std=c99 qoiconv.c -o roiconv

roiconv_exe:
	x86_64-w64-mingw32ucrt-gcc -static -O3 -Iwin32 -DROI -DQOI_SSE -msse -msse2 -msse3 -msse4 -std=c99 qoiconv.c -o roiconv

roibench:
	$(CC) -O3 -DROI -DQOI_SSE -msse -msse2 -msse3 -msse4 -std=gnu99 qoibench.c -o roibench -llz4 -lpng -lzstd

roibench_exe:
	x86_64-w64-mingw32ucrt-gcc -static -O3 -Iwin32 -DROI -DQOI_SSE -msse -msse2 -msse3 -msse4 -std=gnu99 qoibench.c -o roibench -llz4 -lpng -lzstd

.PHONY: clean
clean:
	$(RM) roiconv roiconv.exe roibench roibench.exe
