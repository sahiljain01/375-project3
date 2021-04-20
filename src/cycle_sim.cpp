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
};

static Cache dCache;
static Cache iCache;

static MemoryStore *myMem;

static uint32_t reg[32];
static RegisterInfo regInfo;

static uint32_t PC = 0x00000000;
static uint32_t nPC = WORD_SIZE;

/* End of Global Variable Definitions */

int initSimulator(CacheConfig & icConfig, CacheConfig & dcConfig, MemoryStore *mainMem)
{
  
  if (icConfig.type == DIRECT_MAPPED)
  {
    uint32_t index_size = icConfig.cacheSize / icConfig.blockSize;
    uint32_t index_bits = log2(index_size);
    uint32_t block_offset = icConfig.blockSize / WORD_SIZE;
    uint32_t block_offset_bits = log2(block_offset);
    
    uint32_t tag_bits = 32 - index_bits - block_offset_bits - 2;
    
    CacheEntry iEntries = new CacheEntry[index_size];
    iCache = Cache(iEntries, 1, tag_bits, index_bits, block_offset_bits);
    
  }
  if (icConfig.type == DIRECT_MAPPED)
    {
      uint32_t index_size = icConfig.cacheSize / icConfig.blockSize;
      uint32_t index_bits = log2(index_size);
      uint32_t block_offset = icConfig.blockSize / WORD_SIZE;
      uint32_t block_offset_bits = log2(block_offset);

      uint32_t tag_bits =32 - index_bits- block_offset_bits - 2;

      CacheEntry iEntries = new CacheEntry[index_size];
      struct Cache iCache = Cache(iEntries, 1, tag_bits, index_bits,block_offset_bits);

    }


  return 0;

  

}
