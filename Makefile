CC ?= musl-gcc
WC ?= x86_64-w64-mingw32ucrt-gcc

all: roiconv roiconv_exe roibench

roiconv:
	musl-gcc -static -Wall -Wextra -pedantic -O3 -Iwin32 -DROI -DQOI_SSE -msse -msse2 -msse3 -msse4 -std=c99 qoiconv.c -o roiconv

qoiconv:
	musl-gcc -static -Wall -Wextra -pedantic -O3 -Iwin32 -DQOI -std=c99 qoiconv.c -o qoiconv

roiconv_mlut:
	musl-gcc -c -static -Wall -O3 -Iwin32 -DROI -DQOI_SSE -msse -msse2 -msse3 -msse4 -DQOI_MLUT -std=c99 qoiconv.c -o roiconv_mlut.o
	ld -r -b binary -o roi_mlut.o roi.mlut
	musl-gcc roiconv_mlut.o roi_mlut.o -o roiconv_mlut

roiconv_exe:
	$(WC) -static -O3 -Iwin32 -DROI -DQOI_SSE -msse -msse2 -msse3 -msse4 -std=c99 qoiconv.c -o roiconv

roibench:
	$(CC) -Wall -O3 -DROI -DQOI_SSE -msse -msse2 -msse3 -msse4 -std=gnu99 qoibench.c -o roibench -llz4 -lpng -lzstd

qoibench:
	$(CC) -Wall -O3 -DQOI -std=gnu99 qoibench.c -o qoibench -llz4 -lpng -lzstd

roibench_exe:
	$(WC) -static -O3 -Iwin32 -DROI -DQOI_SSE -msse -msse2 -msse3 -msse4 -std=gnu99 qoibench.c -o roibench -llz4 -lpng -lzstd

.PHONY: clean
clean:
	$(RM) roiconv roiconv.exe roibench roibench.exe
