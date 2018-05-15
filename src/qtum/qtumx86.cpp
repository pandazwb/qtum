#include <util.h>
#include <tinyformat.h>
#include "qtumx86.h"

#include <x86lib.h>

using namespace x86Lib;


//The data field available is only a flat data field, so we need some format for storing
//Code, data, and options.
//Thus, it is prefixed with 4 uint32 integers.
//1st, the size of options, 2nd the size of code, 3rd the size of data
//4th is unused (for now) but is kept for padding and alignment purposes

struct ContractMapInfo {
    //This structure is CONSENSUS-CRITICAL
    //Do not add or remove fields nor reorder them!
    uint32_t optionsSize;
    uint32_t codeSize;
    uint32_t dataSize;
    uint32_t reserved;
} __attribute__((__packed__));

static ContractMapInfo* parseContractData(const uint8_t* contract, const uint8_t** outputCode, const uint8_t** outputData, const uint8_t** outputOptions){
    ContractMapInfo *map = (ContractMapInfo*) contract;
    *outputOptions = &contract[sizeof(ContractMapInfo)];
    *outputCode = &contract[sizeof(ContractMapInfo) + map->optionsSize];
    *outputData = &contract[sizeof(ContractMapInfo) + map->optionsSize + map->codeSize];
    return map;
}


const ContractEnvironment& x86ContractVM::getEnv() {
    return env;
}

#define CODE_ADDRESS 0x1000
#define MAX_CODE_SIZE 0x10000
#define DATA_ADDRESS 0x100000
#define MAX_DATA_SIZE 0x10000
#define STACK_ADDRESS 0x200000
#define MAX_STACK_SIZE (1024 * 8)

bool x86ContractVM::execute(ContractOutput &output, ContractExecutionResult &result, bool commit)
{
    if(true || output.OpCreate) { //temporarily enable opcall
        const uint8_t *code;
        const uint8_t *data;
        const uint8_t *options;
        ContractMapInfo *map;
        map = parseContractData(output.data.data(), &code, &data, &options);
        if (map->optionsSize != 0) {
            LogPrintf("Options specified in x86 contract, but none exist yet!");
            return false;
        }
        MemorySystem memory;
        //ROMemory codeMemory(map->codeSize, "code");
        //RAMemory dataMemory(map->dataSize, "data");
        ROMemory codeMemory(MAX_CODE_SIZE, "code");
        RAMemory dataMemory(MAX_STACK_SIZE, "data");
        RAMemory stackMemory(MAX_STACK_SIZE, "stack");
        //TODO how is .bss loaded!?

        //zero memory for consensus
        //memset(codeMemory.GetMemory(), map->codeSize, 0);
        //memset(dataMemory.GetMemory(), map->dataSize, 0);
        //memset(stackMemory.GetMemory(), MAX_STACK_SIZE, 0);

        //init memory
        memcpy(codeMemory.GetMemory(), code, map->codeSize);
        memcpy(dataMemory.GetMemory(), data, map->dataSize);

        MemorySystem memsys;
        memsys.Add(CODE_ADDRESS, CODE_ADDRESS + MAX_CODE_SIZE - 1, &codeMemory);
        memsys.Add(DATA_ADDRESS, DATA_ADDRESS + MAX_DATA_SIZE - 1, &dataMemory);
        memsys.Add(STACK_ADDRESS, STACK_ADDRESS + MAX_STACK_SIZE, &stackMemory);

        QtumHypervisor qtumhv(*this);

        x86CPU cpu;
        cpu.Memory = &memsys;
        cpu.Hypervisor = &qtumhv;
        try{
            cpu.Exec(output.gasLimit);
        }
        catch(CPUFaultException err){
            LogPrintf("CPU Panic! Message: %s, code: %x, opcode: %s, hex: %x", err.desc, err.code, cpu.GetLastOpcodeName(), cpu.GetLastOpcode());
            return false;
        }
        catch(MemoryException *err){
            LogPrintf("Memory error! address: %x, opcode: %s, hex: %x", err->address, cpu.GetLastOpcodeName(), cpu.GetLastOpcode());
            return false;
        }
        result.usedGas = 1;//set 1 to prove concept
        result.status = ContractStatus::SUCCESS;
        LogPrintf("Execution successful!");


    }else{
        LogPrintf("Call currently not implemented");
        return false;
    }



    return true;
}


void QtumHypervisor::HandleInt(int number, x86Lib::x86CPU &vm)
{

    if(number == 0xF0){
        //exit code
        vm.Stop();
        return;
    }
    if(number != QtumEndpoint::QtumSystem){
        LogPrintf("Invalid interrupt endpoint received");
        vm.Int(QTUM_SYSTEM_ERROR_INT);
        return;
    }
    switch(vm.GetRegister32(EAX)){
        case QtumSystemCall::BlockHeight:
            vm.SetReg32(EAX, contractVM.getEnv().blockNumber);
            break;
        case 0xFFFF0001:
            //internal debug printf
            //Remove before production!
            //ecx is string length
            //ebx is string pointer
            char* msg = new char[vm.GetRegister32(ECX) + 1];
            vm.ReadMemory(vm.GetRegister32(EBX), vm.GetRegister32(ECX), msg, Data);
            msg[vm.GetRegister32(ECX)] = 0; //null termination
            LogPrintf("Contract message: ");
            LogPrintf(msg);
            vm.SetReg32(EAX, 0);
            delete[] msg;
            break;
    }
    return;
}
