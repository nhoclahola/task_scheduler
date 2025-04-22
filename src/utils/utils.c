#include "../../include/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <ctype.h>
#include "../../include/ai.h"

// Khai báo đường dẫn file cấu hình mặc định
#define DEFAULT_CONFIG_PATH "data/config.json"

// Static variables for logging
static FILE *log_file_handle = NULL;
static LogLevel current_log_level = LOG_INFO;
static const char *log_level_strings[] = {
    "ERROR",
    "WARNING",
    "INFO",
    "DEBUG"
};

bool log_init(const char *log_file, LogLevel level) {
    if (log_file_handle) {
        log_cleanup();
    }
    
    current_log_level = level;
    
    if (log_file) {
        log_file_handle = fopen(log_file, "a");
        if (!log_file_handle) {
            fprintf(stderr, "Failed to open log file: %s\n", log_file);
            return false;
        }
    } else {
        log_file_handle = stderr;
    }
    
    return true;
}

void log_message(LogLevel level, const char *format, ...) {
    if (level > current_log_level || !log_file_handle) {
        return;
    }
    
    time_t now = time(NULL);
    struct tm *time_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", time_info);
    
    fprintf(log_file_handle, "[%s] [%s] ", time_str, log_level_strings[level]);
    
    va_list args;
    va_start(args, format);
    vfprintf(log_file_handle, format, args);
    va_end(args);
    
    fprintf(log_file_handle, "\n");
    fflush(log_file_handle);
}

void log_cleanup(void) {
    if (log_file_handle && log_file_handle != stderr) {
        fclose(log_file_handle);
        log_file_handle = NULL;
    }
}

char* time_to_string(time_t time_value, char *buffer, size_t size, const char *format) {
    if (!buffer || size == 0) {
        return NULL;
    }
    
    if (time_value == 0) {
        safe_strcpy(buffer, "Never", size);
        return buffer;
    }
    
    struct tm *time_info = localtime(&time_value);
    if (!time_info) {
        safe_strcpy(buffer, "Invalid time", size);
        return buffer;
    }
    
    const char *fmt = format ? format : "%Y-%m-%d %H:%M:%S";
    if (strftime(buffer, size, fmt, time_info) == 0) {
        safe_strcpy(buffer, "Error formatting time", size);
    }
    
    return buffer;
}

bool ensure_directory_exists(const char *path) {
    if (!path) {
        return false;
    }
    
    // Check if directory already exists
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return true;
        } else {
            log_message(LOG_ERROR, "Path exists but is not a directory: %s", path);
            return false;
        }
    }
    
    // Create the directory with permissions 0755
    if (mkdir(path, 0755) != 0) {
        log_message(LOG_ERROR, "Failed to create directory %s: %s", path, strerror(errno));
        return false;
    }
    
    return true;
}

int generate_unique_id(void) {
    static int last_id = 0;
    return ++last_id;
}

bool file_exists(const char *path) {
    if (!path) {
        return false;
    }
    
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

char* safe_strcpy(char *dest, const char *src, size_t size) {
    if (!dest || !src || size == 0) {
        return dest;
    }
    
    size_t i;
    for (i = 0; i < size - 1 && src[i]; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
    
    return dest;
}

// Helper for run_command_with_timeout
static void alarm_handler(int signum __attribute__((unused))) {
    // Just need to interrupt the wait
}

bool run_command_with_timeout(const char *command, const char *working_dir, 
                            int timeout_sec, int *exit_code) {
    if (!command || !exit_code) {
        return false;
    }
    
    pid_t pid = fork();
    
    if (pid < 0) {
        log_message(LOG_ERROR, "Fork failed: %s", strerror(errno));
        return false;
    }
    
    if (pid == 0) {
        // Child process
        
        // Change to the working directory if specified
        if (working_dir && chdir(working_dir) != 0) {
            _exit(127);
        }
        
        // Redirect output to /dev/null
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        
        // Execute the command
        execl("/bin/sh", "sh", "-c", command, NULL);
        
        // If execl returns, it failed
        _exit(127);
    }
    
    // Parent process
    int status;
    pid_t waited_pid;
    bool timed_out = false;
    
    if (timeout_sec > 0) {
        // Set up alarm for timeout
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = alarm_handler;
        sigaction(SIGALRM, &sa, NULL);
        
        alarm(timeout_sec);
        
        waited_pid = waitpid(pid, &status, 0);
        
        // Cancel the alarm
        alarm(0);
        
        if (waited_pid == -1 && errno == EINTR) {
            // Interrupted by alarm, kill the child
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            timed_out = true;
        }
    } else {
        // No timeout, just wait
        waited_pid = waitpid(pid, &status, 0);
    }
    
    if (waited_pid == -1) {
        log_message(LOG_ERROR, "waitpid failed: %s", strerror(errno));
        return false;
    }
    
    if (timed_out) {
        log_message(LOG_WARNING, "Command timed out after %d seconds: %s", timeout_sec, command);
        *exit_code = -1;
        return false;
    }
    
    if (WIFEXITED(status)) {
        *exit_code = WEXITSTATUS(status);
        return true;
    } else if (WIFSIGNALED(status)) {
        log_message(LOG_WARNING, "Command terminated by signal %d: %s", WTERMSIG(status), command);
        *exit_code = -1;
        return false;
    }
    
    log_message(LOG_ERROR, "Unexpected wait status: %d", status);
    *exit_code = -1;
    return false;
}

void init_system_metrics(SystemMetrics *metrics) {
    if (!metrics) {
        return;
    }
    
    memset(metrics, 0, sizeof(SystemMetrics));
    safe_strcpy(metrics->disk_path, "/", sizeof(metrics->disk_path));
}

// Parse a metrics specification string and mark which metrics to collect
static bool parse_metrics_spec(const char *metrics_spec, bool *collect_cpu, bool *collect_memory, 
                              bool *collect_disk, bool *collect_load, bool *collect_processes, 
                              char *disk_path, size_t path_size) {
    if (!metrics_spec || !collect_cpu || !collect_memory || !collect_disk || 
        !collect_load || !collect_processes || !disk_path) {
        return false;
    }
    
    // Default values
    *collect_cpu = false;
    *collect_memory = false;
    *collect_disk = false;
    *collect_load = false;
    *collect_processes = false;
    safe_strcpy(disk_path, "/", path_size);
    
    // Make a copy of the metrics spec to tokenize
    char metrics_copy[512];
    safe_strcpy(metrics_copy, metrics_spec, sizeof(metrics_copy));
    
    // Parse comma-separated metrics
    char *token = strtok(metrics_copy, ",");
    while (token) {
        // Remove leading/trailing whitespace
        while (*token && (*token == ' ' || *token == '\t')) token++;
        char *end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\t')) {
            *end = '\0';
            end--;
        }
        
        if (strncmp(token, "cpu_load", 8) == 0) {
            *collect_cpu = true;
        } else if (strncmp(token, "mem", 3) == 0) {
            *collect_memory = true;
        } else if (strncmp(token, "disk:", 5) == 0) {
            *collect_disk = true;
            safe_strcpy(disk_path, token + 5, path_size);
        } else if (strncmp(token, "load_avg", 8) == 0) {
            *collect_load = true;
        } else if (strncmp(token, "processes", 9) == 0) {
            *collect_processes = true;
        } else {
            log_message(LOG_WARNING, "Unknown metric: %s", token);
        }
        
        token = strtok(NULL, ",");
    }
    
    return true;
}

// Get CPU usage percentage
static bool get_cpu_usage(double *cpu_percent) {
    if (!cpu_percent) {
        return false;
    }
    
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) {
        log_message(LOG_ERROR, "Failed to open /proc/stat: %s", strerror(errno));
        return false;
    }
    
    unsigned long user, nice, system, idle, iowait, irq, softirq, steal;
    if (fscanf(fp, "cpu %lu %lu %lu %lu %lu %lu %lu %lu", 
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) != 8) {
        log_message(LOG_ERROR, "Failed to parse /proc/stat");
        fclose(fp);
        return false;
    }
    fclose(fp);
    
    unsigned long total_idle = idle + iowait;
    unsigned long total_non_idle = user + nice + system + irq + softirq + steal;
    unsigned long total = total_idle + total_non_idle;
    
    // For accurate measurements, we should compare with previous values,
    // but for simplicity, we'll just return a rough estimate
    *cpu_percent = (double)total_non_idle * 100.0 / (double)total;
    
    return true;
}

// Get memory information
static bool get_memory_info(unsigned long *total_kb, unsigned long *free_kb, 
                            unsigned long *available_kb, unsigned long *swap_total_kb, 
                            unsigned long *swap_free_kb) {
    struct sysinfo info;
    
    if (sysinfo(&info) != 0) {
        log_message(LOG_ERROR, "Failed to get system info: %s", strerror(errno));
        return false;
    }
    
    // Convert to kilobytes
    *total_kb = info.totalram * info.mem_unit / 1024;
    *free_kb = info.freeram * info.mem_unit / 1024;
    *available_kb = *free_kb; // This is a simplification, available != free
    *swap_total_kb = info.totalswap * info.mem_unit / 1024;
    *swap_free_kb = info.freeswap * info.mem_unit / 1024;
    
    // Try to get more accurate "available" memory from /proc/meminfo
    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "MemAvailable:", 13) == 0) {
                unsigned long available;
                if (sscanf(line + 13, "%lu", &available) == 1) {
                    *available_kb = available;
                    break;
                }
            }
        }
        fclose(fp);
    }
    
    return true;
}

// Get disk usage information
static bool get_disk_usage(const char *path, unsigned long *total_kb, 
                          unsigned long *free_kb, double *percent) {
    struct statvfs stat;
    
    if (statvfs(path, &stat) != 0) {
        log_message(LOG_ERROR, "Failed to get disk info for %s: %s", path, strerror(errno));
        return false;
    }
    
    // Calculate disk space in kilobytes
    unsigned long block_size = stat.f_frsize ? stat.f_frsize : stat.f_bsize;
    *total_kb = (stat.f_blocks * block_size) / 1024;
    *free_kb = (stat.f_bfree * block_size) / 1024;
    unsigned long used_kb = *total_kb - *free_kb;
    
    // Calculate percentage
    *percent = (double)used_kb * 100.0 / (double)*total_kb;
    
    return true;
}

// Get load averages
static bool get_load_averages(double *load_1min, double *load_5min, double *load_15min) {
    struct sysinfo info;
    
    if (sysinfo(&info) != 0) {
        log_message(LOG_ERROR, "Failed to get system info: %s", strerror(errno));
        return false;
    }
    
    *load_1min = (double)info.loads[0] / (1 << SI_LOAD_SHIFT);
    *load_5min = (double)info.loads[1] / (1 << SI_LOAD_SHIFT);
    *load_15min = (double)info.loads[2] / (1 << SI_LOAD_SHIFT);
    
    return true;
}

// Get process information
static bool get_process_info(int *running, int *total) {
    struct sysinfo info;
    
    if (sysinfo(&info) != 0) {
        log_message(LOG_ERROR, "Failed to get system info: %s", strerror(errno));
        return false;
    }
    
    *total = info.procs;
    
    // To get running processes, we need to parse /proc/stat
    *running = 0;
    FILE *fp = fopen("/proc/stat", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "procs_running ", 14) == 0) {
                sscanf(line + 14, "%d", running);
                break;
            }
        }
        fclose(fp);
    }
    
    return true;
}

bool collect_system_metrics(const char *metrics_spec, SystemMetrics *metrics) {
    if (!metrics_spec || !metrics) {
        return false;
    }
    
    // Initialize metrics
    init_system_metrics(metrics);
    
    // Parse the metrics specification
    bool collect_cpu, collect_memory, collect_disk, collect_load, collect_processes;
    char disk_path[256];
    
    if (!parse_metrics_spec(metrics_spec, &collect_cpu, &collect_memory, &collect_disk, 
                           &collect_load, &collect_processes, disk_path, sizeof(disk_path))) {
        log_message(LOG_ERROR, "Failed to parse metrics specification: %s", metrics_spec);
        return false;
    }
    
    // Collect the requested metrics
    if (collect_cpu) {
        if (!get_cpu_usage(&metrics->cpu_load)) {
            log_message(LOG_WARNING, "Failed to get CPU usage");
        }
    }
    
    if (collect_memory) {
        if (!get_memory_info(&metrics->mem_total_kb, &metrics->mem_free_kb, 
                            &metrics->mem_available_kb, &metrics->swap_total_kb, 
                            &metrics->swap_free_kb)) {
            log_message(LOG_WARNING, "Failed to get memory information");
        }
    }
    
    if (collect_disk) {
        safe_strcpy(metrics->disk_path, disk_path, sizeof(metrics->disk_path));
        if (!get_disk_usage(disk_path, &metrics->disk_total_kb, &metrics->disk_free_kb, 
                           &metrics->disk_usage_percent)) {
            log_message(LOG_WARNING, "Failed to get disk usage for %s", disk_path);
        }
    }
    
    if (collect_load) {
        if (!get_load_averages(&metrics->load_avg_1min, &metrics->load_avg_5min, 
                              &metrics->load_avg_15min)) {
            log_message(LOG_WARNING, "Failed to get load averages");
        }
    }
    
    if (collect_processes) {
        if (!get_process_info(&metrics->running_processes, &metrics->total_processes)) {
            log_message(LOG_WARNING, "Failed to get process information");
        }
    }
    
    return true;
}

char* system_metrics_to_json(const SystemMetrics *metrics) {
    if (!metrics) {
        return NULL;
    }
    
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        log_message(LOG_ERROR, "Failed to create JSON object");
        return NULL;
    }
    
    // Add CPU information
    cJSON_AddNumberToObject(root, "cpu_load_percent", metrics->cpu_load);
    
    // Add memory information
    cJSON *memory = cJSON_CreateObject();
    cJSON_AddNumberToObject(memory, "total_kb", (double)metrics->mem_total_kb);
    cJSON_AddNumberToObject(memory, "free_kb", (double)metrics->mem_free_kb);
    cJSON_AddNumberToObject(memory, "available_kb", (double)metrics->mem_available_kb);
    cJSON_AddNumberToObject(memory, "used_kb", (double)(metrics->mem_total_kb - metrics->mem_free_kb));
    cJSON_AddNumberToObject(memory, "usage_percent", 
                          100.0 * (double)(metrics->mem_total_kb - metrics->mem_free_kb) / (double)metrics->mem_total_kb);
    cJSON_AddItemToObject(root, "memory", memory);
    
    // Add swap information
    cJSON *swap = cJSON_CreateObject();
    cJSON_AddNumberToObject(swap, "total_kb", (double)metrics->swap_total_kb);
    cJSON_AddNumberToObject(swap, "free_kb", (double)metrics->swap_free_kb);
    cJSON_AddNumberToObject(swap, "used_kb", (double)(metrics->swap_total_kb - metrics->swap_free_kb));
    if (metrics->swap_total_kb > 0) {
        cJSON_AddNumberToObject(swap, "usage_percent", 
                              100.0 * (double)(metrics->swap_total_kb - metrics->swap_free_kb) / (double)metrics->swap_total_kb);
    } else {
        cJSON_AddNumberToObject(swap, "usage_percent", 0.0);
    }
    cJSON_AddItemToObject(root, "swap", swap);
    
    // Add disk information
    cJSON *disk = cJSON_CreateObject();
    cJSON_AddStringToObject(disk, "path", metrics->disk_path);
    cJSON_AddNumberToObject(disk, "total_kb", (double)metrics->disk_total_kb);
    cJSON_AddNumberToObject(disk, "free_kb", (double)metrics->disk_free_kb);
    cJSON_AddNumberToObject(disk, "used_kb", (double)(metrics->disk_total_kb - metrics->disk_free_kb));
    cJSON_AddNumberToObject(disk, "usage_percent", metrics->disk_usage_percent);
    cJSON_AddItemToObject(root, "disk", disk);
    
    // Add load averages
    cJSON *load = cJSON_CreateObject();
    cJSON_AddNumberToObject(load, "1min", metrics->load_avg_1min);
    cJSON_AddNumberToObject(load, "5min", metrics->load_avg_5min);
    cJSON_AddNumberToObject(load, "15min", metrics->load_avg_15min);
    cJSON_AddItemToObject(root, "load_average", load);
    
    // Add process information
    cJSON *processes = cJSON_CreateObject();
    cJSON_AddNumberToObject(processes, "running", metrics->running_processes);
    cJSON_AddNumberToObject(processes, "total", metrics->total_processes);
    cJSON_AddItemToObject(root, "processes", processes);
    
    // Convert to string
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);
    
    return json_str;
} 
