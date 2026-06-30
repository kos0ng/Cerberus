#include <utils/arg_parser.h>
#include <utils/logger.h>
#include <global_defs.h>
#include <filesystem>

namespace fs = std::filesystem;
using namespace std;

string ArgParser::format_help() {
    string res = "$(cyan)$(cyan:b)" + TOOL_ART + "$\n";
    res += " Version: $(white:b)" + TOOL_VERSION + "$\n";
    res += " Author: $(white:b)" + TOOL_AUTHOR + "$\n";
    res += "______________________________________\n\n";
    res += "$(cyan:b)Syntax: $(red:b)cerberus binary [-param value] [--flag]$$\n\n";
    res += "$(cyan:b)Parameters:$\n";
    res += "   $(red:b)output (o)$ -> Specifies the path for the resulting executable file. $(cyan:b)Default value: [input_binary]-patched$\n";
    res += "   $(red:b)part_hash_len (phl)$ -> Specifies the length of a part hash. The part hash of a function is just a reduction of the function with a linear pace. This technique is used to prevent fixed addresses from corrupting a standard hash. $(cyan:b)Default value: 20$\n";
    res += "   $(red:b)part_hash_trust (pht)$ -> Specifies minimum ratio of similarity between the two hashed functions to compare. The kept function will be the one with the most matches anyway. Increasing this value will reduce the number of matched functions but speed up execution time. $(cyan:b)Default value: 0.6$\n";
    res += "   $(red:b)min_func_size (mfs)$ -> Specifies the minimum length a function must be to get analyzed. Decreasing this value will increase matches but also false positives. $(cyan:b)Default value : 10$\n";
    res += "   $(red:b)reference (ref)$ -> Optional path to an unstripped reference binary (e.g. the same program rebuilt from source) whose named functions are also matched against the target. This recovers caller-instantiated symbols that no library build contains (generic trait methods, user functions).\n";
    res += "   $(red:b)profile-template (pt)$ -> Optional path to a TOML file whose [profile.release]/[profile.dev] sections are merged into each library's Cargo.toml before building, so libraries can be compiled with custom options (opt-level, lto, panic...) beyond the default release/debug. See the profiles/ folder for examples.\n\n";
    res += "$(cyan:b)Flags:$\n";
    res += "   $(red:b)help (h)$ -> Displays this message.\n";
    res += "   $(red:b)debug (h)$ -> Displays outputs of commands.\n";
    res += "   $(red:b)no-prompt (np)$ -> Automatically skips user prompts.";
    res += "\n   $(red:b)compile-tests (ct)$ -> Also build each library's test binaries and match them. Recovers generic provided methods (e.g. `<D as Digest>::finalize`) that no library build contains. Slower.";
    res += "\n   $(red:b)all-features (af)$ -> Compile each library with --all-features, to recover feature-gated functions (e.g. an `asm` backend). Use when the target was built with non-default features.";
    res += "\n   $(red:b)target (t)$ -> Explicit rustc target triple to compile libraries for (e.g. x86_64-unknown-linux-musl, x86_64-pc-windows-gnu). Overrides auto-detection; static (musl) ELF targets are detected automatically.";
    return res;
}

void ArgParser::prepare_args() {
    this->parser.add_argument("binary").default_value("");
    this->parser.add_argument("-output", "-o");
    this->parser.add_argument("-part_hash_len", "-phl");
    this->parser.add_argument("-part_hash_trust", "-pht");
    this->parser.add_argument("-min_func_size", "-mfs");
    this->parser.add_argument("--reference", "--ref");
    this->parser.add_argument("--profile-template", "--pt");
    this->parser.add_argument("--help", "--h").implicit_value(true);
    this->parser.add_argument("--debug", "--dbg").implicit_value(true);
    this->parser.add_argument("--no-prompt", "--np").implicit_value(true);
    this->parser.add_argument("--compile-tests", "--ct").implicit_value(true);
    this->parser.add_argument("--all-features", "--af").implicit_value(true);
    this->parser.add_argument("--target", "-t");
}

CONFIG* ArgParser::compute_args(int argc, char **argv) {
    CONFIG* config = new CONFIG;
    if(argc >= 2) {
        this->parser.parse_args(argc, argv);
        config->binary_path = this->parser.get<string>("binary");
        if(this->parser.is_used("-output")) config->output_path = this->parser.get<string>("output");
        else {
            size_t extension_idx;
            string extension = "";
            if((extension_idx = config->binary_path.find_last_of('.')) != string::npos) {
                if(config->binary_path.find_last_of('/') < extension_idx && config->binary_path.find_last_of('\\') < extension_idx) {
                    extension = config->binary_path.substr(extension_idx);
                }
            }
            config->output_path = config->binary_path + extension + "-patched";
        }
        if (this->parser.is_used("-part_hash_len")) config->part_hash_len = this->parser.get<uint16_t>("part_hash_len");
        if (this->parser.is_used("-part_hash_trust"))
            config->part_hash_trust = this->parser.get<float>("part_hash_trust");
        if (this->parser.is_used("-min_func_size")) config->min_func_size = this->parser.get<uint16_t>("min_func_size");
        if (this->parser.is_used("--reference")) config->reference_path = this->parser.get<string>("reference");
        if (this->parser.is_used("--profile-template")) config->profile_template = this->parser.get<string>("profile-template");
        if (this->parser.is_used("--debug")) config->debug = true;
        if (this->parser.is_used("--no-prompt")) config->no_prompt = true;
        if (this->parser.is_used("--compile-tests")) config->compile_tests = true;
        if (this->parser.is_used("--all-features")) config->all_features = true;
        if (this->parser.is_used("--target")) config->target_triple = this->parser.get<string>("target");
    }
    if(argc < 2 || this->parser.is_used("--help") || !config->binary_path.length()) {
        string help = this->format_help();
        fcout << help << endl;
        exit(0);
    }
    if(!fs::exists(config->binary_path)) {
        fcout << "$(critical)File $(critical:u)" << config->binary_path << "$ does not exist !" << endl;
        exit(1);
    }
    if(!config->reference_path.empty() && !fs::exists(config->reference_path)) {
        fcout << "$(critical)Reference file $(critical:u)" << config->reference_path << "$ does not exist !" << endl;
        exit(1);
    }
    if(!config->profile_template.empty() && !fs::exists(config->profile_template)) {
        fcout << "$(critical)Profile template $(critical:u)" << config->profile_template << "$ does not exist !" << endl;
        exit(1);
    }
    return config;
}

ArgParser::ArgParser() {
    this->parser = argparse::ArgumentParser("cerberus");
    this->prepare_args();
}