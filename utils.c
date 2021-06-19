#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils.h"
#include "ascii.h"

void *
xmalloc(size_t count, size_t size)
{
	void *ptr = malloc(count * size);
	if (!ptr) {
		fprintf(stderr, "ERROR(%s:%d) malloc failure: aborting\n", __FILE__, __LINE__);
		exit(1);
	}
	return ptr;
}

void *
xcalloc(size_t count, size_t size)
{
	void *ptr = calloc(count, size);
	if (!ptr) {
		fprintf(stderr, "ERROR(%s:%d) calloc failure: aborting\n", __FILE__, __LINE__);
		exit(1);
	}
	return ptr;
}

void *
xrealloc(void *ptr, size_t count, size_t size)
{
	ptr = realloc(ptr, count * size);
	if (!ptr) {
		fprintf(stderr, "ERROR(%s:%d) realloc failure: aborting\n", __FILE__, __LINE__);
		exit(1);
	}
	return ptr;
}

void
xfree(void *ptr)
{
	if (ptr) { free(ptr); ptr = NULL; }
}

u64
bitround(u64 n, int dir)
{
	n |= (n >> 1);
	n |= (n >> 2);
	n |= (n >> 4);
	n |= (n >> 8);
	n |= (n >> 16);
	n |= (n >> 32);

	return (dir < 0) ? n >> 1 : n;
}

void *
file_load(struct FileBuf *fbp, const char *path)
{
	struct stat stats;
	int fd = open(path, O_RDONLY, S_IRUSR);

	if (fstat(fd, &stats) >= 0) {
		char *buf = mmap(NULL, stats.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (buf && buf != MAP_FAILED) {
			fbp->data = buf;
			fbp->size = stats.st_size;
			fbp->fd   = fd;
			return buf;
		}
	}

	return NULL;
}

void
file_unload(struct FileBuf *fbp)
{
	if (munmap(fbp->data, fbp->size) == 0)
		fbp->data = NULL;
	close(fbp->fd);
}

void
fputn(FILE *stream, int n, int c)
{
	const int size = 1024;

	// somewhat optimized because fputc() runs very slowly in a loop
	if ((n = CLAMP(n, 0, size - 2)) > 0) {
		char buf[size];

		memset(buf, (c & 0x7f), sizeof(buf[0]) * n);
		buf[n] = '\0';

		fputs(buf, stream);
	}
}

char *
asciistr(int cp)
{
	return (BETWEEN(cp, 0, NUM_ASCII_STD)) ? ascii_db[cp].str : "???";
}

