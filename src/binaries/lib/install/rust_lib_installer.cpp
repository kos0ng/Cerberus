#include <binaries/lib/install/rust_lib_installer.h>
#include <utils/file_operations.h>
#include <utils/convert.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <cctype>
#include <command/command_executor.h>
#include <utils/logger.h>
#include <user/user_prompt.h>

namespace fs = std::filesystem;
using namespace std;

bool COMPILE_TESTS = false;
string PROFILE_TEMPLATE = "";
bool ALL_FEATURES = false;
string TARGET_TRIPLE = "";

const string RBF_PREFIX = "$(bright_blue:b)[$(blue:b)RBF$]$ ";

void newer_edition_patch(string crate_path) {
    if(fs::exists(crate_path+"/Cargo.toml")) {
        ifstream cargo_input_file(crate_path + "/Cargo.toml");
        string line;
        vector<string> lines;
        while(getline(cargo_input_file, line)) {
            line = strip(line);
            if(line.find("[package]") != string::npos) {
                lines.push_back("[package]");
                lines.push_back("edition = \"2021\"");
            } else if(line.find("edition ") == string::npos && line.find("edition=") == string::npos) {
                lines.push_back(line);
            }
        }
        cargo_input_file.close();
        ofstream cargo_output_file(crate_path+"/Cargo.toml");
        for(string l : lines) {
            cargo_output_file.write((l + string("\n")).c_str(), l.size() + 1);
        }
        cargo_output_file.close();
    }
}

PATCH NEWER_EDITION_PATCH{
    "EDITION 2021",
    newer_edition_patch
};

void std_redefinition_patch(string crate_path) {
    if(fs::exists(crate_path+"/src/lib.rs")) {
        ifstream lib_input_file(crate_path+"/src/lib.rs");
        string line;
        vector<string> lines;
        while(getline(lib_input_file, line)) {
            if(line.find("no_std") == string::npos && line.find("as std;") == string::npos) lines.push_back(line);
        }
        lib_input_file.close();
        ofstream lib_output_file(crate_path+"/src/lib.rs");
        for(string l : lines) {
            lib_output_file.write((l + string("\n")).c_str(), l.size() + 1);
        }
        lib_output_file.close();
    }
}

PATCH STD_REDEFINITION_PATCH {
    "STD REDEFINITION",
    std_redefinition_patch
};

void add_workspace_patch(string crate_path) {
    if(fs::exists(crate_path+"/Cargo.toml")) {
        ifstream cargo_input_file(crate_path+"/Cargo.toml");
        vector<string> lines;
        string line;
        while(getline(cargo_input_file, line)) {
            lines.push_back(line);
        }
        lines.push_back("[workspace]");
        cargo_input_file.close();
        ofstream cargo_output_file(crate_path+"/Cargo.toml");
        for(string l : lines) {
            cargo_output_file.write((l + string("\n")).c_str(), l.size() + 1);
        }
        cargo_output_file.close();
    }
}

PATCH ADD_WORKSPACE_PATCH {
    "ADD WORKSPACE",
    add_workspace_patch
};

map<string, PATCH> patches = {
    {"maybe a missing crate `core`?", NEWER_EDITION_PATCH},
    {"you might be missing crate `core`", NEWER_EDITION_PATCH},
    {"the name `std` is defined multiple times", STD_REDEFINITION_PATCH},
    {"language item required, but not found: `eh_personality`", STD_REDEFINITION_PATCH},
    {"`#[panic_handler]` function required", STD_REDEFINITION_PATCH},
    {"unwinding panics are not supported without std", STD_REDEFINITION_PATCH},
    {"current package believes it's in a workspace when it's not", ADD_WORKSPACE_PATCH}
};

bool RustBuildFixer::process_error(string command, string error) {
    for(pair<string, PATCH> patch_pair : patches) {
        if(error.find(patch_pair.first) != string::npos) {
            PATCH patch = patch_pair.second;
            if(last_patch && patch.patch_func == last_patch->patch_func) return false;
            last_patch = &patch;
            fcout << "$(info)" << RBF_PREFIX << "Applying patch $(red:b)" << patch.name << "$..." << endl;
            patch.patch_func(crate_path);
            COMMAND_RESULT res;
            executor.execute_command(command, &res);
            if(!res.code) return true;
            return process_error(command, res.response);
        }
    }
    return false;
}

string RustLibInstaller::detect_rustc_version() {
    if(bin_path.empty()) return "";
    ifstream bin_file(bin_path, ios::binary);
    if(!bin_file.is_open()) return "";
    bin_file.seekg(0, ios::end);
    size_t bin_sz = bin_file.tellg();
    bin_file.seekg(0);
    const string needle = "rustc version ";
    string version;
    vector<char> buf(4096);
    size_t off = 0;
    while(off < bin_sz) {
        bin_file.seekg(off);
        bin_file.read(buf.data(), buf.size());
        size_t n = bin_file.gcount();
        if(n <= needle.size()) break;
        string window(buf.data(), n);
        size_t pos = window.find(needle);
        if(pos != string::npos) {
            size_t start = pos + needle.size();
            size_t end = start;
            while(end < n && (isdigit((unsigned char)buf[end]) || buf[end] == '.')) end++;
            string ver = window.substr(start, end - start);
            uint8_t dots = 0;
            for(char c : ver) if(c == '.') dots++;
            if(dots >= 2 && ver.size() >= 5) { version = ver; break; }
        }
        if(n < buf.size()) break;
        off += n - needle.size();
    }
    bin_file.close();
    return version;
}

string RustLibInstaller::get_local_rustc_version() {
    CommandExecutor executor("./");
    COMMAND_RESULT res;
    executor.execute_command("rustc --version", &res);
    if(res.code) return "";
    const string needle = "rustc ";
    size_t pos = res.response.find(needle);
    if(pos == string::npos) return "";
    size_t start = pos + needle.size();
    size_t end = start;
    while(end < res.response.size() && (isdigit((unsigned char)res.response[end]) || res.response[end] == '.')) end++;
    return res.response.substr(start, end - start);
}

bool RustLibInstaller::ensure_toolchain(string version) {
    if(version.empty()) return false;
    CommandExecutor executor("./");
    COMMAND_RESULT res;
    executor.execute_command("rustup toolchain list", &res);
    if(res.code) return false;
    // rustup names installed toolchains like "1.70.0-x86_64-unknown-linux-gnu"
    if(res.response.find(version + "-") != string::npos) {
        fcout << "$(info)Required toolchain $(info:b)" << version << "$ is already installed." << endl;
        return true;
    }
    fcout << "$(info)Target was built with rustc $(info:b)" << version << "$, which differs from your local toolchain." << endl;
    fcout << "$(info)For accurate matching, Cerberus should compile the libraries with the same toolchain." << endl;
    bool agree = ask_yes_no("Install toolchain " + version + " via rustup ?", true);
    if(!agree) return false;
    string install_cmd = "rustup install " + version;
    executor.execute_command(install_cmd, &res);
    if(res.code) {
        fcout << "$(error)An error occurred while installing toolchain $(error:b)" << version << "$. Falling back to the local toolchain." << endl;
        return false;
    }
    fcout << "$(success)Done !" << endl;
    return true;
}

void RustLibInstaller::check_and_install_arch(string arch_name) {
    CommandExecutor executor("./");
    COMMAND_RESULT res;
    executor.execute_command("rustup " + cargo_toolchain + " target list", &res);
    if(res.code) return;
    vector<string> lines = split_string(res.response, '\n');
    for(string& line : lines) {
        if(line.find(arch_name) != string::npos) {
            if(line.find("installed") == string::npos) {
                fcout << "$(info)You need to install the toolchain for $(info:b)" << arch_name << "$ architecture." << endl;
                bool agree_install = ask_yes_no("Proceed to installation ?", true);
                if(agree_install) {
                    COMMAND_RESULT res;
                    string install_cmd = "rustup " + cargo_toolchain + " target install " + arch_name;
                    executor.execute_command(install_cmd, &res);
                    if(res.code) fcout << "$(error)An error occurred during installation... Run $(error:b)" << install_cmd << "$ for more information." << endl;
                    else fcout << "$(success)Done !" << endl;
                    return;
                } else return;
            } else {
                fcout << "$(info)Requested architecture is already installed." << endl;
                return;
            }
        }
    }
}

RustLibInstaller::RustLibInstaller(string work_dir, BIN_ARCH arch, BIN_TYPE type, string bin_path, bool static_target) : LibInstaller(work_dir, arch, type), downloader(), bin_path(bin_path), cargo_toolchain(""), cargo_target("") {
    string target_ver = detect_rustc_version();
    if(!target_ver.empty()) {
        string local_ver = get_local_rustc_version();
        fcout << "$(info)Target was built with rustc $(info:b)" << target_ver << "$" << (local_ver.empty() ? "" : (" (local: " + local_ver + ")")) << "." << endl;
        if(local_ver.empty() || target_ver != local_ver) {
            if(ensure_toolchain(target_ver)) {
                cargo_toolchain = "+" + target_ver;
                fcout << "$(success)Libraries will be built with toolchain $(success:b)" << target_ver << "$." << endl;
            } else if(!local_ver.empty()) {
                fcout << "$(warning)Continuing with the local toolchain; function matching may be less accurate." << endl;
            }
        }
    }
    // Determine the build target triple. Explicit --target wins; otherwise a
    // statically-linked ELF is assumed to be a musl target; otherwise x86 ELF
    // cross-compiles to i686-gnu and x86_64-gnu uses the host triple.
    if(!TARGET_TRIPLE.empty()) {
        cargo_target = TARGET_TRIPLE;
        fcout << "$(info)Using explicit target triple $(info:b)" << cargo_target << "$." << endl;
    } else if(type == BIN_TYPE::ELF) {
        if(static_target) {
            cargo_target = (arch == BIN_ARCH::X86) ? "i686-unknown-linux-musl" : "x86_64-unknown-linux-musl";
            fcout << "$(info)Target is statically linked; building libraries for musl ($(info:b)" << cargo_target << "$)." << endl;
        } else if(arch == BIN_ARCH::X86) {
            cargo_target = "i686-unknown-linux-gnu";
        }
    }
    if(!cargo_target.empty()) check_and_install_arch(cargo_target);
}

void RustLibInstaller::build_test_binaries(string crate_path, vector<string>& out) {
    // Build the crate's test binaries on the ORIGINAL (unpatched) crate. The no_std lib
    // compiles in test mode because the harness provides std + the panic handler, and these
    // binaries instantiate generic provided methods (e.g. <D as digest::Digest>::finalize)
    // that no library dylib contains, making them recoverable by body-matching.
    // Failures are ignored: a crate whose tests don't build simply contributes nothing.
    CommandExecutor executor(crate_path);
    COMMAND_RESULT res;
    string features_flag = ALL_FEATURES ? " --all-features" : "";
    string tflag = cargo_target.empty() ? "" : (" --target " + cargo_target);
    executor.execute_command("cargo " + cargo_toolchain + " test --no-run" + tflag + features_flag, &res);
    executor.execute_command("cargo " + cargo_toolchain + " test --no-run --release" + tflag + features_flag, &res);
    for(string prof : {string("debug"), string("release")}) {
        string subdir = cargo_target.empty() ? "" : (cargo_target + "/");
        string dir = crate_path + "/target/" + subdir + prof + "/deps";
        if(!fs::exists(dir)) continue;
        for(const auto& entry : fs::directory_iterator(dir)) {
            if(!fs::is_regular_file(entry)) continue;
            fs::path p = entry.path();
            // collect executables only (no extension); skip .d / .so / .rmeta / .rlib
            if(p.extension().empty()) out.push_back(p.string());
        }
    }
}

bool RustLibInstaller::install_lib(LIBRARY lib) {
    string output_dir_name = this->work_dir+"/"+lib.name+"-"+lib.version;
    string zip_file_name = output_dir_name+".crate";
    string tar_file_name = output_dir_name+".tar";
    if(!this->downloader.download_file("https://crates.io/api/v1/crates/"+lib.name+"/"+lib.version+"/download", zip_file_name)) return false;
    if(!decompress_gzip_file(zip_file_name, tar_file_name)) return false;
    fs::remove(zip_file_name);
    if(!decompress_tar_file(tar_file_name, output_dir_name)) return false;
    fs::remove(tar_file_name);
    vector<string> test_artifacts;
    if(COMPILE_TESTS) build_test_binaries(output_dir_name, test_artifacts);
    ifstream cargo_toml_input(output_dir_name+"/Cargo.toml");
    if(!cargo_toml_input.is_open()) return false;
    vector<string> cargo_toml_lines;
    string line;
    bool found_lib = false;
    bool strip_profiles = !PROFILE_TEMPLATE.empty();
    bool in_profile = false;
    while(getline(cargo_toml_input, line)) {
        strip(line);
        if(strip_profiles) {
            // Drop any existing [profile.*] sections so the template's don't collide.
            if(!line.empty() && line[0] == '[') in_profile = (line.find("[profile") != string::npos);
            if(in_profile) continue;
        }
        if(line.find("[lib]") != string::npos) {
            found_lib = true;
            cargo_toml_lines.push_back("[lib]");
            cargo_toml_lines.push_back("crate-type = [\"dylib\"]");
        } else if(line.find("crate-type ") && line.find("crate-type=")) cargo_toml_lines.push_back(line);
    }
    cargo_toml_input.close();
    if(!found_lib) {
        cargo_toml_lines.push_back("[lib]");
        cargo_toml_lines.push_back("crate-type = [\"dylib\"]");
    }
    if(strip_profiles) {
        ifstream tpl_input(PROFILE_TEMPLATE);
        if(tpl_input.is_open()) {
            cargo_toml_lines.push_back("");
            string tline;
            while(getline(tpl_input, tline)) cargo_toml_lines.push_back(tline);
            tpl_input.close();
            fcout << "$(info)Applied profile template $(info:b)" << PROFILE_TEMPLATE << "$ to " << lib.name << "." << endl;
        } else {
            fcout << "$(warning)Profile template $(warning:b)" << PROFILE_TEMPLATE << "$ could not be read; using default profiles." << endl;
        }
    }
    if(!fs::exists(output_dir_name+"/Cargo.toml")) return false;
    ofstream cargo_toml_output(output_dir_name+"/Cargo.toml");
    for(string line : cargo_toml_lines) cargo_toml_output.write((line+string("\n")).c_str(), line.size()+1);
    cargo_toml_output.close();
    string lib_extension;
    switch(type) {
        case BIN_TYPE::ELF:
            lib_extension = ".so";
            break;
        case BIN_TYPE::PE:
            lib_extension = ".dll";
            break;
    }
    // Build the crate under every profile a target might have been compiled with, so the
    // hashed functions can match regardless of whether the analyzed binary was built with a
    // release or a debug profile (e.g. unoptimized CTF challenges). Each profile produces its
    // own shared object (suffixed to avoid clobbering the others) and matching picks the best.
    struct BUILD_PROFILE { string flag; string target_dir; string suffix; };
    vector<BUILD_PROFILE> profiles = {
        {"--release", "release", ""},
        {"",          "debug",   "_debug"}
    };
    bool any_success = false;
    for(BUILD_PROFILE& profile : profiles) {
        CommandExecutor executor(output_dir_name);
        COMMAND_RESULT res;
        string command;
        string features_flag = ALL_FEATURES ? " --all-features" : "";
        string tflag = cargo_target.empty() ? "" : (" --target " + cargo_target);
        // musl defaults to a static CRT, which forbids dylibs; relax it so the
        // forced crate-type=["dylib"] can be produced (mirrors rustbinsign's musl handling).
        string env_prefix = (cargo_target.find("musl") != string::npos) ? "RUSTFLAGS=\"-C target-feature=-crt-static\" " : "";
        switch(type) {
            case BIN_TYPE::ELF:
                command = env_prefix + "cargo " + cargo_toolchain + " build " + profile.flag + tflag + features_flag;
                break;
            case BIN_TYPE::PE: {
                string pe_triple = cargo_target.empty() ? ((arch == BIN_ARCH::X86_64) ? "x86_64-pc-windows-gnu" : "i686-pc-windows-gnu") : cargo_target;
                command = env_prefix + "cross " + cargo_toolchain + " build --target " + pe_triple + " " + profile.flag + features_flag;
                break;
            }
        }
        executor.execute_command(command, &res);
        bool success = res.code == 0;
        if(!success) {
            fcout << "$(warning)An error occurred during build, delegating to $(warning:b)RBF$ (Rust Build Fixer)..." << endl;
            success = RustBuildFixer(output_dir_name, type).process_error(command, res.response);
        }
        if(!success) continue;
        string build_dir;
        switch(type) {
            case BIN_TYPE::ELF: {
                string subdir = cargo_target.empty() ? "" : (cargo_target + "/");
                build_dir = output_dir_name+string("/target/")+subdir+profile.target_dir;
                break;
            }
            case BIN_TYPE::PE: {
                string pe_triple = cargo_target.empty() ? ((arch == BIN_ARCH::X86_64) ? "x86_64-pc-windows-gnu" : "i686-pc-windows-gnu") : cargo_target;
                build_dir = output_dir_name+string("/target/")+pe_triple+string("/")+profile.target_dir;
                break;
            }
        }
        if(fs::exists(build_dir)) {
            for (const auto& entry : fs::directory_iterator(build_dir)) {
                if (fs::is_regular_file(entry)) {
                    fs::path file_path = entry.path();
                    string file_name = file_path.filename();
                    if(ends_with(file_name, lib_extension)) {
                        if(profile.suffix.size()) {
                            file_name = file_name.substr(0, file_name.size()-lib_extension.size()) + profile.suffix + lib_extension;
                        }
                        fs::rename(file_path, this->work_dir+"/"+file_name);
                        any_success = true;
                    }
                }
            }
        }
    }
    if(COMPILE_TESTS) {
        for(string& artifact : test_artifacts) {
            if(fs::exists(artifact)) {
                string fname = lib.name + "__" + fs::path(artifact).filename().string();
                fs::rename(artifact, this->work_dir+"/"+fname);
            }
        }
    }
    fs::remove_all(output_dir_name);
    return any_success;
}