#ifndef TASK_H
#define TASK_H

#include <time.h>
#include <stdbool.h>
#include "ai.h"

// Giới hạn tối đa số lượng tác vụ phụ thuộc
#define MAX_DEPENDENCIES 10
#define SCRIPT_CONTENT_MAX_LENGTH 4096

/**
 * Enum for task frequency types
 */
typedef enum {
    ONCE,       // Run only once
    DAILY,      // Run daily
    WEEKLY,     // Run weekly
    MONTHLY,    // Run monthly
    CUSTOM      // Custom schedule
} TaskFrequency;

/**
 * Enum for task schedule types
 */
typedef enum {
    SCHEDULE_MANUAL,   // Manual execution only
    SCHEDULE_INTERVAL, // Interval-based scheduling
    SCHEDULE_CRON      // Cron-based scheduling
} ScheduleType;

/**
 * Enum for task execution mode
 */
typedef enum {
    EXEC_COMMAND,     // Execute a command directly
    EXEC_SCRIPT,      // Execute a script file
    EXEC_AI_DYNAMIC   // Execute AI-generated command based on system metrics
} TaskExecMode;

/**
 * Enum for task dependency behavior
 */
typedef enum {
    DEP_ANY_SUCCESS,    // Run if any dependency succeeds
    DEP_ALL_SUCCESS,    // Run only if all dependencies succeed
    DEP_ANY_COMPLETION, // Run if any dependency completes (regardless of success)
    DEP_ALL_COMPLETION  // Run after all dependencies complete (regardless of success)
} DependencyBehavior;

/**
 * Structure to store task information
 */
typedef struct {
    int id;                  // Unique task ID
    char name[256];          // Task name
    
    // Execution details
    TaskExecMode exec_mode;  // Command or script execution mode
    char command[1024];      // Command to execute or script path
    
    time_t creation_time;    // When the task was created
    time_t next_run_time;    // Next scheduled run time
    time_t last_run_time;    // Last time the task was run
    TaskFrequency frequency; // How often the task should run
    int interval;            // Interval value (depends on frequency)
    bool enabled;            // Whether the task is active
    int exit_code;           // Exit code from the last run
    int max_runtime;         // Maximum runtime in seconds (0 for unlimited)
    char working_dir[512];   // Working directory for the task
    
    // Dependencies
    int dependencies[MAX_DEPENDENCIES]; // IDs of dependent tasks
    int dependency_count;               // Number of dependencies
    DependencyBehavior dep_behavior;    // How to handle dependencies
    
    // Script content (if exec_mode is EXEC_SCRIPT)
    char script_content[SCRIPT_CONTENT_MAX_LENGTH];  // Content of the script if stored in DB
    
    // AI-Dynamic execution (if exec_mode is EXEC_AI_DYNAMIC)
    char ai_prompt[2048];              // AI prompt/goal for dynamic command generation
    char system_metrics[512];          // Comma-separated metrics to monitor (e.g., "disk:/,cpu_load,mem_free")
    
    // Schedule type
    ScheduleType schedule_type; // How this task is scheduled
    char cron_expression[128];  // Cron expression for cron-based scheduling
} Task;

/**
 * Initialize a new task with default values
 * 
 * @param task Pointer to the task structure to initialize
 * @return true on success, false on failure
 */
bool task_init(Task *task);

/**
 * Calculate the next run time for a task based on its frequency and interval
 * 
 * @param task Pointer to the task structure
 * @return true on success, false on failure
 */
bool task_calculate_next_run(Task *task);

/**
 * Check if a task is due to run
 * 
 * @param task Pointer to the task structure
 * @return true if the task should run now, false otherwise
 */
bool task_is_due(Task *task);

/**
 * Mark a task as executed
 * 
 * @param task Pointer to the task structure
 * @param exit_code The exit code of the executed command
 * @return true on success, false on failure
 */
bool task_mark_executed(Task *task, int exit_code);

/**
 * Add a dependency to a task
 * 
 * @param task Pointer to the task structure
 * @param dependency_id ID of the task to depend on
 * @return true on success, false on failure
 */
bool task_add_dependency(Task *task, int dependency_id);

/**
 * Remove a dependency from a task
 * 
 * @param task Pointer to the task structure
 * @param dependency_id ID of the task to remove dependency on
 * @return true on success, false on failure
 */
bool task_remove_dependency(Task *task, int dependency_id);

/**
 * Save script content to a temporary file for execution
 * 
 * @param task Pointer to the task structure
 * @param temp_path Buffer to store the path to the temporary file
 * @param temp_path_size Size of the temp_path buffer
 * @return true on success, false on failure
 */
bool task_prepare_script(Task *task, char *temp_path, size_t temp_path_size);

#endif /* TASK_H */ 