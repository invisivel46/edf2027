// Complete census export. Definitions are transient in a read-only project.
// @category EDF2027
import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.pcode.PcodeOp;
import ghidra.program.model.symbol.Reference;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class RendererAtlas extends GhidraScript {
  private final Gson gson=new GsonBuilder().disableHtmlEscaping().create();
  private String hex(Address a) { return a==null?null:String.format("%08X",a.getOffset()&0xffffffffL); }
  private Address address(long value) {
    for(AddressSpace s:currentProgram.getAddressFactory().getAddressSpaces()) {
      if(!s.isMemorySpace())continue;
      for(long v:new long[]{value,value|0xffffffff00000000L}) try {
        Address a=s.getAddress(v);if(currentProgram.getMemory().contains(a))return a;
      } catch(RuntimeException ignored) {}
    } return null;
  }
  private Map<String,Object> map(Object... pairs) {
    Map<String,Object> m=new LinkedHashMap<>();for(int i=0;i<pairs.length;i+=2)m.put((String)pairs[i],pairs[i+1]);return m;
  }
  private List<String> objects(Object[] values) {
    List<String> result=new ArrayList<>();for(Object o:values)result.add(o.toString());return result;
  }
  private void save(Path path,Object value)throws Exception {
    Path tmp=path.resolveSibling(path.getFileName()+".tmp");
    Files.writeString(tmp,gson.toJson(value),StandardCharsets.UTF_8);
    Files.move(tmp,path,StandardCopyOption.REPLACE_EXISTING);
  }
  public void run()throws Exception {
    String[] args=getScriptArgs();if(args.length!=3)throw new IllegalArgumentException("output-directory addresses context-key");
    Path output=Path.of(args[0]);Files.createDirectories(output);
    String context=args[2];List<String> entries=Files.readAllLines(Path.of(args[1]));
    Map<String,Function> functions=new LinkedHashMap<>();Map<String,String> errors=new HashMap<>();
    // Define all requested entries before decompilation to avoid order-dependent
    // absorption of undefined callees. Never reshape an existing function body.
    int defined=0;
    for(String entry:entries) {
      monitor.checkCancelled();Address a=address(Long.parseUnsignedLong(entry,16));
      try {
        Function f=a==null?null:getFunctionAt(a);
        if(f==null&&a!=null){disassemble(a);f=createFunction(a,"atlas_"+entry);}
        functions.put(entry,f);
        if(f==null)errors.put(entry,"No exact function at requested entry (possibly overlapping body)");
      }catch(Exception e){functions.put(entry,null);errors.put(entry,e.toString());}
      if(++defined%500==0)println("Atlas definitions "+defined+"/"+entries.size());
    }
    DecompInterface decompiler=new DecompInterface();
    if(!decompiler.openProgram(currentProgram))throw new IllegalStateException(decompiler.getLastMessage());
    int done=0,cached=0,failed=0;
    try {
      for(String entry:entries) {
        monitor.checkCancelled();Path destination=output.resolve(entry+".json");
        if(Files.exists(destination)) {
          try {var old=gson.fromJson(Files.readString(destination),com.google.gson.JsonObject.class);
            if(context.equals(old.get("context").getAsString())&&entry.equals(old.get("entry").getAsString())){cached++;done++;continue;}
          }catch(Exception ignored){}
        }
        Function f=functions.get(entry);
        Map<String,Object> result=map("version",1,"context",context,"entry",entry,"function","sub_"+entry,
          "language",currentProgram.getLanguageID().toString(),"decompile_completed",false,"decompiled_c","",
          "decompile_error","","instructions",new ArrayList<>(),"body_ranges",new ArrayList<>());
        List<String> quality=new ArrayList<>();result.put("quality",quality);
        try {
          if(f==null)throw new IllegalStateException(errors.get(entry));
          result.put("ghidra_name",f.getName());result.put("signature",f.getPrototypeString(false,false));
          result.put("thunk",f.isThunk());result.put("external",f.isExternal());
          List<Object> ranges=new ArrayList<>();var rangeIterator=f.getBody().getAddressRanges(true);
          while(rangeIterator.hasNext()){AddressRange r=rangeIterator.next();ranges.add(map("start",hex(r.getMinAddress()),"end",hex(r.getMaxAddress())));}
          result.put("body_ranges",ranges);result.put("body_bytes",f.getBody().getNumAddresses());
          List<Object> instructions=new ArrayList<>();long decoded=0;
          var iterator=currentProgram.getListing().getInstructions(f.getBody(),true);
          while(iterator.hasNext()) {
            Instruction ins=iterator.next();StringBuilder bytes=new StringBuilder();for(byte b:ins.getBytes())bytes.append(String.format("%02X",b&255));
            List<String> flows=new ArrayList<>();for(Address a:ins.getFlows())flows.add(hex(a));
            List<Object> refs=new ArrayList<>();for(Reference ref:ins.getReferencesFrom())
              refs.add(map("target",hex(ref.getToAddress()),"space",ref.getToAddress().getAddressSpace().getName(),
                "type",ref.getReferenceType().toString(),"data",ref.getReferenceType().isData(),
                "call",ref.getReferenceType().isCall(),"operand",ref.getOperandIndex()));
            List<String> ops=new ArrayList<>();for(PcodeOp op:ins.getPcode())ops.add(op.getMnemonic());
            instructions.add(map("address",hex(ins.getAddress()),"bytes",bytes.toString(),"text",ins.toString(),
              "mnemonic",ins.getMnemonicString(),"flow_type",ins.getFlowType().toString(),
              "call",ins.getFlowType().isCall(),"jump",ins.getFlowType().isJump(),"computed",ins.getFlowType().isComputed(),
              "terminal",ins.getFlowType().isTerminal(),"flows",flows,"fallthrough",hex(ins.getFallThrough()),
              "references",refs,"inputs",objects(ins.getInputObjects()),"outputs",objects(ins.getResultObjects()),"pcode_ops",ops));
            decoded+=ins.getLength();
          }
          result.put("instructions",instructions);
          if(decoded!=f.getBody().getNumAddresses())quality.add("incomplete_instruction_coverage");
          var decompiled=decompiler.decompileFunction(f,30,monitor);
          String error=decompiled.getErrorMessage();String code=decompiled.getDecompiledFunction()==null?"":decompiled.getDecompiledFunction().getC();
          result.put("decompiled_c",code);result.put("decompile_error",error==null?"":error);
          String lower=(code+"\n"+error).toLowerCase(Locale.ROOT);
          for(String marker:new String[]{"warning:","halt_baddata","bad instruction","rawunknown","unaff_","unrecovered"})
            if(lower.contains(marker))quality.add(marker);
          boolean complete=decompiled.decompileCompleted()&&!code.isBlank();
          result.put("decompile_completed",complete);
          result.put("decompiler_timeout",decompiled.isTimedOut());
          if(!complete)quality.add("decompilation_incomplete");
        }catch(Exception e){result.put("decompile_error",e.toString());quality.add("export_error");failed++;}
        save(destination,result);done++;
        if(done%100==0)println("Atlas exported "+done+"/"+entries.size()+" cached="+cached+" errors="+failed);
      }
    }finally{decompiler.dispose();}
    save(output.resolve("export-summary.json"),map("context",context,"requested",entries.size(),"processed",done,"cached",cached,"export_errors",failed));
    println("Atlas finished "+done+" functions; cached="+cached+" errors="+failed);
  }
}
