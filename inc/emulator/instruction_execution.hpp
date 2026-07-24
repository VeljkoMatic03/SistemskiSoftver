#include "virtual_cpu.hpp"

class Instruction {
public:
    virtual void execute(VirtualCPU& vcpu) =0;
};

class Halt: public Instruction {
public:
    virtual void execute(VirtualCPU& vcpu) {
        vcpu.haltProcessor();
    }
};

class Int: public Instruction {
public:
    virtual void execute(VirtualCPU& vcpu) {
        vcpu.interruptProcessor();
    }
};

class Iret: public Instruction {
public:
    virtual void execute(VirtualCPU& vcpu) {
        
    }
};