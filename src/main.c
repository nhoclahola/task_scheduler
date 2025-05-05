#include "../include/task.h"
#include "../include/scheduler.h"
#include "../include/cli.h"
#include "../include/utils.h"
#include "../include/db.h"
#include "../include/ai.h"
#include "../include/email.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

// Global objects
static Scheduler scheduler;
static int pid_file_fd = -1;

// Forward declarations
static void cleanup(void);
static void signal_handler(int sig);
static bool setup_daemon(const char *pid_file);
static bool create_pid_file(const char *pid_file);

int main(int argc, char *argv[]) {
    // Parse command line arguments
    CliOptions options;
    if (!cli_parse_args(argc, argv, &options)) {
        fprintf(stderr, "Error parsing command line arguments\n");
        cli_print_help();
        return EXIT_FAILURE;
    }
    
    // Handle help and version options
    if (options.show_help) {
        cli_print_help();
        return EXIT_SUCCESS;
    }
    
    if (options.show_version) {
        cli_print_version();
        return EXIT_SUCCESS;
    }
    
    // Initialize logging
    char log_file[1024];
    snprintf(log_file, sizeof(log_file), "%s/taskscheduler.log", options.data_dir);
    if (!log_init(options.daemon_mode ? log_file : NULL, options.verbosity)) {
        fprintf(stderr, "Failed to initialize logging\n");
        return EXIT_FAILURE;
    }
    
    // Run as daemon if requested
    if (options.daemon_mode) {
        char pid_file[1024];
        snprintf(pid_file, sizeof(pid_file), "%s/taskscheduler.pid", options.data_dir);
        
        if (!setup_daemon(pid_file)) {
            log_message(LOG_ERROR, "Failed to start daemon");
            log_cleanup();
            return EXIT_FAILURE;
        }
    }
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize email module
    if (!email_init(NULL)) {
        log_message(LOG_WARNING, "Failed to initialize email module");
        // Continue anyway, email features will be disabled
    }
    
    // Initialize scheduler
    if (!scheduler_init(&scheduler, options.data_dir)) {
        log_message(LOG_ERROR, "Failed to initialize scheduler");
        cleanup();
        return EXIT_FAILURE;
    }
    
    // Run in appropriate mode
    int exit_code = EXIT_SUCCESS;
    
    if (options.interactive_mode) {
        // Interactive mode
        cli_run_interactive(options.data_dir);
    } else {
        // Daemon mode - just start the scheduler and wait for signals
        if (!scheduler_start(&scheduler)) {
            log_message(LOG_ERROR, "Failed to start scheduler");
            exit_code = EXIT_FAILURE;
        } else {
            log_message(LOG_INFO, "Task Scheduler daemon started");
            
            // Wait forever
            while (1) {
                // Sync with database every minute
                scheduler_sync(&scheduler);
                sleep(60);
            }
        }
    }
    
    // Clean up
    cleanup();
    return exit_code;
}

static void cleanup(void) {
    // Stop and clean up scheduler
    scheduler_cleanup(&scheduler);
    
    // Clean up logging
    log_cleanup();
    
    // Remove PID file if we created one
    if (pid_file_fd != -1) {
        close(pid_file_fd);
        pid_file_fd = -1;
    }
}

static void signal_handler(int sig) {
    log_message(LOG_INFO, "Received signal %d, exiting", sig);
    
    // Sync before exiting to ensure all tasks are saved
    scheduler_sync(&scheduler);
    
    cleanup();
    exit(EXIT_SUCCESS);
}

static bool setup_daemon(const char *pid_file) {
    // Create PID file first
    if (!create_pid_file(pid_file)) {
        return false;
    }
    
    // Fork the process
    pid_t pid = fork();
    
    if (pid < 0) {
        // Fork failed
        log_message(LOG_ERROR, "Failed to fork: %s", strerror(errno));
        close(pid_file_fd);
        pid_file_fd = -1;
        return false;
    }
    
    if (pid > 0) {
        // Parent process exits
        exit(EXIT_SUCCESS);
    }
    
    // Child process continues
    
    // Create a new session
    if (setsid() < 0) {
        log_message(LOG_ERROR, "Failed to create new session: %s", strerror(errno));
        return false;
    }
    
    // Fork again to ensure we can't acquire a controlling terminal
    pid = fork();
    
    if (pid < 0) {
        // Fork failed
        log_message(LOG_ERROR, "Failed to fork second time: %s", strerror(errno));
        return false;
    }
    
    if (pid > 0) {
        // First child exits
        exit(EXIT_SUCCESS);
    }
    
    // Second child continues
    
    // Change working directory
    if (chdir("/") < 0) {
        log_message(LOG_ERROR, "Failed to change directory: %s", strerror(errno));
        return false;
    }
    
    // Reset file mode creation mask
    umask(0);
    
    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    // Redirect standard file descriptors to /dev/null
    if (open("/dev/null", O_RDONLY) < 0 || 
        open("/dev/null", O_WRONLY) < 0 || 
        open("/dev/null", O_WRONLY) < 0) {
        log_message(LOG_ERROR, "Failed to redirect standard file descriptors: %s", strerror(errno));
        return false;
    }
    
    // Update PID file with our new PID
    if (ftruncate(pid_file_fd, 0) < 0) {
        log_message(LOG_ERROR, "Failed to truncate PID file: %s", strerror(errno));
        return false;
    }
    
    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d\n", getpid());
    size_t pid_str_len = strlen(pid_str);
    ssize_t bytes_written = write(pid_file_fd, pid_str, pid_str_len);
    if (bytes_written < 0 || (size_t)bytes_written != pid_str_len) {
        log_message(LOG_ERROR, "Failed to write to PID file: %s", strerror(errno));
        return false;
    }
    
    return true;
}

static bool create_pid_file(const char *pid_file) {
    // Check if the PID file already exists
    if (file_exists(pid_file)) {
        // Read the PID from the file
        FILE *file = fopen(pid_file, "r");
        if (!file) {
            log_message(LOG_ERROR, "Failed to open existing PID file: %s", strerror(errno));
            return false;
        }
        
        pid_t existing_pid;
        if (fscanf(file, "%d", &existing_pid) == 1) {
            fclose(file);
            
            // Check if the process is still running
            if (kill(existing_pid, 0) == 0 || errno != ESRCH) {
                log_message(LOG_ERROR, "Daemon already running with PID %d", existing_pid);
                return false;
            }
            
            // Process is not running, remove the stale PID file
            log_message(LOG_WARNING, "Removing stale PID file for PID %d", existing_pid);
            unlink(pid_file);
        } else {
            fclose(file);
            log_message(LOG_ERROR, "Invalid PID file format");
            return false;
        }
    }
    
    // Create the PID file
    pid_file_fd = open(pid_file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (pid_file_fd < 0) {
        log_message(LOG_ERROR, "Failed to create PID file: %s", strerror(errno));
        return false;
    }
    
    // Write our PID to the file
    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d\n", getpid());
    size_t pid_str_len = strlen(pid_str);
    ssize_t bytes_written = write(pid_file_fd, pid_str, pid_str_len);
    if (bytes_written < 0 || (size_t)bytes_written != pid_str_len) {
        log_message(LOG_ERROR, "Failed to write to PID file: %s", strerror(errno));
        close(pid_file_fd);
        pid_file_fd = -1;
        return false;
    }
    
    return true;
} 