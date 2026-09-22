// @category EDF2027
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.Address;
import java.nio.file.*;
import java.io.PrintWriter;
public class RendererRegistration extends GhidraScript {
 public void run() throws Exception {
  Path out=Path.of(getScriptArgs()[0]).resolve("registration");Files.createDirectories(out);
  long[] addresses={0x820B1F60L,0x821A68C8L,0x8219EA48L,0x821A70A0L,
   0x821A4980L,0x820B1028L,0x820A9618L,0x82170488L,0x82195AB8L,0x821E4C08L,
   0x821A5A10L,0x821CE168L,0x821CDDD8L,0x821AD1F8L,0x821A1628L,
   0x821A1678L,0x821D1880L,0x820D39A8L,0x820D44D8L,0x820B6068L,0x821C79F0L,
   0x820A53D8L,0x820A5448L,0x820A54B8L,0x820A5528L,0x820A5598L,
   0x82113028L,0x820D6C40L,0x820D4928L,0x820B3620L,
   0x820BF180L,0x820BF0D0L,0x820CEEC8L,0x8218E7A8L,0x820D2578L,0x8217BED0L,
   0x820D4850L,0x820B3610L,0x820B35A0L,0x821C0C00L,0x821A3B80L,0x821A3BA0L,0x8252B718L};
  DecompInterface decompiler=new DecompInterface();decompiler.openProgram(currentProgram);
  try(PrintWriter instructions=new PrintWriter(Files.newBufferedWriter(out.resolve("instructions.tsv")))) {
   instructions.println("function\taddress\tinstruction");
   for(long value:addresses) {
    Address address=toAddr(value);Function f=getFunctionAt(address);
    if(f==null){disassemble(address);f=createFunction(address,"registration_"+address);}
    if(f==null)throw new IllegalStateException("Missing function "+address);
    DecompileResults result=decompiler.decompileFunction(f,45,monitor);
    if(!result.decompileCompleted())throw new IllegalStateException(result.getErrorMessage());
    Files.writeString(out.resolve(address+".c"),result.getDecompiledFunction().getC());
    InstructionIterator it=currentProgram.getListing().getInstructions(f.getBody(),true);
    while(it.hasNext()){Instruction ins=it.next();instructions.println(address+"\t"+ins.getAddress()+"\t"+ins);}
   }
  }finally{decompiler.dispose();}
  println("Registration analysis completed: "+addresses.length+" functions");
 }
}
