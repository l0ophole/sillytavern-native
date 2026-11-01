// SillyTavern Port feature configuration tool.
//
// Reads features/features.json, validates the dependency graph, and writes a
// Make-compatible .config plus a generated C++ feature header. Interactive
// mode is a small ncurses browser; planned features are dim and unselectable.

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <unistd.h>

#include <ncurses.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

struct Feature {
    std::string id;
    std::string group;
    std::string name;
    std::string description;
    bool critical = false;
    std::string target;
    std::string status;
    std::vector<std::string> depends_on;
};

struct Registry {
    std::vector<Feature> features;
    std::unordered_map<std::string, std::size_t> index;

    void rebuild_index() {
        index.clear();
        for (std::size_t i = 0; i < features.size(); ++i) {
            index[features[i].id] = i;
        }
    }

    const Feature* find(const std::string& id) const {
        const auto it = index.find(id);
        return it == index.end() ? nullptr : &features[it->second];
    }

    Feature* find(const std::string& id) {
        const auto it = index.find(id);
        return it == index.end() ? nullptr : &features[it->second];
    }

    void validate() const {
        if (features.empty()) {
            throw std::runtime_error("feature registry is empty");
        }
        std::set<std::string> seen;
        for (const auto& feature : features) {
            if (feature.id.empty()) {
                throw std::runtime_error("feature with empty id");
            }
            if (!seen.insert(feature.id).second) {
                throw std::runtime_error("duplicate feature id: " + feature.id);
            }
            if (feature.status != "planned" && feature.status != "ported") {
                throw std::runtime_error("feature " + feature.id + " has invalid status: " + feature.status);
            }
            if (feature.group.empty()) {
                throw std::runtime_error("feature " + feature.id + " has no group");
            }
            for (const auto& dep : feature.depends_on) {
                if (seen.find(dep) == seen.end()) {
                    // The dependency may be declared later; check after the loop.
                }
            }
        }
        for (const auto& feature : features) {
            for (const auto& dep : feature.depends_on) {
                if (seen.find(dep) == seen.end()) {
                    throw std::runtime_error("feature " + feature.id + " depends on missing feature " + dep);
                }
            }
        }

        enum class Color { White, Gray, Black };
        std::map<std::string, Color> colors;
        std::function<void(const std::string&)> visit = [&](const std::string& id) {
            Color& color = colors[id];
            if (color == Color::Gray) {
                throw std::runtime_error("dependency cycle involving feature " + id);
            }
            if (color == Color::Black) {
                return;
            }
            color = Color::Gray;
            const Feature* feature = find(id);
            if (feature == nullptr) {
                throw std::runtime_error("missing feature during cycle check: " + id);
            }
            for (const auto& dep : feature->depends_on) {
                visit(dep);
            }
            color = Color::Black;
        };
        for (const auto& feature : features) {
            visit(feature.id);
        }
    }
};

fs::path registry_path(const std::optional<std::string>& explicit_path) {
    if (explicit_path.has_value()) {
        return *explicit_path;
    }
    if (const char* env = std::getenv("ST_FEATURES"); env != nullptr && *env != '\0') {
        return env;
    }
    return "features/features.json";
}

Registry load_registry(const fs::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open feature registry: " + path.string());
    }
    Json root;
    try {
        in >> root;
    } catch (const std::exception& error) {
        throw std::runtime_error("cannot parse feature registry " + path.string() + ": " + error.what());
    }
    if (!root.contains("features") || !root["features"].is_array()) {
        throw std::runtime_error("feature registry has no features array: " + path.string());
    }

    Registry registry;
    for (const auto& item : root["features"]) {
        if (!item.is_object()) {
            throw std::runtime_error("feature entry is not an object");
        }
        Feature feature;
        feature.id = item.value("id", "");
        feature.group = item.value("group", "");
        feature.name = item.value("name", "");
        feature.description = item.value("description", "");
        feature.critical = item.value("critical", false);
        feature.target = item.value("target", "later");
        feature.status = item.value("status", "planned");
        if (item.contains("depends_on")) {
            if (!item["depends_on"].is_array()) {
                throw std::runtime_error("feature " + feature.id + " depends_on is not an array");
            }
            for (const auto& dep : item["depends_on"]) {
                feature.depends_on.push_back(dep.get<std::string>());
            }
        }
        registry.features.push_back(std::move(feature));
    }
    registry.rebuild_index();
    registry.validate();
    return registry;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::set<std::string> compute_defaults(const Registry& registry) {
    std::set<std::string> enabled;
    for (const auto& feature : registry.features) {
        if (feature.critical && feature.status == "ported") {
            enabled.insert(feature.id);
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        const std::vector<std::string> current(enabled.begin(), enabled.end());
        for (const auto& id : current) {
            const Feature* feature = registry.find(id);
            if (feature == nullptr) {
                continue;
            }
            for (const auto& dep : feature->depends_on) {
                const Feature* dependency = registry.find(dep);
                if (dependency == nullptr) {
                    throw std::runtime_error("feature " + id + " depends on missing feature " + dep);
                }
                if (dependency->status != "ported") {
                    throw std::runtime_error(
                        "ported feature " + id + " depends on unported feature " + dep);
                }
                if (enabled.insert(dep).second) {
                    changed = true;
                }
            }
        }
    }
    return enabled;
}

std::set<std::string> load_config(const fs::path& path, const Registry& registry) {
    std::set<std::string> enabled;
    std::ifstream in(path);
    if (!in) {
        return enabled;
    }
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.rfind("CONFIG_", 0) == 0 && line.size() > 9 && line.substr(line.size() - 2) == "=y") {
            const std::string id = line.substr(7, line.size() - 9);
            const Feature* feature = registry.find(id);
            if (feature != nullptr && feature->status == "ported") {
                enabled.insert(id);
            }
        }
    }
    return enabled;
}

void write_atomic(const fs::path& path, const std::string& data) {
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path());
    }
    fs::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("cannot write " + temporary.string());
        }
        out << data;
        out.flush();
        if (!out) {
            throw std::runtime_error("cannot write " + temporary.string());
        }
    }
    std::error_code error;
    fs::rename(temporary, path, error);
    if (error) {
        fs::remove(temporary);
        throw std::runtime_error("cannot replace " + path.string() + ": " + error.message());
    }
}

void write_config(const fs::path& path, const Registry& registry, const std::set<std::string>& enabled) {
    std::ostringstream out;
    out << "# SillyTavern Port configuration\n";
    out << "# Generated by menuconfig. Planned features are never enabled.\n\n";
    for (const auto& feature : registry.features) {
        if (enabled.find(feature.id) != enabled.end()) {
            out << "CONFIG_" << feature.id << "=y\n";
        } else {
            out << "# CONFIG_" << feature.id << " is not set\n";
        }
    }
    write_atomic(path, out.str());
}

void write_header(const fs::path& path, const Registry& registry, const std::set<std::string>& enabled) {
    std::ostringstream out;
    out << "#pragma once\n";
    out << "// Generated by menuconfig. Do not edit by hand.\n\n";
    for (const auto& feature : registry.features) {
        out << "#define ST_FEATURE_" << feature.id << " "
            << (enabled.find(feature.id) != enabled.end() ? 1 : 0) << "\n";
    }
    write_atomic(path, out.str());
}

void touch_configured(const fs::path& header_path) {
    fs::path marker = header_path.parent_path();
    if (marker.empty()) {
        marker = ".";
    }
    fs::create_directories(marker);
    std::ofstream out(marker / ".configured", std::ios::app);
    out << '\n';
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read " + path.string());
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

int selftest_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++selftest_failures;
    }
}

Feature make_feature(const std::string& id,
                     const std::string& group,
                     bool critical,
                     const std::string& status,
                     std::vector<std::string> depends_on = {}) {
    Feature feature;
    feature.id = id;
    feature.group = group;
    feature.name = id;
    feature.description = "selftest fixture";
    feature.critical = critical;
    feature.target = critical ? "0.1" : "later";
    feature.status = status;
    feature.depends_on = std::move(depends_on);
    return feature;
}

int run_selftest(const std::optional<std::string>& explicit_registry) {
    selftest_failures = 0;

    Registry real;
    try {
        real = load_registry(registry_path(explicit_registry));
    } catch (const std::exception& error) {
        std::cerr << "FAIL: cannot load registry: " << error.what() << '\n';
        return 1;
    }
    check(real.features.size() >= 100, "real registry should contain the full feature set");
    check(real.find("P01") != nullptr, "real registry must contain P01");
    check(real.find("C04") != nullptr, "real registry must contain C04");

    const std::set<std::string> real_defaults = compute_defaults(real);
    for (const auto& feature : real.features) {
        const bool enabled = real_defaults.find(feature.id) != real_defaults.end();
        if (enabled) {
            check(feature.status == "ported", "enabled feature must be ported: " + feature.id);
            check(feature.critical, "enabled feature must be critical: " + feature.id);
        }
        if (feature.status == "planned") {
            check(!enabled, "planned feature must be disabled: " + feature.id);
        }
    }

    Registry fixture;
    fixture.features.push_back(make_feature("P01", "platform", true, "ported"));
    fixture.features.push_back(make_feature("C04", "chat", true, "planned", {"P01"}));
    fixture.features.push_back(make_feature("X01", "ui", false, "planned"));
    fixture.rebuild_index();
    fixture.validate();
    const std::set<std::string> fixture_defaults = compute_defaults(fixture);
    check(fixture_defaults.count("P01") == 1, "fixture defaults should enable ported critical P01");
    check(fixture_defaults.count("C04") == 0, "fixture defaults should not enable planned C04");
    check(fixture_defaults.count("X01") == 0, "fixture defaults should not enable planned X01");

    const fs::path temp = fs::temp_directory_path() /
                          ("st-menuconfig-selftest-" + std::to_string(static_cast<long long>(getpid())));
    std::error_code ignored;
    fs::remove_all(temp, ignored);
    fs::create_directories(temp);
    try {
        write_config(temp / "test.config", fixture, fixture_defaults);
        write_header(temp / "test.h", fixture, fixture_defaults);
        const std::string config = read_file(temp / "test.config");
        const std::string header = read_file(temp / "test.h");
        check(contains(config, "CONFIG_P01=y"), "fixture config should enable P01");
        check(contains(config, "# CONFIG_C04 is not set"), "fixture config should disable C04");
        check(contains(header, "#define ST_FEATURE_P01 1"), "fixture header should define P01=1");
        check(contains(header, "#define ST_FEATURE_C04 0"), "fixture header should define C04=0");
    } catch (const std::exception& error) {
        check(false, std::string("fixture file write failed: ") + error.what());
    }
    fs::remove_all(temp, ignored);

    Registry missing;
    missing.features.push_back(make_feature("A", "test", false, "planned", {"B"}));
    missing.rebuild_index();
    bool missing_threw = false;
    try {
        missing.validate();
    } catch (const std::exception&) {
        missing_threw = true;
    }
    check(missing_threw, "validation should reject a missing dependency");

    Registry cyclic;
    cyclic.features.push_back(make_feature("A", "test", false, "planned", {"B"}));
    cyclic.features.push_back(make_feature("B", "test", false, "planned", {"A"}));
    cyclic.rebuild_index();
    bool cycle_threw = false;
    try {
        cyclic.validate();
    } catch (const std::exception&) {
        cycle_threw = true;
    }
    check(cycle_threw, "validation should reject a dependency cycle");

    if (selftest_failures != 0) {
        std::cerr << "menuconfig selftest: FAIL (" << selftest_failures << " failures)\n";
        return 1;
    }
    std::cout << "menuconfig selftest: PASS\n";
    return 0;
}

std::string group_title(const std::string& group) {
    static const std::map<std::string, std::string> titles = {
        {"platform", "Platform and build"},
        {"characters", "Character management"},
        {"chat", "Chat, prompting, and generation"},
        {"ui", "User interface and media"},
        {"backends", "Model backends and interoperability"},
        {"operations", "Operations and security"},
    };
    const auto it = titles.find(group);
    return it == titles.end() ? group : it->second;
}

class Tui {
public:
    Tui(Registry registry,
        std::set<std::string> enabled,
        fs::path config_path,
        fs::path header_path)
        : registry_(std::move(registry)),
          enabled_(std::move(enabled)),
          config_path_(std::move(config_path)),
          header_path_(std::move(header_path)) {
        build_rows();
    }

    ~Tui() {
        if (active_) {
            endwin();
        }
    }

    int run() {
        initscr();
        active_ = true;
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        curs_set(0);
        start_color();
        use_default_colors();
        init_pair(1, COLOR_GREEN, -1);
        init_pair(2, COLOR_YELLOW, -1);
        init_pair(3, COLOR_RED, -1);
        init_pair(4, COLOR_CYAN, -1);

        bool running = true;
        while (running) {
            draw();
            const int key = getch();
            switch (key) {
                case KEY_UP:
                    move_selection(-1);
                    break;
                case KEY_DOWN:
                    move_selection(1);
                    break;
                case KEY_PPAGE:
                    move_selection(-static_cast<int>(visible_height()));
                    break;
                case KEY_NPAGE:
                    move_selection(static_cast<int>(visible_height()));
                    break;
                case ' ':
                case '\n':
                case KEY_ENTER:
                    toggle_selected();
                    break;
                case 's':
                case 'S':
                    save();
                    break;
                case 'q':
                case 'Q':
                    if (!dirty_ || quit_armed_) {
                        running = false;
                    } else {
                        quit_armed_ = true;
                        status_ = "Unsaved changes. Press q again to quit, or s to save.";
                    }
                    break;
                case '?':
                    show_help();
                    break;
                case KEY_RESIZE:
                    clear();
                    break;
                default:
                    quit_armed_ = false;
                    break;
            }
        }
        endwin();
        active_ = false;
        return 0;
    }

private:
    struct Row {
        bool header = false;
        std::string text;
        std::size_t feature_index = 0;
    };

    Registry registry_;
    std::set<std::string> enabled_;
    fs::path config_path_;
    fs::path header_path_;
    std::vector<Row> rows_;
    int selected_ = 0;
    int offset_ = 0;
    bool dirty_ = false;
    bool quit_armed_ = false;
    bool active_ = false;
    std::string status_;

    void build_rows() {
        rows_.clear();
        std::vector<std::string> groups;
        for (const auto& feature : registry_.features) {
            if (std::find(groups.begin(), groups.end(), feature.group) == groups.end()) {
                groups.push_back(feature.group);
            }
        }
        for (const auto& group : groups) {
            Row header;
            header.header = true;
            header.text = group_title(group);
            rows_.push_back(header);
            for (std::size_t i = 0; i < registry_.features.size(); ++i) {
                if (registry_.features[i].group != group) {
                    continue;
                }
                Row row;
                row.text = registry_.features[i].name;
                row.feature_index = i;
                rows_.push_back(row);
            }
        }
        if (rows_.empty()) {
            rows_.push_back(Row{true, "No features", 0});
        }
        selected_ = 0;
        while (selected_ < static_cast<int>(rows_.size()) && rows_[selected_].header) {
            ++selected_;
        }
        if (selected_ >= static_cast<int>(rows_.size())) {
            selected_ = static_cast<int>(rows_.size()) - 1;
        }
    }

    int visible_height() const {
        return std::max(1, LINES - 8);
    }

    void clamp_offset() {
        const int height = visible_height();
        if (selected_ < offset_) {
            offset_ = selected_;
        }
        if (selected_ >= offset_ + height) {
            offset_ = selected_ - height + 1;
        }
        if (offset_ < 0) {
            offset_ = 0;
        }
        const int max_offset = std::max(0, static_cast<int>(rows_.size()) - height);
        if (offset_ > max_offset) {
            offset_ = max_offset;
        }
    }

    void move_selection(int delta) {
        int next = selected_;
        do {
            next += delta;
        } while (next >= 0 && next < static_cast<int>(rows_.size()) && rows_[next].header);
        if (next >= 0 && next < static_cast<int>(rows_.size())) {
            selected_ = next;
        }
        quit_armed_ = false;
        clamp_offset();
    }

    const Feature* selected_feature() const {
        if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size()) || rows_[selected_].header) {
            return nullptr;
        }
        return &registry_.features[rows_[selected_].feature_index];
    }

    bool is_enabled(const std::string& id) const {
        return enabled_.find(id) != enabled_.end();
    }

    void toggle_selected() {
        const Feature* feature = selected_feature();
        if (feature == nullptr) {
            return;
        }
        quit_armed_ = false;
        if (feature->status != "ported") {
            status_ = feature->id + " is not ported yet and cannot be selected.";
            return;
        }
        if (feature->critical) {
            status_ = feature->id + " is a critical feature and is locked on.";
            return;
        }
        if (is_enabled(feature->id)) {
            std::set<std::string> affected = dependants_closure(feature->id);
            for (const auto& id : affected) {
                const Feature* dependant = registry_.find(id);
                if (dependant != nullptr && dependant->critical && is_enabled(id)) {
                    status_ = "Cannot disable " + feature->id + ": critical feature " + id + " depends on it.";
                    return;
                }
            }
            for (const auto& id : affected) {
                enabled_.erase(id);
            }
            enabled_.erase(feature->id);
            status_ = feature->id + " disabled.";
        } else {
            std::set<std::string> missing;
            std::set<std::string> closure = dependencies_closure(feature->id);
            for (const auto& id : closure) {
                const Feature* dependency = registry_.find(id);
                if (dependency != nullptr && dependency->status != "ported") {
                    missing.insert(id);
                }
            }
            if (!missing.empty()) {
                status_ = "Cannot enable " + feature->id + ": unported dependency " + *missing.begin() + ".";
                return;
            }
            enabled_.insert(feature->id);
            for (const auto& id : closure) {
                enabled_.insert(id);
            }
            status_ = feature->id + " enabled.";
        }
        dirty_ = true;
    }

    std::set<std::string> dependencies_closure(const std::string& id) const {
        std::set<std::string> result;
        std::vector<std::string> stack{id};
        while (!stack.empty()) {
            const std::string current = stack.back();
            stack.pop_back();
            const Feature* feature = registry_.find(current);
            if (feature == nullptr) {
                continue;
            }
            for (const auto& dep : feature->depends_on) {
                if (result.insert(dep).second) {
                    stack.push_back(dep);
                }
            }
        }
        return result;
    }

    std::set<std::string> dependants_closure(const std::string& id) const {
        std::set<std::string> result;
        std::vector<std::string> stack{id};
        while (!stack.empty()) {
            const std::string current = stack.back();
            stack.pop_back();
            for (const auto& feature : registry_.features) {
                if (std::find(feature.depends_on.begin(), feature.depends_on.end(), current) !=
                    feature.depends_on.end()) {
                    if (result.insert(feature.id).second) {
                        stack.push_back(feature.id);
                    }
                }
            }
        }
        return result;
    }

    void save() {
        try {
            write_config(config_path_, registry_, enabled_);
            write_header(header_path_, registry_, enabled_);
            touch_configured(header_path_);
            dirty_ = false;
            quit_armed_ = false;
            status_ = "Saved " + config_path_.string() + " and " + header_path_.string() + ".";
        } catch (const std::exception& error) {
            status_ = std::string("Save failed: ") + error.what();
        }
    }

    void show_help() {
        const Feature* feature = selected_feature();
        if (feature == nullptr) {
            status_ = "Select a feature to see its help.";
            return;
        }
        clear();
        int row = 1;
        auto line = [&](const std::string& text, int color = 0, bool bold = false) {
            if (row >= LINES - 2) {
                return;
            }
            if (bold) {
                attron(A_BOLD);
            }
            if (color != 0) {
                attron(COLOR_PAIR(color));
            }
            mvaddnstr(row++, 2, text.c_str(), std::max(0, COLS - 4));
            if (color != 0) {
                attroff(COLOR_PAIR(color));
            }
            if (bold) {
                attroff(A_BOLD);
            }
        };
        line("Feature " + feature->id + ": " + feature->name, 4, true);
        line("Group: " + group_title(feature->group));
        line("Status: " + feature->status + "   Target: " + feature->target +
             "   Critical: " + (feature->critical ? "yes" : "no"));
        std::string deps = "Depends on: ";
        if (feature->depends_on.empty()) {
            deps += "(none)";
        } else {
            for (std::size_t i = 0; i < feature->depends_on.size(); ++i) {
                if (i != 0) {
                    deps += ", ";
                }
                deps += feature->depends_on[i];
            }
        }
        line(deps);
        line("");
        line(feature->description);
        line("");
        line("Press any key to return.", 2);
        refresh();
        getch();
        clear();
    }

    void draw() {
        clamp_offset();
        erase();
        attron(A_BOLD);
        mvaddnstr(0, 0, "SillyTavern Port Configuration", std::max(0, COLS));
        attroff(A_BOLD);
        const std::string right = "make menuconfig";
        if (static_cast<int>(right.size()) < COLS) {
            mvaddnstr(0, COLS - static_cast<int>(right.size()), right.c_str(), static_cast<int>(right.size()));
        }
        mvaddnstr(1, 0,
                  "Up/Down move   Space/Enter toggle   s save   q quit   ? help",
                  std::max(0, COLS));

        const int height = visible_height();
        const int list_top = 3;
        for (int i = 0; i < height; ++i) {
            const int row_index = offset_ + i;
            if (row_index >= static_cast<int>(rows_.size())) {
                break;
            }
            const Row& row = rows_[row_index];
            const int screen_row = list_top + i;
            if (row.header) {
                attron(A_BOLD | COLOR_PAIR(4));
                mvaddnstr(screen_row, 0, row.text.c_str(), std::max(0, COLS));
                attroff(A_BOLD | COLOR_PAIR(4));
                continue;
            }
            const Feature& feature = registry_.features[row.feature_index];
            const bool enabled = is_enabled(feature.id);
            std::string marker;
            int color = 0;
            if (feature.status != "ported") {
                marker = "(not ported)";
                color = 3;
            } else if (feature.critical) {
                marker = enabled ? "[x] (locked)" : "[ ] (locked)";
                color = 1;
            } else {
                marker = enabled ? "[x]" : "[ ]";
            }
            std::string text = std::string(enabled ? "* " : "  ") + marker + " " + feature.id + "  " + feature.name;
            if (feature.critical && feature.status == "ported") {
                text += "  [critical]";
            }
            if (row_index == selected_) {
                attron(A_REVERSE);
            }
            if (feature.status != "ported") {
                attron(A_DIM | COLOR_PAIR(color));
            } else if (color != 0) {
                attron(COLOR_PAIR(color));
            }
            mvaddnstr(screen_row, 0, text.c_str(), std::max(0, COLS));
            if (feature.status != "ported") {
                attroff(A_DIM | COLOR_PAIR(color));
            } else if (color != 0) {
                attroff(COLOR_PAIR(color));
            }
            if (row_index == selected_) {
                attroff(A_REVERSE);
            }
        }

        const int help_top = std::max(list_top + height + 1, LINES - 5);
        attron(A_BOLD);
        mvaddnstr(help_top, 0,
                  "Help: select a feature and press ? for full details",
                  std::max(0, COLS));
        attroff(A_BOLD);
        const Feature* feature = selected_feature();
        if (feature != nullptr) {
            std::string line = feature->id + ": " + feature->description;
            mvaddnstr(help_top + 1, 0, line.c_str(), std::max(0, COLS));
            std::string deps = "Depends on: ";
            if (feature->depends_on.empty()) {
                deps += "(none)";
            } else {
                for (std::size_t i = 0; i < feature->depends_on.size(); ++i) {
                    if (i != 0) {
                        deps += ", ";
                    }
                    deps += feature->depends_on[i];
                }
            }
            mvaddnstr(help_top + 2, 0, deps.c_str(), std::max(0, COLS));
        }
        if (!status_.empty()) {
            attron(COLOR_PAIR(2));
            mvaddnstr(LINES - 1, 0, status_.c_str(), std::max(0, COLS));
            attroff(COLOR_PAIR(2));
        }
        refresh();
    }
};

int run_interactive(const Registry& registry,
                    const std::set<std::string>& saved_enabled,
                    const fs::path& config_path,
                    const fs::path& header_path) {
    std::set<std::string> enabled;
    for (const auto& feature : registry.features) {
        if (feature.status != "ported") {
            continue;
        }
        if (saved_enabled.find(feature.id) != saved_enabled.end()) {
            enabled.insert(feature.id);
        }
        if (feature.critical) {
            enabled.insert(feature.id);
        }
    }
    // Ensure dependency closure for anything enabled.
    bool changed = true;
    while (changed) {
        changed = false;
        const std::vector<std::string> current(enabled.begin(), enabled.end());
        for (const auto& id : current) {
            const Feature* feature = registry.find(id);
            if (feature == nullptr) {
                continue;
            }
            for (const auto& dep : feature->depends_on) {
                const Feature* dependency = registry.find(dep);
                if (dependency == nullptr || dependency->status != "ported") {
                    enabled.erase(id);
                    changed = true;
                    break;
                }
                if (enabled.insert(dep).second) {
                    changed = true;
                }
            }
        }
    }

    Tui tui(registry, std::move(enabled), config_path, header_path);
    return tui.run();
}

void print_usage() {
    std::cout
        << "usage: menuconfig [options]\n\n"
        << "Options:\n"
        << "  --features PATH   feature registry (default: features/features.json)\n"
        << "  --config PATH     output config (default: .config)\n"
        << "  --header PATH     output C++ header (default: build/features.h)\n"
        << "  --defaults        write defaults without opening the TUI\n"
        << "  --selftest        run built-in registry/config tests\n"
        << "  --help            show this help\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::optional<std::string> features_path;
    fs::path config_path = ".config";
    fs::path header_path = "build/features.h";
    bool defaults = false;
    bool selftest = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need_value = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error(name + " requires a value");
            }
            return argv[++i];
        };
        try {
            if (arg == "--features") {
                features_path = need_value(arg);
            } else if (arg == "--config") {
                config_path = need_value(arg);
            } else if (arg == "--header") {
                header_path = need_value(arg);
            } else if (arg == "--defaults") {
                defaults = true;
            } else if (arg == "--selftest") {
                selftest = true;
            } else if (arg == "--help" || arg == "-h") {
                print_usage();
                return 0;
            } else {
                std::cerr << "menuconfig: unknown option: " << arg << '\n';
                print_usage();
                return 2;
            }
        } catch (const std::exception& error) {
            std::cerr << "menuconfig: " << error.what() << '\n';
            return 2;
        }
    }

    try {
        if (selftest) {
            return run_selftest(features_path);
        }
        Registry registry = load_registry(registry_path(features_path));
        if (defaults) {
            const std::set<std::string> enabled = compute_defaults(registry);
            write_config(config_path, registry, enabled);
            write_header(header_path, registry, enabled);
            touch_configured(header_path);
            std::cout << "menuconfig: wrote " << config_path << " and " << header_path << '\n';
            return 0;
        }
        const std::set<std::string> saved = load_config(config_path, registry);
        return run_interactive(registry, saved, config_path, header_path);
    } catch (const std::exception& error) {
        std::cerr << "menuconfig: " << error.what() << '\n';
        return 2;
    }
}
