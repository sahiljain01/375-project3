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
  uint32_t seimmed;
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
  uint32_t regWrite;
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
/* End of Cache Files */ 

 
// advance PC function
void advance_pc(uint32_t offset)
{
    PC  =  nPC;
   nPC  += offset;
}

void handleException(bool isArithmetic) {
  PC = 0x8000;
  // in the EX stage
  if_id.nPC = 0;
  if_id.IR = 0;

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
    /* Start Updating the Copies */
    ex_mem.BrTgt = 0;
    ex_mem.Zero = 0;
    ex_mem.ALUOut = 0;
    ex_mem.RD = 0;
    ex_mem.B = 0;
    ex_mem.regWrite = 0;
    ex_mem.memWrite = 0;
    ex_mem.memRead = 0;
  }
  PC = 0x8000;
  nPC = PC + WORD_SIZE;
}

// IF section code
void ifSection() {
    /* IF section  */
    uint32_t instruction = 0;

    bool hit = cacheAccess(true, PC, &instruction, true);
    if (!hit) {
      // TODO: Add stalling logic here.
    }

    if_id.nPC = PC + 4;
    if_id.ir = instruction;

    /* ID section  */
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
    case 0x3:
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

bool isValidInstruction(uint32_t opcode, uint32_t func_code) {
            switch (opCode){
            // r-type instructions
            case 0:
            {
                switch (func_code)
                {
                    // add 
                    case 0x20:
                    {
                      return true;
                      break;
                    }
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
                    {
                      return true;
                      break;
                    }
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
                    {
                        return true;
                        break;
                    }
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




void idSection() {
    // retrieve and decode the instruction
    instruction = if_id_cpy.ir;

    id_ex.opcode = instruction >> 26;
    id_ex.RS = instruction << 6 >> 27;
    id_ex.RT = instruction << 11 >> 27;
    id_ex.RD = instruction << 16 >> 27;
    id_ex.func_code = instruction && (63); 
    id_ex.immed = instruction << 16 >> 16;
    id_ex.A = reg[id_ex.RS];
    id_ex.B = reg[id_ex.RT];
    id_ex.shamt = instruction << 21 >> 27;
    id_ex.seimmed = (id_ex.immed >> 15 == 0) ? (uint32_t)id_ex.immed : ((uint32_t)id_ex.immed | 0xffff0000);

    if (!isValidInstruction(id_ex.opcode, id_ex.func_code)) {
      handleException(false);
      break;
    }

    uint32_t mostSig_ex = imm >> 15; // most significant bit in immediate
    uint32_t imm_ex = 0;
    /* Determine if it's a branch. Enter Code Here */
    switch (id_ex.opcode) {
      // beq
      case (0x4):
        if (id_ex.A == id_ex.B) {
            advance_pc(id_ex.seimmed << 2);
        }
        break;
      // bneq
      case (0x5):
        if (id_ex.A != id_ex.B) {
            advance_pc(id_ex.seimmed << 2);
        }
        break;
      // jump
      case (0x2):
        PC = nPC; 
        nPC = (PC & 0xf0000000) | (instruction << 6) >> 4;
        break;
      // jump and link
      case (0x3):
        id_ex.RD = 31;
        id_ex.A = nPC + 4;
        PC = nPC; 
        nPC = (PC & 0xf0000000) | (instruction << 6) >> 4;
        break;
      // blez
      case (0x6):
        if (id_ex.A <= id_ex.B) {
            advance_pc(id_ex.seimmed << 2);
        }
        break;
      // bgtz
      case (0x7):
        if (id_ex.A > id_ex.B) {
            advance_pc(id_ex.seimmed << 2);
        }
        break;
      // jump register
      case (0x0):
        if (id_ex.func_code == 0x8) {
          PC = nPC; 
          nPC = id_ex.A;
        }
        break;
      default:
        branchTaken = false;
        break;
    }

    id_ex.nPC = if_id_cpy.nPC + 4;
}

void exSection() {
    /* Start of EX Section */ 
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
    else if (opCode == 0x3) {
      ex_mem.RD = rd;
      ex_mem.B = id_ex_copy.A;
      regWrite = true;
    }
    else {
      ex_mem.RD = rt;
    }

    bool regWrite = isRegWrite(opCode, func_code);
    bool memWrite = false;
    bool memRead = false;

    /* TODO: Review EX Hazard Forwarding */ 
    if ((regWrite && (RD != 0)) && (RD == id_ex.RS)) {
      A = ex_mem.ALUOut;
    }
    if ((regWrite && (RD != 0)) && (RD == id_ex.RT)) {
      B = ex_mem.ALUOut;
    }

/* TODO: Memory Hazards 
      // Memory Hazards
    if ((mem_wb.regWrite && (mem_wb.RD != 0)) && !((mem_wb_cpy.regWrite && (mem_wb_cpy.RD != 0)) &&
      (mem_wb_cpy.RD == id_ex_cpy.RS) && (mem_wb.RD == id_ex_Cpy.RS)) 
      {
        // First ALU operand is forwarded from WB result
        A = mem_wb.memData;
      }
*/

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
                      // dump registers
                      /* TODO: Handle Exceptions! */
                      handleException(true);
                      break;
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
                      handleException(true);
                      break;
                  }
              }
              else if ((sigbit_rs == 1) && (sigbit_rt == 0)) {
                  if (sigbit_rd == 0) {
                      /* TODO: Handle Exceptions! */
                      handleException(true);
                      break;
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
                break;
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
        // and immediate unsigned
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
      regWrite = false;
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
        ex_mem.B = ex_mem.B && (0x000000ff);
        memWrite = true;
        break;
    }
    case 0x29:
    {
      regWrite = false;
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
        ex_mem.B = ex_mem.B && (0x0000ffff);
        ex_mem.ALUOut = value;
        break;
    }
    case 0x2b:
    {
      regWrite = false;
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
  }
  if (rd == 0) {
    regWrite = false;
  }
  ex_mem.memRead = memRead;
  ex_mem.memWrite = memWrite;
  ex_mem.regWrite = regWrite;

  /* End of EX Section */

}

void memSection() {
  /* Begin Mem Section */

  // MEM hazard
  // if (MEM_WB_Ctrl.RegWrite && (MEM_WB_Reg.RD != 0) &&
  //     !(EX_MEM_Ctrl.RegWrite && (EX_MEM_Reg.RD != 0) &&
  //       (EX_MEM_Reg.RD == ID_EX_Reg.RS)) &&
  //     (MEM_WB_Ctrl.RD == ID_EX_Reg.RS))
  //   HZD.FwdA = 0b01;
  // if (MEM_WB_Ctrl.RegWrite && (MEM_WB_Reg.RD != 0) &&
  //     !(EX_MEM_Ctrl.RegWrite && (EX_MEM_Reg.RD != 0) &&
  //       (EX_MEM_Reg.RD == ID_EX_Reg.RT)) &&
  //     (MEM_WB_Ctrl.RD == ID_EX_Reg.RT))
  //   HZD.FwdB = 0b01;

  mem_wb.RD = ex_mem_cpy.RD;
  mem_wb.ALUOut = ex_mem_cpy.ALUOut;
  mem_wb.regWrite = ex_mem_cpy.regWrite;
  uint32_t storeData = 0;


  bool memRead_mem = ex_mem_cpy.memRead;
  bool memWrite_mem = ex_mem_cpy.memWrite;
  bool regWrite = ex_mem_cpy.regWrite;  

  if (memRead_mem) {
    bool hit = cacheAccess(false, ex_mem_cpy.ALUOut, &storeData, true)
    mem_wb.memData = storeData;
    if (!hit) {
      /* TODO: Work on Cache Latency / Stall Here As Well */
    }
  }
  if (memWrite) {
    bool hit = cacheAccess(false, ex_mem_cpy.ALUOut, ex_mem_cpy.B, false)
    if (!hit) {
      /* TODO: Work on Cache Latency / Stall Here As Well */
    }
  }

  /* End of Mem Section */ 
}

void wbSection() {
  /* Start of WB Section */
  if (regWrite) {
    reg[mem_wb_cpy.RD] = mem_wb_cpy.ALUOut;
  }

  /* End of WB Section */
}

// run cycles function
int runCycles(uint32_t cycles) {

  uint32_t cyclesElapsed = 0;

  while (cyclesElapsed < cycles) {

    wbSection();
    memSection();
    exSection();
    idSection();
    ifSection();

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
    id_ex_cpy.seimmed = id_ex.seimmed;

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
    mem_wb_cpy.regWrite = mem_wb.regWrite;

  }

}

