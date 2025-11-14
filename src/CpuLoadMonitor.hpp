#pragma once
#include "MonitorBase.hpp"
#include <fstream>
#include <sstream>

class CPULoadMonitor : public MonitorBase {
private:
    struct CoreStat {
        unsigned long long user, nice, system, idle, iowait, irq, softirq;
    };
    
    int core_count_ = 0;
    std::vector<CoreStat> last_core_stats_;
    std::vector<nlohmann::json> data_;
    int interval_ms_ = 1000;
    std::string gpu_load_node_;
    bool has_gpu_ = false;
    
public:
    std::string name() override { return "cpu_load"; }
    
    bool start(const std::string& pkgName, int interval_ms = 1000) override {
        interval_ms_ = interval_ms;
        discoverCores();
        running_ = true;
        worker_thread_ = std::thread(&CPULoadMonitor::worker, this);
        return true;
    }
    
    nlohmann::json stop() override {
        running_ = false;
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        return data_;
    }
    
private:
    void discoverCores() {
        core_count_ = 0;
        std::string cpu_base = "/sys/devices/system/cpu";
        DIR* cpu_dir = opendir(cpu_base.c_str());
        if (cpu_dir) {
            std::set<int> cpu_ids; // 使用set自动排序和去重
            
            struct dirent* entry;
            while ((entry = readdir(cpu_dir)) != nullptr) {
                std::string dir_name = entry->d_name;
                if (dir_name.find("cpu") == 0 && dir_name.length() > 3) {
                    std::string cpu_id_str = dir_name.substr(3);
                    
                    // 检查是否是数字
                    bool is_number = true;
                    for (char c : cpu_id_str) {
                        if (!isdigit(c)) {
                            is_number = false;
                            break;
                        }
                    }
                    
                    if (is_number) {
                        int cpu_id = std::stoi(cpu_id_str);
                        cpu_ids.insert(cpu_id);
                    }
                }
            }
            closedir(cpu_dir);
            
            core_count_ = cpu_ids.size();
            

        }

        const std::vector<std::string> gpu_load_nodes = {
            "/sys/class/kgsl/kgsl-3d0/devfreq/gpu_load",  // 高通
            "/sys/devices/platform/soc/3d00000.qcom,kgsl-3d0/devfreq/3d00000.qcom,kgsl-3d0/gpu_load",  //也是高通
            "/sys/kernel/gpu/gpu_busy"
        };
        has_gpu_ = false;
        for (const auto& node : gpu_load_nodes) {
            if (access(node.c_str(), R_OK) == 0) {
                has_gpu_ = true;
                gpu_load_node_ = node;
                break;
            }
        }
        
    }
    
    void worker() {
        auto start_time = std::chrono::steady_clock::now();
        
        while (running_) {
            auto sample_time = std::chrono::steady_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                sample_time - start_time).count();
            
            std::vector<CoreStat> current_stats(core_count_);
            std::ifstream stat_file("/proc/stat");
            init_clock(interval_ms_);
            if (stat_file) {
                std::string line;
                int core_index = 0;
                
                // 跳过总的cpu行
                std::getline(stat_file, line);
                
                while (std::getline(stat_file, line) && core_index < core_count_) {
                    if (line.find("cpu") == 0 && line[3] >= '0' && line[3] <= '9') {
                        std::istringstream iss(line);
                        std::string cpu_label;
                        iss >> cpu_label;
                        
                        CoreStat& stat = current_stats[core_index];
                        iss >> stat.user >> stat.nice >> stat.system >> stat.idle 
                            >> stat.iowait >> stat.irq >> stat.softirq;
                        core_index++;
                    }
                }
                
                nlohmann::json sample;
                sample["time_ms"] = timestamp;
                sample["data"] = nlohmann::json::array();
                
                if (!last_core_stats_.empty()) {
                    for (int i = 0; i < core_count_; i++) {
                        const CoreStat& last = last_core_stats_[i];
                        const CoreStat& current = current_stats[i];
                        
                        unsigned long long last_total = last.user + last.nice + last.system + last.idle + 
                                                      last.iowait + last.irq + last.softirq;
                        unsigned long long current_total = current.user + current.nice + current.system + current.idle + 
                                                         current.iowait + current.irq + current.softirq;
                        
                        unsigned long long total_diff = current_total - last_total;
                        unsigned long long idle_diff = current.idle - last.idle;
                        
                        double load = 0.0;
                        if (total_diff > 0) {
                            load = 100.0 * (1.0 - static_cast<double>(idle_diff) / total_diff);
                        }
                        
                        nlohmann::json core_data;
                        core_data["name"] = "cpu" + std::to_string(i);
                        core_data["load"] = load;
                        sample["data"].push_back(core_data);
                    }
                }

                if(has_gpu_){
                    std::ifstream file(gpu_load_node_);
                    if (file) {
                        int load = 0;
                        if (file >> load) {
                            nlohmann::json gpu_data;
                            gpu_data["name"] = "gpu";
                            gpu_data["load"] = static_cast<double>(load); 
                            sample["data"].push_back(gpu_data);
                        }
                    }
                }
                
                data_.push_back(sample);
                last_core_stats_ = current_stats;
            }
            
            _Sleep__();
        }
    }
};