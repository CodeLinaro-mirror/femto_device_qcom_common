/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <sys/types.h>
#include <sys/wait.h>

#include <android-base/chrono_utils.h>
#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android-base/logging.h>
#include <utils/Log.h>
#include <cutils/properties.h>
#include <modprobe/modprobe.h>
#include <chrono>
#include <thread>
#include <vector>
#include <string>

#include <load_dlkm.h>

using android::base::boot_clock;

#define MOD_LOAD_FILE            "modules.load"
#define MOD_BL_FILE              "modules.blocklist"
#define MOD_SYS_BL_FILE          "system_dlkm.modules.blocklist"
#define AUDIO_MOD_BL_FILE        "modules.audio.legacy.blocklist"
#define AUDIO_AR_MOD_BL_FILE     "modules.audio.ar.blocklist"
#define AUDIO_AR_VIO_MOD_BL_FILE "modules.audio.ar_vio.blocklist"
#define AUDIO_LITE_MOD_BL_FILE   "modules.audio.audiolite.blocklist"

#define AUDIO_AR_PROP            "audioreach"
#define AUDIO_LITE_PROP          "audiolite"
#define AUDIO_AR_VIO_PROP        "audioreach_vio"
#define AUDIO_PROP               "ro.boot.audio"

LoadDlkm::LoadDlkm() :
  num_threads_(1) {
}

int LoadDlkm::init(ModuleLoadType type) {
    load_type_ = type;
    // set default modules load file name
    load_file_ = MOD_LOAD_FILE;
    // set default blocklist file name
    bl_file_ = MOD_BL_FILE;
    // set default blocklist filename for system modules
    sys_bl_file_ = MOD_SYS_BL_FILE;
    // set audio block list based on current Audio flavour configured.
    UpdateAudioBlockListFile();
    // set max parallel threads for loading modules.
    if (ParallelLoadEnabled()) {
        num_threads_ = std::thread::hardware_concurrency();
    }
    ALOGI("LM : Parallel threads %d", num_threads_);

    return 0;
}

// Kernel modules names can have '-', but file names can have '_'
std::string LoadDlkm::GetModuleName(const std::string& mod) {
    std::string mname = mod;
    std::replace(mname.begin(), mname.end(), '-', '_');

    return mname;
}

// Get Vendor Modules list dependent on System modules
void LoadDlkm::GetSysDepModules() {
    std::string words;
    char sysdepvndr_list[PROPERTY_VALUE_MAX] = {0};
    bool add_comma = false;

    property_get("ro.vendor.qti.sysdep.modlist", sysdepvndr_list, "");
    if (sysdepvndr_list[0]) {
       words = sysdepvndr_list;
       add_comma = true;
    }

    sysdepvndr_list[0] = 0;
    property_get("ro.vendor.qti.sysdep.wlan.modlist", sysdepvndr_list, "");
    if (sysdepvndr_list[0]) {
       if (add_comma) words += ",";
       words += sysdepvndr_list;
       add_comma = true;
    }

    sysdepvndr_list[0] = 0;
    property_get("ro.vendor.qti.sysdep.eth.modlist", sysdepvndr_list, "");
    if (sysdepvndr_list[0]) {
       if (add_comma) words += ",";
       words += sysdepvndr_list;
    }

    std::vector<std::string> wlist = android::base::Split(words, ",");
    for (const auto& word : wlist) {
        if (word.empty()) continue;

        // List will have only module name but load file
        // will have extension, hence append ko
        std::string mod = word + ".ko";
        sysdep_list_.emplace(GetModuleName(mod));
    }
    ALOGW("Sys Mod dependent Vnd Mod count: %d ", (int)sysdep_list_.size());
}

// Based on property, get Audio Block list file
void LoadDlkm::UpdateAudioBlockListFile() {
    char value[PROP_VALUE_MAX] = {0};

    property_get(AUDIO_PROP, value, "");

    if (!strncmp(value, AUDIO_AR_VIO_PROP, strlen(AUDIO_AR_VIO_PROP))) {
        audio_bl_file_ = AUDIO_AR_VIO_MOD_BL_FILE;
    } else if (!strncmp(value, AUDIO_AR_PROP, strlen(AUDIO_AR_PROP))) {
        audio_bl_file_ = AUDIO_AR_MOD_BL_FILE;
    } else if (!strncmp(value, AUDIO_LITE_PROP, strlen(AUDIO_LITE_PROP))) {
        audio_bl_file_ = AUDIO_LITE_MOD_BL_FILE;
    } else {
        audio_bl_file_ = AUDIO_MOD_BL_FILE;
    }

    ALOGI("LM : ro.boot.audio '%s'", value);
}

// Utility API: Modules parallel loading supported
bool LoadDlkm::ParallelLoadEnabled() {
    std::string bc;
    android::base::ReadFileToString("/proc/bootconfig", &bc);

    return (bc.find("androidboot.load_modules_parallel = \"true\"") !=
        std::string::npos);
}

// Load dlkm list in parallel. Max threads configured with num_threads_.
int LoadDlkm::LoadModules(std::unique_ptr<Modprobe>& mprobe) {
    std::vector<std::thread> th_list;
    int i = 0;
    auto mod_load_thread_fn = [&] {
        // create mutex and lock
        std::unique_lock lk(mload_lock_);
        while (i < mlist_.size()) {
            auto &mod = mlist_[i++];
            // Ignore if empty or in ignore list
            if (mod.empty() || ilist_.find(mod) != ilist_.end()) continue;
            if (load_type_ == VendorDlkm && sysdep_list_.find(mod) != sysdep_list_.end()) {
                // Access to dfr_mlist_ is protected by same mutex mload_lock_
                dfr_mlist_.emplace_back(mod);
                continue;
            }
            ilist_.emplace(mod); // add to ignore list to avoid duplicates
            lk.unlock();
            // Load entry - insert the module and its dependencies.
            mprobe->LoadWithAliases(mod, true);
            lk.lock();
        }
    };
    // generate num_threads_ parallel threads.
    std::generate_n(std::back_inserter(th_list), num_threads_,
                    [&] { return std::thread(mod_load_thread_fn); });
    for (auto& th : th_list) {
        th.join();
    }

  return 0;
}

// Create list from blocked list file.
void LoadDlkm::UpdateIgnoreList(const std::string blocklist_path) {
    std::string lines;

    if (android::base::ReadFileToString(blocklist_path, &lines, false)) {
        std::vector<std::string> rmlist = android::base::Split(lines, "\n");
        int found, pos;
        for (const auto& line : rmlist) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            // Blocklist file has 2 words with a space
            found = line.find_first_of(" ", 0);
            if (found == line.npos) continue;
            pos = found + 1;
            found = line.find_first_of(" ", pos);
            std::string mod = line.substr(pos, found - pos);
            if (mod.empty()) continue;
            // blocklist will have only module name but load file
            //  will have extension, hence append ko
            mod.append(".ko");
            ilist_.emplace(GetModuleName(mod));
        }
    }

}

// Parse load file and store list of modules
int LoadDlkm::CreateModulesList(const std::string& load_file,
                 const std::string& bl_file) {
    std::string lines;

    if (!android::base::ReadFileToString(load_file, &lines, false)) {
        return -1;
    }

    size_t pos = 0;;
    size_t found;
    while (true) {
        found = lines.find_first_of("\n", pos);
        std::string mod = lines.substr(pos, found - pos);
        mlist_.emplace_back(GetModuleName(mod));
        if (found == lines.npos) break;
        pos = found + 1;
    }

    // Update blocked modules list
    UpdateIgnoreList(bl_file);

    return mlist_.size();
}

#define VENDOR_MODULES_DIR "/vendor_dlkm/lib/modules"
int LoadDlkm::LoadVndrModules() {
    boot_clock::time_point module_start_time = boot_clock::now();
    std::vector<std::string> mlist;
    std::string loadfile_path, bl_file_path;
    int mloaded = 0;

    GetSysDepModules();
    loadfile_path = VENDOR_MODULES_DIR "/";
    loadfile_path.append(load_file_);

    bl_file_path = VENDOR_MODULES_DIR "/";
    bl_file_path.append(bl_file_);

    // Get list of modules from load file along with blocked list filtered.
    if (CreateModulesList(loadfile_path, bl_file_path) <= 0) {
        ALOGE("LM : Create Vendor Modules list Failed!");
        return 0;
    }

    // Update audio related blocked modules list
    if (audio_bl_file_.size()) {
        std::string file_path = VENDOR_MODULES_DIR "/";
        file_path.append(audio_bl_file_);
        UpdateIgnoreList(file_path);
    }

    // Load the modules from list
    std::vector<std::string> mdirs;
    mdirs.emplace_back(VENDOR_MODULES_DIR);
    mprobe_ = std::make_unique<Modprobe>(mdirs, load_file_);
    LoadModules(mprobe_);
    mloaded = mprobe_->GetModuleCount();

    auto module_elapse_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                boot_clock::now() - module_start_time);
    ALOGW("LM : Vendor modules(%d) load time %d ", mloaded,
          (int)module_elapse_time.count());

    return mloaded;
}


int LoadDlkm::LoadDfrVndrModules() {
    if (!mprobe_) {
        ALOGE("LM : Error Loading Deferred Vendor modules - no modprobe obj");
        return 0;
    }
    if (dfr_mlist_.empty()) {
        ALOGW("LM : No deferred vendor list to load!");
        return 0;
    }
    boot_clock::time_point module_start_time = boot_clock::now();
    int mloaded = mprobe_->GetModuleCount();
    load_type_ = DeferredVendorDlkm;
    mlist_ = dfr_mlist_;
    // Use existing Modprobe
    LoadModules(mprobe_);
    mloaded = (mprobe_->GetModuleCount() - mloaded);

    auto module_elapse_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                boot_clock::now() - module_start_time);
    ALOGW("LM : Deferred Vendor modules(%d) load time %d ", mloaded,
          (int)module_elapse_time.count());

    return mloaded;
}

#define SYSTEM_MODULES_DIR "/system_dlkm/lib/modules"
// Load system modules which are generated in subfolder of system modules folder
int LoadDlkm::LoadSysModules() {
    boot_clock::time_point module_start_time = boot_clock::now();
    int mloaded = 0;

    DIR* dir = opendir(SYSTEM_MODULES_DIR);
    // loop through subfolders to check modules load file
    if (dir) {
        int dfd = dirfd(dir);
        dirent* dname = nullptr;
        std::string dir_path;
        // Default blocklist for system modules
        std::string bl_file_path = VENDOR_MODULES_DIR "/";
        bl_file_path.append(sys_bl_file_);
        while ((dname = readdir(dir)) != nullptr) {
            struct stat st;
            if (strcmp(dname->d_name, ".") == 0 || strcmp(dname->d_name, "..") == 0) {
                continue;
            }
            if (fstatat(dfd, dname->d_name, &st, 0) != 0) {
                continue;
            }
            // Check only if its a folder
            if (S_ISDIR(st.st_mode)) {
                std::vector<std::string> mlist;
             	std::string dir_path = SYSTEM_MODULES_DIR "/";
                dir_path.append(dname->d_name);
                std::string load_file_path = dir_path;
                load_file_path.append("/");
                load_file_path.append(load_file_);
                // Check modules load file and create list from current folder
                if (access(load_file_path.c_str(), F_OK) == 0 &&
                    CreateModulesList(load_file_path, bl_file_path) > 0) {
                    // Load the modules from the list
                    std::vector<std::string> mdirs;
                    mdirs.emplace_back(dir_path);
                    mprobe_ = std::make_unique<Modprobe>(mdirs, load_file_);
                    LoadModules(mprobe_);
                    mloaded = mprobe_->GetModuleCount();
                    // Modules loaded from a folder, stop further processing
                    break;
                }
            }
        }
        closedir(dir);
    }

    auto module_elapse_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(
        boot_clock::now() - module_start_time);
    ALOGW("LM : System modules(%d) load time %d ", mloaded,
          (int)module_elapse_time.count());

    return mloaded;
}

void DlkmLogger(android::base::LogId id, android::base::LogSeverity severity,
      const char* tag, const char* file, unsigned int line, const char* msg) {
    if (msg && msg[0]) {
        android::base::KernelLogger(id, severity, tag, file, line, msg);
    }
}

int main(int argc, char** argv) {
    boot_clock::time_point module_start_time = boot_clock::now();
    pid_t spid;
    int wstatus;

    (void)argc;
    android::base::InitLogging(argv, DlkmLogger);
    android::base::SetMinimumLogSeverity(android::base::WARNING);

    if ((spid = fork()) == 0) {
        LoadDlkm smload;
        // Initialize defaults
        smload.init(SystemDlkm);
        // Load System dlkm list
        smload.LoadSysModules();
        exit(0);
    } else if (spid < 0) {
        ALOGE("LM : Fork failed - Sys Modules, err %d", errno);
    }



    LoadDlkm vmload;
    // Initialize defaults
    vmload.init(VendorDlkm);
    // Load Vendor dlkm list
    vmload.LoadVndrModules();

    if (spid > 0) waitpid(spid, &wstatus, 0);

    // Load System dlkm dependent deferred Vendor dlkm list
    vmload.LoadDfrVndrModules();

    // set property to indicate modules loading is completed
    property_set("vendor.all.modules.ready", "1");

    auto module_elapse_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(
        boot_clock::now() - module_start_time);
    ALOGW("LM : All modules load time %dms", (int)module_elapse_time.count());

    return 0;
}
