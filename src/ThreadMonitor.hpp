#pragma once
#include "MonitorBase.hpp"
#include <map>
#include <time.h>
#include <unordered_set>

class ThreadMonitor : public MonitorBase {  //监控所有进程
private:
    struct ThreadInfo {
        std::string name;
        int tid;
        double cpu_usage = 0;
        std::string affinity;
        unsigned long long last_user_time = 0;
        unsigned long long last_sys_time = 0;
        unsigned long long last_total_time = 0;
        timespec last_sample_time = {0, 0};
        bool active = false;
        bool initialized = false; 
    };
    
    struct ProcessInfo {
        int pid;
        std::string name;
        std::map<int, ThreadInfo> threads;
        bool valid = true;
        timespec last_scan_time = {0, 0};
        timespec last_change_time = {0, 0};
    };
    
    std::string package_name_;
    int self_pid_;
    std::vector<nlohmann::json> data_;
    int interval_ms_ = 1000;
    std::map<int, ProcessInfo> processes_;
    double load_threshold_ = 0.1;
    
    const long THREAD_SCAN_INTERVAL_NS = 2 * 1000000000L;
    const long MIN_SLEEP_US = 100;
    
    timespec last_process_scan_time_ = {0, 0};
    
    std::map<std::pair<int, int>, ThreadInfo> global_thread_stats_;
    
public:
    ThreadMonitor() {
        self_pid_ = getpid();
    }
    
    std::string name() override { return "thread"; }
    
    bool start(const std::string& pkgName, int interval_ms = 1000) override {
        package_name_ = pkgName;
        interval_ms_ = interval_ms;
        running_ = true;
        worker_thread_ = std::thread(&ThreadMonitor::worker, this);
        return true;
    }
    
    nlohmann::json stop() override {
        running_ = false;
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        return data_;
    }
    
    void setLoadThreshold(double threshold) {
        load_threshold_ = threshold;
    }
    
private:
    void worker() {
        auto start_time = std::chrono::steady_clock::now();
        init_clock(interval_ms_);
        
        while (running_) {
            if (shouldScanProcesses()) {
                ScanProcess();    //不总是扫进程
            }
            
            updateThreadsInfo();
            
            OptData(start_time);
            
            usleep(MIN_SLEEP_US);
            _Sleep__();
        }
    }
    
    bool shouldScanProcesses() {   
        static u_int8_t i=5;
        if(++i>=5){
            i=0;
            return true;
        }else{
            return false;
        }
    }
    
    void ScanProcess() {  //扫描所有进程
        std::vector<ProcessInfo> new_processes;
        FindProcesses(new_processes);
        
        clock_gettime(CLOCK_MONOTONIC, &last_process_scan_time_);
        
        std::map<int, ProcessInfo> new_process_map;
        std::unordered_set<int> current_pids;
        
        for (const auto& proc : new_processes) {
            current_pids.insert(proc.pid);
            new_process_map[proc.pid] = proc;
        }
        
        for (auto& [pid, old_proc] : processes_) {
            if (current_pids.count(pid)) {
                new_process_map[pid].threads = std::move(old_proc.threads);
                new_process_map[pid].last_scan_time = old_proc.last_scan_time;
                new_process_map[pid].last_change_time = old_proc.last_change_time;
            }
        }
        
        processes_ = std::move(new_process_map);
        
    }
    
    void updateThreadsInfo() {  //更新线程数据
        timespec current_time;
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        static u_int8_t i=2;
        bool sc=false;
        if(++i>=2){
            sc=true;
            i=0;
        }
        
        for (auto& [pid, proc] : processes_) {
            if (!proc.valid) continue;
            
            if (sc) {
                scanAndUpdateThreads(proc, current_time);
            } else {
                updateExistingThreadsCPU(proc, current_time);
            }
        }
    }
    
    void scanAndUpdateThreads(ProcessInfo& proc, const timespec& current_time) {
        std::string task_dir = "/proc/" + std::to_string(proc.pid) + "/task/";
        DIR* dir = opendir(task_dir.c_str());
        if (!dir) {
            proc.valid = false;
            return;
        }
        
        std::unordered_set<int> current_tids;
        bool threads_changed = false;
        
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr && running_) {
            std::string tid_str = entry->d_name;
            if (tid_str == "." || tid_str == "..") continue;
            
            if (tid_str.find_first_not_of("0123456789") == std::string::npos) {
                int tid = std::stoi(tid_str);
                current_tids.insert(tid);
                
                auto thread_it = proc.threads.find(tid);
                if (thread_it == proc.threads.end()) {
                    ThreadInfo thread_info;
                    if (initializeThreadInfo(proc.pid, tid, thread_info, current_time)) {   //发现新线程
                        proc.threads[tid] = thread_info;
                        threads_changed = true;
                    }
                } else {
                    if (updateThreadCPUUsage(proc.pid, tid, thread_it->second, current_time)) {
                        threads_changed = true;
                    }
                }
            }
        }
        
        closedir(dir);
        
        auto thread_it = proc.threads.begin();
        while (thread_it != proc.threads.end()) {
            if (!current_tids.count(thread_it->first)) {
                thread_it = proc.threads.erase(thread_it);  //消失的线程
                threads_changed = true;
            } else {
                ++thread_it;
            }
        }
        
        if (threads_changed) {
            proc.last_change_time = current_time;
        }
        
    }
    
    void updateExistingThreadsCPU(ProcessInfo& proc, const timespec& current_time) {
        for (auto& [tid, thread_info] : proc.threads) {
            updateThreadCPUUsage(proc.pid, tid, thread_info, current_time);
        }
    }
    
    bool initializeThreadInfo(int pid, int tid, ThreadInfo& thread_info, const timespec& current_time) {
        thread_info.tid = tid;
        
        std::string comm_path = "/proc/" + std::to_string(pid) + "/task/" + 
                               std::to_string(tid) + "/comm";
        std::ifstream comm_file(comm_path);
        if (comm_file) {
            std::getline(comm_file, thread_info.name);
            if (!thread_info.name.empty() && thread_info.name.back() == '\n') {
                thread_info.name.pop_back();
            }
        } else {
            thread_info.name = "thread-" + std::to_string(tid);
        }
        
        thread_info.affinity = getThreadAffinity(pid, tid);
        
        if (!readThreadStat(pid, tid, thread_info)) {
            return false;
        }
        
        thread_info.last_sample_time = current_time;
        thread_info.initialized = true;
        
        return true;
    }
    
    bool readThreadStat(int pid, int tid, ThreadInfo& thread_info) {
        std::string stat_path = "/proc/" + std::to_string(pid) + "/task/" + 
                               std::to_string(tid) + "/stat";
        std::ifstream stat_file(stat_path);
        if (!stat_file) {
            return false;
        }
        
        std::string line;
        std::getline(stat_file, line);
        
        size_t first_paren = line.find('(');
        size_t last_paren = line.rfind(')');
        if (first_paren == std::string::npos || last_paren == std::string::npos) {
            return false;
        }
        
        std::string after_paren = line.substr(last_paren + 2);
        std::istringstream iss(after_paren);
        
        std::vector<std::string> fields;
        std::string field;
        while (iss >> field) {
            fields.push_back(field);
        }
        
        if (fields.size() < 13) return false;
        
        thread_info.last_user_time = std::stoull(fields[11]);
        thread_info.last_sys_time = std::stoull(fields[12]);
        thread_info.last_total_time = thread_info.last_user_time + thread_info.last_sys_time;
        
        return true;
    }
    
    bool updateThreadCPUUsage(int pid, int tid, ThreadInfo& thread_info, const timespec& current_time) {    //计算cpu使用量
        unsigned long long old_total_time = thread_info.last_total_time;
        timespec old_sample_time = thread_info.last_sample_time;
        
        if (!readThreadStat(pid, tid, thread_info)) {
            return false;
        }
        
        long clock_ticks_per_second = sysconf(_SC_CLK_TCK); 
        if (clock_ticks_per_second <= 0) {
            clock_ticks_per_second = 100;
        }
        
        if (old_total_time > 0 && thread_info.initialized) {
            unsigned long long time_diff = thread_info.last_total_time - old_total_time;
            double time_seconds = getTimeDiff(old_sample_time, current_time);   //计算时间差
            
            if (time_seconds > 0) {
                double usage = (time_diff * 100.0) / (clock_ticks_per_second * time_seconds);
                
                if (usage < 0) usage = 0;
                if (usage > 100) usage = 100;
                
                thread_info.cpu_usage = usage;
                thread_info.active = (usage >= load_threshold_);
                
            } else {
                thread_info.cpu_usage = 0.0;
                thread_info.active = false;
            }
        } else {
            thread_info.cpu_usage = 0.0;
            thread_info.active = false;
        }
        
        thread_info.last_sample_time = current_time;
        thread_info.initialized = true;
        
        return true;
    }
    
    void OptData(std::chrono::steady_clock::time_point starttime) { //整理数据
        nlohmann::json sample;
        sample["time_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - starttime).count();
        sample["data"] = nlohmann::json::array();
        
        bool has_data = false;
        
        for (const auto& [pid, proc] : processes_) {
            if (!proc.valid) continue;
            
            nlohmann::json process_data = nlohmann::json::array();
            int active_threads = 0;
            
            for (const auto& [tid, thread] : proc.threads) {
                if (thread.cpu_usage >= load_threshold_) {
                    nlohmann::json thread_data;
                    thread_data["name"] = thread.name;
                    thread_data["tid"] = tid;
                    thread_data["load"] = thread.cpu_usage;
                    thread_data["cpu-set"] = thread.affinity;
                    process_data.push_back(thread_data);
                    active_threads++;
                }
            }
            
            if (active_threads > 0) {
                sample["data"].push_back({
                    {"pid", pid},
                    {"name", proc.name},
                    {"threads", process_data}
                });
                has_data = true;
            }
        }
        
        if (has_data) {
            data_.push_back(sample);
        }
    }
    

    void FindProcesses(std::vector<ProcessInfo>& processes) {   //寻找进程
        processes.clear();
        
        DIR* proc_dir = opendir("/proc");
        if (!proc_dir) return;
        
        struct dirent* entry;
        while ((entry = readdir(proc_dir)) != nullptr && running_) {
            std::string dir_name = entry->d_name;
            if (dir_name.find_first_not_of("0123456789") == std::string::npos) {
                int pid = std::stoi(dir_name);
                
                if (pid == self_pid_) continue;
                
                ProcessInfo proc_info;
                proc_info.pid = pid;
                proc_info.valid = true;
                
                std::string comm_path = "/proc/" + dir_name + "/comm";
                std::ifstream comm_file(comm_path);
                if (comm_file) {
                    std::getline(comm_file, proc_info.name);

                    if (!proc_info.name.empty() && proc_info.name.back() == '\n') {
                        proc_info.name.pop_back();
                    }
                }
                
                std::string cmdline_path = "/proc/" + dir_name + "/cmdline";
                std::ifstream cmdline_file(cmdline_path);
                if (cmdline_file) {
                    std::string cmdline;
                    std::getline(cmdline_file, cmdline);
                    
                    size_t first_null = cmdline.find('\0');
                    if (first_null != std::string::npos) {
                        cmdline = cmdline.substr(0, first_null);
                    }
                    
                    size_t last_slash = cmdline.rfind('/');
                    if (last_slash != std::string::npos) {
                        cmdline = cmdline.substr(last_slash + 1);
                    }
                    
                    if (proc_info.name.find(package_name_) != std::string::npos ||
                        cmdline.find(package_name_) != std::string::npos) {
                            processes.push_back(proc_info);
                    }
                } else if (!proc_info.name.empty() && 
                          proc_info.name.find(package_name_) != std::string::npos) {
                        processes.push_back(proc_info);
                }
            }
        }
        
        closedir(proc_dir);
    }
    
    std::string getThreadAffinity(int pid, int tid) {   //读取核心亲和性
        std::string status_path = "/proc/" + std::to_string(pid) + "/task/" + 
                                 std::to_string(tid) + "/status";
        std::ifstream status_file(status_path);
        if (status_file) {
            std::string line;
            while (std::getline(status_file, line)) {
                if (line.find("Cpus_allowed_list:") == 0) {
                    size_t pos = line.find(':');
                    if (pos != std::string::npos) {
                        std::string affinity = line.substr(pos + 1);
                        affinity.erase(0, affinity.find_first_not_of(" \t"));
                        affinity.erase(affinity.find_last_not_of(" \t") + 1);
                        if (!affinity.empty()) {
                            return affinity;
                        }
                    }
                }
            }
        }
        
        return "N/A";
    }

    std::string executeCommand(const std::string& cmd) {
        std::string result;
        char buffer[128];
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return "";
        
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
        
        pclose(pipe);
        return result;
    }

    std::string maskToCpuList(const std::string& mask) {
        try {
            unsigned long mask_value = std::stoul(mask, nullptr, 16);
            std::vector<int> cpus;
            
            for (int i = 0; i < 64; i++) {
                if (mask_value & (1UL << i)) {
                    cpus.push_back(i);
                }
            }
            
            if (cpus.empty()) {
                return "none";
            }
            
            std::stringstream ss;
            for (size_t i = 0; i < cpus.size(); i++) {
                ss << cpus[i];
                if (i < cpus.size() - 1) {
                    ss << ",";
                }
            }
            
            return ss.str();
        } catch (const std::exception& e) {
            return mask;
        }
    }

    double getTimeDiff(const timespec& start, const timespec& end) {
        return static_cast<double>(end.tv_sec - start.tv_sec) +
               static_cast<double>(end.tv_nsec - start.tv_nsec) * 1e-9;
    }
};