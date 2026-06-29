#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <mutex>
#include <deque>
#include <atomic>
#include <csignal>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <unordered_map>

#include <unitree/idl/go2/SportModeState_.hpp>
#include <unitree/idl/go2/LowState_.hpp>
#include <unitree/idl/go2/UwbState_.hpp>
#include <unitree/idl/ros2/PointStamped_.hpp>
#include <unitree/idl/go2/HeightMap_.hpp>
#include <unitree/robot/channel/channel_subscriber.hpp>

// ============ CONFIGURATION ============
struct FeatureConfig {
    size_t index;
    std::string name;
    bool enabled;
    std::string description;
};

struct Config {
    std::string network_interface = "eth0";
    std::string config_file = "feature_config_full.csv";
    std::string output_file = "robot_data.csv";
    uint32_t fusion_rate_hz = 50;
    uint32_t duration_seconds = 0;
    bool verbose = false;
    bool log_all = false;
};

// ============ GLOBAL STATE ============
std::atomic<bool> should_exit(false);
std::vector<FeatureConfig> feature_configs;
std::vector<size_t> enabled_indices;
std::ofstream csv_output;
std::mutex data_mutex;

// Feature storage
constexpr size_t MAX_FEATURES = 256;
std::vector<float> current_features(MAX_FEATURES, 0.0f);
std::vector<bool> feature_valid(MAX_FEATURES, false);

// Buffers for windowed statistics
struct SensorBuffer {
    std::deque<std::vector<float>> samples;
    std::deque<int64_t> timestamps;
    std::mutex mutex;
    int64_t last_update = 0;
    uint64_t callback_count = 0;
};

std::unordered_map<std::string, SensorBuffer> sensor_buffers;

// Height map cache
struct HeightMapCache {
    float mean = 0;
    float std_dev = 0;
    float gradient = 0;
    std::mutex mutex;
};
HeightMapCache height_cache;

// Statistics
std::atomic<uint64_t> frames_written(0);
std::atomic<uint64_t> total_callbacks(0);

// ============ UTILITY FUNCTIONS ============
int64_t now_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        std::cout << "\nShutdown signal received..." << std::endl;
        should_exit = true;
    }
}

bool load_feature_config(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open config file: " << filename << std::endl;
        return false;
    }
    
    std::string line;
    std::getline(file, line); // Skip header
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::stringstream ss(line);
        std::string index_str, name, enabled_str, description;
        
        std::getline(ss, index_str, ',');
        std::getline(ss, name, ',');
        std::getline(ss, enabled_str, ',');
        std::getline(ss, description);
        
        FeatureConfig config;
        config.index = std::stoul(index_str);
        config.name = name;
        config.enabled = (enabled_str == "1");
        config.description = description;
        
        if (config.index < MAX_FEATURES) {
            feature_configs.push_back(config);
            if (config.enabled) {
                enabled_indices.push_back(config.index);
            }
        }
    }
    
    std::cout << "Loaded " << feature_configs.size() << " feature definitions\n";
    std::cout << "Enabled features: " << enabled_indices.size() << "\n";
    
    return !feature_configs.empty();
}

// ============ HEIGHT MAP PROCESSING ============
void compute_height_features(const unitree_go::msg::dds_::HeightMap_* msg) {
    std::vector<float> valid_heights;
    
    // Count total cells and valid cells
    size_t total_cells = msg->data().size();
    size_t empty_cells = 0;
    
    for (const auto& h : msg->data()) {
        if (h != 1.0e9f) {
            valid_heights.push_back(h);
        } else {
            empty_cells++;
        }
    }
    
    std::lock_guard<std::mutex> lock(height_cache.mutex);
    
    // Debug logging
    static int callback_count = 0;
    static int consecutive_empty = 0;
    callback_count++;
    
    // Initialize all features to 0
    current_features[84] = 0;  // mean
    current_features[85] = 0;  // std_dev
    current_features[86] = 0;  // gradient
    current_features[87] = 0;  // min_height
    current_features[88] = 0;  // max_height
    current_features[89] = 0;  // height_range
    current_features[90] = 0;  // coverage_ratio
    current_features[91] = 0;  // roughness
    current_features[92] = 0;  // percentile_25
    current_features[93] = 0;  // percentile_50 (median)
    current_features[94] = 0;  // percentile_75
    current_features[95] = 0;  // obstacle_count (cells > 0.1m)
    current_features[96] = 0;  // pit_count (cells < -0.1m)
    current_features[97] = msg->resolution();  // map resolution
    current_features[98] = static_cast<float>(msg->width());   // map width
    current_features[99] = static_cast<float>(msg->height());  // map height
    
    // Mark all height features as valid
    for (int i = 84; i <= 99; i++) {
        feature_valid[i] = true;
    }
    
    if (valid_heights.empty()) {
        consecutive_empty++;
        
        if (consecutive_empty <= 5 || consecutive_empty % 50 == 0) {
            std::cout << "[HeightMap] Warning: All " << total_cells 
                     << " cells empty. Consecutive: " << consecutive_empty << "\n";
        }
        
        // All features remain at 0
        current_features[90] = 0;  // coverage_ratio = 0
        
    } else {
        // Reset consecutive empty counter
        if (consecutive_empty > 0) {
            std::cout << "[HeightMap] Valid data after " << consecutive_empty << " empty maps\n";
            consecutive_empty = 0;
        }
        
        // Sort heights for percentiles and min/max
        std::sort(valid_heights.begin(), valid_heights.end());
        
        // Basic statistics
        float sum = std::accumulate(valid_heights.begin(), valid_heights.end(), 0.0f);
        current_features[84] = sum / valid_heights.size();  // mean
        
        // Min/Max
        current_features[87] = valid_heights.front();  // min
        current_features[88] = valid_heights.back();   // max
        current_features[89] = valid_heights.back() - valid_heights.front();  // range
        
        // Coverage ratio
        current_features[90] = static_cast<float>(valid_heights.size()) / total_cells;
        
        // Std dev
        if (valid_heights.size() > 1) {
            float mean = current_features[84];
            float sq_sum = 0;
            for (float h : valid_heights) {
                sq_sum += (h - mean) * (h - mean);
            }
            current_features[85] = std::sqrt(sq_sum / (valid_heights.size() - 1));
        }
        
        // Percentiles
        size_t n = valid_heights.size();
        current_features[92] = valid_heights[n * 25 / 100];  // 25th percentile
        current_features[93] = valid_heights[n * 50 / 100];  // median
        current_features[94] = valid_heights[n * 75 / 100];  // 75th percentile
        
        // Roughness (IQR)
        current_features[91] = current_features[94] - current_features[92];
        
        // Count obstacles and pits
        int obstacles = 0, pits = 0;
        for (float h : valid_heights) {
            if (h > 0.1f) obstacles++;
            if (h < -0.1f) pits++;
        }
        current_features[95] = static_cast<float>(obstacles) / valid_heights.size();
        current_features[96] = static_cast<float>(pits) / valid_heights.size();
        
        // Gradient calculation (max slope)
        int width = msg->width();
        int height = msg->height();
        float max_grad = 0;
        float resolution = msg->resolution();
        
        int data_size = msg->data().size();
        
        if (width * height == data_size && resolution > 0) {
            std::vector<float> gradients;
            
            for (int y = 1; y < height-1; y++) {
                for (int x = 1; x < width-1; x++) {
                    int idx = x + width * y;
                    float center = msg->data()[idx];
                    if (center == 1.0e9f) continue;
                    
                    // Check 4-connected neighbors
                    int neighbors[4] = {idx-1, idx+1, idx-width, idx+width};
                    
                    for (int n_idx : neighbors) {
                        if (n_idx >= 0 && n_idx < data_size) {
                            float neighbor = msg->data()[n_idx];
                            if (neighbor != 1.0e9f) {
                                float grad = std::abs(center - neighbor) / resolution;
                                gradients.push_back(grad);
                                max_grad = std::max(max_grad, grad);
                            }
                        }
                    }
                }
            }
            
            current_features[86] = max_grad;
        }
        
        // Log periodically
        if (callback_count % 100 == 0) {
            std::cout << "[HeightMap] Coverage: " << (100.0f * current_features[90]) 
                     << "%, Range: [" << current_features[87] << ", " << current_features[88] 
                     << "], Roughness: " << current_features[91] << "\n";
        }
    }
    
    // Log first few callbacks for debugging
    if (callback_count <= 3) {
        std::cout << "[HeightMap] Features extracted:\n"
                 << "  Mean=" << current_features[84] << ", Std=" << current_features[85] 
                 << ", Gradient=" << current_features[86] << "\n"
                 << "  Range=[" << current_features[87] << "," << current_features[88] 
                 << "], Coverage=" << current_features[90] << "\n"
                 << "  Percentiles=[" << current_features[92] << "," << current_features[93] 
                 << "," << current_features[94] << "]\n";
    }
}

// ============ FEATURE UPDATES ============
void update_sport_features(const unitree_go::msg::dds_::SportModeState_* msg) {
    auto& buffer = sensor_buffers["sport"];
    std::lock_guard<std::mutex> lock(buffer.mutex);
    
    // Store IMU samples for statistics
    std::vector<float> sample;
    auto acc = msg->imu_state().accelerometer();
    sample.insert(sample.end(), acc.begin(), acc.end());
    auto gyro = msg->imu_state().gyroscope();
    sample.insert(sample.end(), gyro.begin(), gyro.end());
    
    buffer.samples.push_back(sample);
    buffer.timestamps.push_back(now_us());
    buffer.last_update = now_us();
    buffer.callback_count++;
    
    // Keep only last 100ms of data
    int64_t cutoff = now_us() - 100000;
    while (!buffer.timestamps.empty() && buffer.timestamps.front() < cutoff) {
        buffer.samples.pop_front();
        buffer.timestamps.pop_front();
    }
    
    // Update current features
    std::lock_guard<std::mutex> data_lock(data_mutex);
    
    // Position
    auto pos = msg->position();
    for (int i = 0; i < 3; i++) {
        current_features[18 + i] = pos[i];
        feature_valid[18 + i] = true;
    }
    
    // Velocity
    auto vel = msg->velocity();
    for (int i = 0; i < 3; i++) {
        current_features[21 + i] = vel[i];
        feature_valid[21 + i] = true;
    }
    
    // RPY
    auto rpy = msg->imu_state().rpy();
    for (int i = 0; i < 3; i++) {
        current_features[24 + i] = rpy[i];
        feature_valid[24 + i] = true;
    }
    
    // Other features
    current_features[27] = msg->yaw_speed();
    feature_valid[27] = true;
    
    // Foot forces
    auto foot = msg->foot_force();
    for (int i = 0; i < 4; i++) {
        current_features[28 + i] = static_cast<float>(foot[i]) / 4095.0f;
        feature_valid[28 + i] = true;
    }
    
    current_features[32] = msg->body_height();
    current_features[33] = static_cast<float>(msg->mode());
    current_features[34] = static_cast<float>(msg->gait_type());
    current_features[35] = static_cast<float>(msg->error_code());
    feature_valid[32] = feature_valid[33] = feature_valid[34] = feature_valid[35] = true;
}

void update_low_features(const unitree_go::msg::dds_::LowState_* msg) {
    auto& buffer = sensor_buffers["low"];
    std::lock_guard<std::mutex> lock(buffer.mutex);
    
    buffer.last_update = now_us();
    buffer.callback_count++;
    
    // Store power samples for statistics
    std::vector<float> sample;
    sample.push_back(msg->power_v());
    sample.push_back(msg->bms_state().soc());
    
    buffer.samples.push_back(sample);
    buffer.timestamps.push_back(now_us());
    
    // Keep only last 100ms
    int64_t cutoff = now_us() - 100000;
    while (!buffer.timestamps.empty() && buffer.timestamps.front() < cutoff) {
        buffer.samples.pop_front();
        buffer.timestamps.pop_front();
    }
    
    std::lock_guard<std::mutex> data_lock(data_mutex);
    
    // Fix current scaling if needed
    float current = msg->bms_state().soc();
    if (std::abs(current) == 100) { //formerly > 100 for bms_state().current()
        current = current / 1000.0f;  // mA to A
    }
    
    current_features[36] = msg->power_v();
    current_features[38] = current;
    feature_valid[36] = feature_valid[38] = true;
    
    // Motor data (all 12 motors)
    for (int i = 0; i < 12; i++) {
        current_features[40 + i] = msg->motor_state()[i].tau_est();
        current_features[52 + i] = msg->motor_state()[i].dq();
        current_features[64 + i] = static_cast<float>(msg->motor_state()[i].temperature());
        feature_valid[40 + i] = feature_valid[52 + i] = feature_valid[64 + i] = true;
    }
    
    static int dbg = 0;
    if (dbg++ < 5) {
        std::cerr << "raw q[0..11]=";
        for (int i = 0; i < 12; ++i)
            std::cerr << std::setprecision(9) << msg->motor_state()[i].q() << " ";
    std::cerr << "\n";
    std::cerr << "raw dq[0..11]=";
    for (int i = 0; i < 12; ++i)
        std::cerr << std::setprecision(9) << msg->motor_state()[i].dq() << " ";
    std::cerr << "\n";
    }
    current_features[76] = static_cast<float>(msg->bit_flag());
    feature_valid[76] = true;
}

void update_uwb_features(const unitree_go::msg::dds_::UwbState_* msg) {
    auto& buffer = sensor_buffers["uwb"];
    std::lock_guard<std::mutex> lock(buffer.mutex);
    
    buffer.last_update = now_us();
    buffer.callback_count++;
    
    std::lock_guard<std::mutex> data_lock(data_mutex);
    
    current_features[77] = msg->distance_est();
    current_features[78] = msg->base_roll();
    current_features[79] = msg->base_pitch();
    current_features[80] = msg->base_yaw();
    
    feature_valid[77] = feature_valid[78] = feature_valid[79] = feature_valid[80] = true;
}

void update_range_features(const geometry_msgs::msg::dds_::PointStamped_* msg) {
    auto& buffer = sensor_buffers["range"];
    std::lock_guard<std::mutex> lock(buffer.mutex);
    
    buffer.last_update = now_us();
    buffer.callback_count++;
    
    std::lock_guard<std::mutex> data_lock(data_mutex);
    
    current_features[81] = msg->point().x();  // Front
    current_features[82] = msg->point().y();  // Left  
    current_features[83] = msg->point().z();  // Right
    
    feature_valid[81] = feature_valid[82] = feature_valid[83] = true;
}

void update_height_features(const unitree_go::msg::dds_::HeightMap_* msg) {
    auto& buffer = sensor_buffers["height"];
    std::lock_guard<std::mutex> lock(buffer.mutex);
    
    buffer.last_update = now_us();
    buffer.callback_count++;
    
    // Compute features
    compute_height_features(msg);
    
    // Update current features
    std::lock_guard<std::mutex> data_lock(data_mutex);
    {
        std::lock_guard<std::mutex> cache_lock(height_cache.mutex);
        current_features[84] = height_cache.mean;
        current_features[85] = height_cache.std_dev;
        current_features[86] = height_cache.gradient;
    }
    
    feature_valid[84] = feature_valid[85] = feature_valid[86] = true;
}

// ============ FUSION THREAD ============
void fusion_thread_main(const Config& config) {
    using namespace std::chrono;
    
    auto period = duration<double>(1.0 / config.fusion_rate_hz);
    auto next_wake = steady_clock::now();
    
    // Write CSV header
    csv_output << "timestamp_ms";    
    // Use all features if log_all is set
    std::vector<size_t> indices_to_log = config.log_all ? 
        std::vector<size_t>() : enabled_indices;
    
    if (config.log_all) {
        for (size_t i = 0; i < MAX_FEATURES; i++) {
            indices_to_log.push_back(i);
            csv_output << ",feature_" << i;
        }
    } else {
        for (size_t idx : enabled_indices) {
            csv_output << ",";
            bool found = false;
            for (const auto& cfg : feature_configs) {
                if (cfg.index == idx) {
                    csv_output << cfg.name;
                    found = true;
                    break;
                }
            }
            if (!found) {
                csv_output << "feature_" << idx;
            }
        }
    }
    csv_output << "\n";
    
    auto start_time = steady_clock::now();
    
    while (!should_exit) {
        auto now = steady_clock::now();
        auto elapsed_ms = duration_cast<milliseconds>(now - start_time).count();
        
        // Compute windowed statistics for IMU
        {
            auto& buffer = sensor_buffers["sport"];
            std::lock_guard<std::mutex> lock(buffer.mutex);
            
            if (!buffer.samples.empty()) {
                for (int axis = 0; axis < 3; axis++) {
                    std::vector<float> accel_vals, gyro_vals;
                    
                    for (const auto& sample : buffer.samples) {
                        if (sample.size() >= 6) {
                            accel_vals.push_back(sample[axis]);
                            gyro_vals.push_back(sample[3 + axis]);
                        }
                    }
                    
                    if (!accel_vals.empty()) {
                        // Accelerometer statistics
                        float mean = std::accumulate(accel_vals.begin(), accel_vals.end(), 0.0f) / accel_vals.size();
                        current_features[axis * 3] = mean;
                        feature_valid[axis * 3] = true;
                        
                        if (accel_vals.size() > 1) {
                            float sq_sum = 0;
                            for (float v : accel_vals) sq_sum += (v - mean) * (v - mean);
                            current_features[axis * 3 + 1] = std::sqrt(sq_sum / (accel_vals.size() - 1));
                        } else {
                            current_features[axis * 3 + 1] = 0;
                        }
                        feature_valid[axis * 3 + 1] = true;
                        
                        current_features[axis * 3 + 2] = *std::max_element(accel_vals.begin(), accel_vals.end());
                        feature_valid[axis * 3 + 2] = true;
                    }
                    
                    if (!gyro_vals.empty()) {
                        // Gyroscope statistics
                        float mean = std::accumulate(gyro_vals.begin(), gyro_vals.end(), 0.0f) / gyro_vals.size();
                        current_features[9 + axis * 3] = mean;
                        feature_valid[9 + axis * 3] = true;
                        
                        if (gyro_vals.size() > 1) {
                            float sq_sum = 0;
                            for (float v : gyro_vals) sq_sum += (v - mean) * (v - mean);
                            current_features[9 + axis * 3 + 1] = std::sqrt(sq_sum / (gyro_vals.size() - 1));
                        } else {
                            current_features[9 + axis * 3 + 1] = 0;
                        }
                        feature_valid[9 + axis * 3 + 1] = true;
                        
                        current_features[9 + axis * 3 + 2] = *std::max_element(gyro_vals.begin(), gyro_vals.end());
                        feature_valid[9 + axis * 3 + 2] = true;
                    }
                }
            }
        }
        
        // Compute power statistics
        {
            auto& buffer = sensor_buffers["low"];
            std::lock_guard<std::mutex> lock(buffer.mutex);
            
            if (!buffer.samples.empty()) {
                std::vector<float> voltage_vals, current_vals;
                
                for (const auto& sample : buffer.samples) {
                    if (sample.size() >= 2) {
                        voltage_vals.push_back(sample[0]);
                        float current = sample[1];
                        if (std::abs(current) > 100) {
                            current = current / 1000.0f;
                        }
                        current_vals.push_back(current);
                    }
                }
                
                if (!voltage_vals.empty() && voltage_vals.size() > 1) {
                    float v_mean = std::accumulate(voltage_vals.begin(), voltage_vals.end(), 0.0f) / voltage_vals.size();
                    float v_sq_sum = 0;
                    for (float v : voltage_vals) v_sq_sum += (v - v_mean) * (v - v_mean);
                    current_features[37] = std::sqrt(v_sq_sum / (voltage_vals.size() - 1));
                    feature_valid[37] = true;
                }
                
                if (!current_vals.empty() && current_vals.size() > 1) {
                    float a_mean = std::accumulate(current_vals.begin(), current_vals.end(), 0.0f) / current_vals.size();
                    float a_sq_sum = 0;
                    for (float a : current_vals) a_sq_sum += (a - a_mean) * (a - a_mean);
                    current_features[39] = std::sqrt(a_sq_sum / (current_vals.size() - 1));
                    feature_valid[39] = true;
                }
            }
        }
        
        // Write row to CSV
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            
            csv_output << elapsed_ms;
	    static int w = 0;
	    if (w++ < 5) {
		std::cerr << "CSV_WRITE t=" << elapsed_ms << " q[0..2]=" 
		<< current_features[40] << " " << current_features[41] << " " << current_features[42] 
		<< " valid=" << static_cast<bool>(feature_valid[40]) 
		<< static_cast<bool>(feature_valid[41]) << static_cast<bool>(feature_valid[42]) 
		<< "\n";
	    }
            for (size_t idx : indices_to_log) {
                csv_output << ",";
                if (idx < MAX_FEATURES && feature_valid[idx]) {
                    csv_output << std::fixed << std::setprecision(6) << current_features[idx];
                }
                // Empty cell for invalid/missing data
            }
            csv_output << "\n";
        }
        
        frames_written++;
        if (frames_written % 50 == 0) {
            csv_output.flush();
        }
        
        // Check duration limit
        if (config.duration_seconds > 0 && elapsed_ms > config.duration_seconds * 1000) {
            should_exit = true;
            break;
        }
        
        // Print status every 5 seconds - FIX: Use .load()
        if (config.verbose && frames_written.load() % (config.fusion_rate_hz * 5) == 0) {
            std::cout << "Status: " << frames_written.load() << " frames, ";
            for (const auto& [name, buffer] : sensor_buffers) {
                std::cout << name << ":" << buffer.callback_count << " " << "\n";
                }
            std::cout << "\n";
            for(int i = 0; i < 12; i++) {
		std::cout << i << ", " << current_features[40+i] << std::endl;
	    }
//std::cout << current_features[40] << ", " << current_features[43] << ", " << current_features[46] << ", " << current_features[49] << std::endl;
        }
        
        next_wake += duration_cast<steady_clock::duration>(period);
        std::this_thread::sleep_until(next_wake);
    }
    
    // FIX: Use .load()
    std::cout << "\nWrote " << frames_written.load() << " frames to CSV\n";
}

// ============ CALLBACKS ============
void SportModeCallback(const void* msg_ptr) {
    const auto* msg = static_cast<const unitree_go::msg::dds_::SportModeState_*>(msg_ptr);
    update_sport_features(msg);
    total_callbacks++;
}

void LowStateCallback(const void* msg_ptr) {
    const auto* msg = static_cast<const unitree_go::msg::dds_::LowState_*>(msg_ptr);
    update_low_features(msg);
    total_callbacks++;
}

void UwbCallback(const void* msg_ptr) {
    const auto* msg = static_cast<const unitree_go::msg::dds_::UwbState_*>(msg_ptr);
    update_uwb_features(msg);
    total_callbacks++;
}

void RangeCallback(const void* msg_ptr) {
    const auto* msg = static_cast<const geometry_msgs::msg::dds_::PointStamped_*>(msg_ptr);
    update_range_features(msg);
    total_callbacks++;
}

void HeightMapCallback(const void* msg_ptr) {
    const auto* msg = static_cast<const unitree_go::msg::dds_::HeightMap_*>(msg_ptr);
    update_height_features(msg);
    total_callbacks++;
}

// ============ MAIN ============
int main(int argc, char** argv) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    Config config;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--interface" && i + 1 < argc) {
            config.network_interface = argv[++i];
        } else if (arg == "--config" && i + 1 < argc) {
            config.config_file = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            config.output_file = argv[++i];
        } else if (arg == "--rate" && i + 1 < argc) {
            config.fusion_rate_hz = std::stoul(argv[++i]);
        } else if (arg == "--duration" && i + 1 < argc) {
            config.duration_seconds = std::stoul(argv[++i]);
        } else if (arg == "--all") {
            config.log_all = true;
        } else if (arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "  --interface <name>  Network interface (default: eth0)\n";
            std::cout << "  --config <file>     Feature config CSV (default: feature_config_full.csv)\n";
            std::cout << "  --output <file>     Output CSV file (default: robot_data.csv)\n";
            std::cout << "  --rate <hz>         Fusion rate (default: 50)\n";
            std::cout << "  --duration <sec>    Recording duration (0=unlimited)\n";
            std::cout << "  --all               Log all 256 features (ignore config)\n";
            std::cout << "  --verbose           Enable verbose output\n";
            return 0;
        }
    }
    
    // Load feature configuration (unless logging all)
    if (!config.log_all && !load_feature_config(config.config_file)) {
        std::cerr << "Failed to load feature configuration\n";
        std::cerr << "Use --all to log all features without config\n";
        return 1;
    }
    
    // Open output file
    csv_output.open(config.output_file);
    if (!csv_output.is_open()) {
        std::cerr << "Cannot open output file: " << config.output_file << "\n";
        return 1;
    }
    
    // Initialize buffers
    sensor_buffers["sport"];
    sensor_buffers["low"];
    sensor_buffers["uwb"];
    sensor_buffers["range"];
    sensor_buffers["height"];
    
    // Initialize DDS
    std::cout << "Initializing DDS on " << config.network_interface << "...\n";
    unitree::robot::ChannelFactory::Instance()->Init(0, config.network_interface);
    
    // Subscribe to all topics
    auto sport_sub = std::make_shared<unitree::robot::ChannelSubscriber<unitree_go::msg::dds_::SportModeState_>>("rt/sportmodestate");
    sport_sub->InitChannel(SportModeCallback);
    
    auto low_sub = std::make_shared<unitree::robot::ChannelSubscriber<unitree_go::msg::dds_::LowState_>>("rt/lowstate");
    low_sub->InitChannel(LowStateCallback);
    
    auto uwb_sub = std::make_shared<unitree::robot::ChannelSubscriber<unitree_go::msg::dds_::UwbState_>>("rt/uwbstate");
    uwb_sub->InitChannel(UwbCallback);
    
    auto range_sub = std::make_shared<unitree::robot::ChannelSubscriber<geometry_msgs::msg::dds_::PointStamped_>>("rt/utlidar/range_info");
    range_sub->InitChannel(RangeCallback);
    
    auto height_sub = std::make_shared<unitree::robot::ChannelSubscriber<unitree_go::msg::dds_::HeightMap_>>("rt/utlidar/height_map_array");
    height_sub->InitChannel(HeightMapCallback);
    
    std::cout << "All subscriptions active\n";
    
    if (config.log_all) {
        std::cout << "Logging ALL 256 features at " << config.fusion_rate_hz << " Hz\n";
    } else {
        std::cout << "Logging " << enabled_indices.size() << " features at " << config.fusion_rate_hz << " Hz\n";
    }
    
    std::cout << "Output: " << config.output_file << "\n";
    
    if (config.duration_seconds > 0) {
        std::cout << "Duration: " << config.duration_seconds << " seconds\n";
    } else {
        std::cout << "Press Ctrl+C to stop\n";
    }
    
    // Start fusion thread
    std::thread fusion_thread(fusion_thread_main, config);
    
    // Main loop with status updates
    auto start = std::chrono::steady_clock::now();
    while (!should_exit) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        if (!should_exit) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            auto sec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            
            // FIX: Use .load() for atomic variables
            std::cout << "Running for " << sec << "s - ";
            std::cout << frames_written.load() << " frames written, ";
            std::cout << total_callbacks.load() << " callbacks total\n";
        }
    }
    
    // Cleanup
    fusion_thread.join();
    csv_output.close();
    
    // Final statistics - FIX: Use .load()
    std::cout << "\n=== Final Statistics ===\n";
    std::cout << "Total frames: " << frames_written.load() << "\n";
    std::cout << "Total callbacks: " << total_callbacks.load() << "\n";
    
    for (const auto& [name, buffer] : sensor_buffers) {
        std::cout << name << ": " << buffer.callback_count << " callbacks\n";
    }
    
    std::cout << "\nData saved to: " << config.output_file << "\n";
    
    return 0;
}
