// The in-game console's command language (src/console/console_logic.h): tokenizer,
// scripts, durations, argument validation, aliases, completion, history and "at".
#include "console/console_logic.h"

#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {
int failures = 0;
void Check(bool condition, const char* expression, int line) {
  if (!condition) {
    std::cerr << "line " << line << ": CHECK(" << expression << ") failed\n";
    ++failures;
  }
}
#define CHECK(value) Check(static_cast<bool>(value), #value, __LINE__)
using Words = std::vector<std::string>;
using namespace edf::console;

void TestSplitAndTokenize() {
  CHECK(SplitCommands("spawn ant 10; wait 60 ;killall") == Words({"spawn ant 10", "wait 60", "killall"}));
  CHECK(SplitCommands("echo \"a; b\"; echo c") == Words({"echo \"a; b\"", "echo c"}));
  CHECK(SplitCommands("  ; ;  ").empty());
  CHECK(SplitCommands("spawn ant // the rest is a comment; killall") == Words({"spawn ant"}));
  CHECK(SplitCommands("# whole line comment").empty());
  CHECK(SplitCommands("echo a#b") == Words({"echo a#b"}));  // '#' only starts a comment at a word start
  CHECK(SplitCommands("echo http://x") == Words({"echo http://x"}));
  CHECK(Tokenize("spawn  ant\t10") == Words({"spawn", "ant", "10"}));
  CHECK(Tokenize("echo \"hello world\" x") == Words({"echo", "hello world", "x"}));
  CHECK(Tokenize("echo \"say \\\"hi\\\"\"") == Words({"echo", "say \"hi\""}));
  CHECK(Tokenize("echo \"unterminated text") == Words({"echo", "unterminated text"}));
  CHECK(Tokenize("echo \"\"") == Words({"echo", ""}));
  CHECK(Tokenize("   ").empty());
}

void TestScripts() {
  const auto commands = ParseScript("# stress\r\nwaitmission\n\nspawn ant 200 40; wait 10s\r\n// done\nkillall\n");
  CHECK(commands == Words({"waitmission", "spawn ant 200 40", "wait 10s", "killall"}));
  CHECK(ParseScript("").empty());
  CHECK(ParseScript("echo last line without newline") == Words({"echo last line without newline"}));
}

void TestNumbers() {
  CHECK(ParseInt("42") == 42);
  CHECK(ParseInt("-7") == -7);
  CHECK(ParseInt("+3") == 3);
  CHECK(!ParseInt("4.5"));
  CHECK(!ParseInt("12abc"));
  CHECK(!ParseInt(""));
  CHECK(ParseFloat("1.5") == 1.5);
  CHECK(ParseFloat("-70.0f") == -70.0);  // the disc's .bat scripts write floats this way
  CHECK(ParseFloat("1e3") == 1000.0);
  CHECK(!ParseFloat("nan"));
  CHECK(!ParseFloat("inf"));
  CHECK(!ParseFloat("f"));
  CHECK(ParseBool("on") == true);
  CHECK(ParseBool("OFF") == false);
  CHECK(ParseBool("1") == true);
  CHECK(!ParseBool("maybe"));
  CHECK(ParseDuration("600") == 600u);
  CHECK(ParseDuration("600t") == 600u);
  CHECK(ParseDuration("10s") == 600u);
  CHECK(ParseDuration("1.5s") == 90u);
  CHECK(ParseDuration("1000ms") == 60u);
  CHECK(ParseDuration("10ms") == 1u);   // rounds up: a wait never ends early
  CHECK(ParseDuration("0") == 0u);
  CHECK(!ParseDuration("-1"));
  CHECK(!ParseDuration("soon"));
  CHECK(!ParseDuration("5m"));
}

Command MakeSpawn() {
  Command spawn;
  spawn.name = "spawn";
  spawn.usage = "spawn <type> [count] [distance | x y z]";
  spawn.args = {{.name = "type", .type = ArgType::Choice, .choices = [] { return Words{"ant", "redant", "spider"}; }},
                {.name = "count", .type = ArgType::Int, .optional = true, .min = 1, .max = 2000},
                {.name = "distance|x", .type = ArgType::Float, .optional = true},
                {.name = "y", .type = ArgType::Float, .optional = true},
                {.name = "z", .type = ArgType::Float, .optional = true}};
  spawn.validate = [](const Words& args) -> std::string {
    return args.size() == 4 ? "give a distance, or all three of x y z" : "";
  };
  return spawn;
}

void TestValidation() {
  const auto spawn = MakeSpawn();
  CHECK(ValidateArgs(spawn, {"ant"}).empty());
  CHECK(ValidateArgs(spawn, {"ANT", "10", "40"}).empty());      // choices are case-insensitive
  CHECK(ValidateArgs(spawn, {"ant", "5", "1", "2", "3"}).empty());
  CHECK(!ValidateArgs(spawn, {}).empty());                       // missing type
  CHECK(!ValidateArgs(spawn, {"dragon"}).empty());               // unknown choice
  CHECK(ValidateArgs(spawn, {"dragon"}).find("ant, redant, spider") != std::string::npos);
  CHECK(!ValidateArgs(spawn, {"ant", "ten"}).empty());           // not a number
  CHECK(!ValidateArgs(spawn, {"ant", "0"}).empty());             // below range
  CHECK(!ValidateArgs(spawn, {"ant", "5000"}).empty());          // above range
  CHECK(!ValidateArgs(spawn, {"ant", "1.5"}).empty());           // count is a whole number
  CHECK(!ValidateArgs(spawn, {"ant", "5", "1", "2"}).empty());   // the custom check: x y without z
  CHECK(!ValidateArgs(spawn, {"ant", "5", "1", "2", "3", "4"}).empty());  // too many
  Command wait{.name = "wait", .args = {{.name = "duration", .type = ArgType::Duration, .max = 3600}}};
  CHECK(ValidateArgs(wait, {"2s"}).empty());
  CHECK(!ValidateArgs(wait, {"2 minutes"}).empty());
  CHECK(!ValidateArgs(wait, {"4000"}).empty());
  Command god{.name = "god", .args = {{.name = "state", .type = ArgType::Bool, .optional = true}}};
  CHECK(ValidateArgs(god, {}).empty());
  CHECK(ValidateArgs(god, {"on"}).empty());
  CHECK(!ValidateArgs(god, {"sometimes"}).empty());
  Command echo{.name = "echo", .args = {{.name = "text", .optional = true}}, .max_args = 4096};
  CHECK(ValidateArgs(echo, {"a", "b", "c"}).empty());
  CHECK(UsageOf(god) == "god [state]");
  CHECK(UsageOf(spawn) == "spawn <type> [count] [distance | x y z]");
}

void TestRegistryAndAliases() {
  Registry registry;
  int ran = 0;
  registry.Add(MakeSpawn());
  registry.Add({.name = "killall", .help = "kill", .handler = [&](Invocation&) { ++ran; }});
  registry.Add({.name = "wait", .args = {{.name = "duration", .type = ArgType::Duration}}});
  CHECK(registry.Find("SPAWN") != nullptr);
  CHECK(registry.Find("nothing") == nullptr);
  CHECK(registry.List().size() == 3);
  CHECK(registry.SetAlias("swarm", "spawn ant 100; wait 5s"));
  CHECK(!registry.SetAlias("spawn", "echo no"));  // commands are not shadowed
  CHECK(registry.Alias("SWARM") && *registry.Alias("swarm") == "spawn ant 100; wait 5s");
  auto expanded = registry.Expand("swarm; killall");
  CHECK(expanded.error.empty());
  CHECK(expanded.commands == Words({"spawn ant 100", "wait 5s", "killall"}));
  // Words after an alias are appended to its last command.
  CHECK(registry.SetAlias("bugs", "spawn"));
  CHECK(registry.Expand("bugs spider 3").commands == Words({"spawn spider 3"}));
  // Nested aliases expand; a loop is reported, not followed forever.
  CHECK(registry.SetAlias("outer", "swarm; killall"));
  CHECK(registry.Expand("outer").commands == Words({"spawn ant 100", "wait 5s", "killall"}));
  CHECK(registry.SetAlias("loop", "loop"));
  CHECK(!registry.Expand("loop").error.empty());
  CHECK(registry.SetAlias("swarm", ""));  // removes
  CHECK(registry.Alias("swarm") == nullptr);
  // Invocation accessors and output.
  std::vector<std::pair<Severity, std::string>> output;
  const auto* spawn = registry.Find("spawn");
  Invocation in(*spawn, {"ant", "12", "35.5"}, [&](Severity s, std::string t) { output.push_back({s, t}); });
  CHECK(in.integer(1) == 12);
  CHECK(in.number(2) == 35.5);
  CHECK(in.integer(7, -1) == -1);
  CHECK(in.rest(1) == "12 35.5");
  in.Error("bad");
  CHECK(output.size() == 1 && output[0].first == Severity::kError && output[0].second == "bad");
  (void)ran;
}

void TestCompletion() {
  Registry registry;
  registry.Add(MakeSpawn());
  registry.Add({.name = "stats"});
  registry.Add({.name = "set", .args = {{.name = "cvar", .choices = [] { return Words{"edf_fps_cap", "edf_show_fps"}; }},
                                        {.name = "value"}}});
  registry.SetAlias("stress", "spawn ant 500");
  std::map<std::string, std::string> cvars{{"edf_fps_cap", "0"}, {"edf_show_fps", "false"}, {"audio_mute", "false"}};
  CvarAccess access{[&] { Words names; for (auto& [k, v] : cvars) names.push_back(k); return names; },
                    [&](std::string_view name) -> std::optional<std::string> {
                      auto it = cvars.find(std::string(name));
                      return it == cvars.end() ? std::nullopt : std::optional<std::string>(it->second);
                    },
                    [&](std::string_view, std::string_view) { return true; },
                    [](std::string_view) { return std::string(); }};
  // First word: commands, aliases and cvars.
  auto c = registry.Complete("sp", &access);
  CHECK(c.candidates == Words({"spawn"}));
  CHECK(Registry::ApplyCompletion("sp", c) == "spawn ");
  c = registry.Complete("st", &access);
  CHECK(c.candidates == Words({"stats", "stress"}));
  CHECK(c.common == "st");
  CHECK(Registry::ApplyCompletion("st", c) == "st");  // ambiguous: the line stays, the list shows
  c = registry.Complete("edf_", &access);
  CHECK(c.candidates == Words({"edf_fps_cap", "edf_show_fps"}));
  // Arguments: the spec's choices.
  c = registry.Complete("spawn r", &access);
  CHECK(c.candidates == Words({"redant"}));
  CHECK(Registry::ApplyCompletion("spawn r", c) == "spawn redant ");
  c = registry.Complete("spawn ", &access);
  CHECK(c.candidates.size() == 3);
  c = registry.Complete("set edf_f", &access);
  CHECK(c.candidates == Words({"edf_fps_cap"}));
  // Only the last command of a line; the cursor word keeps what came before.
  c = registry.Complete("stats; spawn sp", &access);
  CHECK(Registry::ApplyCompletion("stats; spawn sp", c) == "stats; spawn spider ");
  // A cvar's first argument offers its current value.
  c = registry.Complete("edf_fps_cap ", &access);
  CHECK(c.candidates == Words({"0"}));
  c = registry.Complete("zzz", &access);
  CHECK(c.candidates.empty());
  CHECK(Registry::ApplyCompletion("zzz", c) == "zzz");
}

void TestHistory() {
  History history(3);
  CHECK(!history.Older());
  history.Add("a");
  history.Add("b");
  history.Add("b");  // consecutive duplicate
  history.Add("  ");
  history.Add("c");
  history.Add("d");  // drops "a"
  CHECK(history.entries() == Words({"b", "c", "d"}));
  CHECK(history.Older() == "d");
  CHECK(history.Older() == "c");
  CHECK(history.Older() == "b");
  CHECK(history.Older() == "b");  // stays at the oldest
  CHECK(history.Newer() == "c");
  CHECK(history.Newer() == "d");
  CHECK(history.Newer() == "");   // past the newest: an empty line
  CHECK(!history.Newer());
}

void TestAt() {
  auto at = ParseAt(Tokenize("at 600 spawn ant 200 40"));
  CHECK(at && at->tick == 600 && at->command == "spawn ant 200 40");
  at = ParseAt(Tokenize("at 15000ms destroy radius 100"));
  CHECK(at && at->tick == 900 && at->command == "destroy radius 100");
  at = ParseAt(Tokenize("at 20s echo \"two words\""));
  CHECK(at && at->tick == 1200 && at->command == "echo \"two words\"");
  CHECK(!ParseAt(Tokenize("at soon spawn ant")));
  CHECK(!ParseAt(Tokenize("at 600")));
  CHECK(FormatTicks(90) == "90 (1.50 s)");
}
}  // namespace

int main() {
  TestSplitAndTokenize();
  TestScripts();
  TestNumbers();
  TestValidation();
  TestRegistryAndAliases();
  TestCompletion();
  TestHistory();
  TestAt();
  if (failures) {
    std::cerr << failures << " console check(s) failed\n";
    return 1;
  }
  std::cout << "console tests passed\n";
  return 0;
}
