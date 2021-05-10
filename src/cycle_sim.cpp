/*
 *  COS 375 Project 3
 *  cycle_sim.cpp
 *  GID: 175
 */

#include <iostream>
#include <iomanip>
#include "MemoryStore.h"
#include "RegisterInfo.h"
#include "EndianHelpers.h"
#include "DriverFunctions.h"
#include <math.h>
#include <vector>
#include <algorithm>

using namespace std;

/* GLOBAL VARIABLE DEFINITIONS */

// Helpful enums for cache accesses
enum {
   ICACHE = true,
   DCACHE = false,
   READ = true,
   WRITE = false
};

// Represents one cache entry
struct CacheEntry {
  bool isValid;
  uint32_t tag;
  vector<uint32_t> data;
  void resize(size_t size) {
    data.resize(size);
  }
  bool isMRU;
};

// Represent cache
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

// Caches
static Cache dCache;
static Cache iCache;

// Neccessary for correct cache writeback in the middle of a stall
static Cache mostRecentICache;
static Cache mostRecentDCache;

static MemoryStore *myMem;

static uint32_t reg[32];
static RegisterInfo regInfo;

static uint32_t PC = 0x00000000;
static uint32_t nPC = WORD_SIZE;
static uint32_t cyclesElapsed = 0;
static uint32_t PC_cpy = 0x00000000;

// Cache stats
uint32_t icHits = 0;
uint32_t icMisses = 0;
uint32_t dcHits = 0;
uint32_t dcMisses = 0;
uint32_t totalCycles = 0;
bool hit_exception = false;

// Pipeline registers
struct IFID {
  uint32_t nPC;
  uint32_t IR;
};

struct IDEX {
  uint32_t IR;
  uint32_t opcode;
  uint32_t func_code;
  uint32_t nPC;
  uint32_t RS;
  uint32_t RT;
  uint32_t RD;
  uint32_t immed;
  uint32_t A;
  uint32_t B;
  uint32_t seimmed;
  uint32_t shamt;
  bool memRead;
  bool regWrite;
  bool insertedNOP;
};

struct EXMEM {
  uint32_t IR;
  uint32_t BrTgt;
  uint32_t Zero;
  uint32_t ALUOut;
  uint32_t RD;
  uint32_t B;
  bool regWrite;
  bool memWrite;
  bool memRead;
};

struct MEMWB {
  uint32_t IR;
  uint32_t RD;
  uint32_t memData;
  uint32_t ALUOut;
  uint32_t regWrite;
};

// Useful global variable definitions
static bool receivedIR = false;
static bool feedfeed_hit = false;
static bool load_use_stall = false;
static bool load_use_stall_delay = false;
static uint32_t load_use_stalls = 0;

static IFID if_id;
static IDEX id_ex;
static EXMEM ex_mem;
static MEMWB mem_wb;
static IFID if_id_cpy;
static IDEX id_ex_cpy;
static EXMEM ex_mem_cpy;
static MEMWB mem_wb_cpy;

// Various helpers for forwarding/stalling/etc.
static uint32_t ex_fwd_A = 0;
static uint32_t ex_fwd_B = 0;
static uint32_t wb_instruction = 0;
static uint32_t if_instruction = 0;
static int iCache_stalls = 0;
static int dCache_stalls = 0;
static bool started = false;
static bool haltReached = false;
static PipeState mostRecentPS = PipeState();

/* END GLOBAL VARIABLE DEFINITIONS */

/* START OF CACHE SECTION */

// FROM API: Initializes caches, but don't begin exectution
int initSimulator(CacheConfig & icConfig, CacheConfig & dcConfig, MemoryStore *mainMem)
{
  myMem = mainMem;
  bool iIsDirect = (icConfig.type == DIRECT_MAPPED) ? true : false;
  bool dIsDirect = (dcConfig.type == DIRECT_MAPPED) ? true : false;
  int iWays = (icConfig.type == DIRECT_MAPPED) ? 1 : 2;
  int dWays = (dcConfig.type == DIRECT_MAPPED) ? 1 : 2;
  int iBlockSize = icConfig.blockSize;
  int dBlockSize = dcConfig.blockSize;

  // get initialization settings for iCache
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
  iCache.resize(index_size * iWays);
  for (int i = 0; i < index_size * iWays; i++) {
    iCache.entries[i].resize(block_words);
  }

  // get initialization settings for dCache
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
  dCache.resize(index_size * dWays);
  for (int i = 0; i < index_size * dWays; i++) {
    dCache.entries[i].resize(block_words);
  }

  return 0;
}

// Dump registers and memory
static void dump(MemoryStore* mem, uint32_t* myreg) {
   RegisterInfo regs;

   regs.at = myreg[1];
   copy(myreg+2, myreg+4, regs.v);
   copy(myreg+4, myreg+8, regs.a);
   copy(myreg+8, myreg+16, regs.t);
   copy(myreg+16, myreg+24, regs.s);
   regs.t[8] = myreg[24];
   regs.t[9] = myreg[25];
   copy(myreg+24, myreg+26, regs.k);
   regs.gp = myreg[28];
   regs.sp = myreg[29];
   regs.fp = myreg[30];
   regs.ra = myreg[31];

   dumpRegisterState(regs);
   dumpMemoryState(mem);
}

// FROM API: finalize execution
int finalizeSimulator() {
   // Print simulation stats to sim_stats.out file
   SimulationStats final_stats;
   if (!started) {
     final_stats.totalCycles = 0;
   }
   else {
     final_stats.totalCycles = cyclesElapsed+1;
   }
   final_stats.icHits = icHits;
   final_stats.icMisses = icMisses;
   final_stats.dcHits = dcHits;
   final_stats.dcMisses = dcMisses;
   printSimStats(final_stats);

   // Write back all dirty values in the iCache to memory
   uint32_t block_size = 1 << mostRecentICache.block_bits;
   for (int i = 0; i < mostRecentICache.entries.size(); i++) {
      if (iCache.entries[i].isValid) {
         uint32_t first_block_address = mostRecentICache.entries[i].tag << mostRecentICache.index_bits;
         if (mostRecentICache.isDirect)
            first_block_address |= i;
         else
            first_block_address |= (i >> 1);
         first_block_address <<= mostRecentICache.block_bits + 2;
         for (int j = 0; j < block_size; j++) {
            myMem->setMemValue(first_block_address + 4 * j, mostRecentICache.entries[i].data[j], WORD_SIZE);
         }
      }
   }

   // Write back all dirty values in the dCache to memory
   block_size = 1 << mostRecentDCache.block_bits;
   for (int i = 0; i < mostRecentDCache.entries.size(); i++) {
      if (mostRecentDCache.entries[i].isValid) {
         uint32_t first_block_address = mostRecentDCache.entries[i].tag << mostRecentDCache.index_bits;
         if (mostRecentDCache.isDirect)
            first_block_address |= i;
         else
            first_block_address |= (i >> 1);
         first_block_address <<= mostRecentDCache.block_bits + 2;
         for (int j = 0; j < block_size; j++) {
            myMem->setMemValue(first_block_address + 4 * j, mostRecentDCache.entries[i].data[j], WORD_SIZE);
         }
      }
   }

   // Dump memory
   dump(myMem, reg);
}

// Evicts the block at the index from the cache, writing it back (write-through)
static void evict_block(bool isICache, uint32_t index) {

  Cache* cache = isICache ? &iCache : &dCache;
  uint32_t block_size = 1 << cache->block_bits;

  uint32_t first_block_address = cache->entries[index].tag << cache->index_bits;
  if (cache->isDirect)
     first_block_address |= index;
  else
     first_block_address |= (index >> 1);
  first_block_address <<= cache->block_bits + 2;

  for (int i = 0; i < block_size; i++) {
    myMem->setMemValue(first_block_address + 4 * i, cache->entries[index].data[i], WORD_SIZE);
  }
}

// Reads in a block of memory into the cache
static void read_from_mem(bool isICache, uint32_t index, uint32_t size) {

  Cache* cache = isICache ? &iCache : &dCache;
  uint32_t block_size = 1 << cache->block_bits;
  uint32_t first_block_address = cache->entries[index].tag << cache->index_bits;
  if (cache->isDirect)
     first_block_address |= index;
  else
     first_block_address |= (index >> 1);
  first_block_address <<= cache->block_bits + 2;

  for (int i = 0; i < block_size; i++) {
     myMem->getMemValue(first_block_address + 4 * i, cache->entries[index].data[i], (MemEntrySize)size);
  }
}

// Handles all cache accesses
static bool cacheAccess(bool isICache, uint32_t memAddress, uint32_t *data, bool isRead, uint32_t size)
{
  Cache* cache = isICache ? &iCache : &dCache;

  // first figure out the index
  uint32_t index = memAddress >> 2 >> cache->block_bits;
  uint32_t bitmask = (1 << cache->index_bits) - 1;
  index = index & bitmask;

  uint32_t tag = memAddress >> (cache->block_bits + cache->index_bits + 2);
  index *= ((cache->isDirect) ? 1 : 2);

  uint32_t block_offset = memAddress >> 2;
  bitmask = (1 << cache->block_bits) - 1;
  block_offset &= bitmask;

  // Bit bashing to allow for half/byte reads/writes
  uint32_t byte_offset = memAddress & 0x3;
  uint32_t byte_mask = (size == WORD_SIZE) ? 0xffffffff : (0x1 << (8*size)) - 1;
  uint32_t byte_shift = 32 - 8 * (byte_offset + size);

  // Direct-mapped case
  if (cache->isDirect) {
    // handles read hit
    if (cache->entries[index].isValid && cache->entries[index].tag == tag) {
      if (isICache) icHits++;
      else dcHits++;

      if (isRead) {
         // Read data into the value pointer
         *data = ((cache->entries[index].data[block_offset] >> byte_shift) & byte_mask);
      } else {
         // Clear the data, then write over it
         cache->entries[index].data[block_offset] &= ~(byte_mask << byte_shift);
         cache->entries[index].data[block_offset] |= (*data << byte_shift);
      }
      return true;
    } else {
      if (isICache) icMisses++;
      else dcMisses++;
      
      // if valid, evict existing block and write to memory
      if (cache->entries[index].isValid) {
        evict_block(isICache, index);
      }

      // read from memAddress to cache
      cache->entries[index].tag = tag;
      read_from_mem(isICache, index, WORD_SIZE);

      // read from cache into data
      cache->entries[index].isValid = true;
      if (isRead) {
        *data = ((cache->entries[index].data[block_offset] >> byte_shift) & byte_mask);
      } else {
         cache->entries[index].data[block_offset] &= ~(byte_mask << byte_shift);
         cache->entries[index].data[block_offset] |= (*data << byte_shift);
      }
      return false;
    }
    // Two-way set associative case
  } else {
    // cache hit with first block in set
    if (cache->entries[index].isValid && cache->entries[index].tag == tag) {
      if (isICache) icHits++;
      else dcHits++;
      if (isRead) {
        *data = ((cache->entries[index].data[block_offset] >> byte_shift) & byte_mask);
      } else {
         cache->entries[index].data[block_offset] &= ~(byte_mask << byte_shift);
         cache->entries[index].data[block_offset] |= (*data << byte_shift);
      }

      cache->entries[index].isMRU = true;
      cache->entries[index + 1].isMRU = false;

      return true;
    // cache hit with second block in set
    } else if (cache->entries[index + 1].isValid && cache->entries[index + 1].tag == tag) {
      if (isICache) icHits++;
      else dcHits++;
      if (isRead) {
        *data = ((cache->entries[index + 1].data[block_offset] >> byte_shift) & byte_mask);
      } else {
         cache->entries[index + 1].data[block_offset] &= ~(byte_mask << byte_shift);
         cache->entries[index + 1].data[block_offset] |= (*data << byte_shift);
      }

      cache->entries[index].isMRU = false;
      cache->entries[index + 1].isMRU = true;
      return true;
      // cache miss
    } else {
      if (isICache) icMisses++;
      else dcMisses++;
      // find index of block in the set that is LRU to evict
      int LRU = (cache->entries[index].isMRU) ? 1 : 0;

      // write every other entry in cache.entries to memory
      if (cache->entries[index + LRU].isValid) {
        evict_block(isICache, index + LRU);
      }
      // read from memAddress to cache
      cache->entries[index + LRU].tag = tag;
      read_from_mem(isICache, index + LRU, WORD_SIZE);

      // read from cache into data
      cache->entries[index + LRU].isValid = true;
      if (isRead) {
         *data = ((cache->entries[index + LRU].data[block_offset] >> byte_shift) & byte_mask);
      } else {
         cache->entries[index + LRU].data[block_offset] &= ~(byte_mask << byte_shift);
         cache->entries[index + LRU].data[block_offset] |= (*data << byte_shift);
      }

      // set MRU bit for both blocks in the set
      cache->entries[index + LRU].isMRU = true;
      int MRU = (LRU == 1) ? 0 : 1;
      cache->entries[index + MRU].isMRU = false;

      return false;
    }
  }
}

/* END OF CACHE SECTION */

/* START OF PIPELINE SECTION */

// advance PC function
static void advance_pc(uint32_t offset)
{
  PC  += offset;
}

// Handles exceptions when they arise
static void handleException(bool isArithmetic) {
  hit_exception = true;

  // Squash the instruction going into EX stage
  id_ex.opcode = 0;
  id_ex.func_code = 0;
  id_ex.nPC = 0;
  id_ex.RS = 0;
  id_ex.RT = 0;
  id_ex.RD = 0;
  id_ex.immed = 0;
  id_ex.A = 0;
  id_ex.B = 0;
  id_ex.seimmed = 0;

  if (isArithmetic) {
     // Squash instruction going into MEM stage if arithmetic exception
    ex_mem.BrTgt = 0;
    ex_mem.Zero = 0;
    ex_mem.ALUOut = 0;
    ex_mem.RD = 0;
    ex_mem.B = 0;
    ex_mem.regWrite = 0;
    ex_mem.memWrite = 0;
    ex_mem.memRead = 0;
    ex_mem.IR = 0;
  }
  PC = 0x8000;
  nPC = PC + WORD_SIZE;
}

// Determines if an instruction is writing to a register
static bool isRegWrite(uint32_t opcode, uint32_t func_code) {
  switch (opcode) {
    case 0x0:
      if (func_code == 0x8) {
        return false;
      }
      break;
    case 0x2:
      return false;
      break;
    case 0x4:
      return false;
      break;
    case 0x5:
      return false;
      break;
    case 0x6:
      return false;
      break;
    case 0x7:
      return false;
      break;
    case 0x28:
      return false;
      break;
    case 0x29:
      return false;
      break;
    case 0x2b:
      return false;
      break;
    default:
      return true;
      break;
  }
  return true;
}

// Determines if an instruction is reading from memory
static bool isMemRead(uint32_t opcode){
  switch (opcode){
    case 0x23:
      return true;
      break;
    case 0x24:
      return true;
      break;
    case 0x25:
      return true;
      break;
    default:
      return false;
      break;
  }
  return false;
}

// Deterimines if an instruction is writing to memory
static bool isMemWrite(uint32_t opcode){
  switch (opcode){
    case 0x28:
      return true;
      break;
    case 0x29:
      return true;
      break;
    case 0x2b:
      return true;
      break;
    default:
      return false;
      break;
  }
  return false;
}

// Determines if an instruction is valid or illegal
static bool isValidInstruction(uint32_t opcode, uint32_t func_code) {
  switch (opcode){
    // r-type instructions
    case 0:
    {
      switch (func_code)
      {
          // add
          case 0x20:
            return true;
            break;
          // addu
          case 0x21:
            return true;
            break;
          // and
          case 0x24:
            return true;
            break;
          // jr
          case 0x08:
            return true;
            break;
          // nor
          case 0x27:
            return true;
            break;
          // or
          case 0x25:
            return true;
            break;
          // slt (signed)
          case 0x2a:
            return true;
            break;
          // sltu
          case 0x2b:
            return true;
            break;
          // sll
          case 0x00:
            return true;
            break;
          // srl
          case 0x02:
            return true;
            break;
          // sub (signed)
          case 0x22:
            return true;
            break;
          // subu
          case 0x23:
            return true;
            break;
      }
    break;
    }
  // jump address
    case 2:
    {
        return true;
        break;
    }
    // jump and link
    case 3:
    {
        return true;
        break;
    }
    // rest are I-Types
    case 0x8:
    {
        return true;
        break;
    }
    case 0x9:
    {
        return true;
        break;
    }
    case 0xc:
    {
        return true;
        break;
    }
    case 0x4:
    {
        return true;
        break;
    }
    case 0x5:
    {
        return true;
        break;
    }
    case 0x24:
    {
        return true;
        break;
    }
    case 0x25:
    {
        return true;
        break;
    }
    case 0xf:
    {
        return true;
        // load upper immediate
        break;
    }
    case 0x23:
    {
        return true;
        break;
    }
    case 0xd:
    {
        return true;
        break;
    }
    case 0xa:
    {
        return true;
        break;
    }
    case 0xb:
    {
        return true;
      break;
    }
    case 0x28:
    {
        return true;
      // store byte
      break;
    }
    case 0x29:
    {
        return true;
      // store halfword
      break;
    }
    case 0x2b:
    {
      return true;
      // store word
      break;
    }
    case 0x6:
    {
        return true;
      break;
    }
    case 0x7:
    {
        return true;
      break;
    }
  }
    return false;

}

// HANDLE THE IF SECTION
static void ifSection() {
    uint32_t instruction = 0;

    // Now we are no longer fetching instructions until the load-use stall is over
    if (load_use_stall_delay) {
      load_use_stalls = load_use_stalls - 1;
      if (load_use_stalls == 0) {
        load_use_stall_delay = false;
        if_id.IR = if_instruction;
        PC_cpy = PC;
      }
      return;
    }

    // We just hit a load use stall later in the pipeline, so we still need to fetch an insruction
    if (load_use_stall) {
      load_use_stall_delay = true;
      load_use_stall = false;

      bool hit = cacheAccess(ICACHE, PC_cpy, &instruction, READ, WORD_SIZE);
      if (!hit) {
        iCache_stalls = (iCache_stalls <= iCache.missLatency) ? iCache.missLatency: iCache_stalls;
      }
      if_instruction = instruction;
      return;
    }

    if_id.IR = 0;

    // If we haven't hit 0xfeedfeed, then fetch an insruction
    if (!feedfeed_hit) {
      bool hit = cacheAccess(ICACHE, PC_cpy, &instruction, READ, WORD_SIZE);
      if (!hit) {
        iCache_stalls = (iCache_stalls <= iCache.missLatency) ? iCache.missLatency: iCache_stalls;
      }
      if_instruction = instruction;
      PC_cpy = PC;
      if_id.nPC = PC + 4;
      if_id.IR = instruction;
    }
    else {
      if_instruction = 0;
      wb_instruction = 0;
    }

    if (instruction == 0xfeedfeed) {
      if (!hit_exception){
        feedfeed_hit = true;
      }
    }
}

// HANDLE THE ID SECTION
static void idSection() {
    // If we are stalling, do nothing
    if (load_use_stalls > 1) {
      return;
    }

    // Decode the instruction from IF
    uint32_t instruction = if_id_cpy.IR;
    load_use_stall = false;
    id_ex.opcode = instruction >> 26;
    id_ex.RS = instruction << 6 >> 27;
    id_ex.RT = instruction << 11 >> 27;
    id_ex.RD = instruction << 16 >> 27;
    id_ex.func_code = instruction & (63);
    id_ex.immed = instruction << 16 >> 16;
    id_ex.A = reg[id_ex.RS];
    id_ex.B = reg[id_ex.RT];
    id_ex.shamt = instruction << 21 >> 27;
    id_ex.seimmed = (id_ex.immed >> 15 == 0) ? (uint32_t)id_ex.immed : ((uint32_t)id_ex.immed | 0xffff0000);
    id_ex.IR = instruction;
    id_ex.insertedNOP = false;
    bool memRead = isMemRead(id_ex.opcode);

    // Handle illegal instruction exception
    if ((!isValidInstruction(id_ex.opcode, id_ex.func_code)) && (instruction != 0xfeedfeed)) {
      handleException(false);
    }

    // Determines if instruction is a branch, used for branch handling/forwarding
    bool isBranch = false;
    if (((id_ex.opcode >= 0x2) && (id_ex.opcode <= 0x7)) || ((id_ex.opcode == 0) && (id_ex.func_code == 0x8))) {
      isBranch = true;
    }

    // Handles load-use stalls
    if ((!isBranch) && ex_mem.memRead && (ex_mem.RD != 0) &&((ex_mem.RD == id_ex.RS) || (ex_mem.RD == id_ex.RT))) {
      instruction = 0;
      id_ex = IDEX();
      id_ex.insertedNOP = true;
      id_ex.regWrite = false;

      load_use_stall = true;
      load_use_stalls = 1;
      return;
    }

    if (!isBranch) {
      advance_pc(4);
    }

    // ID forwarding for branches
    bool clear_flag = false;
    if (isBranch) {

      // MEM Forwarding to ID (| --- | branch | --- | --- | load |)
      if ((mem_wb_cpy.regWrite && (mem_wb_cpy.RD != 0)) && (mem_wb_cpy.RD == id_ex.RS)) {
        id_ex.A = mem_wb_cpy.ALUOut;
      }
      if ((mem_wb_cpy.regWrite && (mem_wb_cpy.RD != 0)) && (mem_wb_cpy.RD == id_ex.RT)) {
        id_ex.B = mem_wb_cpy.ALUOut;
      }

      // EX Forwarding to ID (| --- | branch | --- | ALU | --- |)
      if ((ex_mem_cpy.regWrite && (ex_mem_cpy.RD != 0)) && (ex_mem_cpy.RD == id_ex.RS)) {
        id_ex.A = ex_mem_cpy.ALUOut;
      }
      if ((ex_mem_cpy.regWrite && (ex_mem_cpy.RD != 0)) && (ex_mem_cpy.RD == id_ex.RT)) {
        id_ex.B = ex_mem_cpy.ALUOut;
      }

      // MEM Stall by 1 cycle (| --- | branch | --- | load | --- |)
      if ((ex_mem_cpy.memRead && (ex_mem_cpy.RD != 0)) && (ex_mem_cpy.RD == id_ex.RS)) {
        instruction = 0;
        clear_flag = true;
        load_use_stall = true;
        load_use_stalls = 1;
      }
      if ((ex_mem_cpy.memRead && (ex_mem_cpy.RD != 0)) && (ex_mem_cpy.RD == id_ex.RT)) {
        instruction = 0;
        clear_flag = true;
        load_use_stall = true;
        load_use_stalls = 1;
      }

      // EX Stall by 1 cycle (| --- | branch | ALU | --- | --- |)
      if (ex_mem.regWrite && ((ex_mem.RD != 0) && (ex_mem.RD == id_ex.RS))) {
        instruction = 0;
        clear_flag = true;
        load_use_stall = true;
        load_use_stalls = 1;
      }
      if (ex_mem.regWrite && ((ex_mem.RD != 0) && (ex_mem.RD == id_ex.RT))) {
        instruction = 0;
        clear_flag = true;
        load_use_stall = true;
        load_use_stalls = 1;
      }

      // MEM Stall by 2 cycles (| --- | branch | load | --- | --- |)
      if (ex_mem.memRead && ((ex_mem.RD != 0) && (ex_mem.RD == id_ex.RS))) {
        instruction = 0;
        clear_flag = true;
        load_use_stall = true;
        load_use_stalls = 2;
      }
      if (ex_mem.memRead && ((ex_mem.RD != 0) && (ex_mem.RD == id_ex.RT))) {
        instruction = 0;
        clear_flag = true;
        load_use_stall = true;
        load_use_stalls = 2;
      }
    }
    if (clear_flag) {
       id_ex = IDEX();
    }

    uint32_t mostSig_ex = id_ex.immed >> 15; // most significant bit in immediate
    uint32_t imm_ex = 0;
    uint32_t target = (instruction << 6) >> 6;

    // Branch handling
    switch (id_ex.opcode) {
      // beq
      case (0x4):
        if (id_ex.A == id_ex.B) {
            advance_pc(id_ex.seimmed << 2);
        }
        else {
          advance_pc(4);
        }
        break;
      // bne
      case (0x5):
        if (id_ex.A != id_ex.B) {
            advance_pc(id_ex.seimmed << 2);
        }
        else {
          advance_pc(4);
        }
        break;
      // j
      case (0x2):
        PC = (PC & 0xf0000000) | (target << 2);
        break;
      // jal
      case (0x3):
        id_ex.RD = 31;
        id_ex.A = PC + 4;
        PC = (PC & 0xf0000000) | (target << 2);
        break;
      // blez
      case (0x6):
         if (static_cast<int32_t>(id_ex.A) <= 0) {
            advance_pc(id_ex.seimmed << 2);
        }
        else {
          advance_pc(4);
        }
        break;
      // bgtz
      case (0x7):
         if (static_cast<int32_t>(id_ex.A) > 0) {
            advance_pc(id_ex.seimmed << 2);
        }
        else {
          advance_pc(4);
        }
        break;
      // jr
      case (0x0):
        if (id_ex.func_code == 0x8) {
          PC = id_ex.A;
        }
        break;
      default:
        break;
    }

    // Squash ID stage if illegal instruction exception occurs
    if (hit_exception) {
       id_ex = IDEX();
    }
    id_ex.nPC = if_id_cpy.nPC + 4;
}

// HANDLE THE EX SECTION
static void exSection() {
    // Get the data from ID
    int opCode = id_ex_cpy.opcode;
    uint32_t rs = id_ex_cpy.RS; // operand
    uint32_t rt = id_ex_cpy.RT; // destination operand for imm instructions
    uint32_t rd = id_ex_cpy.RD; // destination operand
    uint32_t imm = id_ex_cpy.immed; // immediate address
    uint32_t mostSig = imm >> 15; // most significant bit in immediate
    uint32_t func_code = id_ex_cpy.func_code;
    uint32_t A = id_ex_cpy.A;
    uint32_t B = id_ex_cpy.B;
    uint32_t shamt = id_ex_cpy.shamt;

    // Intialize some EXMEM register variables
    ex_mem.B = id_ex_cpy.B;
    ex_mem.IR = id_ex_cpy.IR;

    // More instruction specific details/fixing
    bool regWrite = isRegWrite(opCode, func_code);
    if (id_ex_cpy.IR == 0xfeedfeed){
      regWrite = false;
    }
    bool memWrite = isMemWrite(opCode);
    bool memRead = isMemRead(opCode);

    if (opCode == 0) {
      ex_mem.RD = rd;
    }
    else if (opCode == 0x3) {
      ex_mem.RD = rd;
      ex_mem.ALUOut = id_ex_cpy.A;
    }
    else {
      ex_mem.RD = rt;
    }

    // Forwarding to EX
    if (ex_fwd_A == 1) {
      A = mem_wb_cpy.ALUOut;
    }
    if (ex_fwd_A == 2) {
      A = ex_mem_cpy.ALUOut;
    }
    if (ex_fwd_B == 1) {
      B = mem_wb_cpy.ALUOut;
    }
    if (ex_fwd_B == 2) {
     B = ex_mem_cpy.ALUOut;
    }

    switch (opCode) {
      // R-type instructions
      case 0:
      {
        // nested switch statements based on function code
        // for r-type instructions.
        switch (func_code)
        {
          // add
          case 0x20:
          {
            // Need to handle overflow exceptions
            uint32_t sigbit_rs = A >> 31;
            uint32_t sigbit_rt = B >> 31;
            uint32_t res = (uint32_t)((int)A + (int)B);
            uint32_t sigbit_rd = res >> 31;
            if (sigbit_rs == sigbit_rt) {
                if (sigbit_rd != sigbit_rs) {
                    handleException(true);
                    return;
                }
            }
            ex_mem.ALUOut = res;
            break;
           }
          // addu
          case 0x21:
            ex_mem.ALUOut = A + B;
            break;
          // and
          case 0x24:
            ex_mem.ALUOut = A & B;
            break;
          // nor
          case 0x27:
            ex_mem.ALUOut = ~(A | B);
            break;
          // or
          case 0x25:
            ex_mem.ALUOut = A | B;
            break;
          // slt (signed)
          case 0x2a:
            ex_mem.ALUOut = ((int)A < (int)B) ? 1 : 0;
            break;
          // sltu
          case 0x2b:
            ex_mem.ALUOut = (A < B) ? 1 : 0;
            break;
          // sll
          case 0x00:
            ex_mem.ALUOut = (B << shamt);
            break;
          // srl
          case 0x02:
            ex_mem.ALUOut = (B >> shamt);
            break;
          // sub (signed)
          case 0x22:
          {
            // Need to handle overflow exceptions
            uint32_t sigbit_rs = A >> 31;
            uint32_t sigbit_rt = B >> 31;
            uint32_t res = (uint32_t)((int)A - (int)B);
            uint32_t sigbit_rd = res >> 31;
            if ((sigbit_rs == 0) && (sigbit_rt == 1)) {
                if (sigbit_rd == 1) {
                    handleException(true);
                    return;
                }
            }
            else if ((sigbit_rs == 1) && (sigbit_rt == 0)) {
                if (sigbit_rd == 0) {
                    handleException(true);
                    return;
                }
            }
            ex_mem.ALUOut = res;
            break;
          }
          // subu
          case 0x23:
            ex_mem.ALUOut = A - B;
            break;
        }
    break;
    }
    // I-type instructions
    case 0x8:
    {
      // add-immediate
      // sign extend in c++, get the most significant bit
      if (mostSig == 1) {
          imm = imm | 0xffff0000;
      }
      // More exception handling
      uint32_t sigbit_rs = A >> 31;
      uint32_t sigbit_imm = imm >> 31;
      uint32_t res = (uint32_t)((int)A + (int)imm);
      uint32_t sigbit_rd = res >> 31;
      if (sigbit_rs == sigbit_imm) {
          if (sigbit_rd != sigbit_rs) {
              handleException(true);
              return;
          }
      }
      ex_mem.ALUOut = res;
      break;
    }
    case 0x9:
    {
      // add-immediate unsigned
      // sign extend in c++, get the most significant bit
      if (mostSig == 1) {
          imm = imm | 0xffff0000;
      }
      ex_mem.ALUOut = A + imm;
      break;
    }
    case 0xc:
    {
      // and immediate
      ex_mem.ALUOut = A & imm;
      break;
    }
    case 0xd:
    {
      // or immediate
      ex_mem.ALUOut = A | imm;
      break;
    }
    case 0xa:
    {
      // set less than immediate
      if (mostSig == 1) {
          imm = imm | 0xffff0000;
      }
      ex_mem.ALUOut = ((int)A < (int)imm) ? 1 : 0;
      break;
    }
    case 0xb:
    {
      // set less than immediate unsigned
      if (mostSig == 1) {
         imm = imm | 0xffff0000;
      }
      ex_mem.ALUOut = (A < imm) ? 1 : 0;
      break;
    }
    case 0x24:
    {
      // load byte unsigned
      if (mostSig == 1) {
          imm = imm | 0xfffc0000;
      }
      uint32_t res = imm + A;
      ex_mem.ALUOut = res;
      break;
    }
    case 0x25:
    {
      // load half word unsigned
      if (mostSig == 1) {
          imm = imm | 0xfffc0000;
      }
      uint32_t res = imm + A;
      ex_mem.ALUOut = res;
      break;
    }
    case 0xf:
    {
      // load upper immediate
      ex_mem.ALUOut = (imm << 16);
      break;
    }
    case 0x23:
    {
      // load word
      uint32_t res = 0;
      if (mostSig == 1) {
          imm = imm | 0xffff0000;
      }
      res = imm + A;
      ex_mem.ALUOut = res;
      break;
    }
    case 0x28:
    {
      // store byte
      if (mostSig == 1) {
          imm = imm | 0xffff0000;
      }
      uint32_t location = 0;
      location = A + imm;
      ex_mem.ALUOut = location;
      ex_mem.B = B & (0x000000ff);
      break;
    }
    case 0x29:
    {
      // store halfword
      if (mostSig == 1) {
          imm = imm | 0xffff0000;
      }
      uint32_t location = 0;
      location = A + imm;
      ex_mem.B = B & (0x0000ffff);
      ex_mem.ALUOut = location;
      break;
    }
    case 0x2b:
    {
      // store word
      if (mostSig == 1) {
          imm = imm | 0xffff0000;
      }
      uint32_t location = 0;
      location = A + imm;
      ex_mem.B = B;
      ex_mem.ALUOut = location;
      break;
    }
  }
  // Update pipeline registers
  ex_mem.memRead = memRead;
  ex_mem.memWrite = memWrite;
  ex_mem.regWrite = regWrite;
}

// START OF MEM SECTION
static void memSection() {
  // Getting data about instruction
  mem_wb.RD = ex_mem_cpy.RD;
  mem_wb.ALUOut = ex_mem_cpy.ALUOut;
  mem_wb.regWrite = ex_mem_cpy.regWrite;
  mem_wb.IR = ex_mem_cpy.IR;

  uint32_t storeData = 0;
  uint32_t opcode = 0;
  uint32_t func_code = 0;
  bool memRead_mem = ex_mem_cpy.memRead;
  bool memWrite_mem = ex_mem_cpy.memWrite;
  bool regWrite = ex_mem_cpy.regWrite;

  // Get the size to write to
  uint32_t size = WORD_SIZE;
  if (memRead_mem || memWrite_mem) {
    opcode = mem_wb.IR >> 26;
    func_code = mem_wb.IR  & (63);
    switch (opcode) {
      case 0x28:
	{
	  size = BYTE_SIZE;
	  // store byte
	  break;
	}
      case 0x29:
	{
	  size = HALF_SIZE;
	  // store halfword
	  break;
	}
      case 0x2b:
	{
	  size = WORD_SIZE;
	  // store word
	  break;
	}

      case 0x24:
	{
	  // load byte unsigned
	  size = BYTE_SIZE;
	  break;
	}
      case 0x25:
	{
	  size = HALF_SIZE;
	  break;
	}
      case 0x23:
        {
          size = WORD_SIZE;
          // load word
          break;
        }
      default:
         break;

    }

  }

  // Read from memory
  if (memRead_mem) {
    bool hit = cacheAccess(DCACHE, ex_mem_cpy.ALUOut, &storeData, READ, size);
    if (!hit) {
      dCache_stalls = (dCache_stalls <= dCache.missLatency) ? dCache.missLatency: dCache_stalls;
    }
    mem_wb.ALUOut = storeData;
  }
  // Write to memory
  if (memWrite_mem) {
    bool hit = cacheAccess(DCACHE, ex_mem_cpy.ALUOut, &ex_mem_cpy.B, WRITE, size);
    if (!hit) {
      dCache_stalls = (dCache_stalls <= dCache.missLatency) ? dCache.missLatency: dCache_stalls;
    }
  }
}

// START OF WB SECTION (returns if halt has reached wb)
static bool wbSection() {
  // hardwire zero to ground
  reg[0] = 0;
  wb_instruction = mem_wb_cpy.IR;

  // If feedfeed is in wb, send halt through
  if (mem_wb_cpy.IR == 0xfeedfeed) {
    return true;
  }

  // Writes to register file
  if (isRegWrite(wb_instruction >> 26, wb_instruction & (63))) {
    reg[mem_wb_cpy.RD] = mem_wb_cpy.ALUOut;
  }

  // No 0xfeedfeed reached
  return false;
}

// Helper function that runs only one cycle
static bool runOneCycle() {
    // Corner case for calling runCycles(0);
    started = true;

    // If we are in a cache stall, update
    if ((iCache_stalls > 0) || (dCache_stalls > 0)) {
      iCache_stalls--;
      dCache_stalls--;
      if (iCache_stalls < 0) {
        iCache_stalls = 0;
      }
      if (dCache_stalls < 0) {
        dCache_stalls = 0;
      }

      return false;
    }
    // Once out of cache stall, update cache with new values
    if (iCache_stalls <= 0) {
       mostRecentICache = iCache;
    }
    if (dCache_stalls <= 0) {
       mostRecentDCache = dCache;
    }

    // Forwarding Section
    ex_fwd_A = 0;
    ex_fwd_B = 0;
    // EX Hazard (forward to EX)
    if (ex_mem.regWrite && ((ex_mem.RD != 0) && (ex_mem.RD == id_ex.RS))) {
      ex_fwd_A = 2;
    }
    if (ex_mem.regWrite && ((ex_mem.RD != 0) && (ex_mem.RD == id_ex.RT))) {
      ex_fwd_B = 2;
    }
    // MEM Hazard (forward to EX)
    if ((mem_wb.regWrite && (mem_wb.RD != 0)) && !(ex_mem.regWrite && ((ex_mem.RD != 0) && (ex_mem.RD == id_ex.RS))) && (mem_wb.RD == id_ex.RS)) {
      ex_fwd_A = 1;
    }
    if ((mem_wb.regWrite && (mem_wb.RD != 0)) && !(ex_mem.regWrite && ((ex_mem.RD != 0) && (ex_mem.RD == id_ex.RT))) && (mem_wb.RD == id_ex.RT)) {
      ex_fwd_B = 1;
    }

    // If we've hit an exception, squash the ID stage
    if (hit_exception) {
       if_id_cpy.IR = 0;
       if_id_cpy.nPC = 0;
    }

    // Run all sections backwards
    bool halt = wbSection();
    memSection();
    exSection();
    idSection();
    ifSection();

    return halt;
}


// FROM API: run the specified number of cycles
int runCycles(uint32_t cycles) {
  bool halt;
  uint32_t endCycle = cyclesElapsed + cycles;
  // If we're not running any cycles, just dump the most recent pipe state
  if ((cycles == 0) || haltReached) {
     dumpPipeState(mostRecentPS);
     return (haltReached) ? 1 : 0;
  }

  // Run the correct number of cycles
  while (cyclesElapsed < endCycle) {
     // Run one cycle
     halt = runOneCycle();

     // If we need to dump pipe state, do so
     if (halt || (cyclesElapsed ==  (endCycle - 1))) {
         mostRecentPS.cycle = cyclesElapsed;
         // If iCache stalled, need to pass 0xdeefdeef (UNKNOWN) into pipe state
         if (iCache_stalls > 0) {
           mostRecentPS.ifInstr = 0xdeefdeef;
         }
         else {
           mostRecentPS.ifInstr = if_instruction;
         }
         mostRecentPS.idInstr = if_id_cpy.IR;
         mostRecentPS.exInstr = id_ex_cpy.IR;
         mostRecentPS.memInstr = ex_mem_cpy.IR;
         mostRecentPS.wbInstr = mem_wb_cpy.IR;

         dumpPipeState(mostRecentPS);
         if (halt) {
            haltReached = true;
            break;
         }
     }

     // Update the copies if we aren't in a cache stall
     if ((iCache_stalls <= 0) && (dCache_stalls <= 0)) {
        if_id_cpy.nPC = if_id.nPC;
        if_id_cpy.IR = if_id.IR;

        id_ex_cpy.IR = id_ex.IR;
        id_ex_cpy.opcode = id_ex.opcode;
        id_ex_cpy.func_code = id_ex.func_code;
        id_ex_cpy.nPC = id_ex.nPC;
        id_ex_cpy.RS = id_ex.RS;
        id_ex_cpy.RT = id_ex.RT;
        id_ex_cpy.RD = id_ex.RD;
        id_ex_cpy.immed = id_ex.immed;
        id_ex_cpy.A = id_ex.A;
        id_ex_cpy.B = id_ex.B;
        id_ex_cpy.seimmed = id_ex.seimmed;
        id_ex_cpy.shamt = id_ex.shamt;
        id_ex_cpy.memRead = id_ex.memRead;
        id_ex_cpy.regWrite = id_ex.regWrite;
        id_ex_cpy.insertedNOP = id_ex.insertedNOP;

        ex_mem_cpy.IR = ex_mem.IR;
        ex_mem_cpy.BrTgt = ex_mem.BrTgt;
        ex_mem_cpy.Zero = ex_mem.Zero;
        ex_mem_cpy.ALUOut = ex_mem.ALUOut;
        ex_mem_cpy.RD = ex_mem.RD;
        ex_mem_cpy.B = ex_mem.B;
        ex_mem_cpy.regWrite = ex_mem.regWrite;
        ex_mem_cpy.memWrite = ex_mem.memWrite;
        ex_mem_cpy.memRead = ex_mem.memRead;

        mem_wb_cpy.IR = mem_wb.IR;
        mem_wb_cpy.RD = mem_wb.RD;
        mem_wb_cpy.memData = mem_wb.memData;
        mem_wb_cpy.ALUOut = mem_wb.ALUOut;
        mem_wb_cpy.regWrite = mem_wb.regWrite;
     }

    cyclesElapsed = cyclesElapsed + 1;
  }

  // Return if we reached a halt while running the specified number of cycles
  return (halt) ? 1 : 0;

}

// FROM API: run until halt is reached
int runTillHalt() {
   bool halt = false;
   // run until we hit a halt
   while (true) {
      halt = runOneCycle();

      // If halt reached, break out
      if (halt)
         break;

      // Update the copies if we aren't in a cache stall
      if ((iCache_stalls <= 0) && (dCache_stalls <= 0)) {
         if_id_cpy.nPC = if_id.nPC;
         if_id_cpy.IR = if_id.IR;

         id_ex_cpy.IR = id_ex.IR;
         id_ex_cpy.opcode = id_ex.opcode;
         id_ex_cpy.func_code = id_ex.func_code;
         id_ex_cpy.nPC = id_ex.nPC;
         id_ex_cpy.RS = id_ex.RS;
         id_ex_cpy.RT = id_ex.RT;
         id_ex_cpy.RD = id_ex.RD;
         id_ex_cpy.immed = id_ex.immed;
         id_ex_cpy.A = id_ex.A;
         id_ex_cpy.B = id_ex.B;
         id_ex_cpy.seimmed = id_ex.seimmed;
         id_ex_cpy.shamt = id_ex.shamt;
         id_ex_cpy.memRead = id_ex.memRead;
         id_ex_cpy.regWrite = id_ex.regWrite;
         id_ex_cpy.insertedNOP = id_ex.insertedNOP;

         ex_mem_cpy.IR = ex_mem.IR;
         ex_mem_cpy.BrTgt = ex_mem.BrTgt;
         ex_mem_cpy.Zero = ex_mem.Zero;
         ex_mem_cpy.ALUOut = ex_mem.ALUOut;
         ex_mem_cpy.RD = ex_mem.RD;
         ex_mem_cpy.B = ex_mem.B;
         ex_mem_cpy.regWrite = ex_mem.regWrite;
         ex_mem_cpy.memWrite = ex_mem.memWrite;
         ex_mem_cpy.memRead = ex_mem.memRead;

         mem_wb_cpy.IR = mem_wb.IR;
         mem_wb_cpy.RD = mem_wb.RD;
         mem_wb_cpy.memData = mem_wb.memData;
         mem_wb_cpy.ALUOut = mem_wb.ALUOut;
         mem_wb_cpy.regWrite = mem_wb.regWrite;
      }

    cyclesElapsed = cyclesElapsed + 1;
   }

   // Dump the pipe state after we've reached the halt (should be | nop | nop | nop | nop | HALT |)
   mostRecentPS.cycle = cyclesElapsed;
   mostRecentPS.ifInstr = if_instruction;
   mostRecentPS.idInstr = if_id_cpy.IR;
   mostRecentPS.exInstr = id_ex_cpy.IR;
   mostRecentPS.memInstr = ex_mem_cpy.IR;
   mostRecentPS.wbInstr = mem_wb_cpy.IR;

   dumpPipeState(mostRecentPS);
   haltReached = true;
   return 0;
}
