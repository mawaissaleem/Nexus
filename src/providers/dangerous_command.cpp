#include "nexus/providers/dangerous_command.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace nexus::providers {

namespace {

using Words = std::vector<std::string>;

// Commands that are dangerous whatever their arguments.
constexpr std::array<std::string_view, 11> kAlwaysDangerous = {
    "dd", "shred", "wipefs", "fdisk", "parted", "sfdisk", "shutdown", "reboot", "poweroff", "halt", "mkfs"};
// Words that only run another command: the next word is the real one.
constexpr std::array<std::string_view, 8> kWrappers = {"sudo", "doas",    "env",  "nohup",
                                                       "time", "command", "exec", "xargs"};
// Options of a wrapper that take the next word as their value, so `sudo -u bob rm` skips "bob" too.
constexpr std::array<std::pair<std::string_view, std::string_view>, 14> kValueOptions = {{
    {"sudo", "-u"},
    {"sudo", "-g"},
    {"sudo", "-C"},
    {"sudo", "-D"},
    {"sudo", "-h"},
    {"sudo", "-p"},
    {"sudo", "-r"},
    {"sudo", "-t"},
    {"sudo", "-U"},
    {"sudo", "-T"},
    {"doas", "-u"},
    {"doas", "-C"},
    {"env", "-u"},
    {"env", "-C"},
}};
// Shell keywords that can stand before a command, as in `then reboot` or `{ rm -rf /; }`.
constexpr std::array<std::string_view, 9> kKeywords = {"if", "then", "else", "elif", "while", "until", "do", "{", "!"};
constexpr std::array<std::string_view, 5> kShells = {"sh", "bash", "dash", "zsh", "ksh"};
constexpr std::array<std::string_view, 3> kPowerActions = {"poweroff", "reboot", "halt"};
constexpr std::array<std::string_view, 4> kDevicePrefixes = {"/dev/sd", "/dev/hd", "/dev/vd", "/dev/nvme"};

template <typename Table, typename Value> bool contains(const Table& table, const Value& value) {
    return std::find(table.begin(), table.end(), value) != table.end();
}

// Splits into simple commands at ; | & newline ( ) and backtick. Quotes group words and are dropped; an unquoted
// '>' becomes a word of its own. shortcut: no backslash escapes, upgrade if misses show up.
std::vector<Words> split_commands(std::string_view text) {
    std::vector<Words> commands(1);
    std::string word;
    bool in_word = false;
    char quote = 0;

    auto end_word = [&]() {
        if (!in_word) return;
        commands.back().push_back(word);
        word.clear();
        in_word = false;
    };

    for (const char c : text) {
        if (quote != 0) {
            if (c == quote) {
                quote = 0;
            } else {
                word += c;
            }
        } else if (c == '\'' || c == '"') {
            quote = c;
            in_word = true;
        } else if (std::string_view(";|&\n()`").find(c) != std::string_view::npos) {
            end_word();
            commands.emplace_back();
        } else if (c == '>') {
            end_word();
            commands.back().push_back(">");
        } else if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            end_word();
        } else {
            word += c;
            in_word = true;
        }
    }
    end_word();
    return commands;
}

// A single-dash cluster such as -rf or -Rf.
bool is_short_cluster(const std::string& word) { return word.size() > 1 && word[0] == '-' && word[1] != '-'; }

// A short cluster containing any of `letters`.
bool has_short_flag(const Words& words, std::string_view letters) {
    return std::any_of(words.begin(), words.end(), [&](const std::string& w) {
        return is_short_cluster(w) && w.find_first_of(letters) != std::string::npos;
    });
}

bool has_word(const Words& words, std::string_view wanted) {
    return std::any_of(words.begin(), words.end(), [&](const std::string& w) { return w == wanted; });
}

bool is_env_assignment(const std::string& word) {
    return word.find('=') != std::string::npos && word[0] != '=' && word[0] != '-';
}

// `> /dev/sda` and friends: redirecting into a disk device overwrites it.
bool redirects_into_device(const Words& words) {
    for (size_t i = 0; i + 1 < words.size(); ++i) {
        if (words[i] != ">") continue;
        const std::string& target = words[i + 1];
        const bool is_device = std::any_of(kDevicePrefixes.begin(), kDevicePrefixes.end(),
                                           [&](std::string_view prefix) { return target.starts_with(prefix); });
        if (is_device) return true;
    }
    return false;
}

// How many words at `i` stand before the real command: 1 for a wrapper, keyword, VAR=x or option, 2 for an option
// of `wrapper` that takes a value, 0 when words[i] is the command itself.
size_t prefix_length(const Words& words, size_t i, std::string_view wrapper) {
    const std::string& word = words[i];
    if (contains(kWrappers, word) || contains(kKeywords, word) || is_env_assignment(word)) return 1;

    const bool is_option = i > 0 && word.starts_with('-');
    if (!is_option) return 0;
    const std::pair<std::string_view, std::string_view> option{wrapper, word};
    return contains(kValueOptions, option) ? 2 : 1;
}

// `sh -c "<script>"` (also clusters like -lc) runs its script argument, so that script is checked too.
bool runs_dangerous_script(const Words& args) {
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        const bool is_c_option = is_short_cluster(args[i]) && args[i].find('c') != std::string::npos;
        if (is_c_option) return is_dangerous_command(args[i + 1]);
    }
    return false;
}

// shortcut: `nice`/`watch` prefixes, globs like `chmod -R 777 /*` and long options with a separate value
// (`sudo --user bob`) are missed, upgrade if they show up in practice.
bool is_dangerous_simple_command(const Words& words) {
    if (redirects_into_device(words)) return true;

    size_t i = 0;
    std::string_view wrapper;
    while (i < words.size()) {
        const size_t skip = prefix_length(words, i, wrapper);
        if (skip == 0) break;
        if (contains(kWrappers, words[i])) wrapper = words[i];
        i += skip;
    }
    if (i >= words.size()) return false;

    const std::string& path = words[i];
    const size_t slash = path.rfind('/');
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const Words args(words.begin() + static_cast<std::ptrdiff_t>(i) + 1, words.end());

    if (contains(kAlwaysDangerous, name) || name.starts_with("mkfs.")) return true;
    if (contains(kShells, name)) return runs_dangerous_script(args);
    if (name == "rm") return has_short_flag(args, "rRf") || has_word(args, "--recursive") || has_word(args, "--force");
    if (name == "systemctl") {
        return std::any_of(args.begin(), args.end(), [](const std::string& a) { return contains(kPowerActions, a); });
    }
    if (name == "chmod" || name == "chown") {
        return (has_short_flag(args, "R") || has_word(args, "--recursive")) && has_word(args, "/");
    }
    return false;
}

}  // namespace

bool is_dangerous_command(std::string_view command_line) {
    // The fork bomb is punctuation only, so match it with the whitespace removed.
    std::string compact;
    std::copy_if(command_line.begin(), command_line.end(), std::back_inserter(compact),
                 [](char c) { return std::isspace(static_cast<unsigned char>(c)) == 0; });
    if (compact.find(":(){:|:&};:") != std::string::npos) return true;

    const auto commands = split_commands(command_line);
    return std::any_of(commands.begin(), commands.end(), is_dangerous_simple_command);
}

}  // namespace nexus::providers
