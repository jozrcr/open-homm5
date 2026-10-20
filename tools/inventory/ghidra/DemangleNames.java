// Demangle MSVC names (RTTI type names like ".?AVCPlayerAI@NWorld@@" or
// function symbols) with Ghidra's standalone Microsoft demangler.
// Single-file Java program, no build step (JDK 11+):
//   java -cp <ghidra>/Ghidra/Features/MicrosoftDmang/lib/MicrosoftDmang.jar
//        tools/inventory/ghidra/DemangleNames.java  in.txt  out.tsv
// in.txt: one mangled name per line. out.tsv: mangled <TAB> demangled
// (demangled is empty when the name could not be parsed).

import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import mdemangler.MDMang;
import mdemangler.MDMangVS2015;
import mdemangler.MDParsableItem;

public class DemangleNames {
	public static void main(String[] args) throws Exception {
		try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(Path.of(args[1]), StandardCharsets.UTF_8))) {
			for (String line : Files.readAllLines(Path.of(args[0]), StandardCharsets.UTF_8)) {
				String mangled = line.strip();
				if (mangled.isEmpty()) {
					continue;
				}
				String result = "";
				try {
					MDMang md = new MDMangVS2015();
					md.setArchitectureSize(32);
					md.setErrorOnRemainingChars(false);
					md.setMangledSymbol(mangled);
					MDParsableItem item = md.demangle();
					if (item != null) {
						result = item.toString().replace('\t', ' ');
					}
				}
				catch (Exception e) {
					result = "";
				}
				out.println(mangled + "\t" + result);
			}
		}
	}
}
