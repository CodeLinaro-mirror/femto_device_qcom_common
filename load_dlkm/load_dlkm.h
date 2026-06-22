/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <modprobe/modprobe.h>

enum ModuleLoadType {
    VendorDlkm,
    SystemDlkm,
    DeferredVendorDlkm,
};

class LoadDlkm {
  public:
    LoadDlkm();
    int init(ModuleLoadType type);
    int LoadVndrModules();
    int LoadDfrVndrModules();
    int LoadSysModules();

  private:
    std::mutex mload_lock_;
    std::string load_file_;
    std::string bl_file_;
    std::string sys_bl_file_;
    std::string audio_bl_file_;
    int num_threads_;
    ModuleLoadType load_type_;
    std::vector<std::string> mlist_;
    std::vector<std::string> dfr_mlist_;
    std::unordered_set<std::string> ilist_;
    std::unordered_set<std::string> sysdep_list_;
    std::unique_ptr<Modprobe> mprobe_;

    std::string GetModuleName(const std::string& mod);
    void UpdateAudioBlockListFile();
    bool ParallelLoadEnabled();
    int LoadModules(std::unique_ptr<Modprobe>& mprobe);
    void UpdateIgnoreList(const std::string blocklist_path);
    int CreateModulesList(const std::string& load_file,
                          const std::string& bl_file);
    void GetSysDepModules();
};
