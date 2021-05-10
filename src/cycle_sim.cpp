#include <iostream>
#include <iomanip>
#include "MemoryStore.h"
#include "RegisterInfo.h"
#include "EndianHelpers.h"
#include "DriverFunctions.h"
#include <math.h>
#include <vector>
#include <algorithm>
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

static Cache mostRecentICache;
static Cache mostRecentDCache;

static MemoryStore *myMem;

static uint32_t reg[32];
static RegisterInfo regInfo;

static uint32_t PC = 0x00000000;
static uint32_t nPC = WORD_SIZE;
static uint32_t cyclesElapsed = 0;
static uint32_t PC_cpy = 0x00000000;

uint32_t icHits = 0;
uint32_t icMisses = 0;
uint32_t dcHits = 0;
uint32_t dcMisses = 0;
uint32_t totalCycles = 0;
bool hit_exception = false;

/* End of Global Variable Definitions */

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

bool receivedIR = false;
bool feedfeed_hit = false;
bool load_use_stall = false;
bool load_use_stall_delay = false;
uint32_t load_use_stalls = 0;

IFID if_id;
IDEX id_ex;
EXMEM ex_mem;
MEMWB mem_wb;
IFID if_id_cpy;
IDEX id_ex_cpy;
EXMEM ex_mem_cpy;
MEMWB mem_wb_cpy;

uint32_t ex_fwd_A = 0;
uint32_t ex_fwd_B = 0;
uint32_t wb_instruction = 0;
uint32_t if_instruction = 0;
int iCache_stalls = 0;
int dCache_stalls = 0;
bool started = false;
bool haltReached = false;
PipeState mostRecentPS = PipeState();

/* End of Global Variable Definitions */

/* Start of the Cache Files */

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
  iCache.resize(index_size * iWays);
  for (int i = 0; i < index_size * iWays; i++) {
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
  dCache.resize(index_size * dWays);
  for (int i = 0; i < index_size * dWays; i++) {
    dCache.entries[i].resize(block_words);
  }

  // TODO: initialize iCache and dCache sizes, initialize individual cache entry data sizes
  // TODO: test w driver

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

   // Write back all dirty values in the data cache to memory
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

   dump(myMem, reg);
}

// TODO: static function for block eviction

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

// TODO: static function for reading block from memory into cache

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

// prints statistics
void printStats() {
  cout << "iCache Hits: " << icHits << endl;
  cout << "iCache Misses: " << icMisses << endl;
  cout << "dCache Hits: " << dcHits << endl;
  cout << "dCache Misses: " << dcMisses << endl;
}

// handles cache reads and writes
bool cacheAccess(bool isICache, uint32_t memAddress, uint32_t *data, bool isRead, uint32_t size)
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

  // Bodging to allow for half/byte reads/writes
  uint32_t byte_offset = memAddress & 0x3;
  uint32_t byte_mask = (size == WORD_SIZE) ? 0xffffffff : (0x1 << (8*size)) - 1;
  uint32_t byte_shift = 32 - 8 * (byte_offset + size);


  if (cache->isDirect) {
    // handles read hit
    if (cache->entries[index].isValid && cache->entries[index].tag == tag) {
      if (isICache) icHits++;
      else dcHits++;

      if (isRead) {
         *data = ((cache->entries[index].data[block_offset] >> byte_shift) & byte_mask);
      } else {
         // Clear the data, then write over it
         cache->entries[index].data[block_offset] &= ~(byte_mask << byte_shift);
         cache->entries[index].data[block_offset] |= (*data << byte_shift);
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

/* End of Cache Files */


// advance PC function
void advance_pc(uint32_t offset)
{
    //PC  =  nPC;
  PC  += offset;
}


void handleException(bool isArithmetic) {

  cout << '\n' << "Hit an exception! here's the PC: " << hex << PC << "and here's the exception type: " << isArithmetic << '\n';
  PC_cpy = 0x8000;
  // in the EX stage
  if_id_cpy.nPC = 0;
  if_id_cpy.IR = 0;

  hit_exception = true;

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
  //id_ex.IR = 0;

  if (isArithmetic) {
    /* Start Updating the Copies */
    ex_mem.BrTgt = 0;
    ex_mem.Zero = 0;
    ex_mem.ALUOut = 0;
    ex_mem.RD = 0;
    ex_mem.B = 0;
    ex_mem.regWrite = 0;
    ex_mem.memWrite = 0;
    ex_mem.memRead = 0;
    ex_mem.IR = 0;
    // id_ex_cpy.opcode = 0;
    // id_ex_cpy.func_code = 0;
    // id_ex_cpy.nPC = 0;
    // id_ex_cpy.RS = 0;
    // id_ex_cpy.RT = 0;
    // id_ex_cpy.RD = 0;
    // id_ex_cpy.immed = 0;
    // id_ex_cpy.A = 0;
    // id_ex_cpy.B = 0;
    // id_ex_cpy.seimmed = 0;
    // id_ex_cpy.IR = 0;
  }
  PC = 0x8000;
  PC_cpy = 0x8000;
  nPC = PC_cpy + WORD_SIZE;
}

// determines if an instruction is a regWrite instruction
bool isRegWrite(uint32_t opcode, uint32_t func_code) {
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

// determines if an instruction is a memRead instruction
bool isMemRead(uint32_t opcode){
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

// determines if an instruction is a memRead instruction
bool isMemWrite(uint32_t opcode){
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

bool isValidInstruction(uint32_t opcode, uint32_t func_code) {
  cout << '\n' << hex << opcode << ' ' << hex << func_code << '\n';
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


// IF section code
void ifSection() {
    /* IF section  */
    uint32_t instruction = 0;
    if (load_use_stall_delay) {
      cout << "delaying now \n";
      load_use_stalls = load_use_stalls - 1;
      if (load_use_stalls == 0) {
        load_use_stall_delay = false;
        if_id.IR = if_instruction;
        PC_cpy = PC;
      }
      return;
    }
    if (load_use_stall) {
      cout << "hit a load use stall in IF at cycle " << cyclesElapsed << " with this many stalls: " << load_use_stalls << endl;
      load_use_stall_delay = true;
      load_use_stall = false;

      bool hit = cacheAccess(ICACHE, PC_cpy, &instruction, READ, WORD_SIZE);
      cout << "CACHE ACCESS!";
      if (!hit) {
        iCache_stalls = (iCache_stalls <= iCache.missLatency) ? iCache.missLatency: iCache_stalls;
      }
      if_instruction = instruction;
      cout << if_instruction << "\n";
      return;
    }




    if_id.IR = 0;

    if (!feedfeed_hit) {
      bool hit = cacheAccess(ICACHE, PC_cpy, &instruction, READ, WORD_SIZE);
      cout << "CACHE ACCESS!";
      if (!hit) {
        iCache_stalls = (iCache_stalls <= iCache.missLatency) ? iCache.missLatency: iCache_stalls;
      }
      cout << hex << instruction << '\n' << "PC:" << hex << PC << '\n';
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
    /* ID section  */
}



void idSection() {
    // retrieve and decode the instruction
    if (load_use_stalls > 1) {
      return;
    }
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

    if ((!isValidInstruction(id_ex.opcode, id_ex.func_code)) && (instruction != 0xfeedfeed)) {
      handleException(false);
      return;
    }

    bool isBranch = false;
    // ex hazard forwarding to ID stage bc of branches
    if (((id_ex.opcode >= 0x2) && (id_ex.opcode <= 0x7)) || ((id_ex.opcode == 0) && (id_ex.func_code == 0x8))) {
      isBranch = true;
    }

    if ((!isBranch) && ex_mem.memRead && (ex_mem.RD != 0) &&((ex_mem.RD == id_ex.RS) || (ex_mem.RD == id_ex.RT))) {
      cout << "hit a load use stall at cycle " << cyclesElapsed << endl;
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

    // cout << "isbranch: " << isBranch << '\n';

    bool clear_flag = false;

    if (isBranch) {

      // MEM Forwarding to ID (| --- | branch | --- | --- | load |)
      if ((mem_wb_cpy.regWrite && (mem_wb_cpy.RD != 0)) && (mem_wb_cpy.RD == id_ex.RS)) {
        id_ex.A = mem_wb_cpy.ALUOut;
        cout << "MEM Forwarding A to ID at cycle " << cyclesElapsed << endl;
      }
      if ((mem_wb_cpy.regWrite && (mem_wb_cpy.RD != 0)) && (mem_wb_cpy.RD == id_ex.RT)) {
        id_ex.B = mem_wb_cpy.ALUOut;
        cout << "MEM Forwarding B to ID at cycle " << cyclesElapsed << endl;
      }

      // EX Forwarding to ID (| --- | branch | --- | ALU | --- |)
      if ((ex_mem_cpy.regWrite && (ex_mem_cpy.RD != 0)) && (ex_mem_cpy.RD == id_ex.RS)) {
        id_ex.A = ex_mem_cpy.ALUOut;
        cout << "EX Forwarding A to ID at cycle " << cyclesElapsed << endl;
      }
      if ((ex_mem_cpy.regWrite && (ex_mem_cpy.RD != 0)) && (ex_mem_cpy.RD == id_ex.RT)) {
        id_ex.B = ex_mem_cpy.ALUOut;
        cout << "EX Forwarding B to ID at cycle " << cyclesElapsed << endl;
      }

      // MEM Stall by 1 cycle (| --- | branch | --- | load | --- |)
      if ((ex_mem_cpy.memRead && (ex_mem_cpy.RD != 0)) && (ex_mem_cpy.RD == id_ex.RS)) {
        instruction = 0;
        clear_flag = true;
        load_use_stall = true;
        load_use_stalls = 1;
        cout << "MEM Stall by 1 Cycle to ID at cycle " << cyclesElapsed << endl;
      }
      if ((ex_mem_cpy.memRead && (ex_mem_cpy.RD != 0)) && (ex_mem_cpy.RD == id_ex.RT)) {
        instruction = 0;
        clear_flag = true;
        load_use_stall = true;
        load_use_stalls = 1;
        cout << "MEM Stall by 1 Cycle to ID at cycle " << cyclesElapsed << endl;
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
        cout << "2 STALL CYCLES!!!!!!!!!!!!!!!" << endl;
      }
      if (ex_mem.memRead && ((ex_mem.RD != 0) && (ex_mem.RD == id_ex.RT))) {
        instruction = 0;
        clear_flag = true;
        load_use_stall = true;
        load_use_stalls = 2;
        cout << "2 STALL CYCLES!!!!!!!!!!!!!!!" << endl;
      }
    }

    if (clear_flag) {
       id_ex = IDEX();
    }

    uint32_t mostSig_ex = id_ex.immed >> 15; // most significant bit in immediate
    uint32_t imm_ex = 0;
    uint32_t target = (instruction << 6) >> 6;
    /* Determine if it's a branch. Enter Code Here */
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
      // jump
      case (0x2):
        PC = (PC & 0xf0000000) | (target << 2);
        break;
      // jump and link
      case (0x3):
        id_ex.RD = 31;
        id_ex.A = PC + 8;
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
      // jump register
      case (0x0):
        if (id_ex.func_code == 0x8) {
          PC = id_ex.A;
        }
        break;
      default:
        break;
    }

    id_ex.nPC = if_id_cpy.nPC + 4;
}

void exSection() {
    /* Start of EX Section */
    int opCode = id_ex_cpy.opcode;
    // initialize variables, rs, rt, rd, imm, mostSig
    uint32_t rs = id_ex_cpy.RS; // operand
    uint32_t rt = id_ex_cpy.RT; // destination operand for imm instructions
    uint32_t rd = id_ex_cpy.RD; // destination operand
    uint32_t imm = id_ex_cpy.immed; // immediate address
    uint32_t mostSig = imm >> 15; // most significant bit in immediate
    uint32_t func_code = id_ex_cpy.func_code;
    uint32_t A = id_ex_cpy.A;
    uint32_t B = id_ex_cpy.B;
    uint32_t shamt = id_ex_cpy.shamt;

    /* intialize some EXMEM register variables */
    ex_mem.B = id_ex_cpy.B;
    ex_mem.IR = id_ex_cpy.IR;

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
    // lw r0, 5
    // add r0, r0, r0

    // memory stage
    // add ex stage

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
     cout << "ex_mem_cpy.ALUOut: " << ex_mem_cpy.ALUOut << endl;
    }

    // switch based on op-code.
    switch (opCode) {
      // r-type instructions
      case 0:
      {
        // nested switch statements based on function code
        // for r-type instructions.
        switch (func_code)
        {
          // add
          case 0x20:
          {
            uint32_t sigbit_rs = A >> 31;
            uint32_t sigbit_rt = B >> 31;
            uint32_t res = (uint32_t)((int)A + (int)B);
            uint32_t sigbit_rd = res >> 31;
            if (sigbit_rs == sigbit_rt) {
                if (sigbit_rd != sigbit_rs) {
                    /* TODO: Handle Exceptions! */
                    handleException(true);
                    return;
                }
            }
            ex_mem.ALUOut = res;
            //advance_pc(4);
            break;
           }
          // addu
          case 0x21:
            ex_mem.ALUOut = A + B;
            //advance_pc(4);
            break;
          // and
          case 0x24:
            ex_mem.ALUOut = A & B;
            //advance_pc(4);
            break;
          // nor
          case 0x27:
            ex_mem.ALUOut = ~(A | B);
            //advance_pc(4);
            break;
          // or
          case 0x25:
            ex_mem.ALUOut = A | B;
            //advance_pc(4);
            break;
          // slt (signed)
          case 0x2a:
            ex_mem.ALUOut = ((int)A < (int)B) ? 1 : 0;
            //advance_pc(4);
            break;
          // sltu
          case 0x2b:
            ex_mem.ALUOut = (A < B) ? 1 : 0;
            //advance_pc(4);
            break;
          // sll
          case 0x00:
            ex_mem.ALUOut = (B << shamt);
            // if (!load_use_stall) {
            //   advance_pc(4);
            // }
            break;
          // srl
          case 0x02:
            ex_mem.ALUOut = (B >> shamt);
            //advance_pc(4);
            break;
          // sub (signed)
          case 0x22:
          {
            uint32_t sigbit_rs = A >> 31;
            uint32_t sigbit_rt = B >> 31;
            uint32_t res = (uint32_t)((int)A - (int)B);
            uint32_t sigbit_rd = res >> 31;
            if ((sigbit_rs == 0) && (sigbit_rt == 1)) {
                if (sigbit_rd == 1) {
                    /* TODO: Handle Exceptions! */
                    handleException(true);
                    return;
                }
            }
            else if ((sigbit_rs == 1) && (sigbit_rt == 0)) {
                if (sigbit_rd == 0) {
                    /* TODO: Handle Exceptions! */
                    handleException(true);
                    return;
                }
            }
            //advance_pc(4);
            ex_mem.ALUOut = res;
            break;
          }
          // subu
          case 0x23:
            ex_mem.ALUOut = A - B;
            //advance_pc(4);
            break;
          // jr
          // case 0x8:
          //   ex_mem.ALUOut = A;
          //   break;
        }
    break;
    }
    // rest are I-Types
    case 0x8:
    {
      // add-immediate
      // sign extend in c++, get the most significant bit
      if (mostSig == 1) {
          imm = imm | 0xffff0000;
      }
      uint32_t sigbit_rs = A >> 31;
      uint32_t sigbit_imm = imm >> 31;
      uint32_t res = (uint32_t)((int)A + (int)imm);
      uint32_t sigbit_rd = res >> 31;
      if (sigbit_rs == sigbit_imm) {
          if (sigbit_rd != sigbit_rs) {
              /* TODO: Handle Exception! */
              handleException(true);
              return;
          }
      }
      ex_mem.ALUOut = res;
      //advance_pc(4);
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
      //advance_pc(4);
      break;
    }
    case 0xc:
    {
      // and immediate unsigned
      ex_mem.ALUOut = A & imm;
      //advance_pc(4);
      break;
    }
    case 0xd:
    {
      // or immediate
      ex_mem.ALUOut = A | imm;
      //advance_pc(4);
      break;
    }
    case 0xa:
    {
      // set less than immediate
      if (mostSig == 1) {
          imm = imm | 0xffff0000;
      }
      ex_mem.ALUOut = ((int)A < (int)imm) ? 1 : 0;
      //advance_pc(4);
      break;
    }
    case 0xb:
    {
      // set less than immediate unsigned
      ex_mem.ALUOut = (A < imm) ? 1 : 0;
      //advance_pc(4);
      break;
    }
    case 0x24:
    {
      // load byte unsigned
      // sign extend the imm variable
      if (mostSig == 1) {
          imm = imm | 0xfffc0000;
      }
      uint32_t res = imm + A;
      ex_mem.ALUOut = res;
      //advance_pc(4);
      break;
    }
    case 0x25:
    {
      // load half word unsigned
      // sign extend the imm variable
      if (mostSig == 1) {
          imm = imm | 0xfffc0000;
      }
      uint32_t res = imm + A;
      ex_mem.ALUOut = res;
      //advance_pc(4);
      break;
    }
    case 0xf:
    {
      cout << '\n' << "was inside the load upper immediate stage " << hex << PC << "\n";
      // load upper immediate
      ex_mem.ALUOut = (imm << 16);
      //advance_pc(4);
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
      //advance_pc(4);
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
      //advance_pc(4);
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
      //advance_pc(4);
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
      //advance_pc(4);
      break;
    }
  }
  ex_mem.memRead = memRead;
  ex_mem.memWrite = memWrite;
  ex_mem.regWrite = regWrite;

  /* End of EX Section */

}

void memSection() {
  /* Begin Mem Section */

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

  uint32_t size = WORD_SIZE;
  if (memRead_mem || memWrite_mem) {
    opcode = mem_wb.IR >> 26;
    func_code = mem_wb.IR  & (63);
    cout << "opcode: " << hex << opcode << '\n';
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
	cout << "something went wrong!!";
	break;

    }

  }

  if (memRead_mem) {
    bool hit = cacheAccess(DCACHE, ex_mem_cpy.ALUOut, &storeData, READ, size);
    if (!hit) {
      dCache_stalls = (dCache_stalls <= dCache.missLatency) ? dCache.missLatency: dCache_stalls;
    }
    mem_wb.ALUOut = storeData;
  }
  if (memWrite_mem) {
    bool hit = cacheAccess(DCACHE, ex_mem_cpy.ALUOut, &ex_mem_cpy.B, WRITE, size);
    if (!hit) {
      dCache_stalls = (dCache_stalls <= dCache.missLatency) ? dCache.missLatency: dCache_stalls;
    }
  }

  /* End of Mem Section */
}

bool wbSection() {
  /* Start of WB Section */
   // hardwire zero to ground
   reg[0] = 0;
  wb_instruction = mem_wb_cpy.IR;
  if (mem_wb_cpy.IR == 0xfeedfeed) {
    return true;
  }

  if (isRegWrite(wb_instruction >> 26, wb_instruction & (63))) {
    reg[mem_wb_cpy.RD] = mem_wb_cpy.ALUOut;
  }

  return false;

  /* End of WB Section */
}


static bool runOneCycle() {
    started = true;
    if ((iCache_stalls > 0) || (dCache_stalls > 0)) {
      cout << "iCache stalls: " << iCache_stalls << "\n";
      cout << "dCache stalls: " << dCache_stalls << "\n";
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
    if (iCache_stalls <= 0) {
       mostRecentICache = iCache;
    }
    if (dCache_stalls <= 0) {
       mostRecentDCache = dCache;
    }
   // Forwarding Section
    ex_fwd_A = 0;
    ex_fwd_B = 0;
    cout << cyclesElapsed << '\n';
    cout << "$t2 is " << reg[10] << endl;
    cout << "ex mem regwrite:" << ex_mem.regWrite << '\n';
    cout << "ex mem rd:" << ex_mem.RD << '\n';
    cout << "id ex rs:" << id_ex.RS << '\n';
    cout << "id ex rt:" << id_ex.RT << '\n';
    cout << '\n';

    // EX Hazard (forward to EX)
    if (ex_mem.regWrite && ((ex_mem.RD != 0) && (ex_mem.RD == id_ex.RS))) {
      ex_fwd_A = 2;
      cout << "a from exmem" << '\n';
    }
    if (ex_mem.regWrite && ((ex_mem.RD != 0) && (ex_mem.RD == id_ex.RT))) {
      ex_fwd_B = 2;
      cout << "b from exmem" << '\n';
    }

    // MEM Hazard (forward to EX)
    if ((mem_wb.regWrite && (mem_wb.RD != 0)) && !(ex_mem.regWrite && ((ex_mem.RD != 0) && (ex_mem.RD == id_ex.RS))) && (mem_wb.RD == id_ex.RS)) {
      ex_fwd_A = 1;
      cout << "a from wb" << '\n';
    }
    if ((mem_wb.regWrite && (mem_wb.RD != 0)) && !(ex_mem.regWrite && ((ex_mem.RD != 0) && (ex_mem.RD == id_ex.RT))) && (mem_wb.RD == id_ex.RT)) {
      ex_fwd_B = 1;
      cout << "b from wb" << '\n';
    }


    bool halt = wbSection();
    memSection();
    exSection();
    idSection();
    ifSection();

    return halt;
}


// run cycles function
int runCycles(uint32_t cycles) {

  bool halt;
  uint32_t endCycle = cyclesElapsed + cycles;
  if ((cycles == 0) || haltReached) {
     dumpPipeState(mostRecentPS);
     return (haltReached) ? 1 : 0;
  }
  while (cyclesElapsed < endCycle) {

     halt = runOneCycle();
     if (halt || (cyclesElapsed ==  (endCycle - 1))) {

         mostRecentPS.cycle = cyclesElapsed;
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

    /* Start Updating the Copies */
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
  return (halt) ? 1 : 0;

}

int runTillHalt() {
   bool halt = false;
   while (true) {
      halt = runOneCycle();
      if (halt)
         break;

      /* Start Updating the Copies */
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
