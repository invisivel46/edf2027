// Bounded retry of timed-out C output, retaining original raw facts and errors.
// @category EDF2027
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.Function;
import java.nio.file.*;
import java.util.*;
public class RendererAtlasRetry extends GhidraScript {
  private Address resolve(String entry){
    long low=Long.parseUnsignedLong(entry,16);
    for(AddressSpace s:currentProgram.getAddressFactory().getAddressSpaces())if(s.isMemorySpace())
      for(long n:new long[]{low,low|0xffffffff00000000L})try{Address a=s.getAddress(n);if(currentProgram.getMemory().contains(a))return a;}catch(Exception ignored){}
    return null;
  }
  public void run()throws Exception{
    String[] args=getScriptArgs();Path out=Path.of(args[0]);Gson gson=new Gson();
    List<String> entries=Files.readAllLines(Path.of(args[1]));Map<String,Function> functions=new HashMap<>();
    for(String entry:entries){Address a=resolve(entry);Function f=a==null?null:getFunctionAt(a);if(a!=null&&f==null){disassemble(a);f=createFunction(a,"atlas_"+entry);}functions.put(entry,f);}
    DecompInterface d=new DecompInterface();d.openProgram(currentProgram);
    try{for(String entry:entries){
      Path path=out.resolve(entry+".json");JsonObject fact=gson.fromJson(Files.readString(path),JsonObject.class);
      if(!fact.has("decompiler_timeout")||!fact.get("decompiler_timeout").getAsBoolean())continue;
      println("Atlas retry "+entry+" timeout=120");
      var result=d.decompileFunction(functions.get(entry),120,monitor);
      JsonObject retry=new JsonObject();retry.addProperty("timeout_seconds",120);retry.addProperty("previous_error",fact.get("decompile_error").getAsString());
      retry.addProperty("completed",result.decompileCompleted());retry.addProperty("error",result.getErrorMessage());
      fact.add("retry",retry);
      if(result.decompileCompleted()&&result.getDecompiledFunction()!=null){
        String code=result.getDecompiledFunction().getC();fact.addProperty("decompiled_c",code);
        fact.addProperty("decompile_completed",true);fact.addProperty("decompiler_timeout",false);
        fact.addProperty("decompile_error",result.getErrorMessage());
        JsonArray quality=new JsonArray();for(JsonElement q:fact.getAsJsonArray("quality"))if(!q.getAsString().equals("decompilation_incomplete"))quality.add(q);
        quality.add("completed_after_timeout_retry");String lower=code.toLowerCase(Locale.ROOT);
        for(String marker:new String[]{"warning:","halt_baddata","bad instruction","rawunknown","unaff_","unrecovered"})if(lower.contains(marker))quality.add(marker);
        fact.add("quality",quality);
      }
      Path temp=out.resolve(entry+".retry.tmp");Files.writeString(temp,gson.toJson(fact));Files.move(temp,path,StandardCopyOption.REPLACE_EXISTING);
      println("Atlas retry "+entry+" completed="+result.decompileCompleted());
    }}finally{d.dispose();}
  }
}
