#include <iostream>
#include <iomanip>
#include "MemoryStore.h"
#include "RegisterInfo.h"
#include "CacheConfig.h"
#include "DriverFunctions.h"
#include "EndianHelpers.h"
#include <vector>
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

static uint32_t icHits = 0;
static uint32_t icMisses = 0;
static uint32_t dcHits = 0;
static uint32_t dcMisses = 0;
static uint32_t totalCycles = 0;

/* End of Global Variable Definitions */

int initSimulator(CacheConfig & icConfig, CacheConfig & dcConfig, MemoryStore *mainMem)
{
  myMem = mainMem;
  bool iIsDirect = (icConfig.type == DIRECT_MAPPED) ? true : false;
  bool dIsDirect = (dcConfig.type == DIRECT_MAPPED) ? true : false;
  int iWays = (icConfig.type == DIRECT_MAPPED) ? 1 : 2;
  int dWays = (dcConfig.type == DIRECT_MAPPED) ? 1 : 2;

  uint32_t index_size = icConfig.cacheSize / (icConfig.blockSize * iWays);
  uint32_t index_bits = log2(index_size);
  uint32_t block_offset = icConfig.blockSize / WORD_SIZE;
  uint32_t block_offset_bits = log2(block_offset);
  
  uint32_t tag_bits = 32 - index_bits - block_offset_bits - 2;
  
  CacheEntry iEntries = new CacheEntry[index_size];
  iCache = Cache(iEntries, tag_bits, index_bits, block_offset_bits, true, icConfig.missLatency, iIsDirect);
    
  index_size = dcConfig.cacheSize / (dcConfig.blockSize * dWays);
  index_bits = log2(index_size);
  block_offset = dcConfig.blockSize / WORD_SIZE;
  block_offset_bits = log2(block_offset);
  tag_bits = 32 - index_bits- block_offset_bits - 2;

  CacheEntry dEntries = new CacheEntry[index_size];
  dCache = Cache(dEntries, tag_bits, index_bits,block_offset_bits, false, dcConfig.missLatency, dIsDirect);

  // TODO: initialize iCache and dCache sizes, initialize individual cache entry data sizes 
  // TODO: test w driver

  return 0;  

}
static bool cacheRead(Cache cache, uint32_t memAddress, uint32_t* data) 
{
  // first figure out the index

  uint32_t index = memAddress >> 2 >> cache.block_bits;
  uint32_t bitmask = 1 << cache.index_bits;
  bitmask = bitmask - 1;
  index = index & bitmask;

  tag = tag >> (cache.block_bits + cache.index_bits + 2);
  index *= ((cache.isDirect) ? 1 : 2);

  block_offset = memAddress >> 2;
  bitmask = 1 << cache.block_bits;
  bitmask--;
  block_offset &= bitmask;

  if (cache.isDirect) {
    // handles read hit
    if (cache.entries[index].valid && cache.entries[index].tag == tag) {
      *data = cache.entries[index].data[block_offset];
      return true;
    } else {
      // if valid, evict existing block and write to memory
      if (cache.entries[index].valid) {
        // copy contents of cache entry to memory
        for (int i = 0; i < block_size; i++) {
          first_block_address = cache.entries[index].tag << cache.index_bits;
          first_block_address |= index;
          first_block_address <<= block_bits + 2;
          myMem->setMemValue(first_block_address + 4 * i, cache.entries[index].data[block_offset + i], WORD_SIZE);
        }
      } 

      // read from memAddress to cache
      for (int i = 0; i < block_size; i++) {
        first_block_address = tag << cache.index_bits;
        first_block_address |= index;
        first_block_address <<= block_bits + 2;
        myMem->getMemValue(first_block_address + 4 * i, cache.entries[index].data[block_offset + i], WORD_SIZE);
      }
      
      // read from cache into data
      cache.entries[index].isValid = true;
      cache.entries[index].tag = tag;
      *data = cache.entries[index].data[block_offset];
      return false; 
    }
  } else {
    // cache hit with first block in set
    if (cache.entries[index].valid && cache.entries[index].tag == tag) {
      *data = cache.entries[index].data[block_offset];
      cache.entries[index].isMRU = true;
      cache.entries[index + 1].isMRU = false;
      return true;
    // cache hit with second block in set
    } else if (cache.entries[index + 1].valid && cache.entries[index + 1].tag == tag) {
      *data = cache.entries[index + 1].data[block_offset];
      cache.entries[index].isMRU = false;
      cache.entries[index + 1].isMRU = true;
      return true;
      // cache miss
    } else {
      // find index of block in the set that is LRU to evict
      firstIsLRU = (cache.entries[index].isMRU) ? 0 : 1;

      // write every other entry in cache.entries to memory
      for (int i = 0; i < block_size; i += 2) {
        first_block_address = cache.entries[index + firstIsLRU].tag << cache.index_bits;
        first_block_address |= index + firstIsLRU;
        first_block_address <<= block_bits + 2;
        myMem->setMemValue(first_block_address + 4 * i, cache.entries[index + firstIsLRU].data[block_offset + i], WORD_SIZE);
      }

      // read from memAddress to cache
      for (int i = 0; i < block_size; i++) {
        first_block_address = tag << cache.index_bits;
        first_block_address |= index + firstIsLRU;
        first_block_address <<= block_bits + 2;
        myMem->getMemValue(first_block_address + 4 * i, cache.entries[index + firstIsLRU].data[block_offset + i], WORD_SIZE);
      }

      // read from cache into data
      cache.entries[index + firstIsLRU].isValid = true;
      cache.entries[index + firstIsLRU].tag = tag;
      *data = cache.entries[index + firstIsLRU].data[block_offset];

      // set MRU bit for both blocks in the set
      cache.entries[index + firstIsLRU].isMRU = true;
      cache.entries[index + !firstIsLRU].isMRU = false;

      return false;
    }
  }

} 

static bool cacheWrite(Cache cache, uint32_t memAddress, uint32_t* data) {
  
}
