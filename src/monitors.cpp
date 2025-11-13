// test_monitors.cpp
#include "CpuFreqMonitor.hpp"
#include "CpuLoadMonitor.hpp"
#include "FpsMonitor.hpp"
#include "MonitorBase.hpp"
#include "ThermalMonitor.hpp"
#include "ThreadMonitor.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

#include "draw_svg.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

class MainMonitor {
private:
    std::vector<std::unique_ptr<MonitorBase>> monitors_;
    std::string package_name_;
    int test_duration_;

public:
    MainMonitor(const std::string& pkgName, int duration_seconds = 10)
        : package_name_(pkgName), test_duration_(duration_seconds) {}

    void startTest() {

        std::cout << "包名: " << package_name_ << std::endl;

        // 创建监控器实例
        monitors_.push_back(std::make_unique<CPUFreqMonitor>());
        monitors_.push_back(std::make_unique<CPULoadMonitor>());
        monitors_.push_back(std::make_unique<ThermalMonitor>());
        monitors_.push_back(std::make_unique<FPSMonitor>());
        monitors_.push_back(std::make_unique<ThreadMonitor>());

        std::cout << "启动监控器..." << std::endl;
        for (auto& monitor : monitors_) {
            std::cout << "启动: " << monitor->name() << std::endl;
            if (!monitor->start(package_name_, 1000)) {
                std::cout << monitor->name() << " 启动失败" << std::endl;
            }
        }

        for (int i = test_duration_; i > 0; --i) {
            std::cout << "剩余时间: " << i << "秒\r" << std::flush;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        std::cout << std::endl;

        nlohmann::json result;
        for (auto& monitor : monitors_) {
            std::cout << "停止: " << monitor->name() << std::endl;
            result[monitor->name()] = monitor->stop();
        }

        saveToFile(result);
        draw_svg(result, package_name_);
    }

private:
    void saveToFile(const nlohmann::json& data) {
        std::ofstream file("monitor_test.json");
        if (file.is_open()) {
            file << data.dump(4);
            file.close();
        }
    }
};

std::string getForegroundApp_lru() {  //捕捉前台游戏
    FILE* pipe = popen("dumpsys activity lru", "r");
    if (!pipe) return "";

    char buffer[256];
    int lineCount = 0;
    std::string result = "";

    while (fgets(buffer, sizeof(buffer), pipe)) {
        if (result.empty()) {
            size_t len = strlen(buffer);
            size_t startPos = 0;
            size_t endPos = 0;

            for (size_t i = 16; i < len; ++i) {
                if (buffer[i] == ':') {
                    startPos = i + 1;                       //包名起始点
                } else if (buffer[i] == '/' && startPos) {  //包名结束
                    endPos = i;
                    break;
                }
            }

            //确保是TOP
            if (startPos && endPos && endPos > startPos) {
                bool foundValidTOP = false;
                for (int i = startPos - 4; i >= 0; --i) {
                    if (i + 3 < static_cast<int>(startPos) &&
                        buffer[i] == 'T' &&
                        buffer[i + 1] == 'O' &&
                        buffer[i + 2] == 'P') {
                        if (i == 0 || buffer[i - 1] != 'B') {
                            foundValidTOP = true;
                            break;
                        }
                    }
                }

                if (foundValidTOP) {
                    result.assign(buffer + startPos, endPos - startPos);
                    break;
                }
            }
        }
    }
    pclose(pipe);
    return result;
}

int main(int argc, char* argv[]) {
    std::string pkgname;
    std::string time_value;
    std::string input_file;
    int duration = 30;

    int opt;
    while ((opt = getopt(argc, argv, "t:i:h")) != -1) {
        switch (opt) {
        case 'i':
            input_file = optarg;
            break;
        case 't':
            duration = std::stoi(optarg);
            break;
        case 'h':
            std::cout << "食用方法: \n" 
            << argv[0] << " -t <时间> [包名]\n"
            << argv[0] << " -i <文件>\n";
            return 0;
        default:
            std::cerr << "未知参数\n";
            return 1;
        }
    }

    if (!input_file.empty()) {
        try {
            std::ifstream file(input_file);

            std::string filename = std::filesystem::path(input_file).filename().string();

            size_t dot_pos = filename.find_last_of('.');

            if (dot_pos != std::string::npos && dot_pos > 0) {
                filename = filename.substr(0, dot_pos);
            }

            nlohmann::json result;
            file >> result;
            file.close();

            draw_svg(result, filename);
        } catch (const nlohmann::json::parse_error& e) {
            std::cerr << "无法解析" << std::endl;
        } catch (...) {
            std::cerr << "未知错误" << std::endl;
        }
        return 0;
    }

    if (optind < argc) {
        pkgname = argv[optind];
    } else {
        pkgname = getForegroundApp_lru();
    }

    MainMonitor tester(pkgname, duration);
    tester.startTest();

    return 0;
}