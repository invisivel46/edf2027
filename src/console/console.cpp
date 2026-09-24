// EDF2027 - the in-game console service (console.h): executor, runners, built-in commands.
#include "console.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <SDL3/SDL_filesystem.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include "console_hook.h"
#include "scripted_input_logic.h"

REXCVAR_DEFINE_STRING(edf_exec, "", "EDF2027",
                      "Console script to run at startup (docs/console.md); relative paths are tried against the "
                      "working directory, the executable's folder and its console/ folder");
REXCVAR_DEFINE_STRING(edf_exec_mission, "", "EDF2027",
                      "Console script to run each time a mission starts (the player object appears)");
REXCVAR_DEFINE_BOOL(edf_console_pause, false, "EDF2027",
                    "Pause the simulation while the console is open (game commands then run when it closes)");
REXCVAR_DEFINE_BOOL(edf_console_log, true, "EDF2027",
                    "Write every console command and its output to the log, with its simulation tick");

namespace edf::console {
namespace {
std::vector<EngineTick>& EngineTicks() {
  static std::vector<EngineTick> ticks;
  return ticks;
}
std::string Join(const std::vector<std::string>& words, size_t from) {
  std::string out;
  for (size_t i = from; i < words.size(); ++i) { if (i > from) out += ' '; out += words[i]; }
  return out;
}
// Finds a script: the path as given, then beside the executable and in its console/
// folder, each also with ".cfg" appended.
std::optional<std::filesystem::path> FindScript(std::string_view name) {
  std::vector<std::filesystem::path> bases{std::filesystem::path()};
  if (const char* exe = SDL_GetBasePath()) {
    bases.emplace_back(exe);
    bases.emplace_back(std::filesystem::path(exe) / "console");
  }
  const std::filesystem::path requested{std::string(name)};
  for (const auto& base : bases) {
    for (const auto& candidate : {requested, std::filesystem::path(std::string(name) + ".cfg")}) {
      std::error_code error;
      const auto path = candidate.is_absolute() || base.empty() ? candidate : base / candidate;
      if (std::filesystem::is_regular_file(path, error)) return path;
    }
    if (requested.is_absolute()) break;
  }
  return std::nullopt;
}
}  // namespace

void AddEngineTick(EngineTick tick) { EngineTicks().push_back(std::move(tick)); }

Service& Service::Get() {
  static Service service;
  return service;
}

CvarAccess& Service::Cvars() {
  static CvarAccess access{
      [] { return rex::cvar::ListFlags(); },
      [](std::string_view name) -> std::optional<std::string> {
        const auto* info = rex::cvar::GetFlagInfo(name);
        if (!info || info->type == rex::cvar::FlagType::Command) return std::nullopt;
        return rex::cvar::GetFlagByName(name);
      },
      [](std::string_view name, std::string_view value) { return rex::cvar::SetFlagByName(name, value); },
      [](std::string_view name) -> std::string {
        const auto* info = rex::cvar::GetFlagInfo(name);
        return info ? info->description : std::string();
      }};
  return access;
}

void Service::Initialize(Hooks hooks) {
  if (initialized_) return;
  hooks_ = std::move(hooks);
  {
    std::lock_guard lock(mutex_);
    RegisterBuiltins();
    RegisterGameCommands(registry_);
  }
  initialized_ = true;
  Print(Severity::kInfo, "EDF2027 console. 'help' lists the commands, Tab completes, Up/Down browse the history.");
  if (const auto script = REXCVAR_GET(edf_exec); !script.empty()) ExecFile(script, "edf_exec");
}

uint64_t Service::ScriptTick(uint64_t simulation_tick) const {
  const uint64_t origin = ScriptedClockOrigin().load(std::memory_order_relaxed);
  if (!origin) return simulation_tick;
  return simulation_tick + 1 >= origin ? simulation_tick + 1 - origin : 0;
}

void Service::Print(Severity severity, std::string text) {
  if (REXCVAR_GET(edf_console_log)) {
    if (severity == Severity::kError || severity == Severity::kWarn) REXLOG_WARN("Console: {}", text);
    else REXLOG_INFO("Console: {}", text);
  }
  std::lock_guard lock(output_mutex_);
  output_.push_back({severity, std::move(text)});
  ++output_generation_;
  while (output_.size() > 4000) output_.pop_front();
}

uint64_t Service::CopyOutput(uint64_t since, std::vector<OutputLine>& out, size_t limit) {
  std::lock_guard lock(output_mutex_);
  if (since == output_generation_) return since;
  out.assign(output_.size() > limit ? output_.end() - ptrdiff_t(limit) : output_.begin(), output_.end());
  return output_generation_;
}

void Service::ClearOutput() {
  std::lock_guard lock(output_mutex_);
  output_.clear();
  ++output_generation_;
}

Status Service::status() {
  Status status;
  status.tick = tick_.load(std::memory_order_relaxed);
  status.engine_seen = engine_seen_.load(std::memory_order_relaxed);
  status.in_mission = in_mission_.load(std::memory_order_relaxed);
  status.cheats = cheats_.load(std::memory_order_relaxed);
  std::lock_guard lock(mutex_);
  status.runners = runners_.size();
  for (const auto& runner : runners_) status.queued += runner->queue.size();
  status.queued += at_.size();
  return status;
}

void Service::MarkCheat(std::string_view command) {
  if (cheats_.exchange(true)) return;
  REXLOG_WARN("Console: CHEATS USED from simulation tick {} (first: {}); this run's game state was changed by console "
              "commands",
              tick_.load(std::memory_order_relaxed), command);
}

Completion Service::Complete(std::string_view line) {
  std::lock_guard lock(mutex_);
  return registry_.Complete(line, &Cvars());
}

void Service::Log(const ExecContext& ctx, const std::string& text) {
  if (!REXCVAR_GET(edf_console_log)) return;
  const uint64_t tick = ctx.frame ? ctx.frame->tick : SimulationTicks().load(std::memory_order_relaxed);
  REXLOG_INFO("Console: tick={} script_tick={} thread={} src={} > {}", tick, ScriptTick(tick),
              ctx.engine ? "engine" : "ui", ctx.source.empty() ? "console" : ctx.source, text);
}

bool Service::NeedsEngine(const std::string& line, int depth) {
  Registry::Expanded expanded;
  {
    std::lock_guard lock(mutex_);
    expanded = registry_.Expand(line);
  }
  for (const auto& command : expanded.commands) {
    const auto words = Tokenize(command);
    if (words.empty()) continue;
    const auto name = Lower(words.front());
    if (name == "wait" || name == "waitmission") return true;
    std::lock_guard lock(mutex_);
    if (const auto* cmd = registry_.Find(name); cmd && (cmd->flags & kEngine)) return true;
  }
  (void)depth;
  return false;
}

void Service::Submit(std::string_view line, std::string_view source) {
  const auto trimmed = Trim(line);
  if (trimmed.empty()) return;
  history_.Add(trimmed);
  Print(Severity::kEcho, "] " + std::string(trimmed));
  const std::string text(trimmed);
  if (NeedsEngine(text)) {
    std::lock_guard lock(mutex_);
    Runner* console = nullptr;
    for (auto& runner : runners_)
      if (runner->source == source) console = runner.get();
    if (!console) {
      runners_.push_back(std::make_unique<Runner>());
      console = runners_.back().get();
      console->source = std::string(source);
    }
    for (auto& command : SplitCommands(text)) console->queue.push_back(std::move(command));
    return;
  }
  ExecContext ctx;
  ctx.source = std::string(source);
  for (const auto& command : SplitCommands(text)) Execute(command, ctx);
}

bool Service::ExecFile(std::string_view path, std::string_view source) {
  const auto found = FindScript(path);
  if (!found) {
    Print(Severity::kError, "exec: cannot find script '" + std::string(path) + "'");
    return false;
  }
  std::ifstream file(*found, std::ios::binary);
  const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  if (!file && !file.eof()) {
    Print(Severity::kError, "exec: cannot read '" + found->string() + "'");
    return false;
  }
  auto commands = ParseScript(text);
  Print(Severity::kInfo, "exec: " + found->string() + " (" + std::to_string(commands.size()) + " commands)");
  REXLOG_INFO("Console: exec {} from {} ({} commands)", found->string(), source, commands.size());
  std::lock_guard lock(mutex_);
  auto runner = std::make_unique<Runner>();
  runner->source = std::string(source) + ":" + found->filename().string();
  runner->queue.assign(std::make_move_iterator(commands.begin()), std::make_move_iterator(commands.end()));
  runners_.push_back(std::move(runner));
  return true;
}

void Service::Execute(const std::string& text, ExecContext& ctx, int depth) {
  const auto words = Tokenize(text);
  if (words.empty()) return;
  const auto sink = [this](Severity severity, std::string line) { Print(severity, std::move(line)); };
  const auto name = Lower(words.front());

  // at <time> <command>: schedule on the scripted-pad game clock.
  if (name == "at") {
    const auto at = ParseAt(words);
    if (!at) { Print(Severity::kError, "at: usage: at <time> <command...> (time: ticks, or a number with s or ms)"); return; }
    Log(ctx, text);
    std::lock_guard lock(mutex_);
    at_.emplace(at->tick, std::make_pair(at->command, ctx.source.empty() ? std::string("at") : ctx.source));
    return;
  }

  const Command* command = nullptr;
  std::optional<std::string> alias;
  {
    std::lock_guard lock(mutex_);
    command = registry_.Find(name);
    if (!command)
      if (const auto* found = registry_.Alias(name)) alias = *found;
  }
  if (!command && alias) {
    if (depth >= Registry::kMaxAliasDepth) {
      Print(Severity::kError, "alias '" + words.front() + "' nests too deep (a loop?)");
      return;
    }
    std::string expansion = *alias;
    if (words.size() > 1) expansion += " " + Join(words, 1);
    auto commands = SplitCommands(expansion);
    if (ctx.runner) {
      // Inside a script: in place of the alias, so a wait in it pauses what follows.
      std::lock_guard lock(mutex_);
      ctx.runner->queue.insert(ctx.runner->queue.begin(), commands.begin(), commands.end());
      return;
    }
    for (const auto& sub : commands) Execute(sub, ctx, depth + 1);
    return;
  }
  if (!command) {
    auto& cvars = Cvars();
    if (const auto value = cvars.get(words.front())) {
      Log(ctx, text);
      if (words.size() == 1) {
        const auto description = cvars.describe(words.front());
        Print(Severity::kInfo, words.front() + " = \"" + *value + "\"" + (description.empty() ? "" : "  // " + description));
      } else if (cvars.set(words.front(), Join(words, 1))) {
        Print(Severity::kOk, words.front() + " = \"" + *cvars.get(words.front()) + "\"");
      } else {
        Print(Severity::kError, words.front() + ": value '" + Join(words, 1) + "' rejected");
      }
      return;
    }
    Print(Severity::kError, "unknown command or cvar '" + words.front() + "' (try 'help', or Tab)");
    return;
  }

  std::vector<std::string> args(words.begin() + 1, words.end());
  if (const auto error = ValidateArgs(*command, args); !error.empty()) {
    Print(Severity::kError, command->name + ": " + error);
    return;
  }
  if ((command->flags & kEngine) && !ctx.engine) {
    // Game state is touched on the engine thread only.
    std::lock_guard lock(mutex_);
    Runner* console = nullptr;
    for (auto& runner : runners_)
      if (runner->source == "console") console = runner.get();
    if (!console) {
      runners_.push_back(std::make_unique<Runner>());
      console = runners_.back().get();
      console->source = "console";
    }
    console->queue.push_back(text);
    return;
  }
  if ((command->flags & kMission) && !(ctx.frame && GameInMission(*ctx.frame))) {
    Print(Severity::kError, command->name + ": not in a mission (start or continue a mission first)");
    return;
  }
  if (command->flags & kCheat) MarkCheat(command->name);
  Log(ctx, text);
  Invocation invocation(*command, std::move(args), sink, &ctx);
  try {
    command->handler(invocation);
  } catch (const std::exception& error) {
    Print(Severity::kError, command->name + ": " + error.what());
  }
}

void Service::PumpRunners(ExecContext& base_ctx) {
  const uint64_t tick = base_ctx.frame->tick;
  const bool in_mission = in_mission_.load(std::memory_order_relaxed);
  // "at": due entries, in time order.
  for (;;) {
    std::pair<std::string, std::string> entry;
    {
      std::lock_guard lock(mutex_);
      if (at_.empty() || at_.begin()->first > ScriptTick(tick)) break;
      entry = at_.begin()->second;
      at_.erase(at_.begin());
    }
    ExecContext ctx = base_ctx;
    ctx.source = entry.second;
    Execute(entry.first, ctx);
  }
  for (size_t index = 0;; ++index) {
    Runner* runner = nullptr;
    {
      std::lock_guard lock(mutex_);
      if (index >= runners_.size()) break;
      runner = runners_[index].get();
    }
    for (int budget = 512; budget > 0; --budget) {
      if (runner->wait_until > tick) break;
      if (runner->wait_mission) {
        if (in_mission) {
          runner->wait_mission = false;
          runner->wait_until = tick + runner->mission_settle;
          Print(Severity::kInfo, "waitmission: mission running at tick " + std::to_string(tick) +
                                     (runner->mission_settle ? ", continuing in " + FormatTicks(runner->mission_settle) : ""));
          continue;
        }
        if (runner->mission_deadline && tick >= runner->mission_deadline) {
          runner->wait_mission = false;
          Print(Severity::kWarn, "waitmission: timed out at tick " + std::to_string(tick) + "; continuing");
          continue;
        }
        break;
      }
      std::string command;
      {
        std::lock_guard lock(mutex_);
        if (runner->queue.empty()) break;
        command = std::move(runner->queue.front());
        runner->queue.pop_front();
      }
      ExecContext ctx = base_ctx;
      ctx.runner = runner;
      ctx.source = runner->source;
      Execute(command, ctx);
    }
  }
  std::lock_guard lock(mutex_);
  std::erase_if(runners_, [&](const std::unique_ptr<Runner>& runner) {
    return runner->queue.empty() && runner->wait_until <= tick && !runner->wait_mission;
  });
}

void Service::EngineStepBegin(PPCContext& ctx, uint8_t* base, uint32_t steps) {
  if (!steps || !initialized_) return;
  try {
    engine_thread_ = std::this_thread::get_id();
    EngineFrame frame{&ctx, base, SimulationTicks().load(std::memory_order_relaxed)};
    tick_.store(frame.tick, std::memory_order_relaxed);
    engine_seen_.store(true, std::memory_order_relaxed);
    bool mission = false;
    try { mission = GameInMission(frame); } catch (...) {}
    in_mission_.store(mission, std::memory_order_relaxed);
    if (mission && !was_in_mission_) {
      REXLOG_INFO("Console: mission running (player present) at tick {} script_tick={}", frame.tick, ScriptTick(frame.tick));
      if (const auto script = REXCVAR_GET(edf_exec_mission); !script.empty()) ExecFile(script, "edf_exec_mission");
    }
    was_in_mission_ = mission;
    ExecContext exec;
    exec.engine = true;
    exec.frame = &frame;
    PumpRunners(exec);
    for (auto& tick : EngineTicks()) {
      try { tick(frame); } catch (const std::exception& error) { Print(Severity::kError, error.what()); }
    }
  } catch (const std::exception& error) {
    Print(Severity::kError, std::string("console engine step: ") + error.what());
  } catch (...) {
    Print(Severity::kError, "console engine step: unknown error");
  }
  step_begin_ = std::chrono::steady_clock::now();
  rate_steps_ += steps;
}

void Service::EngineStepEnd() {
  if (step_begin_ == std::chrono::steady_clock::time_point{}) return;
  const auto now = std::chrono::steady_clock::now();
  const double ms = std::chrono::duration<double, std::milli>(now - step_begin_).count();
  step_begin_ = {};
  const double previous = timing_.dispatch_ms_avg.load(std::memory_order_relaxed);
  timing_.dispatch_ms_avg.store(previous > 0 ? previous * 0.95 + ms * 0.05 : ms, std::memory_order_relaxed);
  window_max_ms_ = std::max(window_max_ms_, ms);
  timing_.dispatches.fetch_add(1, std::memory_order_relaxed);
  if (rate_window_ == std::chrono::steady_clock::time_point{}) rate_window_ = now;
  const double elapsed = std::chrono::duration<double>(now - rate_window_).count();
  if (elapsed >= 1.0) {
    timing_.steps_per_second.store(double(rate_steps_) / elapsed, std::memory_order_relaxed);
    timing_.dispatch_ms_max.store(window_max_ms_, std::memory_order_relaxed);
    rate_steps_ = 0;
    window_max_ms_ = 0;
    rate_window_ = now;
  }
}

void EngineStepBegin(PPCContext& ctx, uint8_t* base, uint32_t steps) { Service::Get().EngineStepBegin(ctx, base, steps); }
void EngineStepEnd() { Service::Get().EngineStepEnd(); }

// ---- Built-in commands -----------------------------------------------------------------
void Service::RegisterBuiltins() {
  auto& r = registry_;
  const auto host = [](Invocation& in) -> ExecContext& { return *static_cast<ExecContext*>(in.host()); };
  const auto command_names = [this] {
    std::vector<std::string> names;
    for (const auto* command : registry_.List()) names.push_back(command->name);
    return names;
  };
  const auto cvar_names = [] { return rex::cvar::ListFlags(); };

  r.Add({.name = "help", .usage = "help [command]", .help = "List the commands, or describe one",
         .args = {{.name = "command", .optional = true, .choices = command_names}},
         .handler = [this](Invocation& in) {
           if (in.has(0)) {
             const Command* command = nullptr;
             {
               std::lock_guard lock(mutex_);
               command = registry_.Find(in.str(0));
             }
             if (!command) {
               const auto value = Cvars().get(in.str(0));
               if (value) in.Print(in.str(0) + " (cvar) = \"" + *value + "\"  // " + Cvars().describe(in.str(0)));
               else in.Error("help: no command '" + in.str(0) + "'");
               return;
             }
             in.Print(UsageOf(*command));
             in.Print("  " + command->help);
             std::string notes;
             if (command->flags & kEngine) notes += " runs on the engine thread at the next simulation step;";
             if (command->flags & kMission) notes += " needs a mission in progress;";
             if (command->flags & kCheat) notes += " cheat (marks the run);";
             if (!notes.empty()) in.Print("  " + notes.substr(1));
             return;
           }
           std::vector<const Command*> commands;
           {
             std::lock_guard lock(mutex_);
             commands = registry_.List();
           }
           in.Print("Commands (help <command> for details; any cvar works as 'name' or 'name value'):");
           for (const auto* command : commands) {
             std::string usage = UsageOf(*command);
             if (usage.size() < 44) usage.resize(44, ' ');
             in.Print("  " + usage + " " + command->help + ((command->flags & kCheat) ? "  [cheat]" : ""));
           }
         }});
  r.Add({.name = "echo", .usage = "echo <text...>", .help = "Print text to the console and the log",
         .args = {{.name = "text", .optional = true}}, .max_args = 4096,
         .handler = [](Invocation& in) { in.Print(in.rest(0)); }});
  r.Add({.name = "mark", .usage = "mark <label...>", .help = "Write a marker line with the tick to the log (for reports)",
         .args = {{.name = "label"}}, .max_args = 4096,
         .handler = [this](Invocation& in) {
           const uint64_t tick = SimulationTicks().load(std::memory_order_relaxed);
           REXLOG_INFO("Console: mark '{}' tick={} script_tick={}", in.rest(0), tick, ScriptTick(tick));
           in.Ok("mark '" + in.rest(0) + "' at tick " + std::to_string(tick));
         }});
  r.Add({.name = "exec", .usage = "exec <file>", .help = "Run a console script (one command per line; 'wait' paces it)",
         .args = {{.name = "file"}},
         .handler = [this, host](Invocation& in) {
           auto& ctx = host(in);
           if (!ctx.runner) { ExecFile(in.str(0), ctx.source.empty() ? "exec" : ctx.source); return; }
           // From a script: in place, so the parent continues after the child.
           const auto found = FindScript(in.str(0));
           if (!found) { in.Error("exec: cannot find script '" + in.str(0) + "'"); return; }
           if (++ctx.runner->execs > 64) { in.Error("exec: more than 64 nested execs in one script (a loop?)"); return; }
           std::ifstream file(*found, std::ios::binary);
           const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
           auto commands = ParseScript(text);
           in.Print("exec: " + found->string() + " (" + std::to_string(commands.size()) + " commands)");
           std::lock_guard lock(mutex_);
           ctx.runner->queue.insert(ctx.runner->queue.begin(), commands.begin(), commands.end());
         }});
  r.Add({.name = "wait", .usage = "wait <ticks | Ns | Nms>", .help = "Pause this script for simulation ticks (60 per second)",
         .args = {{.name = "duration", .type = ArgType::Duration, .max = 60.0 * 3600}},
         .handler = [host](Invocation& in) {
           auto& ctx = host(in);
           if (!ctx.runner || !ctx.frame) { in.Error("wait: only in a script or a line (wait 60; spawn ant)"); return; }
           ctx.runner->wait_until = ctx.frame->tick + *ParseDuration(in.str(0));
         }});
  r.Add({.name = "waitmission", .usage = "waitmission [timeout] [settle]",
         .help = "Pause this script until a mission is running (player present), then `settle` more (default 2s)",
         .args = {{.name = "timeout", .type = ArgType::Duration, .optional = true},
                  {.name = "settle", .type = ArgType::Duration, .optional = true}},
         .handler = [host](Invocation& in) {
           auto& ctx = host(in);
           if (!ctx.runner || !ctx.frame) { in.Error("waitmission: only in a script"); return; }
           ctx.runner->wait_mission = true;
           const uint64_t timeout = in.has(0) ? *ParseDuration(in.str(0)) : 0;
           ctx.runner->mission_deadline = timeout ? ctx.frame->tick + timeout : 0;
           ctx.runner->mission_settle = uint32_t(in.has(1) ? *ParseDuration(in.str(1)) : 120);
         }});
  r.Add({.name = "at", .usage = "at <time> <command...>",
         .help = "Run a command at a scripted-pad game-clock time (ticks, Ns or Nms, as input scripts)",
         .args = {{.name = "time", .type = ArgType::Duration}, {.name = "command"}}, .max_args = 4096,
         .handler = [](Invocation& in) { in.Error("at: handled by the executor"); }});
  r.Add({.name = "alias", .usage = "alias [name] [command line...]",
         .help = "List aliases, show one, or define one (\"alias go \\\"spawn ant 50; wait 5s; killall\\\"\")",
         .args = {{.name = "name", .optional = true}, {.name = "text", .optional = true}}, .max_args = 4096,
         .handler = [this](Invocation& in) {
           std::lock_guard lock(mutex_);
           if (!in.has(0)) {
             if (registry_.aliases().empty()) in.Print("no aliases");
             for (const auto& [name, text] : registry_.aliases()) in.Print("  " + name + " = \"" + text + "\"");
             return;
           }
           if (!in.has(1)) {
             const auto* text = registry_.Alias(in.str(0));
             if (text) in.Print(in.str(0) + " = \"" + *text + "\"");
             else in.Error("alias: no alias '" + in.str(0) + "'");
             return;
           }
           if (!registry_.SetAlias(in.str(0), in.rest(1))) in.Error("alias: '" + in.str(0) + "' is a command");
           else in.Ok("alias " + in.str(0) + " = \"" + in.rest(1) + "\"");
         }});
  r.Add({.name = "unalias", .usage = "unalias <name>", .help = "Remove an alias", .args = {{.name = "name"}},
         .handler = [this](Invocation& in) {
           std::lock_guard lock(mutex_);
           if (!registry_.Alias(in.str(0))) { in.Error("unalias: no alias '" + in.str(0) + "'"); return; }
           registry_.SetAlias(in.str(0), "");
           in.Ok("removed alias " + in.str(0));
         }});
  r.Add({.name = "set", .usage = "set <cvar> <value...>", .help = "Set a cvar (same as 'name value')",
         .args = {{.name = "cvar", .choices = cvar_names}, {.name = "value"}}, .max_args = 4096,
         .handler = [this](Invocation& in) {
           auto& cvars = Cvars();
           if (!cvars.get(in.str(0))) { in.Error("set: no cvar '" + in.str(0) + "'"); return; }
           if (!cvars.set(in.str(0), in.rest(1))) { in.Error("set: " + in.str(0) + " rejected '" + in.rest(1) + "'"); return; }
           in.Ok(in.str(0) + " = \"" + *cvars.get(in.str(0)) + "\"");
         }});
  r.Add({.name = "get", .usage = "get <cvar>", .help = "Print a cvar (same as 'name')",
         .args = {{.name = "cvar", .choices = cvar_names}},
         .handler = [this](Invocation& in) {
           const auto value = Cvars().get(in.str(0));
           if (!value) { in.Error("get: no cvar '" + in.str(0) + "'"); return; }
           in.Print(in.str(0) + " = \"" + *value + "\"  // " + Cvars().describe(in.str(0)));
         }});
  r.Add({.name = "reset", .usage = "reset <cvar>", .help = "Reset a cvar to its default",
         .args = {{.name = "cvar", .choices = cvar_names}},
         .handler = [this](Invocation& in) {
           if (!Cvars().get(in.str(0))) { in.Error("reset: no cvar '" + in.str(0) + "'"); return; }
           rex::cvar::ResetToDefault(in.str(0));
           in.Ok(in.str(0) + " = \"" + *Cvars().get(in.str(0)) + "\" (default)");
         }});
  r.Add({.name = "cvarlist", .usage = "cvarlist [filter]", .help = "List cvars whose name contains the filter",
         .args = {{.name = "filter", .optional = true}},
         .handler = [this](Invocation& in) {
           auto names = rex::cvar::ListFlags();
           std::sort(names.begin(), names.end());
           size_t shown = 0;
           for (const auto& name : names) {
             if (in.has(0) && Lower(name).find(Lower(in.str(0))) == std::string::npos) continue;
             const auto value = Cvars().get(name);
             if (!value) continue;
             in.Print("  " + name + " = \"" + *value + "\"");
             ++shown;
           }
           in.Print(std::to_string(shown) + " cvars");
         }});
  r.Add({.name = "clear", .usage = "clear", .help = "Clear the console scrollback",
         .handler = [this](Invocation&) { ClearOutput(); }});
  r.Add({.name = "cheats", .usage = "cheats", .help = "Say whether this run used game-changing console commands",
         .handler = [this](Invocation& in) {
           in.Print(cheats_used() ? "cheats USED this run (see the 'CHEATS USED' log line)" : "no cheats used this run");
         }});
  r.Add({.name = "runners", .usage = "runners", .help = "List running scripts and scheduled 'at' commands",
         .handler = [this](Invocation& in) {
           std::lock_guard lock(mutex_);
           for (const auto& runner : runners_)
             in.Print("  " + runner->source + ": " + std::to_string(runner->queue.size()) + " queued" +
                      (runner->wait_mission ? ", waiting for a mission" : "") +
                      (runner->wait_until > tick_ ? ", waiting until tick " + std::to_string(runner->wait_until) : ""));
           for (const auto& [tick, entry] : at_) in.Print("  at " + std::to_string(tick) + ": " + entry.first);
           if (runners_.empty() && at_.empty()) in.Print("nothing running");
         }});
  r.Add({.name = "stop", .usage = "stop", .help = "Stop every running script and scheduled 'at' command",
         .handler = [this, host](Invocation& in) {
           auto& ctx = host(in);
           std::lock_guard lock(mutex_);
           size_t stopped = 0;
           for (auto& runner : runners_) {
             if (ctx.runner == runner.get()) continue;
             stopped += runner->queue.size();
             runner->queue.clear();
             runner->wait_mission = false;
             runner->wait_until = 0;
           }
           stopped += at_.size();
           at_.clear();
           in.Ok("stopped " + std::to_string(stopped) + " pending commands");
         }});
  r.Add({.name = "console", .usage = "console <open|close|toggle>",
         .help = "Show or hide this window (for scripts: a screenshot with the console down)",
         .args = {{.name = "state", .type = ArgType::Choice,
                   .choices = [] { return std::vector<std::string>{"open", "close", "toggle"}; }}},
         .handler = [this](Invocation& in) {
           const auto state = Lower(in.str(0));
           if (hooks_.show) hooks_.show(state == "open" ? 1 : state == "close" ? 0 : -1);
         }});
  r.Add({.name = "quit", .usage = "quit", .help = "Close the game (end of an unattended run)",
         .handler = [this](Invocation& in) {
           in.Print("quit");
           REXLOG_INFO("Console: quit at tick {}", SimulationTicks().load(std::memory_order_relaxed));
           if (hooks_.quit) hooks_.quit();
         }});
}

}  // namespace edf::console
