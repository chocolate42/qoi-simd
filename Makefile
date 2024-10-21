CC ?= musl-gcc
WC ?= x86_64-w64-mingw32ucrt-gcc

# -DQOI_SSE enables SSE implementation so build also needs to target SSE instructions with -msse -msse2 -msse3 -msse4
# -DQOI_MLUT_EMBED embeds the mlut directly into the executable

roiconv:
	musl-gcc -static -Wall -Wextra -pedantic -O3 -Iwin32 -DROI -DQOI_SSE -msse -msse2 -msse3 -msse4 -std=c99 qoiconv.c -o roiconv

roiconv_exe:
	$(WC) -static -O3 -Iwin32 -DROI -DQOI_SSE -msse -msse2 -msse3 -msse4 -std=c99 qoiconv.c -o roiconv

qoiconv:
	musl-gcc -static -Wall -Wextra -pedantic -O3 -Iwin32 -DQOI -std=c99 qoiconv.c -o qoiconv



#to generate roi.mlut first build roiconv without -DQOI_MLUT_EMBED then run ./roiconv -mlut-gen roi.mlut

roibench_mlut:
	$(CC) -c -Wall -O3 -DROI -DQOI_SSE -msse -msse2 -msse3 -msse4 -DQOI_MLUT_EMBED -std=gnu99 qoibench.c -o roibench_mlut.o
	ld -r -b binary -o roi_mlut.o roi.mlut
	$(CC) roibench_mlut.o roi_mlut.o -o roibench_mlut -llz4 -lpng -lzstd

roiconv_mlut:
	musl-gcc -c -static -Wall -O3 -Iwin32 -DROI -DQOI_SSE -msse -msse2 -msse3 -msse4 -DQOI_MLUT_EMBED -std=c99 qoiconv.c -o roiconv_mlut.o
	ld -r -b binary -o roi_mlut.o roi.mlut
	musl-gcc roiconv_mlut.o roi_mlut.o -o roiconv_mlut

roibench:
	$(CC) -Wall -O3 -DROI -DQOI_SSE -msse -msse2 -msse3 -msse4 -std=gnu99 qoibench.c -o roibench -llz4 -lpng -lzstd

qoibench:
	$(CC) -Wall -O3 -DQOI -std=gnu99 qoibench.c -o qoibench -llz4 -lpng -lzstd

roibench_exe:
	$(WC) -static -O3 -Iwin32 -DROI -DQOI_SSE -msse -msse2 -msse3 -msse4 -std=gnu99 qoibench.c -o roibench -llz4 -lpng -lzstd

.PHONY: clean
clean:
	$(RM) roiconv roiconv.exe roibench roibench.exe
