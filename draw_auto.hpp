#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
//最早是画频率图的，改改用来画别的。变量名懒得改了
class SVGFreqPlotter {
public:
    struct FrameData {
        uint64_t time_ms;
        std::map<std::string, float> frequencies;
    };

    struct StyleParams {
        int width;
        int height;
        int chart_top;
        int chart_bottom;
        int left_margin;
        int right_margin;
        int bottom_margin;

        // 字体大小
        int title_font_size;
        int subtitle_font_size;
        int axis_font_size;
        int legend_font_size;
        int tick_font_size;

        // 线条粗细
        double axis_line_width;
        double grid_line_width;
        double data_line_width;
        double legend_border_width;

        // 颜色
        std::string background_color;
        std::string axis_color;
        std::string grid_color;
        std::string text_color;

        // 布局
        int max_y_ticks;
        int max_x_ticks;
        int legend_items_per_row;
        int legend_item_height;

        // 自定义Y轴范围
        float custom_min_value;
        float custom_max_value;
        bool use_custom_range;
        std::string label;
        std::vector<std::string> order;

        StyleParams() : width(1440), height(720),
                        chart_top(80), chart_bottom(580),
                        left_margin(100), right_margin(50), bottom_margin(120),
                        title_font_size(32), subtitle_font_size(20),
                        axis_font_size(18), legend_font_size(18), tick_font_size(16),
                        axis_line_width(2.5), grid_line_width(1.0),
                        data_line_width(3.0), legend_border_width(1.0),
                        background_color("white"), axis_color("#333333"),
                        grid_color("#E0E0E0"), text_color("black"),
                        max_y_ticks(3), max_x_ticks(6),
                        legend_items_per_row(5), legend_item_height(35),
                        custom_min_value(0.0f), custom_max_value(0.0f), use_custom_range(false), label("") {}
    };

private:
    StyleParams params;
    std::map<std::string, std::string> core_colors;

    std::vector<std::string> color_palette = {
        "#1F3A93", "#2878C9", "#32B8C2", "#00CC99", "#66CC33",
        "#99CC00", "#CCCC00", "#FFCC00", "#FF9933", "#FF6633",
        "#FF3366", "#FF3399", "#CC33CC", "#9966CC", "#6699FF",
        "#3366CC", "#0044CC", "#0066AA", "#008888", "#00AA66",
        "#66AA00", "#AAAA00", "#CC6600", "#CC3300", "#6A5ACD",
        "#20B2AA", "#87CEEB", "#98FB98", "#DDA0DD", "#FFB6C1",
        "#F0E68C", "#D2B48C", "#BC8F8F", "#A0522D", "#8FBC8F",
        "#48D1CC"};

public:
    SVGFreqPlotter(const StyleParams& style_params = StyleParams())
        : params(style_params) {}

    void drawChart(const std::vector<FrameData>& frames,
                   const std::string& title = "数据监控",
                   const std::string& y_label = "数值",
                   const std::string& filename = "chart.svg") {

        if (frames.empty()) return;

        auto core_order = detectAndSortCores(frames);
        generateColors(core_order);
        auto [time_data, value_data] = extractValueData(frames, core_order);
        auto [min_time, max_time] = getTimeRange(time_data);
        auto [min_val, max_val] = getValueRange(value_data);

        float data_min = std::numeric_limits<float>::max();
        float data_max = std::numeric_limits<float>::lowest();

        float realmax = 0.0f;
        for (const auto& [core, values] : value_data) {
            for (float val : values) {
                if (val > realmax) realmax = val;
            }
        }

        std::stringstream svg;
        generateSVG(svg, title, y_label, core_order, time_data, value_data,
                    min_time, max_time, min_val, max_val, realmax);

        saveSVG(svg.str(), filename);
    }

    // 设置自定义Y轴范围
    void setCustomYRange(float min_val, float max_val) {
        params.use_custom_range = true;
        params.custom_min_value = min_val;
        params.custom_max_value = max_val;
    }

    // 使用自动范围
    void useAutoRange() {
        params.use_custom_range = false;
    }

    void setDataLineWidth(double width) {
        params.data_line_width = width;
    }

    void setFontSizes(int title, int axis, int legend, int tick) {
        params.title_font_size = title;
        params.axis_font_size = axis;
        params.legend_font_size = legend;
        params.tick_font_size = tick;
    }

private:
    std::vector<std::string> detectAndSortCores(const std::vector<FrameData>& frames) {
        if (params.order.empty()) {
            std::set<std::string> core_set;
            for (const auto& frame : frames) {
                for (const auto& [core_name, value] : frame.frequencies) {
                    core_set.insert(core_name);
                }
            }

            std::vector<std::string> cores(core_set.begin(), core_set.end());
            return cores;
        }
        return params.order;
    }

    void generateColors(const std::vector<std::string>& core_order) {
        core_colors.clear();

        for (size_t i = 0; i < core_order.size(); ++i) {
            int color_index = i % color_palette.size();
            core_colors[core_order[i]] = color_palette[color_index];
        }
    }

    std::pair<std::vector<uint64_t>, std::map<std::string, std::vector<float>>>
    extractValueData(const std::vector<FrameData>& frames,
                     const std::vector<std::string>& core_order) {
        std::vector<uint64_t> time_data;
        std::map<std::string, std::vector<float>> value_data;

        for (const auto& core : core_order) {
            value_data[core] = std::vector<float>();
        }

        for (const auto& frame : frames) {
            time_data.push_back(frame.time_ms);

            for (const auto& core : core_order) {
                if (frame.frequencies.find(core) != frame.frequencies.end()) {
                    value_data[core].push_back(frame.frequencies.at(core));
                } else {
                    if (!value_data[core].empty()) {
                        value_data[core].push_back(value_data[core].back());
                    } else {
                        value_data[core].push_back(0.0f);
                    }
                }
            }
        }

        return {time_data, value_data};
    }

    std::pair<uint64_t, uint64_t> getTimeRange(const std::vector<uint64_t>& time_data) {
        if (time_data.empty()) return {0, 1};
        return {time_data.front(), time_data.back()};
    }

    std::pair<float, float> getValueRange(const std::map<std::string, std::vector<float>>& value_data) {
        if (params.use_custom_range) {
            bool min_valid = std::isfinite(params.custom_min_value);
            bool max_valid = std::isfinite(params.custom_max_value);

            if (min_valid && max_valid) {
                return {params.custom_min_value, params.custom_max_value};  //inf或nan表自适应
            }

            float data_min = std::numeric_limits<float>::max();
            float data_max = std::numeric_limits<float>::lowest();

            for (const auto& [core, values] : value_data) {
                for (float val : values) {
                    if (val < data_min) data_min = val;
                    if (val > data_max) data_max = val;
                }
            }

            if (data_min == std::numeric_limits<float>::max()) {
                data_min = 0.0f;
                data_max = 1.0f;
            }

            float margin = std::max(0.1f, (data_max - data_min) * 0.1f);

            float final_min = min_valid ? params.custom_min_value : std::max(0.0f, data_min - margin);
            float final_max = max_valid ? params.custom_max_value : data_max + margin;

            return {final_min, final_max};
        }

        float min_val = std::numeric_limits<float>::max();
        float max_val = std::numeric_limits<float>::lowest();

        for (const auto& [core, values] : value_data) {
            for (float val : values) {
                if (val < min_val) min_val = val;
                if (val > max_val) max_val = val;
            }
        }

        if (min_val == std::numeric_limits<float>::max()) {
            min_val = 0.0f;
            max_val = 1.0f;
        }

        float margin = std::max(0.1f, (max_val - min_val) * 0.1f);  //边距
        return {std::max(0.0f, min_val - margin), max_val + margin};
    }

    void generateSVG(std::stringstream& svg,
                     const std::string& title,
                     const std::string& y_label,
                     const std::vector<std::string>& core_order,
                     const std::vector<uint64_t>& time_data,
                     const std::map<std::string, std::vector<float>>& value_data,
                     uint64_t min_time, uint64_t max_time,
                     float min_val, float max_val, float realmax) {

        svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        svg << "<svg width=\"" << params.width << "\" height=\"" << params.height
            << "\" xmlns=\"http://www.w3.org/2000/svg\">\n";

        svg << "  <rect width=\"100%\" height=\"100%\" fill=\"" << params.background_color << "\"/>\n";

        svg << "  <text x=\"" << params.left_margin << "\" y=\"40\" font-size=\""  //标题
            << params.title_font_size << "\" font-weight=\"bold\" fill=\""
            << params.text_color << "\">" << title << "</text>\n";

        svg << "  <text x=\"" << params.left_margin << "\" y=\"70\" font-size=\""
            << params.subtitle_font_size << "\" fill=\"" << params.text_color
            << "\">" << params.label << " </text>\n";

        int chart_width = params.width - params.left_margin - params.right_margin;
        int chart_height = params.chart_bottom - params.chart_top;

        svg << "  <rect x=\"" << params.left_margin << "\" y=\"" << params.chart_top
            << "\" width=\"" << chart_width << "\" height=\"" << chart_height
            << "\" fill=\"none\" stroke=\"" << params.axis_color
            << "\" stroke-width=\"" << params.axis_line_width << "\"/>\n";

        drawGridAndTicks(svg, min_time, max_time, min_val, max_val, chart_width, chart_height, realmax);  // 网格线和刻度

        drawDataLines(svg, time_data, value_data, min_time, max_time, min_val, max_val,  // 数据线条
                      chart_width, chart_height, core_order);

        drawLegend(svg, core_order);  // 图例

        svg << "</svg>";
    }

    void drawGridAndTicks(std::stringstream& svg,
                          uint64_t min_time, uint64_t max_time,
                          float min_val, float max_val,
                          int chart_width, int chart_height, float realmax) {

        std::vector<float> y_ticks = generateYTicks(min_val, max_val, realmax);  //y轴刻度

        for (float val : y_ticks) {
            int y = valueToY(val, min_val, max_val, chart_height);

            svg << "  <line x1=\"" << params.left_margin << "\" y1=\"" << y
                << "\" x2=\"" << (params.width - params.right_margin) << "\" y2=\"" << y
                << "\" stroke=\"" << params.grid_color
                << "\" stroke-width=\"" << params.grid_line_width << "\"/>\n";

            std::string val_str = formatValue(val);
            svg << "  <text x=\"" << (params.left_margin - 10) << "\" y=\"" << (y + 5)
                << "\" font-size=\"" << params.tick_font_size << "\" text-anchor=\"end\" fill=\""
                << params.text_color << "\">" << val_str << "</text>\n";
        }

        uint64_t time_range = max_time - min_time;  //x刻度
        uint64_t x_tick_interval = chooseTimeTickInterval(time_range);
        int x_ticks_drawn = 0;

        for (uint64_t t = (min_time / x_tick_interval) * x_tick_interval;
             t <= max_time;
             t += x_tick_interval) {
            if (t < min_time) continue;

            int x = timeToX(t, min_time, max_time, chart_width);

            svg << "  <line x1=\"" << x << "\" y1=\"" << params.chart_top
                << "\" x2=\"" << x << "\" y2=\"" << params.chart_bottom
                << "\" stroke=\"" << params.grid_color
                << "\" stroke-width=\"" << params.grid_line_width << "\"/>\n";

            std::string time_str = formatTime(t);
            svg << "  <text x=\"" << x << "\" y=\"" << (params.chart_bottom + 25)
                << "\" font-size=\"" << params.tick_font_size << "\" text-anchor=\"middle\" fill=\""
                << params.text_color << "\">" << time_str << "</text>\n";

            x_ticks_drawn++;
        }
    }

    std::vector<float> generateYTicks(float min_val, float max_val, float realmax) {
        std::vector<float> ticks;

        float range = max_val - min_val;
        if (range <= 0) {
            ticks.push_back(min_val);
            return ticks;
        }

        float magnitude = powf(10.0f, floorf(log10f(range)));

        float ratio = range / magnitude;
        float step = magnitude;

        if (ratio < 2.0f) {
            step = magnitude * 0.2f;
        } else if (ratio < 5.0f) {
            step = magnitude * 0.5f;
        } else if (ratio < 20.0f) {
            step = magnitude;
        } else if (ratio < 50.0f) {
            step = magnitude * 2.0f;
        } else {
            step = magnitude * 5.0f;
        }

        int estimated_ticks = static_cast<int>(range / step) + 1;
        if (estimated_ticks > params.max_y_ticks) {

            while (estimated_ticks > params.max_y_ticks && step < magnitude * 10.0f) {
                step *= 2.0f;
                estimated_ticks = static_cast<int>(range / step) + 1;
            }
        } else if (estimated_ticks < 3) {

            float smaller_step = step;
            while (estimated_ticks < 3 && smaller_step > magnitude * 0.1f) {
                smaller_step /= 2.0f;
                estimated_ticks = static_cast<int>(range / smaller_step) + 1;
                if (estimated_ticks <= params.max_y_ticks) {
                    step = smaller_step;
                }
            }
        }

        float start = ceilf(min_val / step) * step;  // 生成刻度
        for (float val = start; val <= max_val && ticks.size() < static_cast<size_t>(params.max_y_ticks); val += step) {
            ticks.push_back(val);
        }

        if (ticks.empty()) {  //至少一个
            ticks.push_back((min_val + max_val) / 2.0f);
        }

        float threshold = range * 0.1f;  // 清除最大值附近的标签
        if (!ticks.empty()) {
            for (auto it = ticks.rbegin(); it != ticks.rend(); ++it) {
                float distance = std::abs(*it - realmax);
                if (distance < threshold) {
                    ticks.erase(std::next(it).base());
                }
            }
        }

        ticks.push_back(realmax);  //最大值
        return ticks;
    }

    void drawDataLines(std::stringstream& svg,
                       const std::vector<uint64_t>& time_data,
                       const std::map<std::string, std::vector<float>>& value_data,
                       uint64_t min_time, uint64_t max_time,
                       float min_val, float max_val,
                       int chart_width, int chart_height,
                       const std::vector<std::string>& core_order) {

        for (auto it = core_order.rbegin(); it != core_order.rend(); ++it) {
            const auto& core = *it;
            if (value_data.find(core) == value_data.end()) continue;

            const auto& values = value_data.at(core);
            const auto& color = core_colors[core];

            std::stringstream path;
            path << "M ";

            for (size_t i = 0; i < time_data.size(); ++i) {
                int x = timeToX(time_data[i], min_time, max_time, chart_width);
                int y = valueToY(values[i], min_val, max_val, chart_height);

                if (i == 0) {
                    path << x << " " << y;
                } else {
                    path << " L " << x << " " << y;
                }
            }

            svg << "  <path d=\"" << path.str() << "\" fill=\"none\" stroke=\""
                << color << "\" stroke-width=\"" << params.data_line_width << "\"/>\n";
        }
    }

    void drawLegend(std::stringstream& svg, const std::vector<std::string>& core_order) {
        int legend_top = params.chart_bottom + 40;
        int item_width = (params.width - params.left_margin - params.right_margin) / params.legend_items_per_row;

        for (size_t i = 0; i < core_order.size(); ++i) {
            const auto& core = core_order[i];
            int row = static_cast<int>(i) / params.legend_items_per_row;
            int col = static_cast<int>(i) % params.legend_items_per_row;

            int x = params.left_margin + col * item_width;
            int y = legend_top + row * params.legend_item_height;
            const auto& color = core_colors[core];

            // 颜色方块
            svg << "  <rect x=\"" << x << "\" y=\"" << y
                << "\" width=\"25\" height=\"18\" fill=\"" << color
                << "\" stroke=\"" << params.text_color
                << "\" stroke-width=\"" << params.legend_border_width << "\"/>\n";

            // 图例文字
            svg << "  <text x=\"" << (x + 30) << "\" y=\"" << (y + 14)
                << "\" font-size=\"" << params.legend_font_size << "\" fill=\""
                << params.text_color << "\">" << core << "</text>\n";
        }
    }

    int timeToX(uint64_t timestamp, uint64_t min_time, uint64_t max_time, int chart_width) {
        if (max_time == min_time) return params.left_margin;
        double ratio = static_cast<double>(timestamp - min_time) / static_cast<double>(max_time - min_time);
        return params.left_margin + static_cast<int>(ratio * chart_width);
    }

    int valueToY(float value, float min_val, float max_val, int chart_height) {
        if (max_val == min_val) return params.chart_bottom;
        double ratio = static_cast<double>(value - min_val) / static_cast<double>(max_val - min_val);
        return params.chart_bottom - static_cast<int>(ratio * chart_height);
    }

    std::string formatTime(uint64_t ms) {
        int total_seconds = static_cast<int>(ms / 1000);
        int minutes = total_seconds / 60;
        int seconds = total_seconds % 60;
        return std::to_string(minutes) + ":" +
               (seconds < 10 ? "0" : "") + std::to_string(seconds);
    }

    std::string formatValue(float value) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << value;
        return oss.str();
    }

    uint64_t chooseTimeTickInterval(uint64_t time_range_ms) {
        if (time_range_ms == 0 || params.max_x_ticks == 0) {
            return 1000;
        }

        std::vector<uint64_t> nice_intervals = {
            1000,
            2000,
            5000,
            10000,
            15000,
            30000,
            60000,
            120000,
            300000,
            600000,
            900000,
            1800000,
            3600000,
            7200000,
            14400000,
            21600000,
            43200000,
            86400000};

        uint64_t ideal_interval = time_range_ms / params.max_x_ticks;
        if (ideal_interval == 0) {
            ideal_interval = 1;
        }

        auto it = std::upper_bound(nice_intervals.begin(), nice_intervals.end(), ideal_interval);
        
        if (it != nice_intervals.begin()) {
            return *(it);
        } else {
            return nice_intervals.front();
        }
    }

    void saveSVG(const std::string& svg_content, const std::string& filename) {
        std::ofstream file(filename);
        file << svg_content;
        file.close();
    }
};