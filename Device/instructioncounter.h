#ifndef INSTRUCTION_COUNTER_H
#define INSTRUCTION_COUNTER_H

#include "core_cm4.h"

class InstructionCounter
{
private:
    uint32_t _count;
public:    
    InstructionCounter() 
    {
    }
    void Enable()
    {
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
    void Disable()
    {
        DWT->CTRL &= ~DWT_CTRL_CYCCNTENA_Msk;
    }
    void Start()
    {
        DWT->CYCCNT = 0; // reset the count
    }
    void Stop()
    {
        _count = DWT->CYCCNT;
    }
    uint32_t GetCount() 
    {
        return _count;
    }
};

#endif