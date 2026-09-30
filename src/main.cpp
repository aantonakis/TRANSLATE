#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Electron.h"
#include "ProgressBar.h"

namespace {

struct RunConfig {
    std::vector<double> fields;
    double electron_energy = 0.0;
    double cutoff = 0.0;
    int write_every = 0;
    int batches = 0;
    int cores = 1;
    int debug = 0;
    int status = 0;
    std::uint64_t seed = 0;
    int job_index = -1;
    std::string output_dir = "../py/simulation-runs";
    std::string output_prefix;
};

std::string trim(const std::string& value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<double> parse_fields(std::string value)
{
    std::replace(value.begin(), value.end(), ',', ' ');
    std::istringstream stream(value);
    std::vector<double> fields;
    double field = 0.0;
    while (stream >> field)
        fields.push_back(field);
    if (!stream.eof())
        throw std::runtime_error("Invalid electric field list: " + value);
    return fields;
}

std::unordered_map<std::string, std::string> read_config(const std::string& path)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Unable to open config file: " + path);

    std::unordered_map<std::string, std::string> values;
    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto comment = line.find('#');
        if (comment != std::string::npos)
            line.erase(comment);
        line = trim(line);
        if (line.empty())
            continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos)
            throw std::runtime_error("Expected key = value at " + path + ":" + std::to_string(line_number));
        const std::string key = trim(line.substr(0, separator));
        const std::string value = trim(line.substr(separator + 1));
        if (key.empty() || value.empty())
            throw std::runtime_error("Empty key or value at " + path + ":" + std::to_string(line_number));
        values[key] = value;
    }
    return values;
}

template <typename T>
T parse_number(const std::unordered_map<std::string, std::string>& values,
               const std::string& key)
{
    const auto found = values.find(key);
    if (found == values.end())
        throw std::runtime_error("Missing required config key: " + key);
    std::istringstream stream(found->second);
    T result;
    char trailing;
    if (!(stream >> result) || (stream >> trailing))
        throw std::runtime_error("Invalid value for config key " + key + ": " + found->second);
    return result;
}

RunConfig config_from_file(const std::string& path)
{
    const auto values = read_config(path);
    RunConfig config;
    const auto fields = values.find("fields_v_cm");
    if (fields == values.end())
        throw std::runtime_error("Missing required config key: fields_v_cm");
    config.fields = parse_fields(fields->second);
    config.electron_energy = parse_number<double>(values, "electron_energy_ev");
    config.cutoff = parse_number<double>(values, "cutoff_s");
    config.write_every = parse_number<int>(values, "write_every");
    config.batches = parse_number<int>(values, "batches");
    config.seed = parse_number<std::uint64_t>(values, "seed");

    if (const auto item = values.find("cores"); item != values.end())
        config.cores = std::stoi(item->second);
    if (const auto item = values.find("debug"); item != values.end())
        config.debug = std::stoi(item->second);
    if (const auto item = values.find("status"); item != values.end())
        config.status = std::stoi(item->second);
    if (const auto item = values.find("output_dir"); item != values.end())
        config.output_dir = item->second;
    if (const auto item = values.find("output_prefix"); item != values.end())
        config.output_prefix = item->second;

    static const std::vector<std::string> known_keys = {
        "fields_v_cm", "electron_energy_ev", "cutoff_s", "write_every",
        "batches", "seed", "cores", "debug", "status", "output_dir",
        "output_prefix"
    };
    for (const auto& item : values) {
        if (std::find(known_keys.begin(), known_keys.end(), item.first) == known_keys.end())
            throw std::runtime_error("Unknown config key: " + item.first);
    }
    return config;
}

RunConfig interactive_config()
{
    RunConfig config;
    std::cout << "V/cm (multiple values separated by spaces): ";
    std::string line;
    std::getline(std::cin, line);
    config.fields = parse_fields(line);

    std::cout << "Electron starting energy [eV]:";
    std::cin >> config.electron_energy;
    std::cout << "Stop After (s): ";
    std::cin >> config.cutoff;
    std::cout << "Record Every n Measurements: ";
    std::cin >> config.write_every;
    std::cout << "Batches (in groups of 1): ";
    std::cin >> config.batches;
    if (!std::cin)
        throw std::runtime_error("Invalid interactive input");
    config.seed = static_cast<std::uint64_t>(
        std::chrono::system_clock::now().time_since_epoch().count());
    return config;
}

void validate(const RunConfig& config)
{
    if (config.fields.empty())
        throw std::runtime_error("At least one electric field is required");
    if (config.electron_energy < 0.0)
        throw std::runtime_error("electron_energy_ev must be non-negative");
    if (config.cutoff <= 0.0)
        throw std::runtime_error("cutoff_s must be greater than zero");
    if (config.write_every <= 0 || config.batches <= 0 || config.cores <= 0)
        throw std::runtime_error("write_every, batches, and cores must be greater than zero");
    if (config.job_index < -1)
        throw std::runtime_error("job index must be non-negative");
}

void print_help(const char* program)
{
    std::cout
        << "Usage:\n"
        << "  " << program << "                         # interactive mode\n"
        << "  " << program << " --config FILE [options] # batch mode\n\n"
        << "Options:\n"
        << "  --job-index N       Grid/process index used in seeds and filenames\n"
        << "  --seed N            Override the config base seed\n"
        << "  --output-dir DIR    Override the config output directory\n"
        << "  --output-prefix P   Override the config filename prefix\n"
        << "  --help              Show this message\n";
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        std::string config_path;
        bool show_help = false;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--help" || argument == "-h") {
                show_help = true;
            } else if (argument == "--config" && i + 1 < argc) {
                config_path = argv[++i];
            } else if ((argument == "--job-index" || argument == "--seed" ||
                        argument == "--output-dir" || argument == "--output-prefix") && i + 1 < argc) {
                ++i;
            } else {
                throw std::runtime_error("Unknown or incomplete argument: " + argument);
            }
        }
        if (show_help) {
            print_help(argv[0]);
            return 0;
        }

        RunConfig config = config_path.empty() ? interactive_config() : config_from_file(config_path);
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--config") {
                ++i;
            } else if (argument == "--job-index") {
                config.job_index = std::stoi(argv[++i]);
            } else if (argument == "--seed") {
                config.seed = std::stoull(argv[++i]);
            } else if (argument == "--output-dir") {
                config.output_dir = argv[++i];
            } else if (argument == "--output-prefix") {
                config.output_prefix = argv[++i];
            }
        }
        validate(config);
        std::filesystem::create_directories(config.output_dir);

        std::cout << "Base seed: " << config.seed << "\n"
                  << "Job index: " << config.job_index << "\n"
                  << "Output directory: " << config.output_dir << "\n";

        for (double field : config.fields) {
            std::vector<std::thread> branches;
            ProgressBar bar(config.cores);
            for (int core = 0; core < config.cores; ++core) {
                branches.emplace_back(generate_plot, static_cast<int>(field),
                    config.electron_energy, config.cutoff, config.cores,
                    config.write_every, core, config.batches, config.debug,
                    config.status, config.seed, config.job_index,
                    std::cref(config.output_dir), std::cref(config.output_prefix),
                    std::ref(bar));
            }
            for (auto& branch : branches)
                branch.join();
            std::cout << "\n";
        }

        std::cout << "Complete.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 2;
    }
}
