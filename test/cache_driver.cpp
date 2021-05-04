#include <iostream>
#include <iomanip>
#include <fstream>
#include <errno.h>
#include <stdlib.h>
#include "../src/MemoryStore.h"
#include "../src/RegisterInfo.h"
#include "../src/EndianHelpers.h"
#include "../src/DriverFunctions.h"
#include "../src/cache_test.h"

using namespace std;

static MemoryStore *mem;

int initMemory(ifstream & inputProg)
{
    if(inputProg && mem)
    {
        uint32_t curVal = 0;
        uint32_t addr = 0;

        while(inputProg.read((char *)(&curVal), sizeof(uint32_t)))
        {
            curVal = ConvertWordToBigEndian(curVal);
            int ret = mem->setMemValue(addr, curVal, WORD_SIZE);

            if(ret)
            {
                cout << "Could not set memory value!" << endl;
                return -EINVAL;
            }

            //We're reading 4 bytes each time...
            addr += 4;
        }
    }
    else
    {
        cout << "Invalid file stream or memory image passed, could not initialise memory values" << endl;
        return -EINVAL;
    }

    return 0;
}

int main(int argc, char **argv)
{
    if(argc != 2)
    {
        cout << "Usage: ./cache_sim <file name>" << endl;
        return -EINVAL;
    }

    ifstream prog;
    prog.open(argv[1], ios::binary | ios::in);

    mem = createMemoryStore();

    if(initMemory(prog))
    {
        return -EBADF;
    }

    CacheConfig icConfig;
    icConfig.cacheSize = 32;
    icConfig.blockSize = 8;
    icConfig.type = TWO_WAY_SET_ASSOC;
    icConfig.missLatency = 5;
    CacheConfig dcConfig = icConfig;

    initSimulator(icConfig, dcConfig, mem);
    uint32_t* write_data = (uint32_t*)malloc(4);
    *write_data = 0xdefaced;
    uint32_t* data = (uint32_t*)malloc(4);
    bool isHit = cacheAccess(ICACHE, 0x0, data, READ, WORD_SIZE);
    isHit = cacheAccess(ICACHE, 0x8, data, READ, WORD_SIZE);
    isHit = cacheAccess(ICACHE, 0x10, data, READ, WORD_SIZE);
    isHit = cacheAccess(ICACHE, 0x8, write_data, WRITE, WORD_SIZE);
    *write_data = 0xba;
    isHit = cacheAccess(ICACHE, 0x13, write_data, WRITE, BYTE_SIZE);
    isHit = cacheAccess(ICACHE, 0xa, data, READ, HALF_SIZE);
    cout << "Is hit: " << boolalpha << isHit << endl;
    cout << "Data: 0x" << hex << *data << endl;
    printStats();

    // runCycles(10);

    // runTillHalt();

    finalizeSimulator();

    delete mem;
    return 0;
}
