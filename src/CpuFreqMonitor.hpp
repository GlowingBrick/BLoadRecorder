#pragma once
#include "MonitorBase.hpp"
#include <fstream>
#include <set>
#include <string>
#include <vector>
class CPUFreqMonitor : public MonitorBase {
private:
    std::vector<std::string> cpu_freq_nodes_;
    std::vector<std::string> cpu_names_;
    std::string gpu_freq_node_;
    bool has_gpu_ = false;
    std::vector<nlohmann::json> data_;
    int interval_ms_ = 1000;

public:
    std::string name() override { return "cpu_freq"; }

    bool start(const std::string& pkgName, int interval_ms = 1000) override {
        interval_ms_ = interval_ms;
        discoverFrequencyNodes();
        running_ = true;
        worker_thread_ = std::thread(&CPUFreqMonitor::worker, this);
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
    void discoverFrequencyNodes() {
        cpu_freq_nodes_.clear();
        cpu_names_.clear();

        std::string cpu_base = "/sys/devices/system/cpu";
        DIR* cpu_dir = opendir(cpu_base.c_str());
        if (cpu_dir) {
            // 使用map来自动排序
            std::map<int, std::pair<std::string, std::string>> cpu_map;  // cpu_id -> (node_path, cpu_name)

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
                        std::string freq_path = cpu_base + "/" + dir_name + "/cpufreq/cpuinfo_cur_freq";

                        if (access(freq_path.c_str(), R_OK) == 0) {
                            cpu_map[cpu_id] = {freq_path, dir_name};
                        }
                    }
                }
            }
            closedir(cpu_dir);

            // 按cpu_id从小到大添加到vector中
            for (const auto& [cpu_id, node_info] : cpu_map) {
                cpu_freq_nodes_.push_back(node_info.first);
                cpu_names_.push_back(node_info.second);
            }
        }

        const std::vector<std::string> gpu_freq_nodes = {
            "/sys/class/kgsl/kgsl-3d0/gpuclk",  // 高通
            "/sys/devices/platform/soc/3d00000.qcom,kgsl-3d0/devfreq/3d00000.qcom,kgsl-3d0/gpuclk",  //也是高通
            "/sys/devices/platform/13000000.mali/devfreq/13000000.mali/cur_freq",  // Mali
            "/sys/kernel/ged/hal/current_freqency",
            "/sys/kernel/debug/ged/hal/current_freqency",
            "/sys/kernel/gpu/gpu_clock",
            "/sys/class/devfreq/gpufreq/cur_freq"
        };
        has_gpu_ = false;
        for (const auto& node : gpu_freq_nodes) {
            if (access(node.c_str(), R_OK) == 0) {
                has_gpu_ = true;
                gpu_freq_node_ = node;
                break;
            }
        }
    }

    void worker() {
        auto start_time = std::chrono::steady_clock::now();
        init_clock(interval_ms_);
        while (running_) {
            auto sample_time = std::chrono::steady_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 sample_time - start_time)
                                 .count();

            nlohmann::json sample;
            sample["time_ms"] = timestamp;
            sample["data"] = nlohmann::json::array();

            // 采集CPU频率
            for (size_t i = 0; i < cpu_freq_nodes_.size(); i++) {
                std::ifstream file(cpu_freq_nodes_[i]);
                if (file) {
                    long freq_hz = 0;
                    if (file >> freq_hz) {
                        nlohmann::json cpu_data;
                        cpu_data["name"] = cpu_names_[i];
                        cpu_data["freq"] = freq_hz;
                        sample["data"].push_back(cpu_data);
                    }
                }
            }

            // 采集GPU频率
            if (has_gpu_) {
                std::ifstream file(gpu_freq_node_);
                if (file) {
                    long freq_hz = 0;
                    if (file >> freq_hz) {
                        nlohmann::json gpu_data;
                        gpu_data["name"] = "gpu";
                        gpu_data["freq"] = freq_hz / 1000;  // 对齐单位
                        sample["data"].push_back(gpu_data);
                    }
                }
            }

            data_.push_back(sample);
            _Sleep__();
        }
    }
};