#include "../include/utils.h"
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
static void alarm_handler(int signum) {
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