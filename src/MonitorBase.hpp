#pragma once
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <csignal>
#include <dirent.h>
#include <iomanip>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>
#include <fstream>
#include <filesystem>
#include <time.h>

class MonitorBase {
public:
    virtual ~MonitorBase() = default;
    
    virtual std::string name() = 0;
    virtual bool start(const std::string& pkgName, int interval_ms = 1000) = 0;
    virtual nlohmann::json stop() = 0;
    
protected:
    std::atomic<bool> running_{false};
    std::thread worker_thread_;
    std::chrono::steady_clock::time_point _starttime__;
    unsigned long _cycles__=0;
    int _interval_ms__=0;

    void init_clock(int interval_ms){
        _cycles__=0;
        _starttime__=std::chrono::steady_clock::now();
        _interval_ms__=interval_ms;
    }
    void _Sleep__() {
        auto nexttime=_starttime__+std::chrono::milliseconds(++_cycles__*_interval_ms__);
        auto nowtime=std::chrono::steady_clock::now();
 
        while(nowtime>nexttime){
            nexttime=_starttime__+std::chrono::milliseconds(++_cycles__*_interval_ms__);
        }
        std::this_thread::sleep_until(nexttime);
    }
};