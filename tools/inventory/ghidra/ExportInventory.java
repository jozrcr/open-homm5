// Export raw function / class / string-reference data from an analyzed program.
// Run headless (see tools/inventory/README.md):
//   analyzeHeadless <projdir> <proj> -process <exe> -noanalysis -readOnly
//       -scriptPath tools/inventory/ghidra -postScript ExportInventory.java <outdir>
// Writes into <outdir>:
//   functions.tsv     one row per function (address, size, name, namespace, flags)
//   func_strings.tsv  strings referenced directly by each function
//   rtti.tsv          RTTI type descriptors and vftables found by Ghidra's RTTI analyzer
//@category open-homm5

import java.io.File;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Bookmark;
import ghidra.program.model.listing.BookmarkManager;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

public class ExportInventory extends GhidraScript {

	private static String clean(String s) {
		if (s == null) {
			return "";
		}
		return s.replace('\t', ' ').replace('\r', ' ').replace('\n', ' ');
	}

	private static String ns(Namespace n) {
		return (n == null || n.isGlobal()) ? "" : n.getName(true);
	}

	@Override
	public void run() throws Exception {
		File out = new File(getScriptArgs()[0]);
		out.mkdirs();
		Listing listing = currentProgram.getListing();
		BookmarkManager bookmarks = currentProgram.getBookmarkManager();
		ReferenceManager refs = currentProgram.getReferenceManager();

		try (PrintWriter fw = new PrintWriter(new File(out, "functions.tsv"), "UTF-8");
				PrintWriter sw = new PrintWriter(new File(out, "func_strings.tsv"), "UTF-8")) {
			fw.println("address\tsize\tname\tnamespace\tsource\tthunk\tfid\tcallers");
			sw.println("function\tstring_address\tstring");
			int n = 0;
			for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
				monitor.checkCancelled();
				if (f.isExternal()) {
					continue;
				}
				Address ep = f.getEntryPoint();
				boolean fid = false;
				for (Bookmark b : bookmarks.getBookmarks(ep)) {
					if (b.getCategory().contains("Function ID")) {
						fid = true;
					}
				}
				fw.printf("%s\t%d\t%s\t%s\t%s\t%d\t%d\t%d%n", ep, f.getBody().getNumAddresses(),
					clean(f.getName()), clean(ns(f.getParentNamespace())), f.getSymbol().getSource(),
					f.isThunk() ? 1 : 0, fid ? 1 : 0, refs.getReferenceCountTo(ep));

				for (Instruction ins : listing.getInstructions(f.getBody(), true)) {
					for (Reference r : ins.getReferencesFrom()) {
						Address to = r.getToAddress();
						if (!to.isMemoryAddress()) {
							continue;
						}
						Data d = listing.getDataAt(to);
						if (d != null && d.hasStringValue()) {
							String v = clean(String.valueOf(d.getValue()));
							if (v.length() > 200) {
								v = v.substring(0, 200);
							}
							sw.printf("%s\t%s\t%s%n", ep, to, v);
						}
					}
				}
				if (++n % 10000 == 0) {
					println("functions exported: " + n);
				}
			}
			println("functions exported: " + n);
		}

		try (PrintWriter rw = new PrintWriter(new File(out, "rtti.tsv"), "UTF-8")) {
			rw.println("kind\taddress\tclass");
			SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
			while (it.hasNext()) {
				monitor.checkCancelled();
				Symbol s = it.next();
				String name = s.getName();
				if (name.equals("RTTI_Type_Descriptor") || name.equals("vftable")
						|| name.equals("RTTI_Complete_Object_Locator")) {
					rw.printf("%s\t%s\t%s%n", name, s.getAddress(), clean(ns(s.getParentNamespace())));
				}
			}
		}
	}
}
