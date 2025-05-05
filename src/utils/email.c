#include "../../include/email.h"
#include "../../include/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#define DEFAULT_CONFIG_PATH "data/config.json"

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
    email_config.email_enabled = false;
    
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
            email_config.email_enabled = cJSON_IsTrue(enabled);
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
        cJSON_AddBoolToObject(email, "enabled", email_config.email_enabled);
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
                       const char *smtp_server, int smtp_port, const char *config_path) {
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
    
    email_config.email_enabled = enabled;
    printf("Debug: Set email_enabled to %d\n", enabled);
    
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
    cJSON_AddBoolToObject(email, "enabled", email_config.email_enabled);
    printf("Debug: Added enabled=%d to JSON\n", email_config.email_enabled);
    
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

// Helper struct for curl
struct UploadStatus {
    const char *data;
    size_t size;
};

// CURL callback function to provide data for sending
static size_t payload_source(void *ptr, size_t size, size_t nmemb, void *userp) {
    struct UploadStatus *upload_ctx = (struct UploadStatus *)userp;
    
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
    if (!email_config.email_enabled) {
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
    
    // Only send notification if task was successful
    if (exit_code != 0) {
        log_message(LOG_INFO, "Task %d (%s) failed with exit code %d, not sending email notification",
                   task->id, task->name, exit_code);
        return false;
    }
    
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
    snprintf(subject, sizeof(subject), "Task Scheduler: Task %d (%s) executed successfully", 
             task->id, task->name);
    
    char from_header[512];
    snprintf(from_header, sizeof(from_header), "From: Task Scheduler <%s>", email_config.email_address);
    
    char to_header[512];
    snprintf(to_header, sizeof(to_header), "To: <%s>", email_config.email_address);
    
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
             "Exit Code: %d (Success)\n"
             "Next Scheduled Run: %s\n\n"
             "Task Details:\n"
             "Command: %s\n"
             "Working Directory: %s\n\n"
             "This is an automated message from Task Scheduler.",
             task->id, task->name, last_run_time_str, exit_code, 
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
    struct UploadStatus upload_ctx;
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
    recipients = curl_slist_append(recipients, email_config.email_address);
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
    
    log_message(LOG_INFO, "Email notification sent successfully for task %d (%s)",
               task->id, task->name);
    return true;
} 