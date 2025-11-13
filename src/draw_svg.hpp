#include "draw_auto.hpp"
#include "nlohmann/json.hpp"
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

double data_line_width(std::size_t size) {
    if (size > 100) {
        if (size <= 800) {
            return 3.0 - ((static_cast<float>(size) - 100.0) / 700.0) * 2.2;
        } else {
            return 0.8;
        }
    } else {
        return 3;
    }
}

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
    std::map<std::string, std::map<std::string, int>> thread_cpu_set_counts;
    std::map<std::string, std::vector<float>> thread_loads;

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

                        std::string thread_id = thread_name + "(" + std::to_string(tid) + ")";  // 线程名(tid),防重名

                        thread_cpu_set_counts[thread_id][cpu_set]++;  //记录cpu-set

                        thread_loads[thread_id].push_back(load);

                        if (all_threads.find(thread_id) == all_threads.end()) {
                            all_threads[thread_id] = {thread_id, "", {}};
                        }
                    }
                }
            }
        }
    }

    for (auto& [thread_id, thread_info] : all_threads) {  //寻找主要存在的cpu-set
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

    for (const auto& frame : result["thread"]) {
        uint64_t time_ms = frame["time_ms"];

        std::map<std::string, SVGFreqPlotter::FrameData> current_frame_by_cpuset;

        if (frame.contains("data") && frame["data"].is_array()) {
            for (const auto& process : frame["data"]) {
                if (process.contains("threads") && process["threads"].is_array()) {
                    for (const auto& thread : process["threads"]) {
                        std::string thread_name = thread["name"];
                        int tid = thread["tid"];
                        std::string cpu_set = thread["cpu-set"];
                        float load = thread["load"];

                        std::string thread_id = thread_name + "(" + std::to_string(tid) + ")";

                        if (all_threads.find(thread_id) != all_threads.end()) {  //按cpu—set分组
                            std::string main_cpu_set = all_threads[thread_id].cpu_set;

                            if (current_frame_by_cpuset.find(main_cpu_set) == current_frame_by_cpuset.end()) {
                                current_frame_by_cpuset[main_cpu_set] = {time_ms, {}};
                            }

                            current_frame_by_cpuset[main_cpu_set].frequencies[thread_id] = load;
                        }
                    }
                }
            }
        }

        for (const auto& [cpu_set, frame_data] : current_frame_by_cpuset) {
            cpu_set_frames[cpu_set].push_back(frame_data);
        }
    }

    for (auto& [cpu_set, frames] : cpu_set_frames) {  //修补数据
        std::set<std::string> cpu_set_threads;
        for (const auto& thread_info : all_threads) {
            if (thread_info.second.cpu_set == cpu_set) {
                cpu_set_threads.insert(thread_info.first);
            }
        }

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

void drawThreadCharts(const nlohmann::json& result, std::vector<std::string>& svgs) {  //绘图
    auto cpu_set_frames = parseThreadData(result);

    if (cpu_set_frames.empty()) {
        return;
    }

    for (const auto& [cpu_set, frames] : cpu_set_frames) {  //分组绘图
        if (frames.empty())
            continue;

        // 创建绘图器
        SVGFreqPlotter::StyleParams style;
        style.use_custom_range = true;
        style.custom_min_value = 0.0f;
        style.custom_max_value = 100.0f;
        style.order = processCPUFramesEfficient(frames, 15);
        style.legend_font_size=18;
        style.data_line_width = data_line_width(frames.size());

        SVGFreqPlotter plotter(style);

        std::string title = "线程负载 - CPU Set: " + cpu_set;

        plotter.drawChart(frames, title, "负载(%)");
        svgs.push_back(plotter.getSVG());
    }
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
    std::vector<std::string> svgs;
    // 绘制fps===================
    {
        auto frame_data = parseFpsData(result);
        SVGFreqPlotter::StyleParams style;
        style.use_custom_range = true;
        style.custom_min_value = 0.0f;
        style.custom_max_value = std::numeric_limits<float>::infinity();
        style.label = "帧率";

        style.data_line_width = data_line_width(frame_data.size());

        SVGFreqPlotter plotter(style);
        plotter.drawChart(frame_data, "帧率", "帧率(FPS)");//, "fps.svg");
        svgs.push_back(plotter.getSVG());
    }
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

        style.data_line_width = data_line_width(frame_data.size());

        SVGFreqPlotter plotter(style);
        plotter.drawChart(frame_data, "CPU_Freq", "Ghz");//, "cpu_freq.svg");
        svgs.push_back(plotter.getSVG());
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

        style.data_line_width = data_line_width(frame_data.size());

        SVGFreqPlotter plotter(style);
        plotter.drawChart(frame_data, "CPU负载", "负载(%)");//, "cpu_load.svg");
        svgs.push_back(plotter.getSVG());
    }
    // 温度=============
    {
        auto frame_data = parseThermalData(result);
        SVGFreqPlotter::StyleParams style;
        style.use_custom_range = true;
        style.custom_min_value = 0.0f;
        style.custom_max_value = std::numeric_limits<float>::infinity();
        style.label = "温度";

        style.data_line_width = data_line_width(frame_data.size());

        SVGFreqPlotter plotter(style);
        plotter.drawChart(frame_data, "CPU温度", "温度(°C)");//, "thermal.svg");
        svgs.push_back(plotter.getSVG());
    }

    {
        drawThreadCharts(result,svgs);
    }

    std::string name;
    std::string time;
    if(result.contains("info")){
        if(result["info"].is_object())
        name=result["info"].value("name","");
        time=result["info"].value("time","");

    }

    std::string out=SVGFreqPlotter::concatenateSVGsVertically(svgs,1440.0,720.0,50.0,name,time);
    std::string filename=pkg+".svg";
    std::ofstream file(filename);
    file << out;
    file.close();

    std::cout << "图表已生成" << std::endl;
}