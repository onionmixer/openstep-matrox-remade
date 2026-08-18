# Original MatroxMGA Static Analysis

These Java Ghidra scripts operate only on the local analysis copy named in
`docs/R2_ORIGINAL_BINARY_CONFIGURATION_AUDIT.md`. They neither contact the
OPENSTEP target nor execute a driver.

Create/import the temporary project, then run the scripts against it:

```text
analyzeHeadless /tmp/openstep-mga-ghidra OriginalMGA \
  -import reference/original-binaries/MatroxMGA_reloc.R0-20260818 \
  -overwrite -analysisTimeoutPerFile 300

analyzeHeadless /tmp/openstep-mga-ghidra OriginalMGA \
  -process MatroxMGA_reloc.R0-20260818 -noanalysis \
  -scriptPath analysis/ghidra -postScript MatroxMGAStringXrefs.java
```

`MatroxMGAFocusDecompile.java` emits the recovered configuration and detection
function bodies for manual review. Ghidra decompiler output is evidence to be
cross-checked with its adjacent disassembly, never an authorization to replay
the original hardware path.
