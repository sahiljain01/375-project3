#ifndef CACHE_H
#define CACHE_H

enum {
   ICACHE = true,
   DCACHE = false,
   READ = true,
   WRITE = false
};


bool cacheAccess(bool isICache, uint32_t memAddress, uint32_t *data, bool isRead, uint32_t size);

void printStats();

#endif
