// Targeted geometry writer inspection. Does not certify complete alias coverage.
// @category EDF2027
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.data.VoidDataType;
import ghidra.program.model.data.UnsignedIntegerDataType;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.ParameterImpl;
import ghidra.program.model.listing.VariableStorage;
import ghidra.program.model.pcode.PcodeOp;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.HashSet;
import java.util.Locale;

public class GeometryWriterTrace extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("Expected output directory");
        Path output = Path.of(args[0]);
        Files.createDirectories(output);
        // These are __savegprlr entry points in generated/default/edf2017_init.cpp,
        // not ABI calls returning a replacement r3. Inline their actual stores
        // so the decompiler preserves the incoming argument registers.
        long[] saveHelpers = {0x821E7F40L, 0x821E7F50L, 0x821E7F5CL, 0x821E7F60L, 0x821E7F64L, 0x821E7F68L, 0x821E7F70L,
            0x821E7F44L, 0x821E7F48L, 0x821E7F4CL, 0x821E7F54L,
            0x821E7F58L, 0x821E7F6CL, 0x821E7F74L, 0x821E7F78L, 0x821E7F7CL};
        for (long helper : saveHelpers) {
            Address address = toAddr(helper);
            disassemble(address);
            Function function = getFunctionAt(address);
            if (function == null) function = createFunction(address, "savegprlr_" + Long.toHexString(helper));
            if (function == null) throw new IllegalStateException("Cannot model save helper " + address);
            function.setInline(true);
        }
        long[] seeds = {0x8214E640L, 0x8214E2C8L, 0x8214E1C8L,
            0x821B3B08L, 0x821B3788L, 0x821B36F8L, 0x821D7420L, 0x821D75B8L,
            0x821B3250L, 0x821B3698L, 0x821B3628L, 0x821B34E0L, 0x821B35B8L,
            0x820AB448L, 0x821B2A88L,
            0x821B3210L, 0x821B3320L, 0x821B3400L,
            0x821CA310L, 0x821CA378L, 0x821D8770L, 0x821CA7A8L,
            0x821CA850L, 0x821CA950L, 0x821CB328L, 0x821CA1C8L, 0x821CA0D0L,
            0x821D8740L, 0x821CB010L, 0x821CAD10L,
            0x821409A0L, 0x82140E98L, 0x821FD428L, 0x821FD958L,
            0x821FDF50L, 0x821FE358L, 0x8213C868L, 0x8213CB30L, 0x8213CF60L,
            0x821D7530L, 0x821D76A8L, 0x821D7468L, 0x821D75F8L,
            0x82134958L, 0x821349B8L, 0x82134A78L, 0x82134AD8L, 0x82134640L,
            0x8242D440L, 0x8242D2B0L, 0x821B3C98L, 0x822D01C0L,
            0x821D4700L, 0x821FAC48L, 0x821A0550L, 0x82412DF8L,
            0x821D3DC8L, 0x821D4490L, 0x82134408L,
            0x821D3748L, 0x821D3928L, 0x821D3B68L, 0x821D4090L, 0x821D4380L,
            0x821D3FF0L, 0x8212FC28L, 0x821D3698L, 0x821D3EB0L,
            0x821B2C28L, 0x821D96D8L, 0x821D88C0L, 0x821D9600L,
            0x82149248L, 0x821B94E8L,
            0x8242E868L, 0x8242EB78L, 0x8242D138L, 0x82139930L,
            0x82412AE0L, 0x82412EA8L, 0x824109F0L, 0x82410830L, 0x82410068L,
            0x824100F8L, 0x82410450L, 0x824105E8L,
            0x8242B4B8L, 0x82418620L, 0x82422D90L, 0x823F4288L, 0x8240CC30L,
            0x8240FB38L, 0x8240FC20L, 0x8240FAE0L, 0x82422E50L, 0x82422F90L,
            0x82423E48L, 0x82420CE8L, 0x824211C0L, 0x82423B38L, 0x82420F88L,
            0x824217F0L, 0x824245F0L, 0x82423090L, 0x82423108L, 0x823E6270L, 0x823E6298L,
            0x82424648L, 0x82132F38L, 0x8212FB50L, 0x82131A48L, 0x82131498L,
            0x82130EF0L, 0x82130638L, 0x8212FFA0L, 0x82130D88L,
            0x824332C0L, 0x82432E48L, 0x82432A80L, 0x82432818L, 0x8243DF20L,
            0x82433800L, 0x8242C7B8L, 0x82432688L, 0x824324F8L, 0x82432608L,
            0x8242C2E8L, 0x82430460L, 0x8242FC50L,
            0x82430800L, 0x8242F388L, 0x8242FA60L, 0x82430C10L,
            0x8242F818L, 0x8242F310L, 0x824302D8L, 0x8242F188L,
            0x824303D0L, 0x8242F288L, 0x82430FC0L, 0x8242FC40L,
            0x82436420L, 0x82437D18L, 0x82440698L, 0x82440490L,
            0x82438B38L, 0x82443408L, 0x82442810L, 0x824410E0L,
            0x82441F70L, 0x82440A18L, 0x82443BF0L, 0x82440B58L,
            0x82444210L, 0x82441350L, 0x82442F18L, 0x824429C0L,
            0x82442338L, 0x82440D28L, 0x82441C38L, 0x82440EE8L, 0x8243E6D0L,
            0x82437DD8L, 0x824391C0L, 0x8244C670L, 0x824454D0L, 0x8244BCB8L,
            0x82445618L, 0x824604C0L, 0x824604C8L, 0x82460238L, 0x8245FC30L,
            0x82460098L, 0x824604D8L, 0x824607D8L, 0x82460958L,
            0x8245FE78L, 0x8245FFD0L, 0x8245FD78L, 0x824601A8L,
            0x8245FBD8L, 0x82460668L, 0x8245FF80L,
            0x82460B28L, 0x82462C38L, 0x82463258L, 0x824668E8L,
            0x8246AD20L, 0x8246C0D8L, 0x8246C7F8L, 0x8246C3C0L,
            0x8245C420L, 0x82444B80L, 0x82444C10L, 0x82445B98L, 0x82466E88L,
            0x8246CD78L, 0x82448370L, 0x82448878L, 0x82448A28L,
            0x82448C08L, 0x82449648L, 0x8245A948L, 0x8245A320L, 0x8245AE18L,
            0x8245A898L, 0x82444C90L, 0x8245EA18L, 0x82445138L, 0x82444880L,
            0x8246BBA0L, 0x8246BE40L, 0x82444938L, 0x8218B030L, 0x8218B2A8L,
            0x8218BF90L, 0x8218C440L, 0x8218BE40L, 0x8218BBA0L,
            0x820A3BD0L, 0x820B2510L, 0x8218B8F8L, 0x8218B868L,
            0x820B24A8L, 0x821E8230L, 0x821B98D8L, 0x821B7000L,
            0x821B7948L, 0x821BA368L, 0x821B6FB0L, 0x821B7E88L,
            0x821BAFD0L, 0x821BAE98L, 0x821C88A0L, 0x820A0420L,
            0x821B8DD0L, 0x820BBB78L, 0x821BB7A8L, 0x821C8A20L,
            0x821BB668L, 0x821B8090L, 0x821D9430L, 0x821D79D8L,
            0x820E0B00L, 0x8210C3B8L, 0x821C8AD0L, 0x8212E520L,
            0x820BC3D0L, 0x820E7130L, 0x820F82D8L, 0x821C8B90L,
            0x821D5020L, 0x821DAF08L, 0x821D5120L, 0x820B7110L,
            0x821DA5E8L, 0x821D9F38L, 0x821DABA0L, 0x821D9820L,
            0x820B7B00L, 0x821DA1D0L, 0x820B75E0L, 0x821DAA08L,
            0x820B7CC0L, 0x820B7050L, 0x820B7ED0L, 0x821D7038L,
            0x821C8980L, 0x821C87A0L, 0x8219EBD0L, 0x821DA0B8L,
            0x821D7110L, 0x821C8808L, 0x820B7AA8L, 0x820B7FC0L,
            0x821DB008L, 0x821D9BD0L, 0x820B77A0L, 0x821CB550L,
            0x821DA8A0L, 0x821CB6A0L, 0x821C9BD0L, 0x821AB708L,
            0x821D6448L, 0x821D6AE0L, 0x821D63D0L, 0x821D5B50L,
            0x821D5CB0L, 0x821D6A08L, 0x821A0EB0L, 0x8219F940L,
            0x8219F9E0L, 0x820B25B8L, 0x820B2550L, 0x82125D28L,
            0x821D5650L, 0x821D56A8L, 0x821D5548L, 0x82125CA8L,
            0x821D6328L, 0x821D6928L, 0x821D6658L, 0x821D5BC0L,
            0x821D6148L, 0x821D65F0L, 0x821D63F0L, 0x821D60D0L,
            0x821D5A40L, 0x821AA130L, 0x821D5C38L,
            0x8242CEE0L, 0x8242D100L, 0x8242E678L, 0x82433B58L,
            0x824338F8L, 0x8242E7E0L, 0x8242EF88L, 0x8242D0B0L,
            0x82431840L, 0x8242D048L, 0x82433978L, 0x82431150L,
            0x82431DD0L, 0x82431390L, 0x82431480L, 0x824318F0L,
            0x824319A8L, 0x82431A60L, 0x82431AF8L, 0x82431B50L,
            0x824317B0L, 0x824316C0L, 0x824312A0L,
            0x8243AD48L, 0x8243B710L, 0x8243A810L,
            0x8243C4E0L, 0x8243D148L, 0x8243D6A0L, 0x824398D8L,
            0x82439B48L, 0x82439AA8L, 0x82439B08L,
            0x824323D8L, 0x82432180L, 0x824396C8L,
            0x82439CD0L, 0x82432240L, 0x82432398L, 0x824323B0L,
            0x823F7CC0L, 0x82432428L, 0x824321C0L, 0x82432168L,
            0x82432158L, 0x82432220L, 0x82432278L, 0x82432308L,
            0x82432080L, 0x8243DF88L, 0x824323C0L, 0x82432178L,
            0x8242E978L, 0x8243B120L, 0x8243B110L, 0x8242CFF8L,
            0x82439DE0L, 0x8243A000L, 0x8243A168L, 0x8243B1F0L,
            0x82431E68L, 0x8243BCF8L, 0x8243A0B0L,
            0x8243D018L, 0x8243B688L, 0x82439978L,
            0x82431BF0L, 0x82431C08L, 0x8243B2F0L, 0x8243B300L,
            0x8243B310L, 0x8243B320L, 0x82431CB8L, 0x82431D80L,
            0x82439FE8L, 0x820DB210L, 0x820DB1D0L, 0x820FD350L,
            0x82103A60L, 0x821E2A00L, 0x822CFDC0L, 0x822CFE58L, 0x82134190L,
            0x82134840L, 0x82134AE8L, 0x821395C8L, 0x821D3EA0L,
            0x821375C0L, 0x82141440L, 0x82141AB8L, 0x82137410L,
            0x8213C328L, 0x8213C120L, 0x8213C928L, 0x821415A8L, 0x82141340L, 0x82141500L,
            0x8213CC20L, 0x8213CDC0L,
            0x821394D8L, 0x82139508L, 0x82139688L, 0x82145620L,
            0x8213BC48L, 0x8213BCE0L, 0x8214E5B8L};
        for (long seed : seeds) {
            monitor.checkCancelled();
            Address address = toAddr(seed);
            disassemble(address);
            if (getFunctionAt(address) == null) createFunction(address, "sub_" + Long.toHexString(seed));
        }
        // Verified against sub_8242C7B8 in generated/default/edf2017_recomp.80.cpp
        // and the image's four absolute switch entries at 8242C810..8242C81F.
        // Default flow discovery misses switch arms. Keep the inline table out
        // of the function body rather than decoding its pointers as instructions.
        for (long arm : new long[]{0x8242CBCCL, 0x8242C820L, 0x8242CA20L, 0x8242CAC0L}) {
            disassemble(toAddr(arm));
        }
        AddressSet factoryBody = new AddressSet(toAddr(0x8242C7B8L), toAddr(0x8242C80FL));
        factoryBody.add(toAddr(0x8242C820L), toAddr(0x8242CE7FL));
        getFunctionAt(toAddr(0x8242C7B8L)).setBody(factoryBody);
        // Parser dispatcher: raw table 82439248..82439257 agrees with the
        // generated switch in edf2017_recomp.2.cpp; include both missing arms.
        disassemble(toAddr(0x82439258L));
        disassemble(toAddr(0x82439308L));
        AddressSet parserBody = new AddressSet(toAddr(0x824391C0L), toAddr(0x82439247L));
        parserBody.add(toAddr(0x82439258L), toAddr(0x82439377L));
        getFunctionAt(toAddr(0x824391C0L)).setBody(parserBody);
        // Producer switch, verified against the raw four-entry table and
        // generated/default/edf2017_recomp.0.cpp. Exclude only inline data.
        for (long arm : new long[]{0x82438E2CL, 0x82438ECCL, 0x82438EECL, 0x82438F84L}) {
            disassemble(toAddr(arm));
        }
        AddressSet producerBody = new AddressSet(toAddr(0x82438B38L), toAddr(0x82438DF7L));
        producerBody.add(toAddr(0x82438E08L), toAddr(0x8243917FL));
        getFunctionAt(toAddr(0x82438B38L)).setBody(producerBody);
        // Callback registration: four absolute switch entries A050..A05F
        // match generated/default/edf2017_recomp.68.cpp. Include all stores.
        for (long arm : new long[]{0x8243A060L, 0x8243A06CL, 0x8243A078L}) {
            disassemble(toAddr(arm));
        }
        AddressSet callbackBody = new AddressSet(toAddr(0x8243A000L), toAddr(0x8243A04FL));
        callbackBody.add(toAddr(0x8243A060L), toAddr(0x8243A0ABL));
        getFunctionAt(toAddr(0x8243A000L)).setBody(callbackBody);
        // Raw aligned pointer candidates complement decoded direct references.
        // Unclassified bytes are not necessarily data/vtables. Runtime-created
        // pointers and computed/unaligned encodings remain outside this scan.
        HashSet<Long> targets = new HashSet<>();
        for (long seed : seeds) targets.add(seed);
        try (PrintWriter pointers = new PrintWriter(Files.newBufferedWriter(
                output.resolve("function-pointer-candidates.csv"), StandardCharsets.UTF_8))) {
            pointers.println("Location,Target,ListingClass");
            for (var block : currentProgram.getMemory().getBlocks()) {
                if (!block.isInitialized()) continue;
                if (block.getSize() > Integer.MAX_VALUE) throw new IllegalStateException("Block too large");
                byte[] bytes = new byte[(int)block.getSize()];
                currentProgram.getMemory().getBytes(block.getStart(), bytes);
                ByteBuffer words = ByteBuffer.wrap(bytes).order(ByteOrder.BIG_ENDIAN);
                int first = (int)((4 - (block.getStart().getOffset() & 3)) & 3);
                for (int offset = first; offset + 4 <= bytes.length; offset += 4) {
                    if ((offset & 0xffff) == 0) monitor.checkCancelled();
                    long value = Integer.toUnsignedLong(words.getInt(offset));
                    if (!targets.contains(value)) continue;
                    Address location = block.getStart().add(offset);
                    String kind = currentProgram.getListing().getInstructionContaining(location) != null
                        ? "decoded_instruction" : "data_or_unclassified";
                    pointers.printf("%s,%08x,%s%n", location, value, kind);
                }
            }
        }
        // Core lock: 82134630 moves the payload address into r3. Both wrappers
        // preserve that result through their epilogues. Guest pointers are 32-bit.
        for (long lock : new long[]{0x82134408L, 0x82134958L, 0x82134A78L}) {
            Function function = getFunctionAt(toAddr(lock));
            function.setCallingConvention("__stdcall");
            int count = lock == 0x82134408L ? 9 : 4;
            Parameter[] parameters = new Parameter[count];
            for (int i = 0; i < count; i++) {
                // Xbox's ninth integer argument is at caller SP+0x54, not
                // the stock compiler specification's SP+8.
                Address storage = i < 8
                    ? currentProgram.getRegister("r" + (i + 3)).getAddress().add(4)
                    : currentProgram.getAddressFactory().getStackSpace().getAddress(0x54);
                parameters[i] = new ParameterImpl("arg" + (i + 1),
                    UnsignedIntegerDataType.dataType,
                    new VariableStorage(currentProgram, storage, 4), currentProgram);
            }
            function.replaceParameters(Function.FunctionUpdateType.CUSTOM_STORAGE, true,
                SourceType.USER_DEFINED, parameters);
            function.setReturnType(
                new PointerDataType(VoidDataType.dataType, 4), SourceType.USER_DEFINED);
        }
        // Checked move wrapper: r3=destination, r4=capacity, r5=source,
        // r6=bytes. Verified against generated/default/edf2017_recomp.47.cpp:
        // 821E82B8 checks capacity; 821E82CC/D0 shuffle r6/r5 into memmove
        // r5/r4 before calling 821EA320. Inferred signatures dropped operands
        // in vector resize/erase callers, obscuring destination provenance.
        Function checkedMove = getFunctionAt(toAddr(0x821E8230L));
        checkedMove.setCallingConvention("__stdcall");
        String[] moveNames = {"destination", "capacity", "source", "bytes"};
        Parameter[] moveParameters = new Parameter[4];
        for (int i = 0; i < moveParameters.length; i++) {
            Address storage = currentProgram.getRegister("r" + (i + 3)).getAddress().add(4);
            moveParameters[i] = new ParameterImpl(moveNames[i],
                UnsignedIntegerDataType.dataType,
                new VariableStorage(currentProgram, storage, 4), currentProgram);
        }
        checkedMove.replaceParameters(Function.FunctionUpdateType.CUSTOM_STORAGE, true,
            SourceType.USER_DEFINED, moveParameters);
        checkedMove.setReturnType(UnsignedIntegerDataType.dataType, SourceType.USER_DEFINED);
        DecompInterface decompiler = new DecompInterface();
        try {
            if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException(decompiler.getLastMessage());
            for (long seed : seeds) {
                monitor.checkCancelled();
                Address address = toAddr(seed);
                Function function = getFunctionAt(address);
                try (PrintWriter writer = new PrintWriter(Files.newBufferedWriter(
                        output.resolve(Long.toHexString(seed) + ".txt"), StandardCharsets.UTF_8))) {
                    writer.println("Language: " + currentProgram.getLanguageID());
                    writer.println("Target: " + address);
                    writer.println("References are limited to analysis performed; absence is not proof.");
                    for (Reference ref : getReferencesTo(address)) writer.println("XREF " + ref);
                    if (function == null) { writer.println("ERROR: no function"); continue; }
                    writer.println("Body: " + function.getBody());
                    DecompileResults result = decompiler.decompileFunction(function, 30, monitor);
                    if (result.decompileCompleted()) {
                        String decompiled = result.getDecompiledFunction().getC();
                        // Ghidra can report completion while truncating at an
                        // undecodable import/VMX instruction. Never silently
                        // treat such output as a closed writer frontier.
                        String normalizedC = decompiled.toLowerCase(Locale.ROOT);
                        if (normalizedC.contains("halt_baddata") || normalizedC.contains("bad instruction")) {
                            writer.println("INCOMPLETE: decompilation contains undecodable control flow");
                            printerr("Incomplete control flow at target " + address);
                        }
                        writer.println(decompiled);
                        writer.println("PCODE store/call frontier (not an alias proof):");
                        var operations = result.getHighFunction().getPcodeOps();
                        while (operations.hasNext()) {
                            var operation = operations.next();
                            int opcode = operation.getOpcode();
                            if (opcode == PcodeOp.STORE || opcode == PcodeOp.CALL || opcode == PcodeOp.CALLIND)
                                writer.println(operation.getSeqnum().getTarget() + " " + operation);
                        }
                    }
                    else writer.println("DECOMPILE ERROR: " + result.getErrorMessage());
                    InstructionIterator instructions = currentProgram.getListing().getInstructions(function.getBody(), true);
                    while (instructions.hasNext()) {
                        var instruction = instructions.next();
                        writer.println(instruction.getAddress() + " " + instruction);
                    }
                }
                println("Exported " + address);
            }
        } finally { decompiler.dispose(); }
    }
}
