#ifndef CACHE_H
#define CACHE_H



bool cacheAccess(bool isICache, uint32_t memAddress, uint32_t *data, bool isRead);

void printStats();

#endif
