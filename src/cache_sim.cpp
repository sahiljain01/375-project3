#include <iostream>
#include <iomanip>
#include "MemoryStore.h"
#include "RegisterInfo.h"
#include "CacheConfig.h"
#include "EndianHelpers.h"
#include <math.h>
#include <vector>
#include "cache_test.h"

using namespace std;

/* Global Variable Definitions */

struct CacheEntry {
  bool isValid;
  uint32_t tag;
  // uint32_t* data;
  vector<uint32_t> data; 
  void resize(size_t size) {
    data.resize(size);
  }

  bool isMRU;
};

struct Cache {
  // CacheEntry* entries;
  vector<CacheEntry> entries;
  void resize(size_t size) {
    entries.resize(size);
  }
  
  uint32_t tag_bits;
  uint32_t index_bits;
  uint32_t block_bits;
  bool isiCache;
  uint32_t missLatency;
  bool isDirect;
};

static Cache dCache;
static Cache iCache;

static MemoryStore *myMem;

static uint32_t reg[32];
static RegisterInfo regInfo;

static uint32_t PC = 0x00000000;
static uint32_t nPC = WORD_SIZE;

uint32_t icHits = 0;
uint32_t icMisses = 0;
uint32_t dcHits = 0;
uint32_t dcMisses = 0;
uint32_t totalCycles = 0;

/* End of Global Variable Definitions */

int initSimulator(CacheConfig & icConfig, CacheConfig & dcConfig, MemoryStore *mainMem)
{
  myMem = mainMem;
  bool iIsDirect = (icConfig.type == DIRECT_MAPPED) ? true : false;
  bool dIsDirect = (dcConfig.type == DIRECT_MAPPED) ? true : false;
  int iWays = (icConfig.type == DIRECT_MAPPED) ? 1 : 2;
  int dWays = (dcConfig.type == DIRECT_MAPPED) ? 1 : 2;
  int iBlockSize = icConfig.blockSize;
  int dBlockSize = dcConfig.blockSize;

  uint32_t index_size = icConfig.cacheSize / (icConfig.blockSize * iWays);
  uint32_t index_bits = log2(index_size);
  uint32_t block_words = iBlockSize / WORD_SIZE;
  uint32_t block_offset_bits = log2(block_words);
  
  uint32_t tag_bits = 16 - index_bits - block_offset_bits - 2;
  
  // initialize iCache
  iCache.tag_bits = tag_bits;
  iCache.index_bits = index_bits;
  iCache.block_bits = block_offset_bits;
  iCache.isiCache = true;
  iCache.missLatency = icConfig.missLatency;
  iCache.isDirect = iIsDirect; 
  iCache.resize(index_size);
  for (int i = 0; i < index_size; i++) {
    iCache.entries[i].resize(block_words);
  }

  index_size = dcConfig.cacheSize / (dBlockSize * dWays);
  index_bits = log2(index_size);
  block_words = dBlockSize / WORD_SIZE;
  block_offset_bits = log2(block_words);
  tag_bits = 16 - index_bits - block_offset_bits - 2;

  // initialize dCache
  dCache.tag_bits = tag_bits;
  dCache.index_bits = index_bits;
  dCache.block_bits = block_offset_bits;
  dCache.isiCache = false;
  dCache.missLatency = dcConfig.missLatency;
  dCache.isDirect = dIsDirect; 
  dCache.resize(index_size);
  for (int i = 0; i < index_size; i++) {
    dCache.entries[i].resize(block_words);
  }

  // TODO: initialize iCache and dCache sizes, initialize individual cache entry data sizes 
  // TODO: test w driver

  return 0;  

}

// TODO: static function for block eviction

static void evict_block(bool isICache, uint32_t index) {

  Cache* cache = isICache ? &iCache : &dCache;
  uint32_t block_size = 1 << cache->block_bits;

  for (int i = 0; i < block_size; i++) {
    uint32_t first_block_address = cache->entries[index].tag << cache->index_bits;
    first_block_address |= index;
    first_block_address <<= cache->block_bits + 2;
    myMem->setMemValue(first_block_address + 4 * i, cache->entries[index].data[i], WORD_SIZE);
  }  
}

// TODO: static function for reading block from memory into cache

static void read_from_mem(bool isICache, uint32_t index) {

  Cache* cache = isICache ? &iCache : &dCache;
  uint32_t block_size = 1 << cache->block_bits;

  for (int i = 0; i < block_size; i++) {
    uint32_t first_block_address = cache->entries[index].tag << cache->index_bits;
    first_block_address |= index;
    first_block_address <<= cache->block_bits + 2;
    myMem->getMemValue(first_block_address + 4 * i, cache->entries[index].data[i], WORD_SIZE);
  }
}

// prints statistics
void printStats() {
  cout << "iCache Hits: " << icHits << endl;
  cout << "iCache Misses: " << icMisses << endl;
  cout << "dCache Hits: " << dcHits << endl;
  cout << "dCache Misses: " << dcMisses << endl;
}

// handles cache reads and writes
bool cacheAccess(bool isICache, uint32_t memAddress, uint32_t *data, bool isRead) 
{
  Cache* cache = isICache ? &iCache : &dCache;

  // first figure out the index
  uint32_t index = memAddress >> 2 >> cache->block_bits;
  uint32_t bitmask = 1 << cache->index_bits;
  bitmask = bitmask - 1;
  index = index & bitmask;

  uint32_t tag = memAddress >> (cache->block_bits + cache->index_bits + 2);
  index *= ((cache->isDirect) ? 1 : 2);

  uint32_t block_offset = memAddress >> 2;
  bitmask = 1 << cache->block_bits;
  bitmask--;
  block_offset &= bitmask;


  if (cache->isDirect) {
    // handles read hit
    if (cache->entries[index].isValid && cache->entries[index].tag == tag) {
      if (isICache) icHits++;
      else dcHits++;      

      if (isRead) {
        *data = cache->entries[index].data[block_offset];
      } else {
        cache->entries[index].data[block_offset] = *data;
      }
      return true;
    } else {
      // if valid, evict existing block and write to memory
      if (isICache) icMisses++;
      else dcMisses++;
      if (cache->entries[index].isValid) {
        evict_block(isICache, index);
      } 

      // read from memAddress to cache
      read_from_mem(true, index);
      
      // read from cache into data
      cache->entries[index].isValid = true;
      cache->entries[index].tag = tag;
      if (isRead) {
        *data = cache->entries[index].data[block_offset];
      } else {
        cache->entries[index].data[block_offset] = *data;
      }
      return false; 
    }
  } else {
    // cache hit with first block in set
    if (cache->entries[index].isValid && cache->entries[index].tag == tag) {
      if (isICache) icHits++;
      else dcHits++;      
      if (isRead) {
        *data = cache->entries[index].data[block_offset];
      } else {
        cache->entries[index].data[block_offset] = *data;
      } 

      cache->entries[index].isMRU = true;
      cache->entries[index + 1].isMRU = false;
 
      return true;
    // cache hit with second block in set
    } else if (cache->entries[index + 1].isValid && cache->entries[index + 1].tag == tag) {
      if (isICache) icHits++;
      else dcHits++;      
      if (isRead) {
        *data = cache->entries[index].data[block_offset];
      } else {
        cache->entries[index].data[block_offset] = *data;
      } 

      cache->entries[index].isMRU = false;
      cache->entries[index + 1].isMRU = true;
      return true;
      // cache miss
    } else {
      if (isICache) icMisses++;
      else dcMisses++;
      // find index of block in the set that is LRU to evict
      int firstIsLRU = (cache->entries[index].isMRU) ? 0 : 1;

      // write every other entry in cache.entries to memory
      if (cache->entries[index + firstIsLRU].isValid) {
        evict_block(isICache, index + firstIsLRU);
      }
      // read from memAddress to cache
      read_from_mem(true, index + firstIsLRU);

      // read from cache into data
      cache->entries[index + firstIsLRU].isValid = true;
      cache->entries[index + firstIsLRU].tag = tag;
      *data = cache->entries[index + firstIsLRU].data[block_offset];

      // set MRU bit for both blocks in the set
      cache->entries[index + firstIsLRU].isMRU = true;
      cache->entries[index + !firstIsLRU].isMRU = false;

      return false;
    }
  }
} 

