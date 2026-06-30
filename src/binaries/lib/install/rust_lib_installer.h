#ifndef CERBERUS_RUST_LIB_INSTALLER_H
#define CERBERUS_RUST_LIB_INSTALLER_H

#include <binaries/lib/install/lib_installer.h>
#include <utils/file_downloader.h>
#include <command/command_executor.h>

extern bool COMPILE_TESTS;
extern std::string PROFILE_TEMPLATE;
extern bool ALL_FEATURES;
extern std::string TARGET_TRIPLE;

struct PATCH {
    std::string name;
    void (*patch_func)(std::string crate_path);
};

class RustBuildFixer {
private:
    std::string crate_path;
    BIN_TYPE type;
    PATCH* last_patch = nullptr;
    CommandExecutor executor;
public:
    RustBuildFixer(std::string crate_path, BIN_TYPE type) : crate_path(crate_path), type(type), executor(crate_path) {}
    bool process_error(std::string command, std::string error);
};

class RustLibInstaller : public LibInstaller {
private:
    FileDownloader downloader;
    std::string bin_path;
    std::string cargo_toolchain;   // "" -> local toolchain, else "+<version>" (e.g. "+1.70.0")
    std::string cargo_target;      // build target triple ("" -> host). e.g. x86_64-unknown-linux-musl
    std::string detect_rustc_version();
    std::string get_local_rustc_version();
    bool ensure_toolchain(std::string version);
    void check_and_install_arch(std::string arch_name);
    void build_test_binaries(std::string crate_path, std::vector<std::string>& out);
public:
    RustLibInstaller(std::string work_dir, BIN_ARCH arch, BIN_TYPE type, std::string bin_path = "", bool static_target = false);
    bool install_lib(LIBRARY lib) override;
    bool pre_install_hook(std::vector<std::unique_ptr<LIBRARY>>& libs) override {return true;}
    bool post_install_hook() override {return true;}
};

#endif //CERBERUS_RUST_LIB_INSTALLER_H
