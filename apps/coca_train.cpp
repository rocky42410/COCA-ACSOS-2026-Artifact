// ============================================================================
// apps/coca_train.cpp - Main COCA training application with CSV support
//
// Accepts multiple training files. Use either:
//   --data file1.csv --data file2.csv --data file3.csv
// or:
//   --data file1.csv,file2.csv,file3.csv
// or both. Files are loaded sequentially and their windows pooled into one
// training set.
//
// Schema validation: all files must have the same feature count. If they
// have headers, the headers must match exactly (column names and order).
// Mismatch is a hard error - silently pooling files with different schemas
// would train a meaningless model.
//
// Per-file stats are reported separately so you can see how each session
// contributes; pooled stats appear at the end.
// ============================================================================
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>

#include "../src/coca_model.hpp"
#include "../src/io/csv_reader.hpp"
#include "../src/utils/config_parser.hpp"
#include "../src/utils/model_io.hpp"

using namespace coca;

// Split "a.csv,b.csv,c.csv" into ["a.csv","b.csv","c.csv"]. Trims whitespace.
static std::vector<std::string> split_comma(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        // trim
        size_t a = item.find_first_not_of(" \t\r\n");
        size_t b = item.find_last_not_of(" \t\r\n");
        if (a != std::string::npos) out.push_back(item.substr(a, b - a + 1));
    }
    return out;
}

int main(int argc, char** argv) {
    std::cout << "\nCOCA Training Application\n\n";

    // Parse command line arguments
    std::vector<std::string> data_files;
    std::string config_file = "coca_config.yaml";
    std::string output_model = "trained_model.coca";
    size_t window_size = 10;
    size_t window_stride = 5;
    bool skip_header = true;
    bool skip_timestamp = true;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--data" || arg == "--csv") && i + 1 < argc) {
            // Accept comma-separated list, or single path. Repeatable.
            for (const auto& f : split_comma(argv[++i])) data_files.push_back(f);
        } else if (arg == "--config" && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output_model = argv[++i];
        } else if (arg == "--window" && i + 1 < argc) {
            window_size = std::stoi(argv[++i]);
        } else if (arg == "--stride" && i + 1 < argc) {
            window_stride = std::stoi(argv[++i]);
        } else if (arg == "--no-header") {
            skip_header = false;
        } else if (arg == "--no-timestamp") {
            skip_timestamp = false;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " --data <file>[,file...] [--data <file>...] [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --data <file>     Input CSV file. Repeatable. Comma-separated lists\n";
            std::cout << "                    are also accepted: --data a.csv,b.csv,c.csv\n";
            std::cout << "                    All files must share the same feature schema\n";
            std::cout << "                    (column count, and column names if headers present).\n";
            std::cout << "  --config <file>   Configuration file (default: coca_config.yaml)\n";
            std::cout << "  --output <file>   Output model file (default: trained_model.coca)\n";
            std::cout << "  --window <size>   Window size (default: 10)\n";
            std::cout << "  --stride <size>   Window stride (default: 5)\n";
            std::cout << "  --no-header       CSV has no header row\n";
            std::cout << "  --no-timestamp    Don't skip first column\n";
            std::cout << "\nExamples:\n";
            std::cout << "  Single file:\n";
            std::cout << "    " << argv[0] << " --data session_1.csv\n";
            std::cout << "  Multiple sessions, repeated flag:\n";
            std::cout << "    " << argv[0] << " --data s1.csv --data s2.csv --data s3.csv\n";
            std::cout << "  Multiple sessions, comma-separated:\n";
            std::cout << "    " << argv[0] << " --data s1.csv,s2.csv,s3.csv\n";
            return 0;
        }
    }

    if (data_files.empty()) {
        std::cerr << "Error: No data files specified. Use --data <file>.\n";
        std::cerr << "Run with --help for usage.\n";
        return 1;
    }

    // Load configuration
    COCAConfig config;
    if (!ConfigParser::load_config(config_file, config)) {
        std::cout << "Using default configuration\n";
    }
    config.T = window_size;

    // ========================================================================
    // Load each file. Validate schema consistency. Concatenate windows.
    // ========================================================================
    std::cout << "Loading " << data_files.size() << " data file(s):\n";
    for (size_t i = 0; i < data_files.size(); ++i) {
        std::cout << "  [" << (i + 1) << "] " << data_files[i] << "\n";
    }
    std::cout << "  Skip header: " << (skip_header ? "yes" : "no") << "\n";
    std::cout << "  Skip timestamp: " << (skip_timestamp ? "yes" : "no") << "\n\n";

    std::vector<std::vector<float>> all_windows;
    size_t total_samples = 0;
    size_t expected_features = 0;
    std::vector<std::string> expected_header;
    std::vector<size_t> per_file_window_counts;
    std::vector<size_t> per_file_sample_counts;

    for (size_t fi = 0; fi < data_files.size(); ++fi) {
        const std::string& path = data_files[fi];
        std::cout << "===== File " << (fi + 1) << "/" << data_files.size()
                  << ": " << path << " =====\n";

        CSVReader reader;
        if (!reader.load(path, skip_header, skip_timestamp, true)) {
            std::cerr << "Error: Failed to load CSV file: " << path << "\n";
            return 1;
        }

        size_t n_features = reader.get_feature_count();
        const auto& header = reader.get_header();

        // Schema validation against the first file
        if (fi == 0) {
            expected_features = n_features;
            expected_header = header;
            std::cout << "  (Schema reference: " << n_features << " features";
            if (skip_header && !header.empty())
                std::cout << ", header validated for subsequent files";
            std::cout << ")\n";
        } else {
            if (n_features != expected_features) {
                std::cerr << "\nFATAL: Feature count mismatch.\n"
                          << "  First file: " << expected_features << " features\n"
                          << "  This file (" << path << "): " << n_features << " features\n"
                          << "Pooling files with different feature schemas would produce a\n"
                          << "meaningless model. Re-record with consistent feature_config.\n";
                return 1;
            }
            // Header check (column names must match if headers are present).
            if (skip_header && !expected_header.empty() && !header.empty()) {
                if (header.size() != expected_header.size()) {
                    std::cerr << "\nFATAL: Header column count mismatch.\n"
                              << "  First file: " << expected_header.size() << " columns\n"
                              << "  This file: " << header.size() << " columns\n";
                    return 1;
                }
                for (size_t c = 0; c < header.size(); ++c) {
                    if (header[c] != expected_header[c]) {
                        std::cerr << "\nFATAL: Header column name mismatch at index " << c << ":\n"
                                  << "  First file: \"" << expected_header[c] << "\"\n"
                                  << "  This file:  \"" << header[c] << "\"\n"
                                  << "Pooling files with reordered or renamed columns would\n"
                                  << "produce a meaningless model.\n";
                        return 1;
                    }
                }
            }
        }

        // Create windows from this file. Each file is windowed independently
        // so we never produce a window that straddles two recordings (which
        // would contain a temporal discontinuity).
        std::vector<std::vector<float>> windows = reader.get_windows(config.T, window_stride, true);
        size_t added = windows.size();
        per_file_window_counts.push_back(added);
        per_file_sample_counts.push_back(reader.get_sample_count());
        total_samples += reader.get_sample_count();

        // Move the windows into the pool (avoids copying).
        all_windows.reserve(all_windows.size() + added);
        for (auto& w : windows) all_windows.push_back(std::move(w));
        std::cout << "  Added " << added << " windows from this file\n\n";
    }

    if (all_windows.empty()) {
        std::cerr << "Error: No training windows produced from any file.\n";
        return 1;
    }

    // Update config with feature count
    config.D = expected_features;

    std::cout << "===== Pooled Training Set =====\n";
    std::cout << "  Files:   " << data_files.size() << "\n";
    std::cout << "  Samples: " << total_samples << " total\n";
    for (size_t i = 0; i < data_files.size(); ++i) {
        std::cout << "    [" << (i + 1) << "] " << per_file_sample_counts[i]
                  << " samples -> " << per_file_window_counts[i] << " windows\n";
    }
    std::cout << "  Windows: " << all_windows.size() << " total\n\n";

    std::cout << "Configuration:\n";
    std::cout << "  Window size: " << config.T << "\n";
    std::cout << "  Window stride: " << window_stride << "\n";
    std::cout << "  Features: " << config.D << "\n";
    std::cout << "  Latent dim: " << config.C << "\n";
    std::cout << "  Projection dim: " << config.K << "\n";
    std::cout << "  λ_rec: " << config.lambda_rec << "\n";
    std::cout << "  λ_inv: " << config.lambda_inv << "\n";
    std::cout << "  λ_var: " << config.lambda_var << "\n";
    std::cout << "  ζ: " << config.zeta << "\n";
    std::cout << "  Score mix: " << config.score_mix << "\n";
    std::cout << "  Threshold mode: " << config.threshold_mode << "\n";
    std::cout << "  Seed: " << config.seed << "\n";
    std::cout << "  Downweight constant features: "
              << (config.downweight_constant_features ? "true" : "false") << "\n\n";

    if (all_windows.size() < 100) {
        std::cerr << "Warning: Very few training windows (" << all_windows.size() << ").\n";
        std::cerr << "Consider longer recordings, more files, or smaller stride.\n";
    }

    // ========================================================================
    // Train
    // ========================================================================
    std::cout << "--- Starting Training ---\n";
    auto start_time = std::chrono::high_resolution_clock::now();

    COCAModel model(config);
    train_coca_model(model, all_windows, config);

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    std::cout << "\nTraining time: " << duration.count() << " seconds\n";

    // Save model. Create the parent directory if necessary and fail loudly
    // if the model cannot be written, so a missing --output directory does
    // not produce a "successful" run with no model.
    std::cout << "\nSaving model to: " << output_model << "\n";
    try {
        std::filesystem::path out_path(output_model);
        if (out_path.has_parent_path() && !out_path.parent_path().empty()) {
            std::filesystem::create_directories(out_path.parent_path());
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: could not create output directory for "
                  << output_model << ": " << e.what() << "\n";
        return 1;
    }
    if (!ModelIO::save_model(model, output_model)) {
        std::cerr << "Error: failed to write model file: " << output_model
                  << " (check path and permissions)\n";
        return 1;
    }

    // Training summary now lists all input files.
    std::ofstream summary("training_summary.txt");
    summary << "# COCA Training Summary\n";
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    summary << "# " << std::ctime(&time_t) << "\n";
    summary << "Data files (" << data_files.size() << "):\n";
    for (size_t i = 0; i < data_files.size(); ++i) {
        summary << "  [" << (i + 1) << "] " << data_files[i]
                << "  (" << per_file_sample_counts[i] << " samples, "
                << per_file_window_counts[i] << " windows)\n";
    }
    summary << "Total samples: " << total_samples << "\n";
    summary << "Features: " << config.D << "\n";
    summary << "Training windows: " << all_windows.size() << "\n";
    summary << "Window size: " << config.T << "\n";
    summary << "Window stride: " << window_stride << "\n";
    summary << "Latent dim: " << config.C << "\n";
    summary << "Projection dim: " << config.K << "\n";
    summary << "Final threshold: " << model.anomaly_threshold << "\n";
    summary << "Training time: " << duration.count() << " seconds\n";
    summary.close();

    ConfigParser::save_config("training_config_used.yaml", config);

    std::cout << "\nTRAINING COMPLETE\n\n";

    std::cout << "Outputs:\n";
    std::cout << "  Model: " << output_model << "\n";
    std::cout << "  Log: training_log.csv\n";
    std::cout << "  Summary: training_summary.txt\n";
    std::cout << "  Config used: training_config_used.yaml\n";
    return 0;
}
