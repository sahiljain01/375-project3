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

struct IFID {
  uint32_t nPC;
  uint32_t IR;
};

struct IDEX {
  uint32_t opcode;
  uint32_t func_code;
  uint32_t nPC;
  uint32_t RS;
  uint32_t RT;
  uint32_t RD;
  uint32_t immed;
  uint32_t A;
  uint32_t B;
};

struct EXMEM {
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
  uint32_t RD;
  uint32_t memData;
  uint32_t ALUOut;
};

if_id = IFID(0, 0);
id_ex = IDEX(0, 0, 0, 0, 0, 0, 0);
ex_mem = EXMEM(0, 0, 0 ,0);
mem_wb = MEMWB(0, 0, 0);
if_id_cpy = IFID(0, 0);
id_ex_cpy = IDEX(0, 0, 0, 0, 0, 0, 0);
ex_mem_cpy = EXMEM(0, 0, 0, 0);
mem_wb_cpy = MEMWB(0, 0, 0);


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



int runCycles(uint32_t cycles) {

  uint32_t cyclesElapsed = 0;

  while (cyclesElapsed < cycles) {

    /* IF section  */
    uint32_t instruction = 0;

    bool hit = cacheRead(iCache, PC, &instruction);
    if (!hit) {
      cyclesElapsed += iCache.missLatency;
      instruction = myMem->getMemValue(PC, da, WORD_SIZE);
      cacheWrite(iCache, PC, instruction);
    }

    PC = PC + 4;    
    if_id.nPC = PC;
    if_id.ir = instruction;

    /* ID section  */

    // retrieve and decode the instruction
    instruction = if_id_cpy.ir;

    id_ex.opcode = instruction >> 26;
    id_ex.RS = instruction << 6 >> 27;
    id_ex.RT = instruction << 11 >> 27;
    id_ex.RD = instructon << 16 >> 27;
    id_ex.func_code = instruction && (63); 
    id_ex.immed = instruction << 16 >> 16;
    id_ex.A = reg[id_ex.RS];
    id_ex.B = reg[id_ex.RT];
    id_ex.shamt = instructon << 21 >> 27;

    /* 

    TODO: Determine if it's a branch. Enter Code Here 

    */

    id_ex.nPC = if_id_cpy.nPC + 4;

    /* Enter of ID Section */

    /* EX Section */ 
    int opCode = id_ex_cpy.opcode;
    // initialize variables, rs, rt, rd, imm, mostSig
    uint32_t rs = id_ex_copy.RS; // operand
    uint32_t rt = id_ex_copy.RT; // destination operand for imm instructions
    uint32_t rd = id_ex_copy.RD; // destination operand 
    uint32_t imm = id_ex_copy.immed; // immediate address
    uint32_t mostSig = imm >> 15; // most significant bit in immediate
    uint32_t func_code = id_ex_copy.func_code;
    uint32_t A = id_ex_copy.A;
    uint32_t B = id_ex_copy.B;
    uint32_t shamt = id_ex_copy.shamt;

    /* intialize some EXMEM register variables */
    ex_mem.B = id_ex_copy.B;
    if (opCode == 0) {
      ex_mem.RD = rd;
    }
    else {
      ex_mem.RD = rt;
    }


    bool regWrite = false;
    bool memWrite = false;
    bool memRead = false;

    // switch based on op-code.
    switch (opCode) {
      // r-type instructions
      case 0:
      {
        regWrite = true;
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
                      // dump registers
                      /* TODO: Handle Exceptions! */
                      fprintf(stderr, "Overflow exception");
                      exit(12);
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
              uint32_t sigbit_rs = A >> 31;
              uint32_t sigbit_rt = B >> 31;
              uint32_t res = (uint32_t)((int)A - (int)B);
              uint32_t sigbit_rd = res >> 31;
              if ((sigbit_rs == 0) && (sigbit_rt == 1)) {
                  if (sigbit_rd == 1) {
                      /* TODO: Handle Exceptions! */
                      fprintf(stderr, "Overflow exception");
                      exit(12);
                  }
              }
              else if ((sigbit_rs == 1) && (sigbit_rt == 0)) {
                  if (sigbit_rd == 0) {
                      /* TODO: Handle Exceptions! */
                      fprintf(stderr, "Overflow  exception");
                      delete myMem;
                      exit(12);
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
    // rest are I-Types
    case 0x8:
    {
        regWrite = true;
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
                fprintf(stderr, "Overflow exception");
                exit(12);
            }
        }
        ex_mem.ALUOut = res;
        break;
    }
    case 0x9:
    {
        regWrite = true;
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
        regWrite = true;
        // and immediate unsigned
        ex_mem.ALUOut = A & imm;
        break;
    }
    case 0xd:
    {
        regWrite = true;
        // or immediate
        ex_mem.ALUOut = A | imm;
        break;
    }
    case 0xa:
    {
        regWrite = true;
        // set less than immediate
        if (mostSig == 1) {
            imm = imm | 0xffff0000;
        }
        ex_mem.ALUOut = ((int)A < (int)imm) ? 1 : 0;
        break;
    }
    case 0xb:
    {
        regWrite = true;
        // set less than immediate unsigned
        ex_mem.ALUOut = (A < imm) ? 1 : 0;
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
        uint32_t result = 0x0;
        ex_mem.ALUOut = result;
        memRead = true;
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
        uint32_t result = 0x0;
        memRead = true;
        ex_mem.ALUOut = result;
        break;
    }
    case 0xf:
    {
        // load upper immediate
        ex_mem.ALUOut = (imm << 16);
        memRead = true;
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
        uint32_t result;
        ex_mem.ALUOut = result;
        memRead = true;
        break;
    }
    case 0xd:
    {
        // or immediate
        ex_mem.ALUOut = A | imm;
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
        uint32_t value = 0;
        value = B << 24;
        value = value >> 24; 
        ex_mem.ALUOut = value;
        memWrite = true;
        break;
    }
    case 0x29:
    {
        memWrite = true;
        // store halfword 
        if (mostSig == 1) {
            imm = imm | 0xffff0000;
        }
        uint32_t location = 0;
        location = A + imm;
        uint32_t value = 0;
        value = B << 16;
        value = value >> 16; 
        ex_mem.ALUOut = value;
        break;
    }
    case 0x2b:
    {
        memWrite = true;
        // store word
        if (mostSig == 1) {
            imm = imm | 0xffff0000;
        }
        uint32_t location = 0;
        location = A + imm;
        uint32_t value = B;
        ex_mem.ALUOut = value;
        break;
    }
    default:
    {
        /* TODO: HANDLE EXCEPTIONS! */
        // if it hits this case, exit with error 127.
        fprintf(stderr, "Illegal operation ...");
        // dump registers
        exit(127);
    }
  }

  regWrite = true;
  ex_mem.memRead = memRead;
  ex_mem.memWrite = memWrite;
  ex_mem.regWrite = regWrite;

  /* End of EX Section */

  /* Begin Mem Section */
  mem_wb.RD = ex_mem_cpy.RD;
  mem_wb.ALUOut = ex_mem_cpy.ALUOut;

  bool memRead_mem = ex_mem_cpy.memRead;
  bool memWrite_mem = ex_mem_cpy.memWrite;

  if (memRead_mem) {
    /* TODO: read the dcache 

    mem_wb.memData = CacheRead()

    */
  }
  if (memWrite) {
    /* 

    TODO: write to the dcache

    CacheWrite(ex_mem_cpy.ALUOut, ex_mem_cpy.B)

    */ 
  }

  /* End of Mem Section */ 

  /* Start of WB Section */

  reg[mem_wb_cpy.RD] = mem_wb_cpy.ALUOut;

  /* End of WB Section */

  /* Start Updating the Copies */
  if_id_cpy.nPC = if_id.nPC;
  if_id_cpy.IR = if_id.IR;

  id_ex_cpy.opcode = id_ex.opcode;
  id_ex_cpy.func_code = id_ex.func_code;
  id_ex_cpy.nPC = id_ex.nPC;
  id_ex_cpy.RS = id_ex.RS;
  id_ex_cpy.RT = id_ex.RT;
  id_ex_cpy.RD = id_ex.RD;
  id_ex_cpy.immed = id_ex.immed;
  id_ex_cpy.A = id_ex.A;
  id_ex_cpy.B = id_ex.B;

  ex_mem_cpy.BrTgt = ex_mem.BrTgt;
  ex_mem_cpy.Zero = ex_mem.Zero;
  ex_mem_cpy.ALUOut = ex_mem.ALUOut;
  ex_mem_cpy.RD = ex_mem.RD;
  ex_mem_cpy.B = ex_mem.B;
  ex_mem_cpy.regWrite = ex_mem.regWrite;
  ex_mem_cpy.memWrite = ex_mem.memWrite;
  ex_mem_cpy.memRead = ex_mem.memRead;

  mem_wb_cpy.RD = mem_wb.RD;
  mem_wb_cpy.memData = mem_wb.memData;
  mem_wb_cpy.ALUOut = mem_wb.ALUOut;

  }

}