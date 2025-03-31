#include "../include/db.h"
#include "../include/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Global database connection
static sqlite3 *db = NULL;

// SQL statements
static const char *CREATE_TABLE_SQL =
    "CREATE TABLE IF NOT EXISTS tasks ("
    "id INTEGER PRIMARY KEY, "
    "name TEXT NOT NULL, "
    "command TEXT NOT NULL, "
    "creation_time INTEGER NOT NULL, "
    "next_run_time INTEGER, "
    "last_run_time INTEGER, "
    "frequency INTEGER NOT NULL, "
    "interval INTEGER, "
    "enabled INTEGER NOT NULL, "
    "exit_code INTEGER, "
    "max_runtime INTEGER, "
    "working_dir TEXT, "
    "exec_mode INTEGER NOT NULL DEFAULT 0, "
    "script_content TEXT, "
    "dep_behavior INTEGER NOT NULL DEFAULT 0"
    ");"
    
    "CREATE TABLE IF NOT EXISTS dependencies ("
    "task_id INTEGER, "
    "depends_on INTEGER, "
    "PRIMARY KEY (task_id, depends_on), "
    "FOREIGN KEY (task_id) REFERENCES tasks(id) ON DELETE CASCADE, "
    "FOREIGN KEY (depends_on) REFERENCES tasks(id) ON DELETE CASCADE"
    ");";

static const char *INSERT_TASK_SQL =
    "INSERT INTO tasks ("
    "id, name, command, creation_time, next_run_time, last_run_time, "
    "frequency, interval, enabled, exit_code, max_runtime, working_dir, "
    "exec_mode, script_content, dep_behavior"
    ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

static const char *UPDATE_TASK_SQL =
    "UPDATE tasks SET "
    "name = ?, command = ?, next_run_time = ?, last_run_time = ?, "
    "frequency = ?, interval = ?, enabled = ?, exit_code = ?, "
    "max_runtime = ?, working_dir = ?, exec_mode = ?, script_content = ?, "
    "dep_behavior = ? "
    "WHERE id = ?;";

static const char *DELETE_TASK_SQL =
    "DELETE FROM tasks WHERE id = ?;";

static const char *SELECT_ALL_TASKS_SQL =
    "SELECT * FROM tasks;";

static const char *SELECT_TASK_BY_ID_SQL =
    "SELECT * FROM tasks WHERE id = ?;";

static const char *SELECT_MAX_ID_SQL =
    "SELECT MAX(id) FROM tasks;";

static const char *INSERT_DEPENDENCY_SQL =
    "INSERT OR IGNORE INTO dependencies (task_id, depends_on) VALUES (?, ?);";

static const char *DELETE_ALL_DEPENDENCIES_SQL =
    "DELETE FROM dependencies WHERE task_id = ?;";

static const char *SELECT_DEPENDENCIES_SQL =
    "SELECT depends_on FROM dependencies WHERE task_id = ?;";

bool db_init(const char *db_path) {
    if (db != NULL) {
        // Database already initialized
        return true;
    }

    // Open database connection
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK) {
        log_message(LOG_ERROR, "Failed to open database: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        db = NULL;
        return false;
    }

    // Create tables if they don't exist
    char *err_msg = NULL;
    rc = sqlite3_exec(db, CREATE_TABLE_SQL, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        log_message(LOG_ERROR, "SQL error: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        db = NULL;
        return false;
    }

    // Enable foreign keys
    rc = sqlite3_exec(db, "PRAGMA foreign_keys = ON;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        log_message(LOG_ERROR, "SQL error enabling foreign keys: %s", err_msg);
        sqlite3_free(err_msg);
        // Not critical, continue anyway
    }

    log_message(LOG_INFO, "Database initialized: %s", db_path);
    return true;
}

void db_cleanup(void) {
    if (db != NULL) {
        sqlite3_close(db);
        db = NULL;
    }
}

// Helper function to save task dependencies
static bool save_task_dependencies(int task_id, const int *dependencies, int count) {
    if (db == NULL || task_id < 0 || count <= 0 || dependencies == NULL) {
        return true; // Not an error if no dependencies
    }

    // Delete existing dependencies for this task
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, DELETE_ALL_DEPENDENCIES_SQL, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_message(LOG_ERROR, "Failed to prepare statement: %s", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_int(stmt, 1, task_id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log_message(LOG_ERROR, "Failed to delete existing dependencies: %s", sqlite3_errmsg(db));
        return false;
    }

    // Insert new dependencies
    rc = sqlite3_prepare_v2(db, INSERT_DEPENDENCY_SQL, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_message(LOG_ERROR, "Failed to prepare statement: %s", sqlite3_errmsg(db));
        return false;
    }

    for (int i = 0; i < count; i++) {
        sqlite3_bind_int(stmt, 1, task_id);
        sqlite3_bind_int(stmt, 2, dependencies[i]);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            log_message(LOG_ERROR, "Failed to insert dependency: %s", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return false;
        }

        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }

    sqlite3_finalize(stmt);
    return true;
}

// Helper function to load task dependencies
static bool load_task_dependencies(int task_id, int *dependencies, int *count) {
    if (db == NULL || task_id < 0 || dependencies == NULL || count == NULL) {
        return false;
    }

    *count = 0;
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, SELECT_DEPENDENCIES_SQL, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_message(LOG_ERROR, "Failed to prepare statement: %s", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_int(stmt, 1, task_id);

    while (sqlite3_step(stmt) == SQLITE_ROW && *count < MAX_DEPENDENCIES) {
        dependencies[*count] = sqlite3_column_int(stmt, 0);
        (*count)++;
    }

    sqlite3_finalize(stmt);
    return true;
}

bool db_save_task(const Task *task) {
    if (db == NULL || task == NULL) {
        return false;
    }

    // Begin transaction
    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, INSERT_TASK_SQL, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_message(LOG_ERROR, "Failed to prepare statement: %s", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    // Bind values to the statement
    sqlite3_bind_int(stmt, 1, task->id);
    sqlite3_bind_text(stmt, 2, task->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, task->command, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, (sqlite3_int64)task->creation_time);
    sqlite3_bind_int64(stmt, 5, (sqlite3_int64)task->next_run_time);
    sqlite3_bind_int64(stmt, 6, (sqlite3_int64)task->last_run_time);
    sqlite3_bind_int(stmt, 7, task->frequency);
    sqlite3_bind_int(stmt, 8, task->interval);
    sqlite3_bind_int(stmt, 9, task->enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 10, task->exit_code);
    sqlite3_bind_int(stmt, 11, task->max_runtime);
    sqlite3_bind_text(stmt, 12, task->working_dir, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 13, task->exec_mode);
    sqlite3_bind_text(stmt, 14, task->script_content, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 15, task->dep_behavior);

    // Execute the statement
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log_message(LOG_ERROR, "Failed to insert task: %s", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    // Save dependencies
    if (task->dependency_count > 0) {
        if (!save_task_dependencies(task->id, task->dependencies, task->dependency_count)) {
            log_message(LOG_ERROR, "Failed to save task dependencies");
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            return false;
        }
    }

    // Commit the transaction
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);

    log_message(LOG_INFO, "Task saved to database: ID=%d", task->id);
    return true;
}

bool db_update_task(const Task *task) {
    if (db == NULL || task == NULL) {
        return false;
    }

    // Begin transaction
    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, UPDATE_TASK_SQL, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_message(LOG_ERROR, "Failed to prepare statement: %s", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    // Bind values to the statement
    sqlite3_bind_text(stmt, 1, task->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, task->command, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)task->next_run_time);
    sqlite3_bind_int64(stmt, 4, (sqlite3_int64)task->last_run_time);
    sqlite3_bind_int(stmt, 5, task->frequency);
    sqlite3_bind_int(stmt, 6, task->interval);
    sqlite3_bind_int(stmt, 7, task->enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 8, task->exit_code);
    sqlite3_bind_int(stmt, 9, task->max_runtime);
    sqlite3_bind_text(stmt, 10, task->working_dir, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 11, task->exec_mode);
    sqlite3_bind_text(stmt, 12, task->script_content, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 13, task->dep_behavior);
    sqlite3_bind_int(stmt, 14, task->id);

    // Execute the statement
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log_message(LOG_ERROR, "Failed to update task: %s", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    // Update dependencies
    if (!save_task_dependencies(task->id, task->dependencies, task->dependency_count)) {
        log_message(LOG_ERROR, "Failed to update task dependencies");
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    // Commit the transaction
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);

    log_message(LOG_INFO, "Task updated in database: ID=%d", task->id);
    return true;
}

bool db_delete_task(int task_id) {
    if (db == NULL) {
        return false;
    }

    // Begin transaction
    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    // Delete all dependencies first
    // With foreign keys enabled, this isn't strictly necessary, but let's be explicit
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, DELETE_ALL_DEPENDENCIES_SQL, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_message(LOG_ERROR, "Failed to prepare statement: %s", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    sqlite3_bind_int(stmt, 1, task_id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log_message(LOG_ERROR, "Failed to delete task dependencies: %s", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    // Now delete the task
    rc = sqlite3_prepare_v2(db, DELETE_TASK_SQL, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_message(LOG_ERROR, "Failed to prepare statement: %s", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    // Bind the task ID
    sqlite3_bind_int(stmt, 1, task_id);

    // Execute the statement
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log_message(LOG_ERROR, "Failed to delete task: %s", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    // Commit the transaction
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);

    log_message(LOG_INFO, "Task deleted from database: ID=%d", task_id);
    return true;
}

bool db_load_tasks(Task **tasks, int *count) {
    if (db == NULL || tasks == NULL || count == NULL) {
        return false;
    }

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, SELECT_ALL_TASKS_SQL, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_message(LOG_ERROR, "Failed to prepare statement: %s", sqlite3_errmsg(db));
        return false;
    }

    // Count the number of tasks
    int task_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        task_count++;
    }

    // Reset the statement to start from the beginning
    sqlite3_reset(stmt);

    // Allocate memory for tasks
    *tasks = NULL;
    *count = 0;

    if (task_count == 0) {
        sqlite3_finalize(stmt);
        return true; // No tasks, not an error
    }

    *tasks = (Task*)malloc(sizeof(Task) * task_count);
    if (*tasks == NULL) {
        log_message(LOG_ERROR, "Failed to allocate memory for tasks");
        sqlite3_finalize(stmt);
        return false;
    }

    // Load all tasks
    int index = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Task *task = &(*tasks)[index++];

        task->id = sqlite3_column_int(stmt, 0);
        safe_strcpy(task->name, (const char*)sqlite3_column_text(stmt, 1), sizeof(task->name));
        safe_strcpy(task->command, (const char*)sqlite3_column_text(stmt, 2), sizeof(task->command));
        task->creation_time = (time_t)sqlite3_column_int64(stmt, 3);
        task->next_run_time = (time_t)sqlite3_column_int64(stmt, 4);
        task->last_run_time = (time_t)sqlite3_column_int64(stmt, 5);
        task->frequency = (TaskFrequency)sqlite3_column_int(stmt, 6);
        task->interval = sqlite3_column_int(stmt, 7);
        task->enabled = sqlite3_column_int(stmt, 8) != 0;
        task->exit_code = sqlite3_column_int(stmt, 9);
        task->max_runtime = sqlite3_column_int(stmt, 10);
        safe_strcpy(task->working_dir, (const char*)sqlite3_column_text(stmt, 11), sizeof(task->working_dir));
        task->exec_mode = (TaskExecMode)sqlite3_column_int(stmt, 12);
        
        const char *script_content = (const char*)sqlite3_column_text(stmt, 13);
        if (script_content) {
            safe_strcpy(task->script_content, script_content, sizeof(task->script_content));
        } else {
            task->script_content[0] = '\0';
        }
        
        task->dep_behavior = (DependencyBehavior)sqlite3_column_int(stmt, 14);
        
        // Load dependencies
        if (!load_task_dependencies(task->id, task->dependencies, &task->dependency_count)) {
            log_message(LOG_WARNING, "Failed to load dependencies for task %d", task->id);
            task->dependency_count = 0;
        }
    }

    sqlite3_finalize(stmt);
    *count = task_count;

    log_message(LOG_INFO, "Loaded %d tasks from database", task_count);
    return true;
}

bool db_get_task(int task_id, Task *task) {
    if (db == NULL || task == NULL) {
        return false;
    }

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, SELECT_TASK_BY_ID_SQL, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_message(LOG_ERROR, "Failed to prepare statement: %s", sqlite3_errmsg(db));
        return false;
    }

    // Bind the task ID
    sqlite3_bind_int(stmt, 1, task_id);

    // Execute the statement
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return false; // Task not found
    }

    // Load task data
    task->id = sqlite3_column_int(stmt, 0);
    safe_strcpy(task->name, (const char*)sqlite3_column_text(stmt, 1), sizeof(task->name));
    safe_strcpy(task->command, (const char*)sqlite3_column_text(stmt, 2), sizeof(task->command));
    task->creation_time = (time_t)sqlite3_column_int64(stmt, 3);
    task->next_run_time = (time_t)sqlite3_column_int64(stmt, 4);
    task->last_run_time = (time_t)sqlite3_column_int64(stmt, 5);
    task->frequency = (TaskFrequency)sqlite3_column_int(stmt, 6);
    task->interval = sqlite3_column_int(stmt, 7);
    task->enabled = sqlite3_column_int(stmt, 8) != 0;
    task->exit_code = sqlite3_column_int(stmt, 9);
    task->max_runtime = sqlite3_column_int(stmt, 10);
    safe_strcpy(task->working_dir, (const char*)sqlite3_column_text(stmt, 11), sizeof(task->working_dir));
    task->exec_mode = (TaskExecMode)sqlite3_column_int(stmt, 12);
    
    const char *script_content = (const char*)sqlite3_column_text(stmt, 13);
    if (script_content) {
        safe_strcpy(task->script_content, script_content, sizeof(task->script_content));
    } else {
        task->script_content[0] = '\0';
    }
    
    task->dep_behavior = (DependencyBehavior)sqlite3_column_int(stmt, 14);
    
    sqlite3_finalize(stmt);
    
    // Load dependencies
    if (!load_task_dependencies(task->id, task->dependencies, &task->dependency_count)) {
        log_message(LOG_WARNING, "Failed to load dependencies for task %d", task->id);
        task->dependency_count = 0;
    }

    return true;
}

int db_get_next_id(void) {
    if (db == NULL) {
        return 1;
    }

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, SELECT_MAX_ID_SQL, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_message(LOG_ERROR, "Failed to prepare statement: %s", sqlite3_errmsg(db));
        return 1;
    }

    // Execute the statement
    rc = sqlite3_step(stmt);
    
    int next_id = 1;
    if (rc == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        next_id = sqlite3_column_int(stmt, 0) + 1;
    }

    sqlite3_finalize(stmt);
    return next_id;
} 