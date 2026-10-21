// List every reference to the given addresses (code or data), with the function it is in.
//   analyzeHeadless <projdir> <proj> -process <exe> -noanalysis -readOnly
//       -scriptPath tools/inventory/ghidra -postScript XrefsTo.java <out.txt> <addr> [<addr> ...]
//@category open-homm5

import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class XrefsTo extends GhidraScript {
	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		try (PrintWriter w = new PrintWriter(args[0], "UTF-8")) {
			for (int i = 1; i < args.length; i++) {
				Address target = toAddr(Long.parseLong(args[i], 16));
				w.println("== references to " + target);
				for (Reference r : currentProgram.getReferenceManager().getReferencesTo(target)) {
					Function f = getFunctionContaining(r.getFromAddress());
					w.printf("  from %s  %-14s in %s%n", r.getFromAddress(), r.getReferenceType(),
						f == null ? "(no function)" : f.getName(true) + " @ " + f.getEntryPoint());
				}
			}
		}
	}
}
