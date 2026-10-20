// Create functions at the addresses listed in a text file (one hex address per line).
// Used to add functions that are reachable only through vftables, which Ghidra's
// auto-analysis misses on our reference exe (see tools/inventory/README.md).
// Use as -preScript so the following auto-analysis also follows their calls:
//   analyzeHeadless <projdir> <proj> -process <exe>
//       -scriptPath tools/inventory/ghidra -preScript CreateFunctionsFromList.java <list.txt>
//@category open-homm5

import java.nio.file.Files;
import java.nio.file.Path;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

public class CreateFunctionsFromList extends GhidraScript {
	@Override
	public void run() throws Exception {
		int listed = 0, created = 0, failed = 0;
		for (String line : Files.readAllLines(Path.of(getScriptArgs()[0]))) {
			line = line.strip();
			if (line.isEmpty()) {
				continue;
			}
			monitor.checkCancelled();
			listed++;
			Address a = toAddr(Long.parseLong(line, 16));
			if (getFunctionAt(a) != null) {
				continue;
			}
			if (getInstructionAt(a) == null) {
				disassemble(a);
			}
			if (createFunction(a, null) != null) {
				created++;
			}
			else {
				failed++;
			}
		}
		println("listed: " + listed + ", created: " + created + ", failed: " + failed);
	}
}
