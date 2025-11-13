#pragma once
#include "MonitorBase.hpp"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <unistd.h>

class FPSMonitor : public MonitorBase {
private:
    std::string package_name_;
    std::vector<nlohmann::json> data_;
    int interval_ms_ = 1000;
    bool force_dumpsys_ = false;
    std::string fps_file_path_;
    bool sysfs_checked_ = false;

public:
    FPSMonitor(bool force_dumpsys = false) {
        force_dumpsys_ = force_dumpsys;
    }

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
        initSysFSPath();
        init_clock(interval_ms_);

        while (running_) {
            auto sample_time = std::chrono::steady_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 sample_time - start_time)
                                 .count();

            double fps = getFPS();

            if (fps > 0) {
                nlohmann::json sample;
                sample["time_ms"] = timestamp;
                sample["data"] = fps;
                data_.push_back(sample);
            }

            _Sleep__();
        }
    }

    double getFPS() {
        if (force_dumpsys_) {
            return getFPSFromDumpsys();
        }

        if (!fps_file_path_.empty()) {
            double fps = getFPSFromSysFS();
            if (fps > 0) {
                return fps;
            }
        }

        return getFPSFromDumpsys();
    }

    double getFPSFromSysFS() {
        std::string content = readFile(fps_file_path_);
        if (content.empty()) {
            return 0.0;
        }

        return extractFPSFromContent(content);
    }

    double getFPSFromDumpsys() {
        static int last_frame_number = -1;
        static auto last_frame_time = std::chrono::steady_clock::now();

        int frame_number = getCurrentFrameNumber();
        auto current_time = std::chrono::steady_clock::now();

        double fps = 0.0;

        if (frame_number != -1 && last_frame_number != -1) {
            int frame_diff = frame_number - last_frame_number;
            double time_diff = std::chrono::duration<double>(current_time - last_frame_time).count();

            if (frame_diff > 0 && time_diff > 0 && frame_diff <= 200 && time_diff <= 10.0) {
                fps = frame_diff / time_diff;
                if (fps > 200.0) {
                    fps = 0.0;  // 超出合理范围，视为无效
                }
            }
        }

        if (frame_number != -1) {
            last_frame_number = frame_number;
            last_frame_time = current_time;
        }

        return fps;
    }

    void initSysFSPath() {
        const char* paths[] = {
            "/sys/class/drm/sde-crtc-0/measured_fps",
            "/sys/class/graphics/fb0/measured_fps",
            nullptr};

        for (int i = 0; paths[i] != nullptr; i++) {
            if (access(paths[i], R_OK) == 0) {
                std::string content = readFile(paths[i]);
                if (!content.empty() && extractFPSFromContent(content) > 0) {
                    fps_file_path_ = paths[i];
                    return;
                }
            }
        }

        fps_file_path_.clear();
    }

    std::string readFile(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return "";
        }

        std::string content;
        std::getline(file, content);
        return content;
    }

    double extractFPSFromContent(const std::string& content) {
        // fps: 58.1 duration:500000 frame_count:30
        size_t fps_pos = content.find("fps:");
        if (fps_pos != std::string::npos) {
            return extractNumberAfterKeyword(content, fps_pos + 4);
        }

        for (size_t i = 0; i < content.size(); i++) {  //单个浮点
            if (isdigit(content[i]) || content[i] == '.' || content[i] == '-') {
                size_t start = i;
                size_t end = i + 1;
                bool has_dot = (content[i] == '.');

                while (end < content.size() &&
                       (isdigit(content[end]) || content[end] == '.' || content[end] == '-')) {
                    if (content[end] == '.') {
                        if (has_dot) break;
                        has_dot = true;
                    }
                    end++;
                }

                if (end > start) {
                    try {
                        double value = std::stod(content.substr(start, end - start));
                        if (value > 0 && value <= 200) {
                            return value;
                        }
                    } catch (const std::exception& e) {
                        ;
                    }
                }
                i = end;
            }
        }

        return 0.0;
    }

    double extractNumberAfterKeyword(const std::string& content, size_t start_pos) {
        size_t value_start = start_pos;
        while (value_start < content.size() &&
               (content[value_start] == ' ' || content[value_start] == '\t' ||
                content[value_start] == ':')) {
            value_start++;
        }

        size_t value_end = value_start;
        while (value_end < content.size() &&
               (isdigit(content[value_end]) || content[value_end] == '.' || content[value_end] == '-')) {
            value_end++;
        }

        if (value_end > value_start) {
            try {
                return std::stod(content.substr(value_start, value_end - value_start));
            } catch (const std::exception& e) {
                return 0.0;
            }
        }

        return 0.0;
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
            try {
                return std::stoi(output.substr(num_start, num_end - num_start));
            } catch (const std::exception& e) {
                return -1;
            }
        }

        return -1;
    }
};