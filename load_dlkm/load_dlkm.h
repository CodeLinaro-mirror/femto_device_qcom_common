/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <unordered_map>
#include <vector>
#include <string>
#include <modprobe/modprobe.h>

class LoadDlkm {
  public:
    LoadDlkm();
    int init();
    int LoadVndrModules();
    int LoadSysModules();

  private:
    std::mutex mload_lock_;
    std::string load_file_;
    std::string bl_file_;
    std::string sys_bl_file_;
    std::string audio_bl_file_;
    int num_threads_;
    std::vector<std::string> mlist_;
    std::unordered_set<std::string> ilist_;

    std::string GetModuleName(const std::string& mod);
    void UpdateAudioBlockListFile();
    bool ParallelLoadEnabled();
    int LoadModules(Modprobe& mprobe);
    void UpdateIgnoreList(const std::string blocklist_path);
    int CreateModulesList(const std::string& load_file,
                          const std::string& bl_file);
};
