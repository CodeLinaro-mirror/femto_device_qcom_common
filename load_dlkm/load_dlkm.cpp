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
#include <algorithm>
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

#define AUDIO_AR_PROP            "audioreach"
#define AUDIO_AR_VIO_PROP        "audioreach_vio"
#define AUDIO_PROP               "ro.boot.audio"
#define LM_MODLIST_CAP           200

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

    property_get("ro.vendor.qti.sysdep.modlist", sysdepvndr_list, "");
    //ALOGI("LM : Vendor Modules list sysdep %s ", sysdepvndr_list);
    if (sysdepvndr_list[0]) {
       words = sysdepvndr_list;
       words += ",";
    }

    sysdepvndr_list[0] = 0;
    property_get("ro.vendor.qti.sysdep.wlan.modlist", sysdepvndr_list, "");
    // ALOGI("LM : Vendor Modules wlan list sysdep %s ", sysdepvndr_list);
    if (sysdepvndr_list[0]) words += sysdepvndr_list;

    std::vector<std::string> wlist = android::base::Split(words, ",");
    for (const auto& word : wlist) {
        if (word.empty()) continue;

        // List will have only module name but load file
        // will have extension, hence append ko
        std::string mod = word + ".ko";
        sysdep_list_.emplace(GetModuleName(mod));
    }
    // ALOGW("Sys Mod dependent Vnd Mod count: %d ", (int)sysdep_list_.size());
}

// Get Vendor Modules list dependent on System modules
void LoadDlkm::GetLastModList() {
    std::string words;
    char vlast_modlist[PROPERTY_VALUE_MAX] = {0};

    property_get("ro.vendor.qti.vlast_modlist", vlast_modlist, "");
    if (vlast_modlist[0]) {
       words = vlast_modlist;
    }
    std::vector<std::string> wlist = android::base::Split(words, ",");
    for (const auto& word : wlist) {
        if (word.empty()) continue;

        // List will have only module name but load file
        // will have extension, hence append ko
        std::string mod = word + ".ko";
        lastmod_list_.emplace(GetModuleName(mod));
    }
    // ALOGW("Get Last Vnd Mod count: %d ", (int)lastmod_list_.size());
}

// Based on property, get Audio Block list file
void LoadDlkm::UpdateAudioBlockListFile() {
    char value[PROP_VALUE_MAX] = {0};

    property_get(AUDIO_PROP, value, "");

    if (!strncmp(value, AUDIO_AR_VIO_PROP, strlen(AUDIO_AR_VIO_PROP))) {
        audio_bl_file_ = AUDIO_AR_VIO_MOD_BL_FILE;
    } else if (!strncmp(value, AUDIO_AR_PROP, strlen(AUDIO_AR_PROP))) {
        audio_bl_file_ = AUDIO_AR_MOD_BL_FILE;
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

int LoadDlkm::GetModListWithNoDep(const std::string& dep_file,
         std::unordered_set<std::string>& nodep_list) {
    std::string lines;
    if (!android::base::ReadFileToString(dep_file, &lines, false)) {
        return -1;
    }

    size_t pos = 0;
    size_t found;
    while (true) {
        found = lines.find_first_of("\n", pos);
        std::string line = lines.substr(pos, found - pos);
        // trim spaces, linefeed etc
        line = android::base::Trim(line);
        // format: ko_name : [dep ko names]
        size_t col_pos = line.find(':');
        if (col_pos != std::string::npos) {
            // No deps if its end of line
            if (line.size() == col_pos + 1) {
                size_t fs_pos = 0;
                if (line.size() >=2 && line[0] == '/' && line[1] == 'v') { // vendor dlkms
                    size_t slash_pos = line.rfind('/');
                    if (slash_pos != std::string::npos) fs_pos = slash_pos + 1;
                }
                if (col_pos > fs_pos) {
                    std::string mod = GetModuleName(line.substr(fs_pos, col_pos-fs_pos));
                    nodep_list.emplace(mod);
                }
            }
        }
        if (found == lines.npos) break;
        pos = found + 1;
    }

    return 0;
}

// Parse load file and store list of modules
int LoadDlkm::CreateModulesList(const std::string& load_file,
                 const std::string& bl_file) {
    std::string lines;
    std::vector<std::string> no_dep_list;
    if (!android::base::ReadFileToString(load_file, &lines, false)) {
        return -1;
    }

    std::unordered_set<std::string> nodep_modlist;

    const std::string expectedExt = ".load";
    const std::string depExt = ".dep";
    size_t dot_pos = load_file.rfind('.');
    // Check dep file from same load file path
    if (dot_pos != std::string::npos) {
        std::string currentExt = load_file.substr(dot_pos);
        if (currentExt == expectedExt) {
            std::string dep_file = load_file.substr(0, dot_pos) + depExt;
            GetModListWithNoDep(dep_file, nodep_modlist);
        }
    }

    size_t pos = 0;
    size_t found;
    std::unique_lock lk(mload_lock_); // Acquire lock
    mlist_.reserve(LM_MODLIST_CAP);

    const int NODEP_LIST_MAX = 32; // limit the nodep list
    int no_dep_count = NODEP_LIST_MAX;
    while (true) {
        found = lines.find_first_of("\n", pos);
        std::string mod = GetModuleName(lines.substr(pos, found - pos));

        // Add to defer list in order
        if (load_type_ == VendorDlkm &&
            (sysdep_list_.find(mod) != sysdep_list_.end())) {
            dfr_mlist_.emplace_back(mod);
        } else if (load_type_ == VendorDlkm &&
            lastmod_list_.find(mod) != lastmod_list_.end()) {
            last_mlist_.emplace_back(mod);
        } else if (nodep_modlist.size() &&
            nodep_modlist.find(mod) != nodep_modlist.end() && no_dep_count > 0) {
            no_dep_list.emplace_back(mod);
            no_dep_count--;
        } else {
            mlist_.emplace_back(mod);
        }
        if (found == lines.npos) break;
        pos = found + 1;
    }

    ALOGW("LM : %d type Modules nodep %d dep %d dfr %d", (int)load_type_,
            (int)no_dep_list.size(), (int)mlist_.size(), (int)dfr_mlist_.size());
    if (no_dep_list.size()) {
        // Insert nodep list at the beginning
        mlist_.insert(mlist_.begin(),
           std::make_move_iterator(no_dep_list.begin()),
           std::make_move_iterator(no_dep_list.end()));
       no_dep_list.clear();
    }
    int count = mlist_.size();

    lk.unlock(); // Reset lock

    // Update blocked modules list
    UpdateIgnoreList(bl_file);

    return count;
}

#define VENDOR_MODULES_DIR "/vendor_dlkm/lib/modules"
int LoadDlkm::GetVndrModulesList() {
    std::string loadfile_path, bl_file_path;
    int count = 0;

    std::vector<std::string> mdirs;
    mdirs.emplace_back(VENDOR_MODULES_DIR);
    mprobe_ = std::make_unique<Modprobe>(mdirs, load_file_);

    GetSysDepModules();
    GetLastModList();

    loadfile_path = VENDOR_MODULES_DIR "/";
    loadfile_path.append(load_file_);

    bl_file_path = VENDOR_MODULES_DIR "/";
    bl_file_path.append(bl_file_);

    // Get list of modules from load file along with blocked list filtered.
    if ((count = CreateModulesList(loadfile_path, bl_file_path)) <= 0) {
        ALOGE("LM : Create Vendor Modules list Failed!");
        return 0;
    }

    // Update audio related blocked modules list
    if (audio_bl_file_.size()) {
        std::string file_path = VENDOR_MODULES_DIR "/";
        file_path.append(audio_bl_file_);
        UpdateIgnoreList(file_path);
    }

    return count;

}
int LoadDlkm::LoadVndrModules() {
    boot_clock::time_point module_start_time = boot_clock::now();
    int mloaded = 0;

    // Load the modules from mlist
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
    std::unique_lock lk(mload_lock_);
    mlist_ = dfr_mlist_;
    lk.unlock();

    // Use existing Modprobe and Ignore List(ilist_)
    LoadModules(mprobe_);
    mloaded = (mprobe_->GetModuleCount() - mloaded);

    auto module_elapse_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                boot_clock::now() - module_start_time);
    ALOGW("LM : Deferred Vendor modules(%d) load time %d ", mloaded,
          (int)module_elapse_time.count());

    return mloaded;
}

int LoadDlkm::LoadVndrLastModules(std::mutex& mtx, std::condition_variable& cv,
                                  bool& th_created) {
    if (!mprobe_) {
        ALOGE("LM : Error Loading Last Set of Vendor modules - no modprobe obj");
        std::unique_lock cv_lk(mtx);
        th_created = true;
        cv_lk.unlock();
        cv.notify_one(); // Fail, notify to unblock.
        return 0;
    }
    boot_clock::time_point module_start_time = boot_clock::now();
    int mloaded = mprobe_->GetModuleCount();
    std::unique_lock lk(mload_lock_);
    mlist_ = last_mlist_;
    lk.unlock();

    std::unique_lock cv_lk(mtx);
    th_created = true;
    cv_lk.unlock();
    cv.notify_one(); // Notify that thread is created.

    // Use existing Modprobe and Ignore List(ilist_)
    LoadModules(mprobe_);
    mloaded = (mprobe_->GetModuleCount() - mloaded);

    auto module_elapse_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                boot_clock::now() - module_start_time);
    ALOGW("LM : Last Vendor modules(%d) load time %d ", mloaded,
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
    pid_t vdpid;
    int wstatus;

    (void)argc;
    android::base::InitLogging(argv, DlkmLogger);
    android::base::SetMinimumLogSeverity(android::base::WARNING);

    LoadDlkm vmload;
    // Initialize defaults
    vmload.init(VendorDlkm);
    // Get Vendor dlkm list
    vmload.GetVndrModulesList();

    if ((vdpid = fork()) == 0) {
        pid_t spid;
        int wstatus;
        if ((spid = fork()) == 0) {
            LoadDlkm smload;
            // Initialize defaults
            smload.init(SystemDlkm);
            // Load System dlkm list
            smload.LoadSysModules();
            _exit(0);
        } else if (spid < 0) {
            ALOGE("LM : Fork failed - Sys Modules, err %d", errno);
        }
        // wait for sys modules load complete
        if (spid > 0) waitpid(spid, &wstatus, 0);
        // Load Vendor Defered dlkm list
        vmload.LoadDfrVndrModules();
        _exit(0);
    } else if (vdpid < 0) {
        ALOGE("LM : Fork failed - VndrDfr Modules, err %d", errno);
    }

    // Load Vendor dlkm list
    vmload.LoadVndrModules();

    if (vdpid > 0) waitpid(vdpid, &wstatus, 0);

    // Load Vendor Last set of modules
    std::mutex mtx;
    std::condition_variable cv;
    bool th_created = false;

    auto lmod_load_thread_fn = [&] {
        vmload.LoadVndrLastModules(mtx, cv, th_created);
    };
    std::thread lmod_th(lmod_load_thread_fn);

    std::unique_lock cv_lk(mtx);
    cv.wait(cv_lk, [&] { return th_created; }); // wait for thread to start

    auto module_elapse_time0 =
        std::chrono::duration_cast<std::chrono::milliseconds>(
        boot_clock::now() - module_start_time);

    // set property to indicate modules loading is completed
    property_set("vendor.all.modules.ready", "1");
    cv_lk.unlock();
    lmod_th.join(); // wait for thread

    auto module_elapse_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(
        boot_clock::now() - module_start_time);
    ALOGW("LM : All modules loaded before prop: %dms Total: %dms",
         (int)module_elapse_time0.count(), (int)module_elapse_time.count());

    return 0;
}
