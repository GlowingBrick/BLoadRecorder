#include "draw_auto.hpp"
#include "lib/json/single_include/nlohmann/json.hpp"
#include <algorithm>
#include <climits>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

std::string sanitizeCpuSet(const std::string& cpu_set);

struct ThreadInfo {
    std::string name;
    std::string cpu_set;
    std::vector<std::pair<uint64_t, float>> time_load_pairs;  // 时间戳和负载的对应关系
};

// 解析线程数据的主函数
std::map<std::string, std::vector<SVGFreqPlotter::FrameData>> parseThreadData(const nlohmann::json& result) {
    std::map<std::string, std::vector<SVGFreqPlotter::FrameData>> cpu_set_frames;
    std::map<std::string, ThreadInfo> all_threads;  // 线程唯一标识 -> 线程信息

    if (!result.contains("thread") || !result["thread"].is_array()) {
        return cpu_set_frames;
    }

    // 第一遍：收集所有线程信息，确定每个线程的主要cpu-set
    std::map<std::string, std::map<std::string, int>> thread_cpu_set_counts;  // 线程唯一标识 -> (cpu-set -> 出现次数)
    std::map<std::string, std::vector<float>> thread_loads;                   // 线程唯一标识 -> 所有负载值（用于计算均值）

    for (const auto& frame : result["thread"]) {
        uint64_t time_ms = frame["time_ms"];

        if (frame.contains("data") && frame["data"].is_array()) {
            for (const auto& process : frame["data"]) {
                if (process.contains("threads") && process["threads"].is_array()) {
                    for (const auto& thread : process["threads"]) {
                        std::string thread_name = thread["name"];
                        int tid = thread["tid"];
                        std::string cpu_set = thread["cpu-set"];
                        float load = thread["load"];

                        // 创建唯一标识：线程名(tid)
                        std::string thread_id = thread_name + "(" + std::to_string(tid) + ")";

                        // 记录cpu-set出现次数
                        thread_cpu_set_counts[thread_id][cpu_set]++;

                        // 记录负载值用于计算均值
                        thread_loads[thread_id].push_back(load);

                        // 初始化线程信息
                        if (all_threads.find(thread_id) == all_threads.end()) {
                            all_threads[thread_id] = {thread_id, "", {}};
                        }
                    }
                }
            }
        }
    }

    // 确定每个线程的主要cpu-set
    for (auto& [thread_id, thread_info] : all_threads) {
        if (thread_cpu_set_counts.find(thread_id) != thread_cpu_set_counts.end()) {
            const auto& cpu_set_counts = thread_cpu_set_counts[thread_id];
            std::string main_cpu_set = "";
            int max_count = 0;

            for (const auto& [cpu_set, count] : cpu_set_counts) {
                if (count > max_count) {
                    max_count = count;
                    main_cpu_set = cpu_set;
                }
            }

            thread_info.cpu_set = main_cpu_set;
        }
    }

    // 第二遍：按时间帧填充数据
    for (const auto& frame : result["thread"]) {
        uint64_t time_ms = frame["time_ms"];

        // 为每个cpu-set创建当前时间帧的数据
        std::map<std::string, SVGFreqPlotter::FrameData> current_frame_by_cpuset;

        if (frame.contains("data") && frame["data"].is_array()) {
            for (const auto& process : frame["data"]) {
                if (process.contains("threads") && process["threads"].is_array()) {
                    for (const auto& thread : process["threads"]) {
                        std::string thread_name = thread["name"];
                        int tid = thread["tid"];
                        std::string cpu_set = thread["cpu-set"];
                        float load = thread["load"];

                        // 创建唯一标识
                        std::string thread_id = thread_name + "(" + std::to_string(tid) + ")";

                        // 使用主要cpu-set进行分组
                        if (all_threads.find(thread_id) != all_threads.end()) {
                            std::string main_cpu_set = all_threads[thread_id].cpu_set;

                            // 初始化该cpu-set的帧数据
                            if (current_frame_by_cpuset.find(main_cpu_set) == current_frame_by_cpuset.end()) {
                                current_frame_by_cpuset[main_cpu_set] = {time_ms, {}};
                            }

                            // 添加线程负载数据
                            current_frame_by_cpuset[main_cpu_set].frequencies[thread_id] = load;
                        }
                    }
                }
            }
        }

        // 将当前帧数据添加到对应的cpu-set分组中
        for (const auto& [cpu_set, frame_data] : current_frame_by_cpuset) {
            cpu_set_frames[cpu_set].push_back(frame_data);
        }
    }

    // 第三遍：填充缺失的数据（线程不在时的0值）
    for (auto& [cpu_set, frames] : cpu_set_frames) {
        // 收集这个cpu-set下的所有线程唯一标识
        std::set<std::string> cpu_set_threads;
        for (const auto& thread_info : all_threads) {
            if (thread_info.second.cpu_set == cpu_set) {
                cpu_set_threads.insert(thread_info.first);
            }
        }

        // 为每个帧填充缺失的线程数据为0
        for (auto& frame : frames) {
            for (const auto& thread_id : cpu_set_threads) {
                if (frame.frequencies.find(thread_id) == frame.frequencies.end()) {
                    frame.frequencies[thread_id] = 0.0f;
                }
            }
        }
    }

    return cpu_set_frames;
}

std::vector<std::string> processCPUFramesEfficient(const std::vector<SVGFreqPlotter::FrameData>& frames, int keep_count) {

    std::map<std::string, float> totdata;
    for (auto& [name, value] : frames[0].frequencies) {
        totdata[name] = 0.0f;
    }

    for (auto& frame : frames) {
        for (auto& [name, value] : frame.frequencies) {
            totdata[name] += value;
        }
    }
    std::vector<std::string> tmpdata;
    std::vector<std::pair<std::string, float>> temp(totdata.begin(), totdata.end());

    std::sort(temp.begin(), temp.end(), [](auto& a, auto& b) { return a.second > b.second; });

    std::transform(temp.begin(), temp.end(), std::back_inserter(tmpdata),
                   [](auto& pair) { return pair.first; });
    if (tmpdata.size() > keep_count) {
        tmpdata.resize(keep_count);
    }

    return tmpdata;
}

// 绘制所有cpu-set的线程图表
void drawThreadCharts(const nlohmann::json& result, const std::string& base_filename = "thread") {
    auto cpu_set_frames = parseThreadData(result);

    if (cpu_set_frames.empty()) {
        std::cout << "没有找到线程数据" << std::endl;
        return;
    }

    // 为每个cpu-set创建图表
    for (const auto& [cpu_set, frames] : cpu_set_frames) {
        if (frames.empty())
            continue;

        // 创建绘图器
        SVGFreqPlotter::StyleParams style;
        style.use_custom_range = true;
        style.custom_min_value = 0.0f;
        style.custom_max_value = 100.0f;
        style.order = processCPUFramesEfficient(frames, 15);

        SVGFreqPlotter plotter(style);
        plotter.setDataLineWidth(2.0);

        // 生成文件名和标题
        std::string filename = base_filename + "_cpu" + sanitizeCpuSet(cpu_set) + ".svg";
        std::string title = "线程负载监控 - CPU Set: " + cpu_set;

        plotter.drawChart(frames, title, "负载(%)", filename);

        std::cout << "线程图表已生成: " << filename
                  << " (包含 " << frames[0].frequencies.size() << " 个线程)" << std::endl;
    }

    std::cout << "共生成 " << cpu_set_frames.size() << " 个线程监控图表" << std::endl;
}

// 辅助函数：清理cpu-set字符串用于文件名
std::string sanitizeCpuSet(const std::string& cpu_set) {
    std::string sanitized = cpu_set;
    std::replace(sanitized.begin(), sanitized.end(), ',', '_');
    std::replace(sanitized.begin(), sanitized.end(), '-', '_');
    std::replace(sanitized.begin(), sanitized.end(), ' ', '_');
    return sanitized;
}

std::vector<SVGFreqPlotter::FrameData> parseThermalData(const nlohmann::json& result) {
    std::vector<SVGFreqPlotter::FrameData> frames;

    if (!result.is_object() || !result.contains("thermal") || !result["thermal"].is_array()) {
        return frames;
    }
    for (const auto& frame : result["thermal"]) {
        if (!frame.is_object() || !frame.contains("time_ms") || !frame["time_ms"].is_number()) {
            continue;
        }

        SVGFreqPlotter::FrameData frame_data;
        frame_data.time_ms = frame["time_ms"];

        float temp_value = 0.0f;
        if (frame.contains("data") && frame["data"].is_number()) {
            temp_value = frame["data"];
        }

        frame_data.frequencies["temperature"] = temp_value;
        frames.push_back(frame_data);
    }

    return frames;
}

// 处理fps帧率数据
std::vector<SVGFreqPlotter::FrameData> parseFpsData(const nlohmann::json& result) {
    std::vector<SVGFreqPlotter::FrameData> frames;

    if (!result.is_object() || !result.contains("fps") || !result["fps"].is_array()) {
        return frames;
    }

    for (const auto& frame : result["fps"]) {
        if (!frame.is_object() || !frame.contains("time_ms") || !frame["time_ms"].is_number()) {
            continue;
        }

        SVGFreqPlotter::FrameData frame_data;
        frame_data.time_ms = frame["time_ms"];

        float fps_value = 0.0f;
        if (frame.contains("data") && frame["data"].is_number()) {
            fps_value = frame["data"];
        }

        frame_data.frequencies["fps"] = fps_value;
        frames.push_back(frame_data);
    }

    return frames;
}

std::vector<SVGFreqPlotter::FrameData> parseCpuLoadData(const nlohmann::json& result) {
    std::vector<SVGFreqPlotter::FrameData> frames;

    if (!result.is_object() || !result.contains("cpu_load") || !result["cpu_load"].is_array()) {
        return frames;
    }

    for (const auto& frame : result["cpu_load"]) {
        if (!frame.is_object() || !frame.contains("time_ms") || !frame["time_ms"].is_number()) {
            continue;
        }

        SVGFreqPlotter::FrameData frame_data;
        frame_data.time_ms = frame["time_ms"];

        if (frame.contains("data") && frame["data"].is_array()) {
            for (const auto& cpu_data : frame["data"]) {
                if (cpu_data.is_object()) {
                    std::string cpu_name = cpu_data.value("name", "unknown");
                    float load_value = 0.0f;

                    if (cpu_data.contains("load") && cpu_data["load"].is_number()) {
                        load_value = cpu_data["load"];
                    }

                    frame_data.frequencies[cpu_name] = load_value;
                }
            }
        }

        frames.push_back(frame_data);
    }

    if (frames.size() > 1) {
        frames[0] = frames[1];
        frames[0].time_ms = 0;
    }

    return frames;
}

std::vector<SVGFreqPlotter::FrameData> CPUFreqFrameData(const nlohmann::json& json_data) {
    std::vector<SVGFreqPlotter::FrameData> frames;

    if (!json_data.is_object() || !json_data.contains("cpu_freq") || !json_data["cpu_freq"].is_array()) {
        return frames;
    }

    const auto& cpu_freq_array = json_data["cpu_freq"];

    for (const auto& frame : cpu_freq_array) {
        if (!frame.is_object()) {
            continue;
        }

        if (!frame.contains("time_ms") || !frame["time_ms"].is_number()) {
            continue;
        }

        SVGFreqPlotter::FrameData frame_data;
        frame_data.time_ms = frame["time_ms"];

        if (frame.contains("data") && frame["data"].is_array()) {
            for (const auto& core_data : frame["data"]) {
                if (core_data.is_object()) {
                    std::string name = "unknown";
                    float freq_mhz = 0.0f;

                    if (core_data.contains("name") && core_data["name"].is_string()) {
                        name = core_data["name"];
                    }

                    if (core_data.contains("freq") && core_data["freq"].is_number()) {
                        freq_mhz = static_cast<float>(core_data["freq"]) / 1000000.0f;
                    }

                    frame_data.frequencies[name] = freq_mhz;
                }
            }
        }
        frames.push_back(frame_data);
    }

    return frames;
}

std::vector<std::string> sortcpus(const std::map<std::string, float>& ord) {
    std::vector<std::string> cpuKeys;
    std::vector<std::string> otherKeys;

    std::regex cpuPattern("^cpu(\\d+)$");
    std::smatch matches;

    for (const auto& pair : ord) {
        const std::string& key = pair.first;

        if (std::regex_match(key, matches, cpuPattern)) {
            cpuKeys.push_back(key);
        } else {
            otherKeys.push_back(key);
        }
    }

    std::sort(cpuKeys.begin(), cpuKeys.end(), [](const std::string& a, const std::string& b) {  // 按数字降序排序
        std::regex pattern("^cpu(\\d+)$");
        std::smatch aMatch, bMatch;

        std::regex_match(a, aMatch, pattern);
        std::regex_match(b, bMatch, pattern);

        int aNum = std::stoi(aMatch[1]);
        int bNum = std::stoi(bMatch[1]);

        return aNum > bNum;  // 降序排列
    });

    std::vector<std::string> result;

    result.insert(result.end(), cpuKeys.begin(), cpuKeys.end());
    result.insert(result.end(), otherKeys.begin(), otherKeys.end());

    return result;
}

void draw_svg(nlohmann::json& result, std::string pkg) {
    // 绘制频率============
    {
        auto frame_data = CPUFreqFrameData(result);
        SVGFreqPlotter::StyleParams style;
        style.use_custom_range = true;
        style.custom_min_value = 0.0f;
        style.custom_max_value = std::numeric_limits<float>::infinity();
        style.label = "频率";
        if (!frame_data.empty()) {
            style.order = sortcpus(frame_data[0].frequencies);
        }
        SVGFreqPlotter plotter(style);
        plotter.drawChart(frame_data, "CPU_Freq", "Ghz", "cpu_freq_chart.svg");
    }

    // 绘制负载=============
    {
        auto frame_data = parseCpuLoadData(result);

        SVGFreqPlotter::StyleParams style;
        style.use_custom_range = true;
        style.custom_min_value = 0.0f;
        style.custom_max_value = 100.0f;

        if (!frame_data.empty()) {
            style.order = sortcpus(frame_data[0].frequencies);
        }

        SVGFreqPlotter plotter(style);
        plotter.drawChart(frame_data, "CPU负载监控", "负载(%)", "cpu_load.svg");
    }
    // 绘制fps和温度=============
    {
        auto frame_data = parseThermalData(result);
        SVGFreqPlotter::StyleParams style;
        style.use_custom_range = true;
        style.custom_min_value = 0.0f;
        style.custom_max_value = std::numeric_limits<float>::infinity();
        style.label = "温度";
        SVGFreqPlotter plotter(style);
        plotter.drawChart(frame_data, "处理器温度监控", "温度(°C)", "thermal.svg");
    }
    {
        auto frame_data = parseFpsData(result);
        SVGFreqPlotter::StyleParams style;
        style.use_custom_range = true;
        style.custom_min_value = 0.0f;
        style.custom_max_value = std::numeric_limits<float>::infinity();
        style.label = "帧率";
        SVGFreqPlotter plotter(style);
        plotter.drawChart(frame_data, "帧率", "帧率(FPS)", "fps.svg");
    }

    {
        drawThreadCharts(result, pkg);
    }
}