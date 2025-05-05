#ifndef EMAIL_H
#define EMAIL_H

#include <stdbool.h>
#include "task.h"

/**
 * Structure to hold email configuration
 */
typedef struct {
    char email_address[256];    // Email address
    char email_password[256];   // Email password or app password
    char smtp_server[256];      // SMTP server
    int smtp_port;              // SMTP port
    bool email_enabled;         // Email notification enabled flag
} EmailConfig;

/**
 * Initialize email configuration
 * 
 * @param config_path Path to the configuration file, NULL for default
 * @return true on success, false on failure
 */
bool email_init(const char *config_path);

/**
 * Save default email configuration
 * 
 * @param config_path Path to save configuration, NULL for default
 * @return true on success, false on failure
 */
bool email_save_default_config(const char *config_path);

/**
 * Update email configuration
 * 
 * @param email_address Email address
 * @param email_password Email password
 * @param smtp_server SMTP server
 * @param smtp_port SMTP port
 * @param config_path Configuration file path, NULL for default
 * @return true on success, false on failure
 */
bool email_update_config(const char *email_address, const char *email_password, 
                         const char *smtp_server, int smtp_port, const char *config_path);

/**
 * Enable or disable email notifications
 * 
 * @param enabled true to enable, false to disable
 * @param config_path Configuration file path, NULL for default
 * @return true on success, false on failure
 */
bool email_set_enabled(bool enabled, const char *config_path);

/**
 * Get current email configuration
 * 
 * @param config Pointer to store the configuration
 * @return true if configuration found, false otherwise
 */
bool email_get_config(EmailConfig *config);

/**
 * Send email notification after successful task execution
 * 
 * @param task Pointer to the task
 * @param exit_code Exit code of the task execution
 * @return true on success, false on failure
 */
bool email_send_task_notification(const Task *task, int exit_code);

#endif /* EMAIL_H */ 