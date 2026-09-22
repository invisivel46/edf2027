// Batch renderer boundary inventory. Reachability is not execution coverage.
// @category EDF2027
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.pcode.PcodeOp;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;
import java.io.PrintWriter;
import java.util.*;

public class RendererInventory extends GhidraScript {
  public void run() throws Exception {
    Path output=Path.of(getScriptArgs()[0]); Files.createDirectories(output);
    Set<String> generated=new HashSet<>(Files.readAllLines(output.resolve("generated-function-addresses.txt")));
    long[] seeds={0x821A5080L,0x821A3BA0L,0x820B4310L,0x820B4250L,
      0x821C3BB8L,0x821D96D8L,0x821BE8D0L,0x821BE9D8L,0x820B0B80L,
      0x820D3FD0L,0x821C9478L,0x821C9C20L,0x821D9600L,0x821B94E8L,
      0x821CDDF8L,0x821A4DE8L};
    ArrayDeque<Function> queue=new ArrayDeque<>(); Set<Address> visited=new HashSet<>();
    DecompInterface decompiler=new DecompInterface();
    try(PrintWriter verification=new PrintWriter(Files.newBufferedWriter(output.resolve("vtable-verification.tsv"),StandardCharsets.UTF_8))) {
      verification.println("address\texpected\tactual\tmatch");
      for(String row:Files.readAllLines(output.resolve("candidate-vtables.tsv"))) {
        String[] fields=row.split("\t"); Address address=toAddr(Long.parseUnsignedLong(fields[0],16));
        long expected=Long.parseUnsignedLong(fields[1],16);
        long actual=Integer.toUnsignedLong(currentProgram.getMemory().getInt(address));
        verification.println(address+"\t"+fields[1]+"\t"+Long.toHexString(actual)+"\t"+(actual==expected));
      }
    }
    try(PrintWriter functions=new PrintWriter(Files.newBufferedWriter(output.resolve("functions.tsv"),StandardCharsets.UTF_8));
        PrintWriter edges=new PrintWriter(Files.newBufferedWriter(output.resolve("edges.tsv"),StandardCharsets.UTF_8));
        PrintWriter sites=new PrintWriter(Files.newBufferedWriter(output.resolve("sites.tsv"),StandardCharsets.UTF_8));
        PrintWriter missing=new PrintWriter(Files.newBufferedWriter(output.resolve("missing.tsv"),StandardCharsets.UTF_8))) {
      functions.println("address\tname\tbytes"); edges.println("caller\tsite\ttarget\tkind");
      sites.println("function\tsite\tkind\tinstruction"); missing.println("address\treason");
      decompiler.openProgram(currentProgram);
      for(long seed:seeds) {
        Address address=toAddr(seed); Function f=getFunctionAt(address);
        if(f==null) { disassemble(address); f=createFunction(address,"renderer_"+Long.toHexString(seed)); }
        if(f==null) { missing.println(address+"\tno function at renderer seed"); continue; }
        queue.add(f);
        DecompileResults result=decompiler.decompileFunction(f,45,monitor);
        Files.writeString(output.resolve(address+".c"),result.decompileCompleted()?
          result.getDecompiledFunction().getC():"DECOMPILE ERROR: "+result.getErrorMessage(),StandardCharsets.UTF_8);
      }
      for(String entry:Files.readAllLines(output.resolve("inventory-functions.txt"))) {
        Address address=toAddr(Long.parseUnsignedLong(entry,16)); Function f=getFunctionAt(address);
        if(f==null) { disassemble(address); f=createFunction(address,"renderer_"+entry); }
        if(f==null) missing.println(address+"\tno function at expanded inventory root");
        else queue.add(f);
      }
      while(!queue.isEmpty() && !monitor.isCancelled()) {
        Function f=queue.remove(); if(!visited.add(f.getEntryPoint())) continue;
        functions.println(f.getEntryPoint()+"\t"+f.getName()+"\t"+f.getBody().getNumAddresses());
        InstructionIterator instructions=currentProgram.getListing().getInstructions(f.getBody(),true);
        while(instructions.hasNext()) {
          Instruction instruction=instructions.next();
          boolean call=instruction.getFlowType().isCall();
          boolean jump=instruction.getFlowType().isJump();
          if(call || jump) {
            Address[] targets=instruction.getFlows();
            if(instruction.getFlowType().isComputed())
              sites.println(f.getEntryPoint()+"\t"+instruction.getAddress()+"\tindirect\t"+instruction);
            for(Address target:targets) {
              if(!call && f.getBody().contains(target)) continue;
              edges.println(f.getEntryPoint()+"\t"+instruction.getAddress()+"\t"+target+"\t"+(call?"call":"tail_or_branch"));
              Function callee=getFunctionAt(target);
              if(callee==null && generated.contains(target.toString().toLowerCase(Locale.ROOT))) {
                disassemble(target); callee=createFunction(target,"renderer_"+target);
              }
              if(callee!=null) queue.add(callee);
              else missing.println(target+"\tunresolved direct target from "+instruction.getAddress());
            }
          }
          for(PcodeOp op:instruction.getPcode()) if(op.getOpcode()==PcodeOp.STORE) {
            sites.println(f.getEntryPoint()+"\t"+instruction.getAddress()+"\tstore\t"+instruction); break;
          }
        }
      }
      println("Renderer inventory: "+visited.size()+" reachable defined functions. Missing definitions and indirect calls require separate resolution.");
    } finally { decompiler.dispose(); }
  }
}
