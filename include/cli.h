#ifndef CLI_H
#define CLI_H

#include "task.h"
#include "scheduler.h"
#include <stdbool.h>
#include <time.h>

/**
 * Structure to hold command line options
 */
typedef struct {
    char data_dir[256];    // Data directory
    bool interactive_mode; // Interactive mode flag
    bool daemon_mode;      // Run as daemon
    int verbosity;         // Logging verbosity level
    char config_file[512]; // Configuration file path
    bool show_help;        // Show help message
    bool show_version;     // Show version information
} CliOptions;

/**
 * Parse command line arguments
 * 
 * @param argc Argument count
 * @param argv Argument values
 * @param options Pointer to options structure to fill
 * @return true on success, false on failure
 */
bool cli_parse_args(int argc, char **argv, CliOptions *options);

/**
 * Print help message
 */
void cli_print_help(void);

/**
 * Print version information
 */
void cli_print_version(void);

/**
 * Display task information
 * 
 * @param task Pointer to the task
 * @param detailed Whether to show detailed information
 */
void cli_display_task(const Task *task, bool detailed);

/**
 * Parse a time string into a time_t value
 * 
 * @param time_str Time string in format "YYYY-MM-DD HH:MM:SS" or "HH:MM:SS"
 * @param result Pointer to store the result
 * @return true on success, false on failure
 */
bool cli_parse_time(const char *time_str, time_t *result);

/**
 * Run interactive mode
 * 
 * @param data_dir Directory to store task data
 */
void cli_run_interactive(const char *data_dir);

/**
 * Get a command from the user (interactive mode)
 * 
 * @return Allocated string with command (must be freed by caller) or NULL on error/EOF
 */
char* cli_get_command();

/**
 * Initialize the CLI
 * 
 * @param data_dir Directory to store task data
 * @return true on success, false on failure
 */
bool cli_init(const char *data_dir);

/**
 * Clean up CLI resources
 */
void cli_cleanup();

/**
 * CLI command handlers
 */
void cli_add_task(int argc, char *argv[]);
void cli_list_tasks(int argc, char *argv[]);
void cli_remove_task(int argc, char *argv[]);
void cli_run_task(int argc, char *argv[]);
void cli_enable_task(int argc, char *argv[]);
void cli_disable_task(int argc, char *argv[]);
void cli_edit_task(int argc, char *argv[]);
void cli_view_task(int argc, char *argv[]);
void cli_help(int argc, char *argv[]);

/**
 * CLI commands for dependencies and script execution
 */
void cli_add_dependency(int argc, char *argv[]);
void cli_remove_dependency(int argc, char *argv[]);
void cli_set_dep_behavior(int argc, char *argv[]);
void cli_convert_to_script(int argc, char *argv[]);
void cli_convert_to_command(int argc, char *argv[]);

/**
 * CLI commands for email configuration
 */
int cli_set_email_config(int argc, char *argv[]);
int cli_enable_email(int argc, char *argv[]);
int cli_disable_email(int argc, char *argv[]);
int cli_show_email_config(int argc, char *argv[]);
int cli_set_recipient_email(int argc, char *argv[]);

/**
 * Process a command line
 * 
 * @param argc Argument count
 * @param argv Argument values
 */
void cli_process_command(int argc, char *argv[]);

#endif /* CLI_H */ 