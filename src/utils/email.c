#include "../../include/email.h"
#include "../../include/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#define DEFAULT_CONFIG_PATH "data/config.json"

// Helper struct for curl
struct upload_status {
    const char *data;
    size_t size;
};

// Get current time as string
char* get_current_time_str(void) {
    static char buffer[64];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S %z", tm_info);
    return buffer;
}

// Global email configuration
static EmailConfig email_config;
static bool is_initialized = false;

bool email_init(const char *config_path) {
    if (is_initialized) {
        return true;
    }
    
    // Set default values
    memset(&email_config, 0, sizeof(EmailConfig));
    email_config.smtp_port = 587; // Default TLS port
    email_config.enabled = false;
    
    const char *path = config_path ? config_path : DEFAULT_CONFIG_PATH;
    
    // Try to load from config file
    FILE *file = fopen(path, "r");
    if (!file) {
        // Config file doesn't exist, create default config
        return email_save_default_config(path);
    }
    
    // Read file content
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *buffer = (char *)malloc(file_size + 1);
    if (!buffer) {
        fclose(file);
        return false;
    }
    
    size_t read_size = fread(buffer, 1, file_size, file);
    fclose(file);
    
    if (read_size != (size_t)file_size) {
        free(buffer);
        return false;
    }
    
    buffer[file_size] = '\0';
    
    // Parse JSON
    cJSON *json = cJSON_Parse(buffer);
    free(buffer);
    
    if (!json) {
        return false;
    }
    
    // Extract email configuration
    cJSON *email = cJSON_GetObjectItem(json, "email");
    if (email) {
        cJSON *address = cJSON_GetObjectItem(email, "email_address");
        cJSON *password = cJSON_GetObjectItem(email, "email_password");
        cJSON *server = cJSON_GetObjectItem(email, "smtp_server");
        cJSON *port = cJSON_GetObjectItem(email, "smtp_port");
        cJSON *enabled = cJSON_GetObjectItem(email, "enabled");
        cJSON *recipient = cJSON_GetObjectItem(email, "recipient_email");
        
        if (address && cJSON_IsString(address)) {
            safe_strcpy(email_config.email_address, address->valuestring, sizeof(email_config.email_address));
        }
        
        if (password && cJSON_IsString(password)) {
            safe_strcpy(email_config.email_password, password->valuestring, sizeof(email_config.email_password));
        }
        
        if (server && cJSON_IsString(server)) {
            safe_strcpy(email_config.smtp_server, server->valuestring, sizeof(email_config.smtp_server));
        }
        
        if (port && cJSON_IsNumber(port)) {
            email_config.smtp_port = port->valueint;
        }
        
        if (enabled && cJSON_IsBool(enabled)) {
            email_config.enabled = cJSON_IsTrue(enabled);
        }
        
        if (recipient && cJSON_IsString(recipient)) {
            safe_strcpy(email_config.recipient_email, recipient->valuestring, sizeof(email_config.recipient_email));
        } else if (address && cJSON_IsString(address)) {
            // If recipient is not specified, use sender address by default
            safe_strcpy(email_config.recipient_email, address->valuestring, sizeof(email_config.recipient_email));
        }
    }
    
    cJSON_Delete(json);
    is_initialized = true;
    return true;
}

bool email_save_default_config(const char *config_path) {
    const char *path = config_path ? config_path : DEFAULT_CONFIG_PATH;
    
    // Try to open existing config first
    FILE *file = fopen(path, "r");
    cJSON *json = NULL;
    
    if (file) {
        // Read existing config
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        fseek(file, 0, SEEK_SET);
        
        char *buffer = (char *)malloc(file_size + 1);
        if (!buffer) {
            fclose(file);
            return false;
        }
        
        size_t read_size = fread(buffer, 1, file_size, file);
        fclose(file);
        
        if (read_size == (size_t)file_size) {
            buffer[file_size] = '\0';
            json = cJSON_Parse(buffer);
        }
        
        free(buffer);
    }
    
    // Create new JSON object if we couldn't read existing one
    if (!json) {
        json = cJSON_CreateObject();
        if (!json) {
            return false;
        }
    }
    
    // Create or update email section
    cJSON *email = cJSON_GetObjectItem(json, "email");
    if (!email) {
        email = cJSON_CreateObject();
        if (!email) {
            cJSON_Delete(json);
            return false;
        }
        cJSON_AddItemToObject(json, "email", email);
    } else if (!cJSON_IsObject(email)) {
        cJSON_DeleteItemFromObject(json, "email");
        email = cJSON_CreateObject();
        if (!email) {
            cJSON_Delete(json);
            return false;
        }
        cJSON_AddItemToObject(json, "email", email);
    }
    
    // Set default values if not already present
    if (!cJSON_GetObjectItem(email, "email_address")) {
        cJSON_AddStringToObject(email, "email_address", email_config.email_address);
    }
    
    if (!cJSON_GetObjectItem(email, "email_password")) {
        cJSON_AddStringToObject(email, "email_password", email_config.email_password);
    }
    
    if (!cJSON_GetObjectItem(email, "smtp_server")) {
        cJSON_AddStringToObject(email, "smtp_server", email_config.smtp_server);
    }
    
    if (!cJSON_GetObjectItem(email, "smtp_port")) {
        cJSON_AddNumberToObject(email, "smtp_port", email_config.smtp_port);
    }
    
    if (!cJSON_GetObjectItem(email, "enabled")) {
        cJSON_AddBoolToObject(email, "enabled", email_config.enabled);
    }
    
    if (!cJSON_GetObjectItem(email, "recipient_email")) {
        // Default to sender address if not set
        const char *recipient = email_config.recipient_email[0] ? 
                              email_config.recipient_email : 
                              email_config.email_address;
        cJSON_AddStringToObject(email, "recipient_email", recipient);
    }
    
    // Write to file
    char *json_str = cJSON_Print(json);
    if (!json_str) {
        cJSON_Delete(json);
        return false;
    }
    
    file = fopen(path, "w");
    if (!file) {
        free(json_str);
        cJSON_Delete(json);
        return false;
    }
    
    fprintf(file, "%s", json_str);
    fclose(file);
    
    free(json_str);
    cJSON_Delete(json);
    
    is_initialized = true;
    return true;
}

bool email_update_config(const char *email_address, const char *email_password, 
                       const char *smtp_server, int smtp_port,
                       const char *recipient_email, const char *config_path) {
    if (!is_initialized && !email_init(config_path)) {
        return false;
    }
    
    // Update configuration
    if (email_address) {
        safe_strcpy(email_config.email_address, email_address, sizeof(email_config.email_address));
    }
    
    if (email_password) {
        safe_strcpy(email_config.email_password, email_password, sizeof(email_config.email_password));
    }
    
    if (smtp_server) {
        safe_strcpy(email_config.smtp_server, smtp_server, sizeof(email_config.smtp_server));
    }
    
    if (smtp_port > 0) {
        email_config.smtp_port = smtp_port;
    }
    
    if (recipient_email) {
        safe_strcpy(email_config.recipient_email, recipient_email, sizeof(email_config.recipient_email));
    } else if (email_address && email_config.recipient_email[0] == '\0') {
        // If recipient is not specified but we have a new sender, use it as recipient too
        safe_strcpy(email_config.recipient_email, email_address, sizeof(email_config.recipient_email));
    }
    
    const char *path = config_path ? config_path : DEFAULT_CONFIG_PATH;
    
    // Try to open existing config
    FILE *file = fopen(path, "r");
    cJSON *json = NULL;
    
    if (file) {
        // Read existing config
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        fseek(file, 0, SEEK_SET);
        
        char *buffer = (char *)malloc(file_size + 1);
        if (!buffer) {
            fclose(file);
            return false;
        }
        
        size_t read_size = fread(buffer, 1, file_size, file);
        fclose(file);
        
        if (read_size == (size_t)file_size) {
            buffer[file_size] = '\0';
            json = cJSON_Parse(buffer);
        }
        
        free(buffer);
    }
    
    // Create new JSON object if we couldn't read existing one
    if (!json) {
        json = cJSON_CreateObject();
        if (!json) {
            return false;
        }
    }
    
    // Create or update email section
    cJSON *email = cJSON_GetObjectItem(json, "email");
    if (!email) {
        email = cJSON_CreateObject();
        if (!email) {
            cJSON_Delete(json);
            return false;
        }
        cJSON_AddItemToObject(json, "email", email);
    } else if (!cJSON_IsObject(email)) {
        cJSON_DeleteItemFromObject(json, "email");
        email = cJSON_CreateObject();
        if (!email) {
            cJSON_Delete(json);
            return false;
        }
        cJSON_AddItemToObject(json, "email", email);
    }
    
    // Update email configuration
    cJSON_DeleteItemFromObject(email, "email_address");
    cJSON_AddStringToObject(email, "email_address", email_config.email_address);
    
    cJSON_DeleteItemFromObject(email, "email_password");
    cJSON_AddStringToObject(email, "email_password", email_config.email_password);
    
    cJSON_DeleteItemFromObject(email, "smtp_server");
    cJSON_AddStringToObject(email, "smtp_server", email_config.smtp_server);
    
    cJSON_DeleteItemFromObject(email, "smtp_port");
    cJSON_AddNumberToObject(email, "smtp_port", email_config.smtp_port);
    
    cJSON_DeleteItemFromObject(email, "recipient_email");
    cJSON_AddStringToObject(email, "recipient_email", email_config.recipient_email);
    
    // Write to file
    char *json_str = cJSON_Print(json);
    if (!json_str) {
        cJSON_Delete(json);
        return false;
    }
    
    file = fopen(path, "w");
    if (!file) {
        free(json_str);
        cJSON_Delete(json);
        return false;
    }
    
    fprintf(file, "%s", json_str);
    fclose(file);
    
    free(json_str);
    cJSON_Delete(json);
    
    return true;
}

bool email_set_enabled(bool enabled, const char *config_path) {
    printf("Debug: Entering email_set_enabled with enabled=%d\n", enabled);
    
    if (!is_initialized && !email_init(config_path)) {
        printf("Debug: Failed to initialize email configuration\n");
        return false;
    }
    
    email_config.enabled = enabled;
    printf("Debug: Set enabled to %d\n", enabled);
    
    const char *path = config_path ? config_path : DEFAULT_CONFIG_PATH;
    printf("Debug: Using config path: %s\n", path);
    
    // Try to open existing config
    FILE *file = fopen(path, "r");
    cJSON *json = NULL;
    
    if (file) {
        printf("Debug: Successfully opened config file for reading\n");
        // Read existing config
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        fseek(file, 0, SEEK_SET);
        
        char *buffer = (char *)malloc(file_size + 1);
        if (!buffer) {
            printf("Debug: Failed to allocate memory for config file buffer\n");
            fclose(file);
            return false;
        }
        
        size_t read_size = fread(buffer, 1, file_size, file);
        fclose(file);
        
        if (read_size == (size_t)file_size) {
            buffer[file_size] = '\0';
            json = cJSON_Parse(buffer);
            if (json) {
                printf("Debug: Successfully parsed JSON config\n");
            } else {
                printf("Debug: Failed to parse JSON config: %s\n", cJSON_GetErrorPtr());
            }
        } else {
            printf("Debug: Read size (%zu) doesn't match file size (%ld)\n", read_size, file_size);
        }
        
        free(buffer);
    } else {
        printf("Debug: Could not open config file for reading, creating new one\n");
    }
    
    // Create new JSON object if we couldn't read existing one
    if (!json) {
        json = cJSON_CreateObject();
        if (!json) {
            printf("Debug: Failed to create new JSON object\n");
            return false;
        }
    }
    
    // Create or update email section
    cJSON *email = cJSON_GetObjectItem(json, "email");
    if (!email) {
        printf("Debug: Creating new email section in config\n");
        email = cJSON_CreateObject();
        if (!email) {
            printf("Debug: Failed to create email JSON object\n");
            cJSON_Delete(json);
            return false;
        }
        cJSON_AddItemToObject(json, "email", email);
    } else if (!cJSON_IsObject(email)) {
        printf("Debug: Replacing non-object email section in config\n");
        cJSON_DeleteItemFromObject(json, "email");
        email = cJSON_CreateObject();
        if (!email) {
            printf("Debug: Failed to create email JSON object after deletion\n");
            cJSON_Delete(json);
            return false;
        }
        cJSON_AddItemToObject(json, "email", email);
    } else {
        printf("Debug: Updating existing email section in config\n");
    }
    
    // Update enabled flag
    cJSON_DeleteItemFromObject(email, "enabled");
    cJSON_AddBoolToObject(email, "enabled", email_config.enabled);
    printf("Debug: Added enabled=%d to JSON\n", email_config.enabled);
    
    // Print the entire JSON structure
    char *debug_json = cJSON_Print(json);
    if (debug_json) {
        printf("Debug: JSON to be written:\n%s\n", debug_json);
        free(debug_json);
    }
    
    // Write to file
    char *json_str = cJSON_Print(json);
    if (!json_str) {
        printf("Debug: Failed to serialize JSON to string\n");
        cJSON_Delete(json);
        return false;
    }
    
    file = fopen(path, "w");
    if (!file) {
        printf("Debug: Failed to open config file for writing: %s\n", path);
        free(json_str);
        cJSON_Delete(json);
        return false;
    }
    
    size_t written = fprintf(file, "%s", json_str);
    printf("Debug: Wrote %zu bytes to config file\n", written);
    fclose(file);
    
    free(json_str);
    cJSON_Delete(json);
    
    printf("Debug: Successfully completed email_set_enabled\n");
    return true;
}

bool email_get_config(EmailConfig *config) {
    if (!is_initialized && !email_init(NULL)) {
        return false;
    }
    
    if (config) {
        memcpy(config, &email_config, sizeof(EmailConfig));
        return true;
    }
    
    return false;
}

// CURL callback function to provide data for sending
static size_t payload_source(void *ptr, size_t size, size_t nmemb, void *userp) {
    struct upload_status *upload_ctx = (struct upload_status *)userp;
    
    if ((size == 0) || (nmemb == 0) || ((size*nmemb) < 1)) {
        return 0;
    }
    
    size_t len = upload_ctx->size;
    if (len > size * nmemb) {
        len = size * nmemb;
    }
    
    if (len == 0) {
        return 0;
    }
    
    memcpy(ptr, upload_ctx->data, len);
    upload_ctx->data += len;
    upload_ctx->size -= len;
    
    return len;
}

bool email_send_task_notification(const Task *task, int exit_code) {
    if (!is_initialized && !email_init(NULL)) {
        return false;
    }
    
    // Check if email notifications are enabled
    if (!email_config.enabled) {
        log_message(LOG_INFO, "Email notifications are disabled");
        return false;
    }
    
    // Check if email configuration is valid
    if (strlen(email_config.email_address) == 0 || 
        strlen(email_config.email_password) == 0 || 
        strlen(email_config.smtp_server) == 0 || 
        email_config.smtp_port <= 0) {
        log_message(LOG_ERROR, "Invalid email configuration");
        return false;
    }
    
    // Check if task is valid
    if (!task) {
        log_message(LOG_ERROR, "Task is NULL");
        return false;
    }
    
    // Get recipient email - use recipient_email if set, otherwise use sender address
    const char *recipient = email_config.recipient_email[0] != '\0' ? 
                            email_config.recipient_email : 
                            email_config.email_address;

    log_message(LOG_INFO, "Sending notification to: %s", recipient);
    
    // Prepare email content
    char date_str[64];
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(date_str, sizeof(date_str), "%a, %d %b %Y %H:%M:%S %z", &tm_info);
    
    char last_run_time_str[64] = "N/A";
    if (task->last_run_time > 0) {
        struct tm last_run_tm;
        localtime_r(&task->last_run_time, &last_run_tm);
        strftime(last_run_time_str, sizeof(last_run_time_str), "%Y-%m-%d %H:%M:%S", &last_run_tm);
    }
    
    char next_run_time_str[64] = "N/A";
    if (task->next_run_time > 0) {
        struct tm next_run_tm;
        localtime_r(&task->next_run_time, &next_run_tm);
        strftime(next_run_time_str, sizeof(next_run_time_str), "%Y-%m-%d %H:%M:%S", &next_run_tm);
    }
    
    // Create email headers and body
    char subject[256];
    if (exit_code == 0) {
        snprintf(subject, sizeof(subject), "Task Scheduler: Task %d (%s) executed successfully", 
                task->id, task->name);
    } else {
        snprintf(subject, sizeof(subject), "Task Scheduler: Task %d (%s) execution completed with exit code %d", 
                task->id, task->name, exit_code);
    }
    
    char from_header[512];
    snprintf(from_header, sizeof(from_header), "From: Task Scheduler <%s>", email_config.email_address);
    
    char to_header[512];
    snprintf(to_header, sizeof(to_header), "To: <%s>", recipient);
    
    char subject_header[512];
    snprintf(subject_header, sizeof(subject_header), "Subject: %s", subject);
    
    char date_header[512];
    snprintf(date_header, sizeof(date_header), "Date: %s", date_str);
    
    char message_id_header[512];
    snprintf(message_id_header, sizeof(message_id_header), 
             "Message-ID: <task%d.%ld.%s>", task->id, (long)time(NULL), email_config.email_address);
    
    // Build the email body
    char body[4096];
    snprintf(body, sizeof(body),
             "Task execution report:\n\n"
             "Task ID: %d\n"
             "Task Name: %s\n"
             "Execution Time: %s\n"
             "Exit Code: %d (%s)\n"
             "Next Scheduled Run: %s\n\n"
             "Task Details:\n"
             "Command: %s\n"
             "Working Directory: %s\n\n"
             "This is an automated message from Task Scheduler.",
             task->id, task->name, last_run_time_str, exit_code, 
             exit_code == 0 ? "Success" : "Failed", 
             next_run_time_str, task->command, task->working_dir);
    
    // Combined message (headers + body)
    char email_data[8192];
    snprintf(email_data, sizeof(email_data),
             "%s\r\n%s\r\n%s\r\n%s\r\n%s\r\n"
             "MIME-Version: 1.0\r\n"
             "Content-Type: text/plain; charset=utf-8\r\n"
             "Content-Transfer-Encoding: 8bit\r\n"
             "\r\n%s\r\n",
             from_header, to_header, subject_header, date_header, message_id_header, body);
    
    // Initialize upload status
    struct upload_status upload_ctx;
    upload_ctx.data = email_data;
    upload_ctx.size = strlen(email_data);
    
    // Initialize libcurl
    CURL *curl = curl_easy_init();
    if (!curl) {
        log_message(LOG_ERROR, "Failed to initialize libcurl");
        return false;
    }
    
    // Set up URL
    char url[512];
    snprintf(url, sizeof(url), "smtp://%s:%d", email_config.smtp_server, email_config.smtp_port);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    
    // Set credentials
    curl_easy_setopt(curl, CURLOPT_USERNAME, email_config.email_address);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, email_config.email_password);
    
    // Set TLS for SMTP
    curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
    // Set up sender and recipient
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, email_config.email_address);
    
    struct curl_slist *recipients = NULL;
    recipients = curl_slist_append(recipients, recipient);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    
    // Set up callback function to provide the email data
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
    curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    
    // Verbose output for debugging (uncomment if needed)
    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    
    // Send the email
    CURLcode res = curl_easy_perform(curl);
    
    // Clean up
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        log_message(LOG_ERROR, "Failed to send email: %s", curl_easy_strerror(res));
        return false;
    }
    
    log_message(LOG_INFO, "Email notification sent successfully to %s for task %d (%s)",
               recipient, task->id, task->name);
    return true;
}

bool email_set_recipient(const char *recipient_email, const char *config_path) {
    printf("Setting email recipient to: %s\n", recipient_email);
    
    if (!is_initialized && !email_init(config_path)) {
        printf("Failed to initialize email\n");
        return false;
    }
    
    if (!recipient_email) {
        printf("Recipient email is NULL\n");
        return false;
    }
    
    const char *path = config_path ? config_path : DEFAULT_CONFIG_PATH;
    printf("Using configuration path: %s\n", path);
    
    // Update configuration
    safe_strcpy(email_config.recipient_email, recipient_email, sizeof(email_config.recipient_email));
    
    // Try to open existing config
    FILE *file = fopen(path, "r");
    cJSON *json = NULL;
    
    if (file) {
        printf("Reading existing configuration file\n");
        // Read existing config
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        fseek(file, 0, SEEK_SET);
        
        char *buffer = (char *)malloc(file_size + 1);
        if (!buffer) {
            printf("Failed to allocate memory for file content\n");
            fclose(file);
            return false;
        }
        
        size_t read_size = fread(buffer, 1, file_size, file);
        fclose(file);
        
        if (read_size == (size_t)file_size) {
            buffer[file_size] = '\0';
            json = cJSON_Parse(buffer);
            if (!json) {
                printf("Failed to parse JSON: %s\n", cJSON_GetErrorPtr());
            }
        } else {
            printf("Failed to read entire file: %zu of %ld bytes read\n", read_size, file_size);
        }
        
        free(buffer);
    } else {
        printf("No existing configuration file found, creating new one\n");
    }
    
    // Create new JSON object if we couldn't read existing one
    if (!json) {
        json = cJSON_CreateObject();
        if (!json) {
            printf("Failed to create JSON object\n");
            return false;
        }
    }
    
    // Create or update email section
    cJSON *email = cJSON_GetObjectItem(json, "email");
    if (!email) {
        printf("Creating new email section\n");
        email = cJSON_CreateObject();
        if (!email) {
            printf("Failed to create email JSON object\n");
            cJSON_Delete(json);
            return false;
        }
        cJSON_AddItemToObject(json, "email", email);
    } else if (!cJSON_IsObject(email)) {
        printf("Replacing invalid email section\n");
        cJSON_DeleteItemFromObject(json, "email");
        email = cJSON_CreateObject();
        if (!email) {
            printf("Failed to create replacement email JSON object\n");
            cJSON_Delete(json);
            return false;
        }
        cJSON_AddItemToObject(json, "email", email);
    } else {
        printf("Updating existing email section\n");
    }
    
    // Update recipient email
    cJSON_DeleteItemFromObject(email, "recipient_email");
    cJSON_AddStringToObject(email, "recipient_email", email_config.recipient_email);
    printf("Added recipient_email to JSON: %s\n", email_config.recipient_email);
    
    // Write to file
    char *json_str = cJSON_Print(json);
    if (!json_str) {
        printf("Failed to serialize JSON\n");
        cJSON_Delete(json);
        return false;
    }
    
    file = fopen(path, "w");
    if (!file) {
        printf("Failed to open file for writing: %s\n", path);
        free(json_str);
        cJSON_Delete(json);
        return false;
    }
    
    size_t bytes_written = fprintf(file, "%s", json_str);
    printf("Wrote %zu bytes to configuration file\n", bytes_written);
    fclose(file);
    
    free(json_str);
    cJSON_Delete(json);
    
    printf("Successfully set recipient email to: %s\n", email_config.recipient_email);
    return true;
}

bool email_send_message(const char *subject, const char *body) {
    if (!is_initialized) {
        return false;
    }
    
    if (!email_config.enabled) {
        // Email notifications are disabled
        return true;
    }
    
    if (email_config.email_address[0] == '\0' || 
        email_config.email_password[0] == '\0' || 
        email_config.smtp_server[0] == '\0' || 
        email_config.smtp_port <= 0) {
        // Email configuration is incomplete
        return false;
    }
    
    // Check if recipient is configured, if not, use sender address
    const char *recipient = email_config.recipient_email[0] != '\0' ? 
                           email_config.recipient_email : 
                           email_config.email_address;
    
    printf("Sending email from %s to %s via %s:%d\n", 
           email_config.email_address, recipient,
           email_config.smtp_server, email_config.smtp_port);
    
    curl_global_init(CURL_GLOBAL_ALL);
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        curl_global_cleanup();
        return false;
    }
    
    struct curl_slist *recipients = NULL;
    recipients = curl_slist_append(recipients, recipient);
    
    // Create email content
    char *email_content = malloc(1024 + (body ? strlen(body) : 0));
    if (!email_content) {
        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return false;
    }
    
    char *time_str = get_current_time_str();
    sprintf(email_content,
            "Date: %s\r\n"
            "To: <%s>\r\n"
            "From: <%s>\r\n"
            "Subject: %s\r\n"
            "\r\n"
            "%s\r\n",
            time_str,
            recipient,
            email_config.email_address,
            subject ? subject : "Task Scheduler Notification",
            body ? body : "");
    
    struct upload_status upload_ctx;
    upload_ctx.data = email_content;
    upload_ctx.size = strlen(email_content);
    
    char url[256];
    snprintf(url, sizeof(url), "smtp://%s:%d", email_config.smtp_server, email_config.smtp_port);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, email_config.email_address);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
    curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_USERNAME, email_config.email_address);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, email_config.email_password);
    curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
    CURLcode res = curl_easy_perform(curl);
    
    free(email_content);
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    
    if (res != CURLE_OK) {
        printf("Failed to send email: %s\n", curl_easy_strerror(res));
        return false;
    }
    
    printf("Email sent successfully to %s\n", recipient);
    return true;
} 