#include "../../include/cli.h"
#include "../../include/scheduler.h"
#include "../../include/task.h"
#include "../../include/utils.h"
#include "../../include/db.h"
#include "../../include/ai.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include <readline/readline.h>
#include <readline/history.h>

// Global variables
static const char *VERSION = "1.0.0";
#if 0
static const char *DEFAULT_DATA_DIR = ".taskscheduler";
#endif

// Forward declarations
#if 0
static void cli_print_task_list(Scheduler *scheduler);
static char* cli_get_frequency_name(TaskFrequency freq);
static void cli_print_command_help(void);
static char** cli_command_completion(const char *text, int start __attribute__((unused)), int end __attribute__((unused)));
static char* cli_command_generator(const char *text, int state);

// List of available commands
static const char *commands[] = {
    "help", "list", "add", "remove", "update", "enable", "disable",
    "execute", "start", "stop", "exit", "quit", NULL
};
#endif

// Helper function for trimming whitespace
static void trim_whitespace(char *str);

// Khai báo tiên quyết
void cli_convert_to_ai_dynamic(int argc, char *argv[]);
void cli_convert_to_command(int argc, char *argv[]);
void cli_help(int argc, char *argv[]);
void cli_set_api_key(int argc, char *argv[]);
void cli_view_api_key(int argc, char *argv[]);

// Global scheduler instance
static Scheduler scheduler;
static bool scheduler_initialized = false;

// Hàm để lấy tên tương ứng cho TaskFrequency
static const char* cli_get_frequency_name(TaskFrequency freq) {
    switch (freq) {
        case ONCE:
            return "Once";
        case DAILY:
            return "Daily";
        case WEEKLY:
            return "Weekly";
        case MONTHLY:
            return "Monthly";
        case CUSTOM:
            return "Custom";
        default:
            return "Unknown";
    }
}

bool cli_parse_args(int argc, char **argv, CliOptions *options) {
    if (!options) {
        return false;
    }
    
    // Set default values
    safe_strcpy(options->data_dir, "./data", sizeof(options->data_dir));
    options->interactive_mode = true;
    options->verbosity = LOG_INFO;
    options->daemon_mode = false;
    options->show_help = false;
    options->show_version = false;
    options->config_file[0] = '\0';
    
    // Define long options
    static struct option long_options[] = {
        {"daemon",     no_argument,       0, 'd'},
        {"interactive", no_argument,      0, 'i'},
        {"data-dir",   required_argument, 0, 'D'},
        {"config",     required_argument, 0, 'c'},
        {"verbose",    no_argument,       0, 'v'},
        {"quiet",      no_argument,       0, 'q'},
        {"help",       no_argument,       0, 'h'},
        {"version",    no_argument,       0, 'V'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "diD:c:vqhV", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'd':
                options->daemon_mode = true;
                options->interactive_mode = false;
                break;
                
            case 'i':
                options->interactive_mode = true;
                options->daemon_mode = false;
                break;
                
            case 'D':
                safe_strcpy(options->data_dir, optarg, sizeof(options->data_dir));
                break;
                
            case 'c':
                safe_strcpy(options->config_file, optarg, sizeof(options->config_file));
                break;
                
            case 'v':
                options->verbosity = LOG_DEBUG;
                break;
                
            case 'q':
                options->verbosity = LOG_ERROR;
                break;
                
            case 'h':
                options->show_help = true;
                break;
                
            case 'V':
                options->show_version = true;
                break;
                
            case '?':
                return false;
                
            default:
                break;
        }
    }
    
    return true;
}

void cli_print_help(void) {
    printf("Usage: taskscheduler [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -d, --daemon         Run as a daemon\n");
    printf("  -i, --interactive    Run in interactive mode (default)\n");
    printf("  -D, --data-dir=DIR   Set the data directory (default: ~/.taskscheduler)\n");
    printf("  -c, --config=FILE    Use the specified configuration file\n");
    printf("  -v, --verbose        Enable verbose output\n");
    printf("  -q, --quiet          Quiet mode, only show errors\n");
    printf("  -h, --help           Display this help and exit\n");
    printf("  -V, --version        Output version information and exit\n");
    printf("\n");
    printf("Interactive commands:\n");
    printf("  help                 Show available commands\n");
    printf("  list                 List all tasks\n");
    printf("  add                  Add a new task\n");
    printf("  remove <id>          Remove a task\n");
    printf("  update <id>          Update a task\n");
    printf("  enable <id>          Enable a task\n");
    printf("  disable <id>         Disable a task\n");
    printf("  execute <id>         Execute a task immediately\n");
    printf("  start                Start the scheduler\n");
    printf("  stop                 Stop the scheduler\n");
    printf("  exit, quit           Exit the program\n");
}

void cli_print_version(void) {
    printf("Task Scheduler version %s\n", VERSION);
    printf("Copyright (C) 2023\n");
    printf("License: MIT\n");
}

// Thay hàm cli_interactive_mode cũ bằng phiên bản mới
#if 0
bool cli_interactive_mode(Scheduler *scheduler) {
    if (!scheduler) {
        return false;
    }
    
    printf("Task Scheduler Interactive Mode\n");
    printf("Type 'help' for available commands\n");
    
    // Initialize readline
    rl_attempted_completion_function = cli_command_completion;
    
    // Start the scheduler
    scheduler_start(scheduler);
    
    char *line;
    bool running = true;
    
    while (running && (line = readline("taskscheduler> ")) != NULL) {
        // Skip empty lines
        if (line[0] != '\0') {
            // Add to history
            add_history(line);
            
            // Process command
            running = cli_process_command(argc, argv);
        }
        
        // Free the line buffer
        free(line);
    }
    
    // Stop the scheduler
    scheduler_stop(scheduler);
    
    return true;
}
#endif

// Phiên bản mới của cli_process_command
void cli_process_command(int argc, char *argv[]) {
    if (argc < 2) {
        cli_help(argc, argv);
        return;
    }
    
    const char *command = argv[1];
    
    if (strcmp(command, "help") == 0) {
        cli_help(argc, argv);
    } else if (strcmp(command, "list") == 0) {
        cli_list_tasks(argc, argv);
    } else if (strcmp(command, "add") == 0) {
        cli_add_task(argc, argv);
    } else if (strcmp(command, "remove") == 0) {
        cli_remove_task(argc, argv);
    } else if (strcmp(command, "enable") == 0) {
        cli_enable_task(argc, argv);
    } else if (strcmp(command, "disable") == 0) {
        cli_disable_task(argc, argv);
    } else if (strcmp(command, "run") == 0) {
        cli_run_task(argc, argv);
    } else if (strcmp(command, "view") == 0) {
        cli_view_task(argc, argv);
    } else if (strcmp(command, "edit") == 0) {
        cli_edit_task(argc, argv);
    } else if (strcmp(command, "start") == 0) {
        if (scheduler_start(&scheduler)) {
            printf("Scheduler started\n");
        } else {
            printf("Failed to start scheduler\n");
        }
    } else if (strcmp(command, "stop") == 0) {
        if (scheduler_stop(&scheduler)) {
            printf("Scheduler stopped\n");
        } else {
            printf("Failed to stop scheduler\n");
        }
    } else if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {
        exit(0);
    } else if (strcmp(command, "add-dep") == 0) {
        cli_add_dependency(argc, argv);
    } else if (strcmp(command, "remove-dep") == 0) {
        cli_remove_dependency(argc, argv);
    } else if (strcmp(command, "set-dep-behavior") == 0) {
        cli_set_dep_behavior(argc, argv);
    } else if (strcmp(command, "to-script") == 0) {
        cli_convert_to_script(argc, argv);
    } else if (strcmp(command, "to-command") == 0) {
        cli_convert_to_command(argc, argv);
    } else if (strcmp(command, "to-ai") == 0) {
        cli_convert_to_ai_dynamic(argc, argv);
    } else if (strcmp(command, "set-api-key") == 0) {
        cli_set_api_key(argc, argv);
    } else if (strcmp(command, "view-api-key") == 0) {
        cli_view_api_key(argc, argv);
    } else {
        printf("Unknown command: %s\n", command);
        cli_help(argc, argv);
    }
}

void cli_display_task(const Task *task, bool detailed) {
    if (!task) {
        return;
    }
    
    const char *frequency_name = cli_get_frequency_name(task->frequency);
    const char *mode_name;
    
    switch (task->exec_mode) {
        case EXEC_SCRIPT:
            mode_name = "Script";
            break;
        case EXEC_AI_DYNAMIC:
            mode_name = "AI Dynamic";
            break;
        case EXEC_COMMAND:
        default:
            mode_name = "Command";
            break;
    }
    
    printf("Task ID: %d\n", task->id);
    printf("Name: %s\n", task->name);
    printf("Enabled: %s\n", task->enabled ? "Yes" : "No");
    printf("Execution Mode: %s\n", mode_name);

    if (task->exec_mode == EXEC_COMMAND) {
        printf("Command: %s\n", task->command);
    } else if (task->exec_mode == EXEC_SCRIPT) {
        printf("Script Content:\n%s\n", task->script_content);
    } else if (task->exec_mode == EXEC_AI_DYNAMIC) {
        printf("AI Prompt: %s\n", task->ai_prompt);
        printf("System Metrics: %s\n", task->system_metrics);
    }
    
    printf("Frequency: %s\n", frequency_name);
    
    if (task->frequency != ONCE) {
        printf("Interval: %d %s\n", task->interval, 
              (task->frequency == DAILY) ? "days" : 
              (task->frequency == WEEKLY) ? "weeks" : 
              (task->frequency == MONTHLY) ? "months" : "minutes");
    }
    
    char time_buf[64];
    time_t creation = task->creation_time;
    if (creation > 0) {
        struct tm *tm_info = localtime(&creation);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
        printf("Created: %s\n", time_buf);
    } else {
        printf("Created: N/A\n");
    }
    
    time_t last = task->last_run_time;
    if (last > 0) {
        struct tm *tm_info = localtime(&last);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
        printf("Last Run: %s (Exit Code: %d)\n", time_buf, task->exit_code);
    } else {
        printf("Last Run: Never\n");
    }
    
    time_t next = task->next_run_time;
    if (next > 0) {
        struct tm *tm_info = localtime(&next);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
        printf("Next Run: %s\n", time_buf);
    } else {
        printf("Next Run: N/A\n");
    }
    
    if (detailed) {
        printf("Working Directory: %s\n", task->working_dir[0] ? task->working_dir : "Default");
        printf("Max Runtime: %d seconds (0 = unlimited)\n", task->max_runtime);
        
        if (task->dependency_count > 0) {
            printf("Dependencies: ");
            for (int i = 0; i < task->dependency_count; i++) {
                printf("%d ", task->dependencies[i]);
            }
            printf("\n");
            
            const char *behavior_names[] = {
                "Any Success", "All Success", 
                "Any Completion", "All Completion"
            };
            printf("Dependency Behavior: %s\n", behavior_names[task->dep_behavior]);
        }
        
        if (task->schedule_type == SCHEDULE_CRON) {
            printf("Cron Expression: %s\n", task->cron_expression);
        }
    }
}

bool cli_parse_time(const char *time_str, time_t *result) {
    if (!time_str || !result) {
        return false;
    }
    
    struct tm time_components;
    time_t now = time(NULL);
    struct tm *local_now = localtime(&now);
    
    // Initialize with current date/time
    memcpy(&time_components, local_now, sizeof(struct tm));
    
    // Check format
    if (strchr(time_str, '-') != NULL) {
        // Format: YYYY-MM-DD HH:MM:SS or YYYY-MM-DD HH:MM
        if (strptime(time_str, "%Y-%m-%d %H:%M:%S", &time_components) == NULL) {
            if (strptime(time_str, "%Y-%m-%d %H:%M", &time_components) == NULL) {
                return false;
            }
            time_components.tm_sec = 0;
        }
    } else {
        // Format: HH:MM:SS or HH:MM
        if (strptime(time_str, "%H:%M:%S", &time_components) == NULL) {
            if (strptime(time_str, "%H:%M", &time_components) == NULL) {
                return false;
            }
            time_components.tm_sec = 0;
        }
    }
    
    *result = mktime(&time_components);
    return true;
}

// Helper functions

#if 0
static void cli_print_task_list(Scheduler *scheduler) {
    if (!scheduler) {
        return;
    }
    
    int count;
    Task *tasks = scheduler_get_all_tasks(scheduler, &count);
    
    if (!tasks || count == 0) {
        printf("No tasks found\n");
        return;
    }
    
    // Print header
    printf("%-4s %-20s %-10s %-20s %-20s %s\n", 
           "ID", "Name", "Frequency", "Last Run", "Next Run", "Status");
    printf("%-4s %-20s %-10s %-20s %-20s %s\n", 
           "----", "--------------------", "----------", "--------------------", "--------------------", "------");
    
    // Print tasks
    for (int i = 0; i < count; i++) {
        cli_display_task(&tasks[i], false);
    }
    
    free(tasks);
}

static char* cli_get_frequency_name(TaskFrequency freq) {
    switch (freq) {
        case ONCE:
            return "Once";
        case DAILY:
            return "Daily";
        case WEEKLY:
            return "Weekly";
        case MONTHLY:
            return "Monthly";
        case CUSTOM:
            return "Custom";
        default:
            return "Unknown";
    }
}

static void cli_print_command_help(void) {
    printf("Available commands:\n");
    printf("  help                 Show this help\n");
    printf("  list                 List all tasks\n");
    printf("  add                  Add a new task\n");
    printf("  remove <id>          Remove a task\n");
    printf("  update <id>          Update a task\n");
    printf("  enable <id>          Enable a task\n");
    printf("  disable <id>         Disable a task\n");
    printf("  execute <id>         Execute a task immediately\n");
    printf("  start                Start the scheduler\n");
    printf("  stop                 Stop the scheduler\n");
    printf("  exit, quit           Exit the program\n");
}

// Readline completion functions
static char** cli_command_completion(const char *text, int start __attribute__((unused)), int end __attribute__((unused))) {
    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, cli_command_generator);
}

static char* cli_command_generator(const char *text, int state) {
    static int list_index, len;
    
    if (!state) {
        list_index = 0;
        len = strlen(text);
    }
    
    while (commands[list_index]) {
        const char *name = commands[list_index++];
        
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }
    
    return NULL;
}
#endif

bool cli_init(const char *data_dir) {
    if (scheduler_initialized) {
        return true;
    }
    
    if (!scheduler_init(&scheduler, data_dir)) {
        printf("Failed to initialize scheduler\n");
        return false;
    }
    
    if (!scheduler_start(&scheduler)) {
        printf("Failed to start scheduler\n");
        scheduler_cleanup(&scheduler);
        return false;
    }
    
    scheduler_initialized = true;
    return true;
}

void cli_cleanup() {
    if (scheduler_initialized) {
        // Đồng bộ dữ liệu với cơ sở dữ liệu trước khi dừng
        scheduler_sync(&scheduler);
        
        scheduler_stop(&scheduler);
        scheduler_cleanup(&scheduler);
        scheduler_initialized = false;
    }
}

void cli_add_task(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s add <name> [command] [options]\n", argv[0]);
        printf("Options:\n");
        printf("  -t <minutes>     : Task interval in minutes\n");
        printf("  -s <cron>        : Schedule in cron format (e.g., \"0 9 * * 1-5\")\n");
        printf("  -d <directory>   : Working directory\n");
        printf("  -m <max_runtime> : Maximum runtime in seconds\n");
        printf("  -x <script>      : Treat as script (provide script content)\n");
        printf("  -f <script_file> : Execute script from file\n");
        return;
    }
    
    // Initialize task with default values
    Task task;
    task_init(&task);
    
    // Set name
    safe_strcpy(task.name, argv[2], sizeof(task.name));
    
    // Default to command mode
    task.exec_mode = EXEC_COMMAND;
    
    // Default to manual schedule (no automatic scheduling)
    task.schedule_type = SCHEDULE_MANUAL;
    
    // Parse arguments
    int i = 3;
    bool is_script = false;
    char script_content[SCRIPT_CONTENT_MAX_LENGTH] = {0};
    char script_file[512] = {0};
    bool is_script_file = false;
    bool command_set = false;
    
    // Phân tích các tùy chọn
    while (i < argc) {
        if (argv[i][0] == '-') {
            // Xử lý tùy chọn
            if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
                // Set interval
                task.interval = atoi(argv[i + 1]);
                task.schedule_type = SCHEDULE_INTERVAL;
                // Đặt frequency thành CUSTOM để đảm bảo tương thích ngược
                task.frequency = CUSTOM;
                i += 2;
            } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
                // Set cron schedule
                safe_strcpy(task.cron_expression, argv[i + 1], sizeof(task.cron_expression));
                task.schedule_type = SCHEDULE_CRON;
                i += 2;
            } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
                // Set working directory
                safe_strcpy(task.working_dir, argv[i + 1], sizeof(task.working_dir));
                i += 2;
            } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
                // Set max runtime
                task.max_runtime = atoi(argv[i + 1]);
                i += 2;
            } else if (strcmp(argv[i], "-x") == 0 && i + 1 < argc) {
                // Set script content and mode
                is_script = true;
                safe_strcpy(script_content, argv[i + 1], sizeof(script_content));
                task.exec_mode = EXEC_SCRIPT;
                i += 2;
            } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
                // Set script file path
                is_script_file = true;
                safe_strcpy(script_file, argv[i + 1], sizeof(script_file));
                i += 2;
            } else {
                printf("Unknown option: %s\n", argv[i]);
                return;
            }
        } else {
            // Đây là lệnh nếu chưa được đặt
            if (!command_set && !is_script_file && !is_script) {
                safe_strcpy(task.command, argv[i], sizeof(task.command));
                command_set = true;
                i++;
            } else {
                printf("Unknown option: %s\n", argv[i]);
                return;
            }
        }
    }
    
    // If it's a script file, read the file content
    if (is_script_file) {
        FILE *file = fopen(script_file, "r");
        if (!file) {
            printf("Failed to open script file: %s\n", script_file);
            return;
        }
        
        size_t bytes_read = fread(script_content, 1, sizeof(script_content) - 1, file);
        fclose(file);
        
        if (bytes_read == 0) {
            printf("Script file is empty or failed to read\n");
            return;
        }
        
        // Null-terminate the content
        script_content[bytes_read] = '\0';
        
        // Set to script mode
        is_script = true;
        task.exec_mode = EXEC_SCRIPT;
        
        // Use script file path as the command (for reference)
        safe_strcpy(task.command, script_file, sizeof(task.command));
    }
    
    // Validate the task
    if (task.name[0] == '\0') {
        printf("Task name is required\n");
        return;
    }
    
    if (task.exec_mode == EXEC_COMMAND && !command_set) {
        printf("Command is required for command-mode tasks\n");
        return;
    }
    
    if (task.exec_mode == EXEC_SCRIPT && script_content[0] == '\0') {
        printf("Script content is required for script-mode tasks\n");
        return;
    }
    
    // Apply script content if in script mode
    if (is_script) {
        safe_strcpy(task.script_content, script_content, sizeof(task.script_content));
    }
    
    // Calculate initial next run time
    task_calculate_next_run(&task);
    
    // Add task to scheduler
    int task_id = scheduler_add_task(&scheduler, task);
    if (task_id < 0) {
        printf("Failed to add task\n");
    } else {
        printf("Task added with ID: %d\n", task_id);
    }
}

void cli_list_tasks(int argc, char *argv[]) {
    (void)argc; // Unused parameter
    (void)argv; // Unused parameter
    
    int count = 0;
    Task *tasks = scheduler_get_all_tasks(&scheduler, &count);
    
    if (!tasks || count == 0) {
        printf("No tasks found\n");
        return;
    }
    
    printf("Task List:\n");
    printf("----------\n");
    
    for (int i = 0; i < count; i++) {
        Task *task = &tasks[i];
        char next_run[64] = "Not scheduled";
        
        if (task->next_run_time > 0) {
            time_t next = task->next_run_time;
            struct tm *tm_info = localtime(&next);
            strftime(next_run, sizeof(next_run), "%Y-%m-%d %H:%M:%S", tm_info);
        }
        
        char last_run[64] = "Never";
        if (task->last_run_time > 0) {
            time_t last = task->last_run_time;
            struct tm *tm_info = localtime(&last);
            strftime(last_run, sizeof(last_run), "%Y-%m-%d %H:%M:%S", tm_info);
        }
        
        printf("ID: %d\n", task->id);
        printf("Name: %s\n", task->name);
        printf("Enabled: %s\n", task->enabled ? "Yes" : "No");
        
        // Hiển thị loại tác vụ dựa trên exec_mode
        const char *type_name;
        switch (task->exec_mode) {
            case EXEC_COMMAND:
                type_name = "Command";
                break;
            case EXEC_SCRIPT:
                type_name = "Script";
                break;
            case EXEC_AI_DYNAMIC:
                type_name = "AI Dynamic";
                break;
            default:
                type_name = "Unknown";
        }
        printf("Type: %s\n", type_name);
        
        if (task->exec_mode == EXEC_COMMAND) {
            printf("Command: %s\n", task->command);
        } else if (task->exec_mode == EXEC_SCRIPT) {
            printf("Script size: %ld bytes\n", strlen(task->script_content));
        } else if (task->exec_mode == EXEC_AI_DYNAMIC) {
            printf("AI Prompt: %s\n", task->ai_prompt);
            printf("System Metrics: %s\n", task->system_metrics);
        }
        
        printf("Schedule: ");
        if (task->schedule_type == SCHEDULE_INTERVAL) {
            printf("Every %d minutes\n", task->interval);
        } else if (task->schedule_type == SCHEDULE_CRON) {
            printf("Cron: %s\n", task->cron_expression);
        } else {
            printf("Manual\n");
        }
        
        printf("Working Dir: %s\n", task->working_dir[0] ? task->working_dir : "(default)");
        printf("Max Runtime: %d seconds\n", task->max_runtime);
        
        // Luôn hiển thị thông tin Last Run nếu có, bất kể trạng thái enabled
        printf("Last Run: %s\n", last_run);
        
        // Hiển thị Exit Code nếu đã từng chạy
        if (task->last_run_time > 0) {
            printf("Exit Code: %d\n", task->exit_code);
        }
        
        // Hiển thị Next Run
        printf("Next Run: %s\n", next_run);
        
        // Show dependencies if any
        if (task->dependency_count > 0) {
            printf("Dependencies: ");
            for (int j = 0; j < task->dependency_count; j++) {
                printf("%d", task->dependencies[j]);
                if (j < task->dependency_count - 1) {
                    printf(", ");
                }
            }
            printf("\n");
            
            // Show dependency behavior
            printf("Dependency Behavior: ");
            switch (task->dep_behavior) {
                case DEP_ANY_SUCCESS:
                    printf("Any Success\n");
                    break;
                case DEP_ALL_SUCCESS:
                    printf("All Success\n");
                    break;
                case DEP_ANY_COMPLETION:
                    printf("Any Completion\n");
                    break;
                case DEP_ALL_COMPLETION:
                    printf("All Completion\n");
                    break;
                default:
                    printf("Unknown\n");
            }
        }
        
        printf("----------\n");
    }
    
    free(tasks);
}

void cli_remove_task(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s remove <task_id>\n", argv[0]);
        return;
    }
    
    int task_id = atoi(argv[2]);
    if (task_id <= 0) {
        printf("Invalid task ID\n");
        return;
    }
    
    if (scheduler_remove_task(&scheduler, task_id)) {
        printf("Task %d removed\n", task_id);
    } else {
        printf("Failed to remove task %d\n", task_id);
    }
}

void cli_enable_task(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s enable <task_id>\n", argv[0]);
        return;
    }
    
    int task_id = atoi(argv[2]);
    if (task_id <= 0) {
        printf("Invalid task ID\n");
        return;
    }
    
    Task *task = scheduler_get_task(&scheduler, task_id);
    if (!task) {
        printf("Task %d not found\n", task_id);
        return;
    }
    
    // Lưu lại giá trị last_run_time trước khi thay đổi trạng thái
    time_t last_run_time = task->last_run_time;
    
    // Log thông tin chi tiết về last_run_time ban đầu
    if (last_run_time > 0) {
        char last_run_str[64] = "Never";
        struct tm *tm_info = localtime(&last_run_time);
        strftime(last_run_str, sizeof(last_run_str), "%Y-%m-%d %H:%M:%S", tm_info);
        printf("Original last run time: %s\n", last_run_str);
    } else {
        printf("Original last run time: None\n");
    }
    
    // Cập nhật trạng thái task thành enabled
    task->enabled = true;
    
    // Đảm bảo giữ nguyên last_run_time
    task->last_run_time = last_run_time;
    
    // Tính toán lại thời gian chạy tiếp theo
    // Thay vì gọi trực tiếp task_calculate_next_run, chúng ta tự xử lý để đảm bảo last_run_time không bị mất
    time_t now = time(NULL);
    
    // Đặt next_run_time tùy thuộc vào loại lịch trình
    if (task->schedule_type == SCHEDULE_INTERVAL && task->interval > 0) {
        // Nếu đã chạy trước đó, lên lịch dựa trên last_run_time
        if (last_run_time > 0) {
            task->next_run_time = last_run_time + (task->interval * 60);
            // Nếu thời gian đã qua, lên lịch dựa trên hiện tại
            if (task->next_run_time < now) {
                task->next_run_time = now + (task->interval * 60);
            }
        } else {
            // Nếu chưa từng chạy, lên lịch từ thời điểm hiện tại
            task->next_run_time = now + (task->interval * 60);
        }
    } else if (task->schedule_type == SCHEDULE_CRON && task->cron_expression[0] != '\0') {
        // Xử lý đơn giản cho một số biểu thức cron
        if (strcmp(task->cron_expression, "* * * * *") == 0) {
            // Mỗi phút
            task->next_run_time = now + 60;
        } else if (strncmp(task->cron_expression, "*/", 2) == 0) {
            // */n định dạng (mỗi n phút)
            int minutes = atoi(task->cron_expression + 2);
            if (minutes > 0) {
                task->next_run_time = now + (minutes * 60);
            } else {
                task->next_run_time = now + 3600; // Mặc định 1 giờ
            }
        } else {
            // Biểu thức cron khác, mặc định 1 giờ
            task->next_run_time = now + 3600;
        }
    } else {
        // Schedule_MANUAL hoặc không xác định, không thiết lập next_run_time
        task->next_run_time = 0;
    }
    
    // Log thông tin sau khi thay đổi trạng thái
    printf("Task status changed: enabled = %s\n", task->enabled ? "true" : "false");
    if (task->next_run_time > 0) {
        char next_run_str[64];
        struct tm *tm_info = localtime(&task->next_run_time);
        strftime(next_run_str, sizeof(next_run_str), "%Y-%m-%d %H:%M:%S", tm_info);
        printf("Next run time set to: %s\n", next_run_str);
    } else {
        printf("Next run time not scheduled (manual task)\n");
    }
    printf("Preserving last run time: %ld\n", last_run_time);
    
    if (scheduler_update_task(&scheduler, *task)) {
        printf("Task %d enabled\n", task_id);
        
        // Kiểm tra xác nhận last_run_time đã được bảo toàn
        Task *check_task = scheduler_get_task(&scheduler, task_id);
        if (check_task) {
            if (check_task->last_run_time == last_run_time) {
                printf("Confirmed: last_run_time preserved correctly\n");
            } else {
                printf("Warning: last_run_time changed from %ld to %ld\n", 
                       last_run_time, check_task->last_run_time);
            }
            free(check_task);
        }
    } else {
        printf("Failed to enable task %d\n", task_id);
    }
    
    free(task);
}

void cli_disable_task(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s disable <task_id>\n", argv[0]);
        return;
    }
    
    int task_id = atoi(argv[2]);
    if (task_id <= 0) {
        printf("Invalid task ID\n");
        return;
    }
    
    Task *task = scheduler_get_task(&scheduler, task_id);
    if (!task) {
        printf("Task %d not found\n", task_id);
        return;
    }
    
    // Lưu lại các giá trị quan trọng trước khi thay đổi
    time_t last_run_time = task->last_run_time;
    int exit_code = task->exit_code;
    
    // Log thông tin chi tiết về last_run_time ban đầu
    if (last_run_time > 0) {
        char last_run_str[64] = "Never";
        struct tm *tm_info = localtime(&last_run_time);
        strftime(last_run_str, sizeof(last_run_str), "%Y-%m-%d %H:%M:%S", tm_info);
        printf("Original last run time: %s\n", last_run_str);
    } else {
        printf("Original last run time: None\n");
    }
    
    // Chỉ thay đổi trạng thái enabled thành false
    task->enabled = false;
    
    // Đảm bảo giữ nguyên last_run_time và exit_code
    task->last_run_time = last_run_time;
    task->exit_code = exit_code;
    
    // Đặt next_run_time = 0 thay vì gọi task_calculate_next_run
    task->next_run_time = 0;
    
    // Log thông tin sau khi thay đổi trạng thái
    printf("Task status changed: enabled = %s, next_run_time = 0\n", 
           task->enabled ? "true" : "false");
    printf("Preserving last run time: %ld\n", last_run_time);
    printf("Preserving exit code: %d\n", exit_code);
    
    // Vẫn cập nhật task trong database
    if (scheduler_update_task(&scheduler, *task)) {
        printf("Task %d disabled\n", task_id);
        
        // Kiểm tra xác nhận last_run_time và exit_code đã được bảo toàn
        Task *check_task = scheduler_get_task(&scheduler, task_id);
        if (check_task) {
            if (check_task->last_run_time == last_run_time) {
                printf("Confirmed: last_run_time preserved correctly\n");
            } else {
                printf("Warning: last_run_time changed from %ld to %ld\n", 
                       last_run_time, check_task->last_run_time);
            }
            
            if (check_task->exit_code == exit_code) {
                printf("Confirmed: exit_code preserved correctly\n");
            } else {
                printf("Warning: exit_code changed from %d to %d\n", 
                       exit_code, check_task->exit_code);
            }
            
            free(check_task);
        }
    } else {
        printf("Failed to disable task %d\n", task_id);
    }
    
    free(task);
}

void cli_run_task(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s run <task_id>\n", argv[0]);
        return;
    }
    
    int task_id = atoi(argv[2]);
    if (task_id <= 0) {
        printf("Invalid task ID\n");
        return;
    }
    
    printf("Running task %d...\n", task_id);
    if (scheduler_execute_task(&scheduler, task_id)) {
        printf("Task %d executed\n", task_id);
    } else {
        printf("Failed to execute task %d\n", task_id);
    }
}

void cli_view_task(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s view <task_id>\n", argv[0]);
        return;
    }
    
    int task_id = atoi(argv[2]);
    if (task_id <= 0) {
        printf("Invalid task ID\n");
        return;
    }
    
    Task *task = scheduler_get_task(&scheduler, task_id);
    if (!task) {
        printf("Task %d not found\n", task_id);
        return;
    }
    
    printf("Task Details:\n");
    printf("ID: %d\n", task->id);
    printf("Name: %s\n", task->name);
    printf("Enabled: %s\n", task->enabled ? "Yes" : "No");
    
    // Hiển thị loại tác vụ dựa trên exec_mode
    const char *type_name;
    switch (task->exec_mode) {
        case EXEC_COMMAND:
            type_name = "Command";
            break;
        case EXEC_SCRIPT:
            type_name = "Script";
            break;
        case EXEC_AI_DYNAMIC:
            type_name = "AI Dynamic";
            break;
        default:
            type_name = "Unknown";
    }
    printf("Type: %s\n", type_name);
    
    // Hiển thị thông tin dựa trên loại tác vụ
    if (task->exec_mode == EXEC_COMMAND) {
        printf("Command: %s\n", task->command);
    } else if (task->exec_mode == EXEC_SCRIPT) {
        printf("Script:\n%s\n", task->script_content);
    } else if (task->exec_mode == EXEC_AI_DYNAMIC) {
        printf("AI Prompt: %s\n", task->ai_prompt);
        printf("System Metrics: %s\n", task->system_metrics);
        
        // Kiểm tra API key có được cấu hình hay không
        const char *api_key = ai_get_api_key();
        if (!api_key) {
            printf("\nWARNING: No DeepSeek API key is configured!\n");
            printf("You must set an API key before running this task:\n");
            printf("  %s set-api-key your_api_key_here\n", argv[0]);
        }
    }
    
    printf("Schedule: ");
    if (task->schedule_type == SCHEDULE_INTERVAL) {
        printf("Every %d minutes\n", task->interval);
    } else if (task->schedule_type == SCHEDULE_CRON) {
        printf("Cron: %s\n", task->cron_expression);
    } else {
        printf("Manual\n");
    }
    
    printf("Working Directory: %s\n", task->working_dir[0] ? task->working_dir : "(default)");
    printf("Max Runtime: %d seconds\n", task->max_runtime);
    
    // Hiển thị thời gian tạo
    char creation_time_str[64] = "Unknown";
    if (task->creation_time > 0) {
        time_t creation = task->creation_time;
        struct tm *tm_info = localtime(&creation);
        strftime(creation_time_str, sizeof(creation_time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    }
    printf("Created: %s\n", creation_time_str);
    
    char last_run[64] = "Never";
    if (task->last_run_time > 0) {
        time_t last = task->last_run_time;
        struct tm *tm_info = localtime(&last);
        strftime(last_run, sizeof(last_run), "%Y-%m-%d %H:%M:%S", tm_info);
    }
    printf("Last Run: %s\n", last_run);
    
    if (task->last_run_time > 0) {
        printf("Exit Code: %d\n", task->exit_code);
    }
    
    char next_run[64] = "Not scheduled";
    if (task->next_run_time > 0) {
        time_t next = task->next_run_time;
        struct tm *tm_info = localtime(&next);
        strftime(next_run, sizeof(next_run), "%Y-%m-%d %H:%M:%S", tm_info);
    }
    printf("Next Run: %s\n", next_run);
    
    // Show dependencies if any
    if (task->dependency_count > 0) {
        printf("Dependencies: ");
        for (int i = 0; i < task->dependency_count; i++) {
            printf("%d", task->dependencies[i]);
            if (i < task->dependency_count - 1) {
                printf(", ");
            }
        }
        printf("\n");
        
        // Show dependency behavior
        printf("Dependency Behavior: ");
        switch (task->dep_behavior) {
            case DEP_ANY_SUCCESS:
                printf("Any Success\n");
                break;
            case DEP_ALL_SUCCESS:
                printf("All Success\n");
                break;
            case DEP_ANY_COMPLETION:
                printf("Any Completion\n");
                break;
            case DEP_ALL_COMPLETION:
                printf("All Completion\n");
                break;
            default:
                printf("Unknown\n");
        }
    }
    
    free(task);
}

void cli_edit_task(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s edit <task_id> <field> <value>\n", argv[0]);
        printf("Fields: name, command, interval, cron, dir, runtime\n");
        return;
    }
    
    int task_id = atoi(argv[2]);
    if (task_id <= 0) {
        printf("Invalid task ID\n");
        return;
    }
    
    const char *field = argv[3];
    const char *value = argc > 4 ? argv[4] : "";
    
    Task *task = scheduler_get_task(&scheduler, task_id);
    if (!task) {
        printf("Task %d not found\n", task_id);
        return;
    }
    
    if (strcmp(field, "name") == 0) {
        safe_strcpy(task->name, value, sizeof(task->name));
    } else if (strcmp(field, "command") == 0) {
        if (task->exec_mode == EXEC_COMMAND) {
            safe_strcpy(task->command, value, sizeof(task->command));
        } else {
            printf("Cannot set command for script-mode task. Use 'script' field instead.\n");
            free(task);
            return;
        }
    } else if (strcmp(field, "script") == 0) {
        if (task->exec_mode == EXEC_SCRIPT) {
            safe_strcpy(task->script_content, value, sizeof(task->script_content));
        } else {
            // Convert to script mode
            task->exec_mode = EXEC_SCRIPT;
            safe_strcpy(task->script_content, value, sizeof(task->script_content));
        }
    } else if (strcmp(field, "interval") == 0) {
        task->interval = atoi(value);
        task->schedule_type = SCHEDULE_INTERVAL;
        task_calculate_next_run(task);
    } else if (strcmp(field, "cron") == 0) {
        safe_strcpy(task->cron_expression, value, sizeof(task->cron_expression));
        task->schedule_type = SCHEDULE_CRON;
        task_calculate_next_run(task);
    } else if (strcmp(field, "dir") == 0) {
        safe_strcpy(task->working_dir, value, sizeof(task->working_dir));
    } else if (strcmp(field, "runtime") == 0) {
        task->max_runtime = atoi(value);
    } else if (strcmp(field, "dep_behavior") == 0) {
        int behavior = atoi(value);
        if (behavior >= DEP_ANY_SUCCESS && behavior <= DEP_ALL_COMPLETION) {
            task->dep_behavior = (DependencyBehavior)behavior;
        } else {
            printf("Invalid dependency behavior (valid values: 0-3)\n");
            free(task);
            return;
        }
    } else {
        printf("Unknown field: %s\n", field);
        free(task);
        return;
    }
    
    if (scheduler_update_task(&scheduler, *task)) {
        printf("Task %d updated\n", task_id);
    } else {
        printf("Failed to update task %d\n", task_id);
    }
    
    free(task);
}

void cli_add_dependency(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s add-dep <task_id> <dependency_id>\n", argv[0]);
        return;
    }
    
    int task_id = atoi(argv[2]);
    int dependency_id = atoi(argv[3]);
    
    if (task_id <= 0 || dependency_id <= 0) {
        printf("Invalid task ID or dependency ID\n");
        return;
    }
    
    if (scheduler_add_dependency(&scheduler, task_id, dependency_id)) {
        printf("Added dependency: Task %d now depends on Task %d\n", task_id, dependency_id);
    } else {
        printf("Failed to add dependency\n");
    }
}

void cli_remove_dependency(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s remove-dep <task_id> <dependency_id>\n", argv[0]);
        return;
    }
    
    int task_id = atoi(argv[2]);
    int dependency_id = atoi(argv[3]);
    
    if (task_id <= 0 || dependency_id <= 0) {
        printf("Invalid task ID or dependency ID\n");
        return;
    }
    
    if (scheduler_remove_dependency(&scheduler, task_id, dependency_id)) {
        printf("Removed dependency: Task %d no longer depends on Task %d\n", task_id, dependency_id);
    } else {
        printf("Failed to remove dependency\n");
    }
}

void cli_set_dep_behavior(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s set-dep-behavior <task_id> <behavior>\n", argv[0]);
        printf("Behaviors:\n");
        printf("  0 - Any Success: Execute if any dependency succeeded\n");
        printf("  1 - All Success: Execute if all dependencies succeeded\n");
        printf("  2 - Any Completion: Execute if any dependency completed (success or failure)\n");
        printf("  3 - All Completion: Execute if all dependencies completed (success or failure)\n");
        return;
    }
    
    int task_id = atoi(argv[2]);
    int behavior = atoi(argv[3]);
    
    if (task_id <= 0) {
        printf("Invalid task ID\n");
        return;
    }
    
    if (behavior < DEP_ANY_SUCCESS || behavior > DEP_ALL_COMPLETION) {
        printf("Invalid behavior value (valid values: 0-3)\n");
        return;
    }
    
    Task *task = scheduler_get_task(&scheduler, task_id);
    if (!task) {
        printf("Task %d not found\n", task_id);
        return;
    }
    
    task->dep_behavior = (DependencyBehavior)behavior;
    
    if (scheduler_update_task(&scheduler, *task)) {
        printf("Task %d dependency behavior updated\n", task_id);
    } else {
        printf("Failed to update task %d dependency behavior\n", task_id);
    }
    
    free(task);
}

void cli_convert_to_script(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s to-script <task_id> <script_content>\n", argv[0]);
        return;
    }
    
    int task_id = atoi(argv[2]);
    if (task_id <= 0) {
        printf("Invalid task ID\n");
        return;
    }
    
    const char *script_content = argv[3];
    
    if (scheduler_set_exec_mode(&scheduler, task_id, EXEC_SCRIPT, script_content, NULL, NULL)) {
        printf("Task %d converted to script mode\n", task_id);
    } else {
        printf("Failed to convert task %d to script mode\n", task_id);
    }
}

void cli_convert_to_command(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s to-command <task_id> <command>\n", argv[0]);
        return;
    }
    
    int task_id = atoi(argv[2]);
    if (task_id <= 0) {
        printf("Invalid task ID\n");
        return;
    }
    
    const char *command = argv[3];
    
    // First set mode to command
    if (!scheduler_set_exec_mode(&scheduler, task_id, EXEC_COMMAND, NULL, NULL, NULL)) {
        printf("Failed to convert task %d to command mode\n", task_id);
        return;
    }
    
    // Then update the command
    Task *task = scheduler_get_task(&scheduler, task_id);
    if (!task) {
        printf("Task %d not found\n", task_id);
        return;
    }
    
    safe_strcpy(task->command, command, sizeof(task->command));
    
    if (scheduler_update_task(&scheduler, *task)) {
        printf("Task %d converted to command mode with command: %s\n", task_id, command);
    } else {
        printf("Failed to update command for task %d\n", task_id);
    }
    
    free(task);
}

void cli_convert_to_ai_dynamic(int argc, char *argv[]) {
    if (argc < 5) {
        printf("Usage: %s to-ai <task_id> <ai_prompt> <system_metrics>\n", argv[0]);
        printf("Example: %s to-ai 1 \"Clean temporary files\" \"cpu_load,disk:/tmp\"\n", argv[0]);
        return;
    }
    
    int task_id = atoi(argv[2]);
    const char *ai_prompt = argv[3];
    const char *system_metrics = argv[4];
    
    if (scheduler_set_exec_mode(&scheduler, task_id, EXEC_AI_DYNAMIC, NULL, ai_prompt, system_metrics)) {
        printf("Task %d converted to AI-Dynamic mode\n", task_id);
        printf("AI Prompt: %s\n", ai_prompt);
        printf("System Metrics: %s\n", system_metrics);
        
        // Kiểm tra xem API key đã được cấu hình chưa
        const char *api_key = ai_get_api_key();
        if (!api_key) {
            printf("\nWARNING: No DeepSeek API key is configured!\n");
            printf("You must set an API key before running this task:\n");
            printf("  %s set-api-key your_api_key_here\n", argv[0]);
        } else {
            printf("\nAPI key is configured and ready to use.\n");
        }
    } else {
        printf("Failed to convert task %d to AI-Dynamic mode\n", task_id);
    }
}

void cli_help(int argc, char *argv[]) {
    (void)argc; // Unused parameter
    
    printf("Task Scheduler Commands:\n");
    printf("  %s add <n> [command] [options]  : Add a new task\n", argv[0]);
    printf("    Options:\n");
    printf("      -t <minutes>     : Task interval in minutes\n");
    printf("      -s <cron>        : Schedule in cron format (e.g., \"0 9 * * 1-5\")\n");
    printf("      -d <directory>   : Working directory\n");
    printf("      -m <max_runtime> : Maximum runtime in seconds\n");
    printf("      -x <script>      : Treat as script (provide script content)\n");
    printf("      -f <script_file> : Execute script from file\n");
    printf("  %s list              : List all tasks\n", argv[0]);
    printf("  %s view <task_id>    : View task details\n", argv[0]);
    printf("  %s remove <task_id>  : Remove a task\n", argv[0]);
    printf("  %s run <task_id>     : Run a task immediately\n", argv[0]);
    printf("  %s enable <task_id>  : Enable a task\n", argv[0]);
    printf("  %s disable <task_id> : Disable a task\n", argv[0]);
    printf("  %s edit <task_id> <field> <value> : Edit a task field\n", argv[0]);
    printf("  %s add-dep <task_id> <dependency_id> : Add dependency between tasks\n", argv[0]);
    printf("  %s remove-dep <task_id> <dependency_id> : Remove dependency\n", argv[0]);
    printf("  %s set-dep-behavior <task_id> <behavior> : Set dependency behavior\n", argv[0]);
    printf("  %s to-script <task_id> <script> : Convert task to script mode\n", argv[0]);
    printf("  %s to-command <task_id> <command> : Convert task to command mode\n", argv[0]);
    printf("  %s to-ai <task_id> <ai_prompt> <system_metrics> : Convert task to AI-Dynamic mode\n", argv[0]);
    printf("  %s set-api-key <api_key> : Set DeepSeek API key in config file\n", argv[0]);
    printf("  %s view-api-key     : View DeepSeek API key configuration\n", argv[0]);
    printf("  %s help              : Show this help message\n", argv[0]);
    
    printf("\nCron Format Guide:\n");
    printf("  Cron expressions consist of 5 fields separated by spaces:\n");
    printf("  <minute> <hour> <day_of_month> <month> <day_of_week>\n\n");
    printf("  Each field can contain:\n");
    printf("    * - Any value (e.g., * in hour field = every hour)\n");
    printf("    Number - Specific value (e.g., 5 in minute field = 5th minute)\n");
    printf("    Comma - Value list (e.g., 1,3,5 = 1st, 3rd, and 5th minute)\n");
    printf("    Hyphen - Range (e.g., 1-5 = minutes 1 through 5)\n");
    printf("    / - Step values (e.g., */15 in minute field = every 15 minutes)\n\n");
    printf("  Valid values for each field:\n");
    printf("    Minute: 0-59\n");
    printf("    Hour: 0-23\n");
    printf("    Day of month: 1-31\n");
    printf("    Month: 1-12 or JAN-DEC\n");
    printf("    Day of week: 0-6 (0=Sunday) or SUN-SAT\n\n");
    printf("  Examples:\n");
    printf("    * * * * *        : Run every minute\n");
    printf("    0 * * * *        : Run every hour (at minute 0)\n");
    printf("    0 9 * * *        : Run at 9:00 AM every day\n");
    printf("    0 9 * * 1-5      : Run at 9:00 AM Monday through Friday\n");
    printf("    0 9 15 * *       : Run at 9:00 AM on the 15th of every month\n");
    printf("    */15 * * * *     : Run every 15 minutes\n");
    printf("    0 9-17 * * 1-5   : Run every hour from 9 AM to 5 PM, Monday through Friday\n");
    printf("    0 0 1,15 * *     : Run at midnight on the 1st and 15th of every month\n");
    
    printf("\nScript File Usage:\n");
    printf("  You can use the -f option to specify a script file to execute:\n");
    printf("  %s add \"backup\" -f /path/to/backup.sh -t 60\n", argv[0]);
    printf("  This will create a task that executes the backup.sh script every 60 minutes.\n");
    printf("  \n");
    printf("  You can also create a task with a direct command:\n");
    printf("  %s add \"notify\" \"notify-send 'Hello' 'This is a notification'\" -t 5\n", argv[0]);
}

// Utility function to trim whitespace from a string
static void trim_whitespace(char *str) {
    if (!str) return;
    
    // Trim leading space
    char *start = str;
    while (isspace((unsigned char)*start)) start++;
    
    // All spaces?
    if (*start == 0) {
        *str = 0;
        return;
    }
    
    // Trim trailing space
    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;
    
    // Write new null terminator
    *(end + 1) = 0;
    
    // Move if needed
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

void cli_run_interactive(const char *data_dir) {
    if (!cli_init(data_dir)) {
        fprintf(stderr, "Failed to initialize CLI\n");
        return;
    }
    
    printf("Task Scheduler Interactive Mode\n");
    printf("Type 'help' for available commands\n");
    
    bool running = true;
    while (running) {
        char *line = cli_get_command();
        if (line) {
            // Bỏ qua dòng trống
            if (line[0] == '\0') {
                free(line);
                continue;
            }
            
            // Phân tích dòng lệnh thành các đối số
            int argc = 0;
            char *argv[64]; // Giả sử tối đa 64 đối số
            
            // Xử lý phân tích tham số, hỗ trợ dấu ngoặc kép
            char *p = line;
            bool in_quotes = false;
            char quote_char = '\0';
            char token[1024] = {0}; // Bộ đệm cho token hiện tại
            int token_len = 0;
            
            while (*p != '\0' && argc < 63) {
                if ((*p == '"' || *p == '\'') && (!in_quotes || *p == quote_char)) {
                    // Bắt đầu hoặc kết thúc chuỗi trong ngoặc kép
                    if (in_quotes) {
                        // Kết thúc chuỗi trong ngoặc kép
                        in_quotes = false;
                        quote_char = '\0';
                        
                        // Lưu token nếu có nội dung
                        if (token_len > 0) {
                            token[token_len] = '\0';
                            argv[argc++] = strdup(token);
                            token_len = 0;
                        }
                    } else {
                        // Bắt đầu chuỗi trong ngoặc kép
                        in_quotes = true;
                        quote_char = *p;
                    }
                    p++;
                } else if (isspace((unsigned char)*p) && !in_quotes) {
                    // Khoảng trắng ngoài ngoặc kép - kết thúc token hiện tại
                    if (token_len > 0) {
                        token[token_len] = '\0';
                        argv[argc++] = strdup(token);
                        token_len = 0;
                    }
                    p++;
                    // Bỏ qua các khoảng trắng liên tiếp
                    while (isspace((unsigned char)*p)) p++;
                } else {
                    // Thêm ký tự vào token hiện tại
                    if (token_len < (int)(sizeof(token) - 1)) {
                        token[token_len++] = *p;
                    }
                    p++;
                }
            }
            
            // Xử lý token cuối cùng nếu có
            if (token_len > 0) {
                token[token_len] = '\0';
                argv[argc++] = strdup(token);
            }
            
            argv[argc] = NULL; // Kết thúc danh sách đối số
            
            // Xử lý lệnh đặc biệt 'exit' hoặc 'quit'
            if (argc > 0 && (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "quit") == 0)) {
                running = false;
            } else if (argc > 0) {
                // Thêm tên lệnh vào đầu danh sách đối số để phù hợp với cách gọi từ dòng lệnh
                for (int i = argc; i > 0; i--) {
                    argv[i] = argv[i-1];
                }
                argv[0] = "taskscheduler"; // Tên giả định của chương trình
                argc++;
                
                // Xử lý lệnh
                cli_process_command(argc, argv);
            }
            
            // Giải phóng các tham số đã được cấp phát
            for (int i = 1; i < argc; i++) {
                free(argv[i]);
            }
            
            free(line);
        } else {
            // EOF (Ctrl+D)
            running = false;
            printf("\n");
        }
    }
    
    cli_cleanup();
}

// Hàm đọc lệnh từ người dùng
char* cli_get_command() {
    char *line = readline("> ");
    
    if (line == NULL) {
        // EOF hoặc lỗi
        return NULL;
    }
    
    // Thêm vào lịch sử lệnh nếu không trống
    if (line[0] != '\0') {
        add_history(line);
    }
    
    // Cắt bỏ khoảng trắng đầu và cuối
    trim_whitespace(line);
    
    return line;
}

#if 0
// Không sử dụng time_to_string nếu có (không tìm thấy trong code)
void time_to_string(time_t time, char *buffer, size_t buffer_size, const char *format) {
    // ...
}
#endif

// Thêm các hàm mới để quản lý API key
void cli_set_api_key(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s set-api-key <api_key>\n", argv[0]);
        printf("Example: %s set-api-key sk-abcdefgh12345678\n", argv[0]);
        return;
    }
    
    const char *api_key = argv[2];
    
    if (ai_update_api_key(api_key, NULL)) {
        printf("API key updated successfully in config file.\n");
    } else {
        printf("Failed to update API key. Check error logs for details.\n");
    }
}

void cli_view_api_key(int argc, char *argv[]) {
    (void)argc; // Unused parameter
    (void)argv; // Unused parameter
    
    // Đảm bảo cấu hình đã được tải
    if (!ai_load_config(NULL)) {
        printf("Failed to load configuration.\n");
        return;
    }
    
    const char *api_key = ai_get_api_key();
    
    if (api_key) {
        // Hiển thị API key với một phần đầu và cuối, giữa bị che
        int len = strlen(api_key);
        if (len > 8) {
            printf("Current API key: ");
            // Hiển thị 4 ký tự đầu
            for (int i = 0; i < 4 && i < len; i++) {
                printf("%c", api_key[i]);
            }
            
            // Che khoảng giữa bằng dấu *
            printf("****");
            
            // Hiển thị 4 ký tự cuối
            if (len > 8) {
                for (int i = len - 4; i < len; i++) {
                    printf("%c", api_key[i]);
                }
            }
            printf("\n");
        } else {
            printf("Current API key: %s\n", api_key);
        }
        
        printf("\nAPI key is configured and ready to use with AI-Dynamic tasks.\n");
    } else {
        printf("No API key is currently configured.\n");
        printf("Use '%s set-api-key <api_key>' to set your DeepSeek API key.\n", argv[0]);
    }
}
