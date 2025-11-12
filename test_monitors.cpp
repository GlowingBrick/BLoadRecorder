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

#include <algorithm>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "draw_svg.hpp"

class MonitorTester {
private:
    std::vector<std::unique_ptr<MonitorBase>> monitors_;
    std::string package_name_;
    int test_duration_;

public:
    MonitorTester(const std::string& pkgName, int duration_seconds = 10)
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

int main(int argc, char* argv[]) {
    std::string pkgname;
    std::string time_value;
    bool verbose = false;
    int duration = 10;

    int opt;
    while ((opt = getopt(argc, argv, "t:h")) != -1) {
        switch (opt) {
        case 't':
            duration = std::stoi(optarg);
            break;
        case 'h':
            std::cout << "用法: " << argv[0] << " [包名] -o <输出位置> -t <时间>\n";
            std::cout << "选项:\n";
            std::cout << "  -t, --time    时间\n";
            return 0;
        default:
            std::cerr << "未知参数\n";
            return 1;
        }
    }

    if (optind < argc) {
        pkgname = argv[optind];
    }

    MonitorTester tester(pkgname, duration);
    tester.startTest();

    return 0;
}