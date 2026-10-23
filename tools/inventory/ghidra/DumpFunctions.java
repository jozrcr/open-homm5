// Dump Ghidra's decompiled C and the disassembly (with bytes) of chosen functions.
// For reading code during research and for writing decomp candidates.
//   analyzeHeadless <projdir> <proj> -process <exe> -noanalysis -readOnly
//       -scriptPath tools/inventory/ghidra -postScript DumpFunctions.java <outdir> <addr> [<addr> ...]
// An argument starting with @ is a file with one hex address per line.
// Writes <outdir>/<address>.txt per function. Output contains game code:
// keep it in gitignored folders (analysis/export/).
//@category open-homm5

import java.io.File;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

public class DumpFunctions extends GhidraScript {
	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		File out = new File(args[0]);
		out.mkdirs();
		List<String> addrs = new ArrayList<>();
		for (int i = 1; i < args.length; i++) {
			if (args[i].startsWith("@")) {
				for (String l : Files.readAllLines(Path.of(args[i].substring(1)))) {
					if (!l.isBlank()) {
						addrs.add(l.strip());
					}
				}
			}
			else {
				addrs.add(args[i]);
			}
		}
		DecompInterface dec = new DecompInterface();
		dec.openProgram(currentProgram);
		for (String a : addrs) {
			var entry = toAddr(Long.parseLong(a, 16));
			Function f = getFunctionAt(entry);
			if (f == null) {
				// code only reached through pointers (e.g. message handlers) may not
				// be a function yet: make one (in memory only, the project is read-only)
				disassemble(entry);
				f = createFunction(entry, null);
			}
			if (f == null) {
				println("no function at " + a);
				continue;
			}
			try (PrintWriter w = new PrintWriter(new File(out, a + ".txt"), "UTF-8")) {
				w.println("// " + f.getName(true) + " @ " + f.getEntryPoint() + "  size " +
					f.getBody().getNumAddresses() + "  callers " +
					currentProgram.getReferenceManager().getReferenceCountTo(f.getEntryPoint()));
				w.println("// signature: " + f.getSignature().getPrototypeString());
				StringBuilder callers = new StringBuilder();
				for (Function c : f.getCallingFunctions(monitor)) {
					callers.append(' ').append(c.getEntryPoint());
				}
				w.println("// called from:" + callers);
				StringBuilder callees = new StringBuilder();
				for (Function c : f.getCalledFunctions(monitor)) {
					callees.append(' ').append(c.getEntryPoint());
				}
				w.println("// calls:" + callees);
				for (var ref : currentProgram.getReferenceManager().getReferencesTo(f.getEntryPoint())) {
					if (ref.getReferenceType().isData()) {
						w.println("// address taken at: " + ref.getFromAddress());
					}
				}
				w.println();
				for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
					StringBuilder hex = new StringBuilder();
					for (byte b : ins.getBytes()) {
						hex.append(String.format("%02x ", b & 0xff));
					}
					w.printf("%s  %-30s %s%n", ins.getAddress(), hex, ins);
				}
				w.println();
				DecompileResults r = dec.decompileFunction(f, 60, monitor);
				w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// decompile failed: " + r.getErrorMessage());
			}
		}
		dec.dispose();
		println("dumped " + addrs.size() + " functions to " + out);
	}
}
