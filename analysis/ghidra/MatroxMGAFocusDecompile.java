// Decompile selected original MatroxMGA functions for offline review.

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

public class MatroxMGAFocusDecompile extends GhidraScript {
    private final long[] entries = { 0x000000f0L, 0x00003900L };

    @Override
    public void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        for (int index = 0; index < entries.length; index++) {
            Address address = toAddr(entries[index]);
            Function function = getFunctionContaining(address);
            if (function == null) {
                println("MGA_STATIC_FOCUS=missing address=" + address);
                continue;
            }
            println("MGA_STATIC_FOCUS=function=" + function.getName() +
                    " entry=" + function.getEntryPoint() +
                    " body=" + function.getBody());
            DecompileResults results = decompiler.decompileFunction(function, 60, monitor);
            if (results.decompileCompleted()) {
                println("MGA_STATIC_DECOMPILE_BEGIN=" + function.getEntryPoint());
                println(results.getDecompiledFunction().getC());
                println("MGA_STATIC_DECOMPILE_END=" + function.getEntryPoint());
            }
            else {
                println("MGA_STATIC_DECOMPILE=failed entry=" + function.getEntryPoint() +
                        " message=" + results.getErrorMessage());
            }
            InstructionIterator iterator = currentProgram.getListing().getInstructions(
                function.getBody(), true);
            int instructionCount = 0;
            println("MGA_STATIC_DISASSEMBLY_BEGIN=" + function.getEntryPoint());
            while (iterator.hasNext()) {
                Instruction instruction = iterator.next();
                println("  " + instruction.getAddress() + " " + instruction);
                instructionCount++;
                if (instructionCount >= 500) {
                    println("  ... truncated-at-500-instructions");
                    break;
                }
            }
            println("MGA_STATIC_DISASSEMBLY_END=" + function.getEntryPoint());
        }
        decompiler.dispose();
        println("MGA_STATIC_FOCUS_STATUS=pass");
    }
}
