// Read-only typed scalar facts for bounded offline review.
// The export is evidence, not a semantic-completeness claim.
// @category EDF2027

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.stream.JsonWriter;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.address.AddressRangeIterator;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.pcode.PcodeOp;
import ghidra.program.model.pcode.Varnode;

import java.io.BufferedWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

public class RendererScalarFacts extends GhidraScript {
    private static final int DECOMPILE_TIMEOUT_SECONDS = 45;
    private Map<Long, Function> functionsByLowAddress;

    private static final class BodyRangeFact {
        String start;
        String end;
    }

    private static final class NodeFact {
        String space;
        String offset;
        int size;
    }

    private static final class PcodeFact {
        String opcode;
        List<NodeFact> inputs = new ArrayList<>();
        NodeFact output;
    }

    private static final class InstructionFact {
        String address;
        String bytes;
        String mnemonic;
        String flow_type;
        List<String> flows = new ArrayList<>();
        List<PcodeFact> pcode = new ArrayList<>();
    }

    private static final class HighPcodeFact {
        String address;
        String opcode;
        List<NodeFact> inputs = new ArrayList<>();
        NodeFact output;
    }

    private static final class FunctionFact {
        String function;
        String entry;
        List<BodyRangeFact> body_ranges = new ArrayList<>();
        List<InstructionFact> instructions = new ArrayList<>();
        List<HighPcodeFact> high_pcode = new ArrayList<>();
        boolean decompile_completed;
        String decompile_error = "";
        List<String> quality = new ArrayList<>();
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2) {
            throw new IllegalArgumentException(
                "RendererScalarFacts requires output JSON and address-list paths");
        }

        Path output = Path.of(args[0]).toAbsolutePath().normalize();
        Path addressList = Path.of(args[1]).toAbsolutePath().normalize();
        Path parent = output.getParent();
        if (parent != null) Files.createDirectories(parent);
        List<String> entries = readEntries(addressList);
        Path temporary = output.resolveSibling(output.getFileName().toString() + ".tmp");
        Gson gson = new GsonBuilder().disableHtmlEscaping().create();
        DecompInterface decompiler = new DecompInterface();
        int functionCount = 0;
        long instructionCount = 0;

        try {
            if (!decompiler.openProgram(currentProgram)) {
                throw new IllegalStateException("Unable to open decompiler: " + decompiler.getLastMessage());
            }
            try (BufferedWriter stream = Files.newBufferedWriter(
                    temporary, StandardCharsets.UTF_8);
                 JsonWriter json = new JsonWriter(stream)) {
                json.setIndent("  ");
                json.beginObject();
                json.name("version").value(1);
                json.name("language").value(currentProgram.getLanguageID().toString());
                json.name("functions").beginArray();
                for (String entry : entries) {
                    monitor.checkCancelled();
                    FunctionFact fact = exportFunction(entry, decompiler);
                    gson.toJson(fact, FunctionFact.class, json);
                    functionCount++;
                    instructionCount += fact.instructions.size();
                    if ((functionCount % 100) == 0) {
                        println("Exported " + functionCount + " / " + entries.size() + " functions");
                    }
                }
                json.endArray();
                json.endObject();
            }
            try {
                Files.move(temporary, output, StandardCopyOption.REPLACE_EXISTING,
                    StandardCopyOption.ATOMIC_MOVE);
            }
            catch (java.nio.file.AtomicMoveNotSupportedException unsupported) {
                Files.move(temporary, output, StandardCopyOption.REPLACE_EXISTING);
            }
        }
        finally {
            decompiler.dispose();
            Files.deleteIfExists(temporary);
        }
        println("Renderer scalar facts: " + functionCount + " functions, " +
            instructionCount + " raw instructions -> " + output);
    }

    private List<String> readEntries(Path path) throws Exception {
        Set<String> unique = new LinkedHashSet<>();
        for (String source : Files.readAllLines(path, StandardCharsets.UTF_8)) {
            String value = source.trim();
            if (value.isEmpty() || value.startsWith("#")) continue;
            if (value.regionMatches(true, 0, "sub_", 0, 4)) value = value.substring(4);
            if (value.regionMatches(true, 0, "0x", 0, 2)) value = value.substring(2);
            if (!value.matches("(?i)[0-9a-f]+")) {
                throw new IllegalArgumentException("Invalid function address in " + path + ": " + source);
            }
            unique.add(value.toUpperCase(Locale.ROOT));
        }
        if (unique.isEmpty()) throw new IllegalArgumentException("Address list is empty: " + path);
        return new ArrayList<>(unique);
    }

    private FunctionFact exportFunction(String requestedEntry, DecompInterface decompiler) {
        FunctionFact fact = new FunctionFact();
        long offset = Long.parseUnsignedLong(requestedEntry, 16);
        fact.function = "sub_" + requestedEntry;
        fact.entry = requestedEntry;
        fact.quality.add("raw_evidence_only_no_semantic_completion_claim");
        Address address = resolveAddress(offset);
        Function function = resolveFunction(offset);
        if (function == null && address != null) {
            // The established renderer inventory was produced from transient
            // definitions in a -readOnly headless process. Recreate only each
            // explicitly requested root; the project cannot be saved.
            disassemble(address);
            function = createFunction(address, "scalar_fact_" + requestedEntry);
            if (function != null) {
                addQuality(fact, "transient_read_only_function_definition");
            }
        }
        if (function == null) {
            fact.decompile_completed = false;
            fact.decompile_error = "No function defined at requested entry";
            fact.quality.add("missing_function");
            return fact;
        }

        fact.entry = hex(function.getEntryPoint());
        fact.function = "sub_" + fact.entry;
        boolean transientDefinition = fact.quality.contains("transient_read_only_function_definition");
        AddressRangeIterator ranges = function.getBody().getAddressRanges(true);
        while (ranges.hasNext()) {
            AddressRange range = ranges.next();
            if (transientDefinition && !range.contains(function.getEntryPoint())) {
                // Preserve Ghidra's complete body membership, including an
                // absorbed undefined tail target, while making the boundary
                // discrepancy explicit for downstream quarantine.
                addQuality(fact, "body_membership_discrepancy:additional_range=" +
                    hex(range.getMinAddress()) + "-" + hex(range.getMaxAddress()));
            }
            BodyRangeFact bodyRange = new BodyRangeFact();
            bodyRange.start = hex(range.getMinAddress());
            bodyRange.end = hex(range.getMaxAddress());
            fact.body_ranges.add(bodyRange);
        }

        long decodedBytes = 0;
        InstructionIterator instructions = currentProgram.getListing().getInstructions(function.getBody(), true);
        while (instructions.hasNext()) {
            Instruction instruction = instructions.next();
            InstructionFact instructionFact = new InstructionFact();
            instructionFact.address = hex(instruction.getAddress());
            instructionFact.mnemonic = instruction.getMnemonicString();
            instructionFact.flow_type = instruction.getFlowType().toString();
            try {
                instructionFact.bytes = bytes(instruction.getBytes());
            }
            catch (Exception error) {
                instructionFact.bytes = "";
                addQuality(fact, "instruction_bytes_unavailable:" + instructionFact.address);
            }
            decodedBytes += instruction.getLength();
            for (Address flow : instruction.getFlows()) instructionFact.flows.add(hex(flow));

            PcodeOp[] rawOps = instruction.getPcode();
            if (rawOps == null || rawOps.length == 0) {
                addQuality(fact, "instruction_without_raw_pcode:" + instructionFact.address);
            }
            else {
                for (PcodeOp operation : rawOps) {
                    PcodeFact operationFact = new PcodeFact();
                    operationFact.opcode = opcode(operation.getOpcode());
                    if (operationFact.opcode.toLowerCase(Locale.ROOT).contains("unknown")) {
                        addQuality(fact, "rawunknown_pcode:" + instructionFact.address);
                    }
                    for (int i = 0; i < operation.getNumInputs(); i++) {
                        operationFact.inputs.add(node(operation.getInput(i)));
                    }
                    operationFact.output = node(operation.getOutput());
                    instructionFact.pcode.add(operationFact);
                }
            }
            String rawText = (instructionFact.mnemonic + " " + instruction).toLowerCase(Locale.ROOT);
            if (rawText.contains("bad instruction") || rawText.contains("baddata")) {
                addQuality(fact, "bad_instruction:" + instructionFact.address);
            }
            if (rawText.contains("rawunknown")) {
                addQuality(fact, "rawunknown_instruction:" + instructionFact.address);
            }
            fact.instructions.add(instructionFact);
        }
        if (decodedBytes != function.getBody().getNumAddresses()) {
            addQuality(fact, "incomplete_instruction_coverage:decoded=" + decodedBytes +
                ",body=" + function.getBody().getNumAddresses());
        }

        DecompileResults result = decompiler.decompileFunction(
            function, DECOMPILE_TIMEOUT_SECONDS, monitor);
        boolean completed = result.decompileCompleted();
        String error = result.getErrorMessage() == null ? "" : result.getErrorMessage();
        String decompiled = "";
        if (completed && result.getDecompiledFunction() != null) {
            decompiled = result.getDecompiledFunction().getC();
        }
        String diagnostic = (error + "\n" + decompiled).toLowerCase(Locale.ROOT);
        List<String> incompleteReasons = new ArrayList<>();
        if (diagnostic.contains("halt_baddata")) {
            addQuality(fact, "decompiler_halt_baddata");
            incompleteReasons.add("decompiler output contains halt_baddata");
        }
        if (diagnostic.contains("bad instruction")) {
            addQuality(fact, "decompiler_bad_instruction");
            incompleteReasons.add("decompiler output reports bad instruction data");
        }
        if (diagnostic.contains("rawunknown")) {
            addQuality(fact, "decompiler_rawunknown");
            incompleteReasons.add("decompiler output contains rawunknown");
        }

        if (completed && result.getHighFunction() != null) {
            var operations = result.getHighFunction().getPcodeOps();
            while (operations.hasNext()) {
                PcodeOp operation = operations.next();
                HighPcodeFact operationFact = new HighPcodeFact();
                operationFact.address = hex(operation.getSeqnum().getTarget());
                operationFact.opcode = opcode(operation.getOpcode());
                if (operationFact.opcode.toLowerCase(Locale.ROOT).contains("unknown")) {
                    addQuality(fact, "high_pcode_rawunknown:" + operationFact.address);
                    incompleteReasons.add("high p-code contains unknown opcode");
                }
                for (int i = 0; i < operation.getNumInputs(); i++) {
                    operationFact.inputs.add(node(operation.getInput(i)));
                }
                operationFact.output = node(operation.getOutput());
                fact.high_pcode.add(operationFact);
            }
        }
        else if (completed) {
            completed = false;
            incompleteReasons.add("decompiler returned no high function");
            addQuality(fact, "missing_high_pcode");
        }

        for (String quality : fact.quality) {
            if (quality.startsWith("bad_instruction:") ||
                quality.startsWith("rawunknown_") ||
                quality.startsWith("incomplete_instruction_coverage:")) {
                completed = false;
            }
        }
        if (!incompleteReasons.isEmpty()) completed = false;
        fact.decompile_completed = completed;
        if (!completed) {
            if (!error.isBlank()) incompleteReasons.add(0, error.trim());
            if (incompleteReasons.isEmpty()) incompleteReasons.add("Ghidra decompilation did not complete");
            fact.decompile_error = String.join("; ", new LinkedHashSet<>(incompleteReasons));
            addQuality(fact, "incomplete");
        }
        return fact;
    }

    private Function resolveFunction(long offset) {
        // Imported console images can place code in a non-default memory space.
        // Resolve by unsigned offset across memory spaces without creating or
        // changing any program object.
        for (AddressSpace space : currentProgram.getAddressFactory().getAddressSpaces()) {
            if (!space.isMemorySpace()) continue;
            try {
                Function function = getFunctionAt(space.getAddress(offset));
                if (function != null) return function;
            }
            catch (RuntimeException ignored) {
                // The offset is outside this space; try the next memory space.
            }
        }
        // PowerPC:BE:64:64-32addr stores 32-bit guest addresses sign-extended
        // in the program database. Index the low 32 bits so an inventory entry
        // such as 82134EB8 resolves to the database entry FFFFFFFF82134EB8.
        if (functionsByLowAddress == null) {
            functionsByLowAddress = new HashMap<>();
            FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext()) {
                Function function = functions.next();
                functionsByLowAddress.putIfAbsent(
                    function.getEntryPoint().getOffset() & 0xffffffffL, function);
            }
            println("Indexed " + functionsByLowAddress.size() + " defined function entries");
        }
        return functionsByLowAddress.get(offset & 0xffffffffL);
    }

    private Address resolveAddress(long offset) {
        long[] candidates = { offset, offset | 0xffffffff00000000L };
        for (AddressSpace space : currentProgram.getAddressFactory().getAddressSpaces()) {
            if (!space.isMemorySpace()) continue;
            for (long candidateOffset : candidates) {
                try {
                    Address candidate = space.getAddress(candidateOffset);
                    if (currentProgram.getMemory().contains(candidate)) return candidate;
                }
                catch (RuntimeException ignored) {
                    // Candidate is outside this address space.
                }
            }
        }
        return null;
    }

    private void addQuality(FunctionFact fact, String quality) {
        if (!fact.quality.contains(quality)) fact.quality.add(quality);
    }

    private NodeFact node(Varnode value) {
        if (value == null) return null;
        NodeFact fact = new NodeFact();
        fact.space = value.getAddress().getAddressSpace().getName();
        fact.offset = unsignedHex(value.getOffset());
        fact.size = value.getSize();
        return fact;
    }

    private String opcode(int value) {
        try {
            return PcodeOp.getMnemonic(value);
        }
        catch (RuntimeException error) {
            return "OP_" + value;
        }
    }

    private String hex(Address address) {
        String text = address.toString();
        int separator = text.lastIndexOf(':');
        if (separator >= 0) text = text.substring(separator + 1);
        return text.toUpperCase(Locale.ROOT);
    }

    private String unsignedHex(long value) {
        return Long.toUnsignedString(value, 16).toUpperCase(Locale.ROOT);
    }

    private String bytes(byte[] values) {
        StringBuilder result = new StringBuilder(values.length * 2);
        for (byte value : values) result.append(String.format(Locale.ROOT, "%02X", value & 0xff));
        return result.toString();
    }
}
