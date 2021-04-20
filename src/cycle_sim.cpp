#include <iostream>
#include <iomanip>
#include "MemoryStore.h"
#include "RegisterInfo.h"
#include "CacheConfig.h"
#include "DriverFunctions.h"
#include "EndianHelpers.h"


/* Global Variable Definitions */

struct CacheEntry {
  bool valid;
  uint32_t tag;
  uint32_t* data;
};

struct Cache {
  CacheEntry* entries;
  uint32_t ways;
  uint32_t tag_bits;
  uint32_t index_bits;
  uint32_t block_bits;
  bool isiCache;
  uint32_t missLatency;
};

static Cache dCache;
static Cache iCache;

static MemoryStore *myMem;

static uint32_t reg[32];
static RegisterInfo regInfo;

static uint32_t PC = 0x00000000;
static uint32_t nPC = WORD_SIZE;

static uint32_t icHits = 0;
static uint32_t icMisses = 0;
static uint32_t dcHits = 0;
static uint32_t dcMisses = 0;
static uint32_t totalCycles = 0;

/* End of Global Variable Definitions */

int initSimulator(CacheConfig & icConfig, CacheConfig & dcConfig, MemoryStore *mainMem)
{
  myMem = mainMem;
  int iWays = (icConfig.type == DIRECT_MAPPED) ? 1 : 2;
  int dWays = (dcConfig.type == DIRECT_MAPPED) ? 1 : 2;

  uint32_t index_size = icConfig.cacheSize / (icConfig.blockSize * iWays);
  uint32_t index_bits = log2(index_size);
  uint32_t block_offset = icConfig.blockSize / WORD_SIZE;
  uint32_t block_offset_bits = log2(block_offset);
  
  uint32_t tag_bits = 32 - index_bits - block_offset_bits - 2;
  
  CacheEntry iEntries = new CacheEntry[index_size];
  iCache = Cache(iEntries, iWays, tag_bits, index_bits, block_offset_bits, true, icConfig.missLatency);
    
  index_size = dcConfig.cacheSize / (dcConfig.blockSize * dWays);
  index_bits = log2(index_size);
  block_offset = dcConfig.blockSize / WORD_SIZE;
  block_offset_bits = log2(block_offset);
  tag_bits = 32 - index_bits- block_offset_bits - 2;

  CacheEntry dEntries = new CacheEntry[index_size];
  dCache = Cache(dEntries, dWays, tag_bits, index_bits,block_offset_bits, false, dcConfig.missLatency);

  return 0;  

}
static bool isCacheHit(Cache cache, uint32_t memAddress, uint32_t* data) 
{
  // first figure out the index
  3, 5

  uint32_t index = memAddress >> 2 >> cache.block_bits;
  uint32_t bitmask = 1 << cache.index_bits;
  bitmask = bitmask - 1;
  index = index && bitmask;

  tag = tag >> (cache.block_bits + cache.index_bits + 2);
  index = index * cache.ways;

  for (int i = 0; i < cache.ways; i++) {
    CacheEntry entry = cache.entries[index + i];
    if (entry.valid == true) {
      if (entry.tag == tag) {
        *data = entry.data;
        return true;
      }
    }
  } 

} 