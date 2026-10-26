#include "m6502.h"
#undef M6502_H
#undef BRANCH_CONDITION
#define M6502 ReferenceM6502
#define Interrupt ReferenceInterrupt
#include "reference/m6502.h"
#undef Interrupt
#undef M6502

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

struct Event {
    u16 address;
    u8 value;
    bool write;
};

struct Bus {
    u8 memory[65536];
    Event events[16];
    unsigned count = 0;
    u8 Read(u16 address) {
        if (count == 16) std::abort();
        events[count++] = {address, memory[address], false};
        return memory[address];
    }
    void Write(u16 address, u8 value) {
        if (count == 16) std::abort();
        events[count++] = {address, value, true};
        memory[address] = value;
    }
};

static Bus referenceBus, currentBus;
static u8 ReadReference(u16 address) { return referenceBus.Read(address); }
static u8 ReadCurrent(u16 address) { return currentBus.Read(address); }
static void WriteReference(u16 address, u8 value) { referenceBus.Write(address, value); }
static void WriteCurrent(u16 address, u8 value) { currentBus.Write(address, value); }

struct M6502DifferentialTest {
    static ReferenceM6502::AddressModeCycleFunction FetchFunction(ReferenceM6502&) {
        return &ReferenceM6502::InstructionFetch;
    }
    static M6502::CycleFunction FetchFunction(M6502&) {
        return &M6502::DispatchCycle<&M6502::InstructionFetch>;
    }

    static bool SameDispatch(const ReferenceM6502& a, const M6502& b) {
        typedef std::unordered_map<M6502::CycleFunction,
            ReferenceM6502::AddressModeCycleFunction> DispatchMap;
        static const DispatchMap corresponding = {
#define CPU_DISPATCH_CASE(name) {&M6502::DispatchCycle<&M6502::name>, &ReferenceM6502::name},
#include "m6502_dispatch_pairs.inc"
#undef CPU_DISPATCH_CASE
        };
        const auto mode = corresponding.find(b.addressModeCycleFn);
        const auto opcode = corresponding.find(b.opcodeCycleFn);
        return mode != corresponding.end() && opcode != corresponding.end() &&
            mode->second == a.addressModeCycleFn && opcode->second == a.opcodeCycleFn;
    }

    static unsigned Random(unsigned& state) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return state;
    }

    template<typename T> static void Seed(T& cpu, unsigned seed, u16 pc,
                                         DataBusReadFn read, DataBusWriteFn write) {
        cpu.dataBusReadFn = read;
        cpu.dataBusWriteFn = write;
        cpu.ea = Random(seed); cpu.ia = Random(seed); cpu.value = Random(seed);
        cpu.pc = pc; cpu.opcode = Random(seed);
        cpu.a = Random(seed); cpu.x = Random(seed); cpu.y = Random(seed);
        cpu.sp = Random(seed); cpu.status = Random(seed);
        cpu.CLIMaskingInterrupt = Random(seed) & 1;
        cpu.BranchTakenMaskingInterrupt = Random(seed) & 1;
        cpu.IRQPending = Random(seed) & 1;
        cpu.IRQ.Release();
        cpu.addressModeCycleFn = FetchFunction(cpu);
        cpu.opcodeCycleFn = T::opcodeFunctions[cpu.opcode];
    }

    static bool SameState(const ReferenceM6502& a, const M6502& b) {
        // Match the selected handler by name across the two representations,
        // in addition to comparing all data state and observable bus effects.
        return a.ea == b.ea && a.ia == b.ia && a.value == b.value &&
            a.pc == b.pc && a.opcode == b.opcode && a.a == b.a && a.x == b.x &&
            a.y == b.y && a.sp == b.sp && a.status == b.status &&
            a.CLIMaskingInterrupt == b.CLIMaskingInterrupt &&
            a.BranchTakenMaskingInterrupt == b.BranchTakenMaskingInterrupt &&
            a.IRQPending == b.IRQPending && a.SYNC() == b.SYNC() && SameDispatch(a, b);
    }

    static bool SameBus() {
        if (referenceBus.count != currentBus.count) return false;
        for (unsigned i = 0; i < referenceBus.count; ++i) {
            const Event& a = referenceBus.events[i];
            const Event& b = currentBus.events[i];
            if (a.address != b.address || a.value != b.value || a.write != b.write)
                return false;
        }
        return true;
    }

    static bool NZ() {
        ReferenceM6502 reference;
        M6502 current;
        for (unsigned status = 0; status < 256; ++status) {
            for (unsigned value = 0; value < 65536; ++value) {
                reference.status = current.status = status;
                reference.EstablishNZ(value);
                current.EstablishNZ(value);
                if (reference.status != current.status) {
                    std::fprintf(stderr, "N/Z mismatch: status %02x, value %04x\n", status, value);
                    return false;
                }
            }
        }
        std::puts("PASS N/Z flags: all 256 status bytes and 65,536 input values");
        return true;
    }

    static bool InterruptFlags() {
        ReferenceM6502 reference;
        M6502 current;
        for (unsigned status = 0; status < 256; ++status) {
            for (unsigned flags = 0; flags < 16; ++flags) {
                Seed(reference, 0x65020001, 0x200, ReadReference, WriteReference);
                Seed(current, 0x65020001, 0x200, ReadCurrent, WriteCurrent);
                reference.status = current.status = status;
                reference.CLIMaskingInterrupt = current.CLIMaskingInterrupt = flags & 1;
                reference.BranchTakenMaskingInterrupt = current.BranchTakenMaskingInterrupt = (flags >> 1) & 1;
                reference.IRQPending = current.IRQPending = (flags >> 2) & 1;
                if (flags & 8) { reference.IRQ.Assert(); current.IRQ.Assert(); }
                // A no-op cycle isolates the interrupt-latch processing in Step().
                reference.addressModeCycleFn = &ReferenceM6502::JAM;
                current.addressModeCycleFn = &M6502::DispatchCycle<&M6502::JAM>;
                reference.Step(); current.Step();
                if (!SameState(reference, current)) {
                    std::fprintf(stderr, "IRQ latch mismatch: status %02x, flags %x\n", status, flags);
                    return false;
                }
            }
        }
        std::puts("PASS IRQ pending/CLI/branch mask flags: all 4,096 combinations");
        return true;
    }

    static bool Traces() {
        ReferenceM6502 reference;
        M6502 current;
        static u8 initialMemory[65536];
        unsigned seed = 0x65021551;
        for (u8& byte : initialMemory) byte = Random(seed);
        const unsigned variants = 256, cycles = 64;
        const u16 pcs[] = {0x0200, 0x02ff, 0xfffe, 0x00ff};
        unsigned long long steps = 0;
        for (unsigned opcode = 0; opcode < 256; ++opcode) {
            for (unsigned variant = 0; variant < variants; ++variant) {
                unsigned state = 0x65020001u + opcode * 65537u + variant * 257u;
                const u16 pc = pcs[variant % 4];
                Seed(reference, state, pc, ReadReference, WriteReference);
                Seed(current, state, pc, ReadCurrent, WriteCurrent);
                // Cover all status-byte patterns for every initial opcode.
                reference.status = current.status = variant;
                std::memcpy(referenceBus.memory, initialMemory, sizeof(initialMemory));
                referenceBus.memory[pc] = opcode;
                referenceBus.memory[(pc + 1) & 65535] = Random(state);
                referenceBus.memory[(pc + 2) & 65535] = Random(state);
                std::memcpy(currentBus.memory, referenceBus.memory, sizeof(initialMemory));
                for (unsigned cycle = 0; cycle < cycles; ++cycle) {
                    referenceBus.count = currentBus.count = 0;
                    const unsigned signals = Random(state);
                    if (cycle && (signals & 7) == 0) {
                        reference.IRQ.Assert(); current.IRQ.Assert();
                    } else {
                        reference.IRQ.Release(); current.IRQ.Release();
                    }
                    if (cycle && (signals & 63) == 1) { reference.SO(); current.SO(); }
                    if (cycle == 32 && (variant & 1)) { reference.Reset(); current.Reset(); }
                    reference.Step(); current.Step();
                    if (!SameState(reference, current) || !SameBus()) {
                        std::fprintf(stderr, "CPU mismatch: opcode %02x, variant %u, cycle %u\n", opcode, variant, cycle);
                        return false;
                    }
                    if (!cycle && current.opcode != opcode) {
                        std::fprintf(stderr, "Initial opcode was not fetched\n"); return false;
                    }
                    ++steps;
                }
                if (std::memcmp(referenceBus.memory, currentBus.memory, sizeof(initialMemory))) {
                    std::fprintf(stderr, "Memory mismatch: opcode %02x, variant %u\n", opcode, variant);
                    return false;
                }
            }
        }
        std::printf("PASS %u CPU scenarios, %llu matched cycles: all 256 opcodes, decimal mode, page/stack wrap, IRQ, SO, resets and ordered bus reads/writes\n",
                    256 * variants, steps);
        return true;
    }
};

int main() {
    return M6502DifferentialTest::NZ() && M6502DifferentialTest::InterruptFlags() &&
        M6502DifferentialTest::Traces() ? 0 : 1;
}
