// EDF2027 - the in-game console's command language: tokenizer, command registry, argument
// validation, aliases, tab completion and script parsing.
//
// No SDK, ImGui or platform headers, so all of it is unit tested (tests/unit_tests.cpp).
// The console service (console.cpp) owns one Registry, fills it once at startup, and runs
// commands through it on the UI thread (immediate commands) or the engine thread (commands
// that touch game state; see console.h).
//
// The language, Quake style:
//   line      := command (';' command)*        a ';' inside "quotes" does not split
//   command   := word arg*                      words separated by blanks
//   arg       := word | "quoted text"           \" and \\ escape inside quotes
//   comments  := '//' or '#' at the start of a word ends the line
// A command word is looked up as a registered command, then as an alias, then as a cvar
// ("name" prints it, "name value" sets it). Names are case-insensitive.
#pragma once

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace edf::console {

inline std::string Lower(std::string_view text) {
  std::string out(text);
  for (auto& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
  return out;
}
inline bool StartsWithNoCase(std::string_view text, std::string_view prefix) {
  if (prefix.size() > text.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(text[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
      return false;
  return true;
}
inline std::string_view Trim(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
  return text;
}

// ---- Tokenizer -------------------------------------------------------------------------
// Splits a line into its commands at ';' (outside quotes), drops comments, trims each
// command and leaves out empty ones.
inline std::vector<std::string> SplitCommands(std::string_view line) {
  std::vector<std::string> commands;
  std::string current;
  bool quoted = false;
  bool word_start = true;
  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (quoted) {
      current += c;
      if (c == '\\' && i + 1 < line.size()) { current += line[++i]; continue; }
      if (c == '"') quoted = false;
      continue;
    }
    if (word_start && (c == '#' || (c == '/' && i + 1 < line.size() && line[i + 1] == '/'))) break;
    if (c == '"') { quoted = true; current += c; word_start = false; continue; }
    if (c == ';') {
      const auto trimmed = Trim(current);
      if (!trimmed.empty()) commands.emplace_back(trimmed);
      current.clear();
      word_start = true;
      continue;
    }
    word_start = std::isspace(static_cast<unsigned char>(c)) != 0;
    current += c;
  }
  const auto trimmed = Trim(current);
  if (!trimmed.empty()) commands.emplace_back(trimmed);
  return commands;
}

// Splits one command into words. Quotes group (and are removed); an unterminated quote
// runs to the end of the command.
inline std::vector<std::string> Tokenize(std::string_view command) {
  std::vector<std::string> words;
  size_t i = 0;
  while (i < command.size()) {
    while (i < command.size() && std::isspace(static_cast<unsigned char>(command[i]))) ++i;
    if (i >= command.size()) break;
    std::string word;
    if (command[i] == '"') {
      ++i;
      while (i < command.size() && command[i] != '"') {
        if (command[i] == '\\' && i + 1 < command.size()) ++i;
        word += command[i++];
      }
      if (i < command.size()) ++i;  // closing quote
    } else {
      while (i < command.size() && !std::isspace(static_cast<unsigned char>(command[i]))) word += command[i++];
    }
    words.push_back(std::move(word));
  }
  return words;
}

// A script file: one command per line (lines may hold several, split at ';').
inline std::vector<std::string> ParseScript(std::string_view text) {
  std::vector<std::string> commands;
  size_t begin = 0;
  while (begin <= text.size()) {
    size_t end = text.find('\n', begin);
    if (end == std::string_view::npos) end = text.size();
    for (auto& command : SplitCommands(text.substr(begin, end - begin))) commands.push_back(std::move(command));
    begin = end + 1;
  }
  return commands;
}

// ---- Numbers and durations -------------------------------------------------------------
inline std::optional<int64_t> ParseInt(std::string_view text) {
  text = Trim(text);
  if (text.empty()) return std::nullopt;
  if (text.front() == '+') text.remove_prefix(1);
  int64_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc() || end != text.data() + text.size()) return std::nullopt;
  return value;
}
inline std::optional<double> ParseFloat(std::string_view text) {
  text = Trim(text);
  if (text.empty()) return std::nullopt;
  if (text.back() == 'f' || text.back() == 'F') text.remove_suffix(1);  // "1.0f", as the disc's .bat scripts write
  if (!text.empty() && text.front() == '+') text.remove_prefix(1);
  if (text.empty()) return std::nullopt;
  double value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc() || end != text.data() + text.size() || !std::isfinite(value)) return std::nullopt;
  return value;
}
inline std::optional<bool> ParseBool(std::string_view text) {
  const auto value = Lower(Trim(text));
  if (value == "1" || value == "on" || value == "true" || value == "yes") return true;
  if (value == "0" || value == "off" || value == "false" || value == "no") return false;
  return std::nullopt;
}

// Simulation ticks run at 60 Hz. A duration is "<n>" or "<n>t" (ticks), "<n>s" (seconds)
// or "<n>ms" (milliseconds, the unit of the scripted-pad input files). Fractions of a tick
// round up, so a wait never ends early.
inline constexpr uint64_t kTicksPerSecond = 60;
inline std::optional<uint64_t> ParseDuration(std::string_view text) {
  text = Trim(text);
  double scale = 1.0;  // ticks per unit
  if (text.size() > 2 && Lower(text.substr(text.size() - 2)) == "ms") { scale = 60.0 / 1000.0; text.remove_suffix(2); }
  else if (!text.empty() && (text.back() == 's' || text.back() == 'S')) { scale = 60.0; text.remove_suffix(1); }
  else if (!text.empty() && (text.back() == 't' || text.back() == 'T')) text.remove_suffix(1);
  const auto value = ParseFloat(text);
  if (!value || *value < 0 || *value * scale > 1e12) return std::nullopt;
  return uint64_t(std::ceil(*value * scale - 1e-9));
}

// ---- Commands --------------------------------------------------------------------------
enum class ArgType { Int, Float, String, Choice, Bool, Duration };
struct ArgSpec {
  std::string name;
  ArgType type = ArgType::String;
  bool optional = false;
  double min = -std::numeric_limits<double>::infinity();
  double max = std::numeric_limits<double>::infinity();
  // Choice: the accepted values (also the completion list); any other type may offer
  // completion suggestions here without restricting the value.
  std::function<std::vector<std::string>()> choices;
};

enum CommandFlags : uint32_t {
  kNone = 0,
  kEngine = 1u << 0,    // runs on the engine thread at the start of a simulation step
  kCheat = 1u << 1,     // changes the game: marks the run as "cheats used"
  kMission = 1u << 2,   // needs a mission in progress (a player object)
};

class Invocation;
struct Command {
  std::string name;
  std::string usage;        // e.g. "spawn <type> [count] [distance | x y z]"; defaults to name
  std::string help;         // one line
  std::vector<ArgSpec> args;
  uint32_t flags = kNone;
  size_t max_args = SIZE_MAX;  // SIZE_MAX: args.size(); more words than this are an error
  // Extra checks after the per-argument ones; returns an error message or "".
  std::function<std::string(const std::vector<std::string>& args)> validate;
  std::function<void(Invocation&)> handler;
};

// What a command handler sees: its arguments (validated), a place to write output, and an
// opaque host context (console.cpp passes the engine/UI context there).
enum class Severity { kInfo, kOk, kWarn, kError, kEcho };
using OutputSink = std::function<void(Severity, std::string)>;
class Invocation {
 public:
  Invocation(const Command& command, std::vector<std::string> args, OutputSink sink, void* host = nullptr)
      : command_(command), args_(std::move(args)), sink_(std::move(sink)), host_(host) {}
  const Command& command() const { return command_; }
  const std::vector<std::string>& args() const { return args_; }
  size_t count() const { return args_.size(); }
  bool has(size_t i) const { return i < args_.size(); }
  const std::string& str(size_t i) const { static const std::string empty; return i < args_.size() ? args_[i] : empty; }
  int64_t integer(size_t i, int64_t fallback = 0) const {
    return has(i) ? ParseInt(args_[i]).value_or(fallback) : fallback;
  }
  double number(size_t i, double fallback = 0) const {
    return has(i) ? ParseFloat(args_[i]).value_or(fallback) : fallback;
  }
  // The words from i on, joined by single spaces.
  std::string rest(size_t i) const {
    std::string out;
    for (size_t k = i; k < args_.size(); ++k) { if (!out.empty()) out += ' '; out += args_[k]; }
    return out;
  }
  void* host() const { return host_; }
  void Print(std::string text, Severity severity = Severity::kInfo) const { if (sink_) sink_(severity, std::move(text)); }
  void Ok(std::string text) const { Print(std::move(text), Severity::kOk); }
  void Warn(std::string text) const { Print(std::move(text), Severity::kWarn); }
  void Error(std::string text) const { Print(std::move(text), Severity::kError); }

 private:
  const Command& command_;
  std::vector<std::string> args_;
  OutputSink sink_;
  void* host_;
};

inline std::string UsageOf(const Command& command) {
  if (!command.usage.empty()) return command.usage;
  std::string usage = command.name;
  for (const auto& arg : command.args) usage += arg.optional ? " [" + arg.name + "]" : " <" + arg.name + ">";
  return usage;
}

// Checks the argument words against the command's spec. "" when they are acceptable.
inline std::string ValidateArgs(const Command& command, const std::vector<std::string>& args) {
  size_t required = 0;
  for (const auto& arg : command.args) required += arg.optional ? 0 : 1;
  const size_t maximum = command.max_args == SIZE_MAX ? command.args.size() : command.max_args;
  if (args.size() < required) return "missing argument; usage: " + UsageOf(command);
  if (args.size() > maximum) return "too many arguments; usage: " + UsageOf(command);
  for (size_t i = 0; i < args.size() && i < command.args.size(); ++i) {
    const auto& spec = command.args[i];
    const auto& word = args[i];
    const auto range_error = [&](double value) -> std::string {
      if (value < spec.min || value > spec.max) {
        auto bound = [](double v) {
          if (std::isinf(v)) return std::string(v < 0 ? "-inf" : "inf");
          std::string text = std::to_string(v);
          text.erase(text.find_last_not_of('0') + 1);
          if (!text.empty() && text.back() == '.') text.pop_back();
          return text;
        };
        return spec.name + " must be between " + bound(spec.min) + " and " + bound(spec.max) + " (got " + word + ")";
      }
      return {};
    };
    switch (spec.type) {
      case ArgType::Int: {
        const auto value = ParseInt(word);
        if (!value) return spec.name + " must be a whole number (got '" + word + "')";
        if (auto error = range_error(double(*value)); !error.empty()) return error;
        break;
      }
      case ArgType::Float: {
        const auto value = ParseFloat(word);
        if (!value) return spec.name + " must be a number (got '" + word + "')";
        if (auto error = range_error(*value); !error.empty()) return error;
        break;
      }
      case ArgType::Bool:
        if (!ParseBool(word)) return spec.name + " must be on or off (got '" + word + "')";
        break;
      case ArgType::Duration: {
        const auto ticks = ParseDuration(word);
        if (!ticks) return spec.name + " must be a duration: ticks, or a number with s or ms (got '" + word + "')";
        if (auto error = range_error(double(*ticks)); !error.empty()) return error;
        break;
      }
      case ArgType::Choice: {
        if (!spec.choices) break;
        const auto choices = spec.choices();
        const bool known = std::any_of(choices.begin(), choices.end(),
                                       [&](const std::string& c) { return Lower(c) == Lower(word); });
        if (!known) {
          std::string list;
          for (size_t k = 0; k < choices.size() && k < 12; ++k) list += (k ? ", " : "") + choices[k];
          if (choices.size() > 12) list += ", ...";
          return "unknown " + spec.name + " '" + word + "' (" + list + ")";
        }
        break;
      }
      case ArgType::String:
        break;
    }
  }
  if (command.validate) return command.validate(args);
  return {};
}

// The cvar system, as the registry sees it (console.cpp adapts rex::cvar to this; tests
// use a map).
struct CvarAccess {
  std::function<std::vector<std::string>()> list;
  std::function<std::optional<std::string>(std::string_view)> get;  // nullopt: no such cvar
  std::function<bool(std::string_view, std::string_view)> set;       // false: rejected
  std::function<std::string(std::string_view)> describe;             // description, may be empty
};

// ---- Completion ------------------------------------------------------------------------
struct Completion {
  size_t replace_from = 0;             // byte offset in the line of the word being completed
  std::string word;                    // that word as typed
  std::vector<std::string> candidates; // sorted, unique
  std::string common;                  // longest common prefix of the candidates (case of the first)
};

class Registry {
 public:
  void Add(Command command) {
    const auto key = Lower(command.name);
    commands_[key] = std::move(command);
  }
  const Command* Find(std::string_view name) const {
    const auto it = commands_.find(Lower(name));
    return it == commands_.end() ? nullptr : &it->second;
  }
  std::vector<const Command*> List() const {
    std::vector<const Command*> out;
    for (const auto& [key, command] : commands_) out.push_back(&command);
    return out;
  }
  // Aliases: a name for a command line. "alias name" alone prints it, an empty text removes it.
  bool SetAlias(std::string_view name, std::string_view text) {
    const auto key = Lower(name);
    if (key.empty() || Find(key)) return false;  // commands cannot be shadowed
    if (Trim(text).empty()) aliases_.erase(key);
    else aliases_[key] = std::string(Trim(text));
    return true;
  }
  const std::string* Alias(std::string_view name) const {
    const auto it = aliases_.find(Lower(name));
    return it == aliases_.end() ? nullptr : &it->second;
  }
  const std::map<std::string, std::string>& aliases() const { return aliases_; }

  // Expands aliases at the head of each command, recursively (to kMaxAliasDepth), and
  // splits the result into single commands. Words after an alias name are appended to
  // its expansion's last command. An over-deep expansion yields an error entry.
  static constexpr int kMaxAliasDepth = 16;
  struct Expanded {
    std::vector<std::string> commands;
    std::string error;
  };
  Expanded Expand(std::string_view line) const {
    Expanded out;
    ExpandInto(line, 0, out);
    return out;
  }

  Completion Complete(std::string_view line, const CvarAccess* cvars) const {
    Completion result;
    // Only the last command of the line is completed.
    size_t start = 0;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
      if (line[i] == '"') quoted = !quoted;
      else if (line[i] == ';' && !quoted) start = i + 1;
    }
    const std::string_view command = line.substr(start);
    // Words before the one under the cursor (the cursor is at the end of the line).
    const bool fresh_word = command.empty() || std::isspace(static_cast<unsigned char>(command.back()));
    auto words = Tokenize(command);
    if (!fresh_word && !words.empty()) {
      result.word = words.back();
      words.pop_back();
    }
    result.replace_from = line.size() - result.word.size();
    std::vector<std::string> pool;
    if (words.empty()) {
      for (const auto& [key, cmd] : commands_) pool.push_back(cmd.name);
      for (const auto& [key, text] : aliases_) pool.push_back(key);
      if (cvars && cvars->list)
        for (auto& name : cvars->list()) pool.push_back(std::move(name));
    } else {
      const auto* cmd = Find(words.front());
      const size_t index = words.size() - 1;  // argument index being completed
      if (cmd) {
        if (index < cmd->args.size() && cmd->args[index].choices) pool = cmd->args[index].choices();
        else if (!cmd->args.empty() && index >= cmd->args.size() && cmd->max_args > cmd->args.size() &&
                 cmd->args.back().choices)
          pool = cmd->args.back().choices();  // variadic tail
      } else if (index == 0 && cvars && cvars->get) {
        // "cvar <value>": offer the current value.
        if (auto value = cvars->get(words.front())) pool.push_back(*value);
      }
    }
    std::vector<std::string> matches;
    for (auto& candidate : pool)
      if (StartsWithNoCase(candidate, result.word)) matches.push_back(std::move(candidate));
    std::sort(matches.begin(), matches.end(),
              [](const std::string& a, const std::string& b) { return Lower(a) < Lower(b); });
    matches.erase(std::unique(matches.begin(), matches.end(),
                              [](const std::string& a, const std::string& b) { return Lower(a) == Lower(b); }),
                  matches.end());
    result.candidates = std::move(matches);
    if (!result.candidates.empty()) {
      result.common = result.candidates.front();
      for (const auto& candidate : result.candidates) {
        size_t n = 0;
        while (n < result.common.size() && n < candidate.size() &&
               std::tolower(static_cast<unsigned char>(result.common[n])) ==
                   std::tolower(static_cast<unsigned char>(candidate[n])))
          ++n;
        result.common.resize(n);
      }
    }
    return result;
  }

  // Applies a completion to the line: the common prefix, plus a space when it is the only
  // candidate. Returns the line unchanged when there is nothing to add.
  static std::string ApplyCompletion(std::string_view line, const Completion& completion) {
    if (completion.candidates.empty()) return std::string(line);
    std::string replacement = completion.candidates.size() == 1 ? completion.candidates.front() : completion.common;
    if (replacement.size() < completion.word.size()) return std::string(line);
    if (replacement.find(' ') != std::string::npos) replacement = "\"" + replacement + "\"";
    std::string out(line.substr(0, completion.replace_from));
    out += replacement;
    if (completion.candidates.size() == 1) out += ' ';
    return out;
  }

 private:
  void ExpandInto(std::string_view line, int depth, Expanded& out) const {
    for (const auto& command : SplitCommands(line)) {
      const auto words = Tokenize(command);
      if (words.empty()) continue;
      const auto* alias = Find(words.front()) ? nullptr : Alias(words.front());
      if (!alias) { out.commands.push_back(command); continue; }
      if (depth >= kMaxAliasDepth) {
        out.error = "alias '" + words.front() + "' nests deeper than " + std::to_string(kMaxAliasDepth) + " (a loop?)";
        return;
      }
      std::string text = *alias;
      const auto first_space = command.find_first_of(" \t");
      if (first_space != std::string::npos) text += " " + std::string(Trim(std::string_view(command).substr(first_space)));
      ExpandInto(text, depth + 1, out);
      if (!out.error.empty()) return;
    }
  }

  std::map<std::string, Command> commands_;
  std::map<std::string, std::string> aliases_;
};

// ---- History ---------------------------------------------------------------------------
// Input history with Up/Down browsing. Consecutive duplicates are stored once.
class History {
 public:
  explicit History(size_t limit = 128) : limit_(limit) {}
  void Add(std::string_view line) {
    const auto trimmed = Trim(line);
    cursor_ = -1;
    if (trimmed.empty()) return;
    if (!entries_.empty() && entries_.back() == trimmed) return;
    entries_.emplace_back(trimmed);
    if (entries_.size() > limit_) entries_.erase(entries_.begin());
  }
  // Up: older (returns the entry to show). Down: newer; past the newest returns "".
  std::optional<std::string> Older() {
    if (entries_.empty()) return std::nullopt;
    if (cursor_ < 0) cursor_ = int(entries_.size()) - 1;
    else if (cursor_ > 0) --cursor_;
    return entries_[size_t(cursor_)];
  }
  std::optional<std::string> Newer() {
    if (cursor_ < 0) return std::nullopt;
    if (++cursor_ >= int(entries_.size())) { cursor_ = -1; return std::string(); }
    return entries_[size_t(cursor_)];
  }
  const std::vector<std::string>& entries() const { return entries_; }

 private:
  std::vector<std::string> entries_;
  size_t limit_;
  int cursor_ = -1;
};

// ---- Scheduling ------------------------------------------------------------------------
// "at <time> <command...>": run a command when the script clock reaches <time>, a
// duration (ticks, s or ms) measured on the scripted-pad game clock.
struct AtCommand {
  uint64_t tick = 0;
  std::string command;
};
inline std::optional<AtCommand> ParseAt(const std::vector<std::string>& words) {
  if (words.size() < 3 || Lower(words[0]) != "at") return std::nullopt;
  const auto tick = ParseDuration(words[1]);
  if (!tick) return std::nullopt;
  AtCommand at{*tick, {}};
  for (size_t i = 2; i < words.size(); ++i) {
    if (i > 2) at.command += ' ';
    const bool needs_quotes = words[i].find_first_of(" ;\t") != std::string::npos || words[i].empty();
    at.command += needs_quotes ? "\"" + words[i] + "\"" : words[i];
  }
  return at;
}

// Formats ticks as seconds for messages: "600 (10.00 s)".
inline std::string FormatTicks(uint64_t ticks) {
  char text[64];
  std::snprintf(text, sizeof(text), "%llu (%.2f s)", static_cast<unsigned long long>(ticks), double(ticks) / 60.0);
  return text;
}

}  // namespace edf::console
