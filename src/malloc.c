
#include <stddef.h>

extern void* delugeAllocFatFs(unsigned int requiredSize, bool mayUseOnChipRam);
extern void delugeDealloc(void* address);

void* malloc(size_t size) {
	return delugeAllocFatFs(size, true);
}

void free(void* addr) {
	delugeDealloc(addr);
}
