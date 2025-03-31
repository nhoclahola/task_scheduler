#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>
#include <time.h>

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

#endif /* UTILS_H */ 