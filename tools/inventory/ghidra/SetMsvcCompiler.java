// Mark the program as built by Visual Studio.
// Ghidra recognizes MSVC binaries by their Rich header / DOS stub. The Steam DRM
// wrapper zeroed both in our reference exe, so Ghidra records the compiler as
// "unknown" and refuses its MSVC-only analysis (RTTI recovery among them).
// Use as -preScript on import, or -postScript before RunRtti.java.
//@category open-homm5

import ghidra.app.script.GhidraScript;
import ghidra.app.util.opinion.PeLoader.CompilerOpinion.CompilerEnum;

public class SetMsvcCompiler extends GhidraScript {
	@Override
	public void run() throws Exception {
		println("compiler before: " + currentProgram.getCompiler());
		currentProgram.setCompiler(CompilerEnum.VisualStudio.toString());
		println("compiler after: " + currentProgram.getCompiler());
	}
}
