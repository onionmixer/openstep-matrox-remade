// Static xref report for safety-relevant strings in the original MatroxMGA
// relocatable. It only reads Ghidra's local analysis database.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

public class MatroxMGAStringXrefs extends GhidraScript {
    private final String[] needles = {
        "MGA Memory Size",
        "MGACountRam:",
        "MGAReadBios",
        "MGAProbe",
        "determineConfiguration:",
        "reset VideoRAM to 2 MB for safety!",
        "reset VideoRAM to 4 MB for safety!",
        "reset VideoRAM to 8 MB for safety!"
    };

    private boolean isNeedle(String value) {
        for (int index = 0; index < needles.length; index++) {
            if (needles[index].equals(value)) {
                return true;
            }
        }
        return false;
    }

    @Override
    public void run() throws Exception {
        Listing listing = currentProgram.getListing();
        ReferenceManager references = currentProgram.getReferenceManager();
        FunctionManager functions = currentProgram.getFunctionManager();

        for (int needleIndex = 0; needleIndex < needles.length; needleIndex++) {
            String needle = needles[needleIndex];
            boolean found = false;
            println("MGA_STATIC_STRING=" + needle);
            DataIterator dataIterator = listing.getDefinedData(true);
            while (dataIterator.hasNext()) {
                Data data = dataIterator.next();
                Object value = data.getValue();
                if (value == null || !needle.equals(String.valueOf(value))) {
                    continue;
                }
                found = true;
                Address address = data.getAddress();
                println("  DATA=" + address);
                ReferenceIterator refIterator = references.getReferencesTo(address);
                int referenceCount = 0;
                while (refIterator.hasNext()) {
                    Reference reference = refIterator.next();
                    Function caller = functions.getFunctionContaining(reference.getFromAddress());
                    String callerName = caller == null ? "no-function" : caller.getName();
                    String entry = caller == null ? "-" : caller.getEntryPoint().toString();
                    println("  XREF from=" + reference.getFromAddress() +
                            " function=" + callerName + " entry=" + entry +
                            " type=" + reference.getReferenceType());
                    referenceCount++;
                }
                if (referenceCount == 0) {
                    println("  XREF=none");
                }
            }
            if (!found) {
                println("  DATA=not-found");
            }
        }
        println("MGA_STATIC_XREF_STATUS=pass");
    }
}
