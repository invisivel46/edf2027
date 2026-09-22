// @category EDF2027
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.Address;
import java.nio.file.*;
import java.io.PrintWriter;
public class RendererRenderable extends GhidraScript {
 public void run() throws Exception {
  Path root=Path.of(getScriptArgs()[0]);
  String set=getScriptArgs().length>1?getScriptArgs()[1]:"renderable";
  if(!set.equals("renderable")&&!set.equals("state")&&!set.equals("contracts")&&!set.equals("helpers")&&!set.equals("dispatch")&&!set.equals("passes")&&!set.equals("retained"))throw new IllegalArgumentException("Unknown manifest set");
  Path out=root.resolve(set.equals("renderable")?"renderables":set);Files.createDirectories(out);
  DecompInterface decompiler=new DecompInterface();decompiler.openProgram(currentProgram);
  int count=0;
  try(PrintWriter manifest=new PrintWriter(Files.newBufferedWriter(out.resolve("decompilation.tsv")))) {
   manifest.println("address\tstatus\tbytes");
   for(String entry:Files.readAllLines(root.resolve(set+"-functions.txt"))) {
    Address address=toAddr(Long.parseUnsignedLong(entry,16));Function f=getFunctionAt(address);
    if(f==null){disassemble(address);f=createFunction(address,"renderable_"+address);}
    if(f==null){manifest.println(address+"\tmissing function\t0");continue;}
    DecompileResults result=decompiler.decompileFunction(f,45,monitor);
    if(!result.decompileCompleted()){manifest.println(address+"\t"+result.getErrorMessage()+"\t"+f.getBody().getNumAddresses());continue;}
    Files.writeString(out.resolve(address+".c"),result.getDecompiledFunction().getC());
    manifest.println(address+"\tok\t"+f.getBody().getNumAddresses());count++;
   }
  }finally{decompiler.dispose();}
  println("Renderable analysis completed: "+count+" functions");
 }
}
