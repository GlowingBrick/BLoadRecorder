#pragma once
#include "MonitorBase.hpp"
#include <cstdlib>
#include <cstring>

class FPSMonitor : public MonitorBase {
private:
    std::string package_name_;
    std::vector<nlohmann::json> data_;
    int interval_ms_ = 1000;
    
public:
    std::string name() override { return "fps"; }
    
    bool start(const std::string& pkgName, int interval_ms = 1000) override {
        package_name_ = pkgName;
        interval_ms_ = interval_ms;
        running_ = true;
        worker_thread_ = std::thread(&FPSMonitor::worker, this);
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
    void worker() {
        auto start_time = std::chrono::steady_clock::now();
        int last_frame_number = -1;
        auto last_frame_time = std::chrono::steady_clock::now();
        std::vector<double> fps_history;
        init_clock(interval_ms_);
        while (running_) {
            auto sample_time = std::chrono::steady_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                sample_time - start_time).count();
            
            int frame_number = getCurrentFrameNumber();
            auto current_time = std::chrono::steady_clock::now();
            
            double fps = 0.0;
            bool valid = false;
            
            if (frame_number != -1 && last_frame_number != -1) {
                int frame_diff = frame_number - last_frame_number;
                double time_diff = std::chrono::duration<double>(current_time - last_frame_time).count();
                
                if (frame_diff > 0 && time_diff > 0 && frame_diff <= 200 && time_diff <= 10.0) {
                    fps = frame_diff / time_diff;
                    if (fps <= 200.0) {
                        fps_history.push_back(fps);
                        valid = true;
                    }
                }
                
                last_frame_number = frame_number;
                last_frame_time = current_time;
            } else if (frame_number != -1) {
                last_frame_number = frame_number;
                last_frame_time = current_time;
            }
            
            if (valid) {
                nlohmann::json sample;
                sample["time_ms"] = timestamp;
                sample["data"] = fps;
                data_.push_back(sample);
            }
            
            _Sleep__();
        }
    }
    
    int getCurrentFrameNumber() {
        std::string cmd = "dumpsys SurfaceFlinger -latency " + package_name_ + 
                         " | grep 'frameNumber:' | tail -1";
        std::string output = executeCommand(cmd);
        return extractFrameNumber(output);
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
    
    int extractFrameNumber(const std::string& output) {
        if (output.empty()) return -1;
        
        size_t pos = output.find("frameNumber:");
        if (pos == std::string::npos) return -1;
        
        size_t num_start = output.find(':', pos) + 1;
        while (num_start < output.size() && 
               (output[num_start] == ' ' || output[num_start] == '\t')) {
            num_start++;
        }
        
        size_t num_end = num_start;
        while (num_end < output.size() && isdigit(output[num_end])) {
            num_end++;
        }
        
        if (num_end > num_start) {
            return std::stoi(output.substr(num_start, num_end - num_start));
        }
        
        return -1;
    }
};