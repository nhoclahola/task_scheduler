#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>
#include <time.h>
#include "ai.h"

/**
 * Logging levels
 */
typedef enum {
    LOG_ERROR,
    LOG_WARNING,
    LOG_INFO,
    LOG_DEBUG
} LogLevel;

/**
 * Structure to store system metrics
 */
typedef struct SystemMetrics {
    double cpu_load;              // CPU load (0-100%)
    unsigned long mem_total_kb;   // Total memory in KB
    unsigned long mem_free_kb;    // Free memory in KB
    unsigned long mem_available_kb; // Available memory in KB
    unsigned long swap_total_kb;  // Total swap in KB
    unsigned long swap_free_kb;   // Free swap in KB
    double disk_usage_percent;    // Disk usage percentage (0-100%)
    unsigned long disk_total_kb;  // Total disk space in KB
    unsigned long disk_free_kb;   // Free disk space in KB
    char disk_path[256];          // Path to monitor for disk usage
    double load_avg_1min;         // 1-minute load average
    double load_avg_5min;         // 5-minute load average
    double load_avg_15min;        // 15-minute load average
    int running_processes;        // Number of running processes
    int total_processes;          // Total number of processes
} SystemMetrics;

/**
 * Đọc cấu hình từ file
 * 
 * @param config_path Đường dẫn đến file cấu hình, NULL để sử dụng đường dẫn mặc định
 * @return true nếu thành công, false nếu thất bại
 */
bool load_config(const char *config_path);

/**
 * Lưu cấu hình mặc định vào file
 * 
 * @param config_path Đường dẫn đến file cấu hình, NULL để sử dụng đường dẫn mặc định
 * @return true nếu thành công, false nếu thất bại
 */
bool save_default_config(const char *config_path);

/**
 * Cập nhật API key trong cấu hình
 * 
 * @param api_key API key mới
 * @param config_path Đường dẫn đến file cấu hình, NULL để sử dụng đường dẫn mặc định
 * @return true nếu thành công, false nếu thất bại
 */
bool update_api_key(const char *api_key, const char *config_path);

/**
 * Lấy API key từ cấu hình hoặc biến môi trường
 * 
 * @return API key hoặc NULL nếu không tìm thấy
 */
const char* get_api_key();

/**
 * Initialize logging
 * 
 * @param log_file Path to log file, NULL for stderr
 * @param level Maximum logging level
 * @return true on success, false on failure
 */
bool log_init(const char *log_file, LogLevel level);

/**
 * Log a message
 * 
 * @param level Logging level
 * @param format Format string
 * @param ... Additional arguments
 */
void log_message(LogLevel level, const char *format, ...);

/**
 * Clean up logging resources
 */
void log_cleanup(void);

/**
 * Convert a time_t value to a string
 * 
 * @param time Time value to convert
 * @param buffer Buffer to store the result
 * @param size Size of the buffer
 * @param format Format string for strftime
 * @return Pointer to the buffer on success, NULL on failure
 */
char* time_to_string(time_t time, char *buffer, size_t size, const char *format);

/**
 * Create a directory if it doesn't exist
 * 
 * @param path Directory path
 * @return true on success, false on failure
 */
bool ensure_directory_exists(const char *path);

/**
 * Generate a unique ID
 * 
 * @return A unique integer ID
 */
int generate_unique_id(void);

/**
 * Check if a file exists
 * 
 * @param path File path
 * @return true if the file exists, false otherwise
 */
bool file_exists(const char *path);

/**
 * Safely copy a string
 * 
 * @param dest Destination buffer
 * @param src Source string
 * @param size Size of the destination buffer
 * @return Pointer to the destination buffer
 */
char* safe_strcpy(char *dest, const char *src, size_t size);

/**
 * Run a command with timeout
 * 
 * @param command Command to run
 * @param working_dir Working directory, NULL for current directory
 * @param timeout_sec Timeout in seconds, 0 for no timeout
 * @param exit_code Pointer to store the exit code
 * @return true on success, false on failure
 */
bool run_command_with_timeout(const char *command, const char *working_dir, 
                              int timeout_sec, int *exit_code);

/**
 * Initialize the SystemMetrics structure with default values
 * 
 * @param metrics Pointer to the metrics structure
 */
void init_system_metrics(SystemMetrics *metrics);

/**
 * Collect system metrics specified in the metrics string
 * 
 * @param metrics_spec Comma-separated string of metrics to collect (e.g. "disk:/,cpu_load,mem_free")
 * @param metrics Pointer to the metrics structure to populate
 * @return true on success, false on failure
 */
bool collect_system_metrics(const char *metrics_spec, SystemMetrics *metrics);

/**
 * Convert system metrics to a JSON string
 * 
 * @param metrics Pointer to the metrics structure
 * @return Dynamically allocated JSON string (caller must free)
 */
char* system_metrics_to_json(const SystemMetrics *metrics);

#endif /* UTILS_H */ 