#pragma once
#include "MonitorBase.hpp"
#include <vector>
#include <fstream>

class ThermalMonitor : public MonitorBase {
private:
    std::vector<std::string> temp_nodes_;
    std::vector<nlohmann::json> data_;
    int interval_ms_ = 1000;
    
public:
    std::string name() override { return "thermal"; }
    
    bool start(const std::string& pkgName, int interval_ms = 1000) override {
        interval_ms_ = interval_ms;
        discoverThermalNodes();
        running_ = true;
        worker_thread_ = std::thread(&ThermalMonitor::worker, this);
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
    void discoverThermalNodes() {
        temp_nodes_.clear();
        
        std::string thermal_base = "/sys/devices/virtual/thermal";
        DIR* thermal_dir = opendir(thermal_base.c_str());
        if (thermal_dir) {
            struct dirent* entry;
            while ((entry = readdir(thermal_dir)) != nullptr) {
                std::string dir_name = entry->d_name;
                if (dir_name == "." || dir_name == "..") continue;
                
                std::string device_path = thermal_base + "/" + dir_name;
                std::string type_path = device_path + "/type";
                std::string temp_path = device_path + "/temp";
                
                std::ifstream type_file(type_path);
                if (!type_file) continue;
                
                std::string device_type;
                std::getline(type_file, device_type);
                
                if (device_type.find("cpu") != std::string::npos ||
                    device_type.find("soc") != std::string::npos) {
                    
                    std::ifstream temp_test(temp_path);
                    if (temp_test) {
                        temp_nodes_.push_back(temp_path);
                    }
                }
            }
            closedir(thermal_dir);
        }
    }
    
    void worker() {
        auto start_time = std::chrono::steady_clock::now();
        
        while (running_) {
            auto sample_time = std::chrono::steady_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                sample_time - start_time).count();
            
            unsigned long long max_temp = 0;
            init_clock(interval_ms_);
            for (const auto& node : temp_nodes_) {
                std::ifstream temp_file(node);
                if (temp_file) {
                    std::string temp_str;
                    std::getline(temp_file, temp_str);
                    
                    if (!temp_str.empty()) {
                        try {
                            unsigned long long temp = std::stoull(temp_str);
                            if (temp > 1000) {
                                temp = temp / 1000; // 毫摄氏度转摄氏度
                            }
                            if (temp > max_temp) {
                                max_temp = temp;
                            }
                        } catch (...) {}
                    }
                }
            }
            
            nlohmann::json sample;
            sample["time_ms"] = timestamp;
            sample["data"] = max_temp;
            
            data_.push_back(sample);
            _Sleep__();
        }
    }
};