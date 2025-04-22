/**
 * @file ai.c
 * @brief Triển khai các hàm liên quan đến xử lý AI
 */

#include "../../include/ai.h"
#include "../../include/utils.h"
#include <curl/curl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <cjson/cJSON.h>
#include <errno.h>

#define DEFAULT_CONFIG_PATH "data/config.json"

// Cấu trúc để lưu trữ cấu hình AI
static struct {
    char api_key[256];
    bool loaded;
} ai_config = {
    .api_key = "",
    .loaded = false
};

// Hàm callback để xử lý dữ liệu từ cURL
static size_t ai_write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t real_size = size * nmemb;
    AIResponse *mem = (AIResponse *)userp;
    
    char *ptr = realloc(mem->data, mem->size + real_size + 1);
    if (!ptr) {
        log_message(LOG_ERROR, "Failed to allocate memory for API response");
        return 0;
    }
    
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, real_size);
    mem->size += real_size;
    mem->data[mem->size] = 0;
    
    return real_size;
}

char* ai_call_deepseek_api(const char *api_key, const char *system_state_json, const char *user_prompt) {
    if (!api_key || !system_state_json || !user_prompt) {
        log_message(LOG_ERROR, "Invalid parameters for Deepseek API call");
        return NULL;
    }
    
    // Initialize cURL
    CURL *curl = curl_easy_init();
    if (!curl) {
        log_message(LOG_ERROR, "Failed to initialize cURL");
        return NULL;
    }
    
    // Initialize response structure
    AIResponse response = { .data = malloc(1), .size = 0 };
    if (!response.data) {
        log_message(LOG_ERROR, "Failed to allocate memory for API response");
        curl_easy_cleanup(curl);
        return NULL;
    }
    response.data[0] = '\0';
    
    // Create the request JSON
    cJSON *request = cJSON_CreateObject();
    cJSON *messages = cJSON_CreateArray();
    
    // System message
    cJSON *system_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(system_msg, "role", "system");
    cJSON_AddStringToObject(system_msg, "content", 
                          "You are an AI task executor that helps manage system resources by generating shell commands. "
                          "You will be provided with system metrics and a task goal. Your job is to generate a single valid "
                          "shell command or a small script that addresses the goal based on the current system state. "
                          "Only output the exact command to run, nothing else - no explanations, no markdown formatting, "
                          "just the raw command or script to execute.");
    cJSON_AddItemToArray(messages, system_msg);
    
    // User message with system state
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    char prompt_buffer[4096];
    snprintf(prompt_buffer, sizeof(prompt_buffer), 
            "Current system metrics:\n%s\n\nTask goal: %s\n\nGenerate a single shell command to accomplish this task:",
            system_state_json, user_prompt);
    cJSON_AddStringToObject(user_msg, "content", prompt_buffer);
    cJSON_AddItemToArray(messages, user_msg);
    
    cJSON_AddItemToObject(request, "messages", messages);
    cJSON_AddStringToObject(request, "model", "deepseek-chat");
    cJSON_AddNumberToObject(request, "temperature", 0.7);
    cJSON_AddNumberToObject(request, "max_tokens", 1024);
    
    char *request_str = cJSON_Print(request);
    cJSON_Delete(request);
    
    if (!request_str) {
        log_message(LOG_ERROR, "Failed to create JSON request");
        free(response.data);
        curl_easy_cleanup(curl);
        return NULL;
    }
    
    // Set up the headers
    struct curl_slist *headers = NULL;
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    // Set up cURL options
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.deepseek.com/v1/chat/completions");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_str);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ai_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    // Log the request (but mask the API key)
    log_message(LOG_DEBUG, "Calling Deepseek API with request: %s", request_str);
    
    // Perform the request
    CURLcode res = curl_easy_perform(curl);
    
    // Clean up
    free(request_str);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        log_message(LOG_ERROR, "cURL request failed: %s", curl_easy_strerror(res));
        free(response.data);
        return NULL;
    }
    
    // Log the response (for debugging)
    log_message(LOG_DEBUG, "Received response from Deepseek API: %s", response.data);
    
    return response.data;
}

char* ai_parse_deepseek_response(const char *response_json) {
    if (!response_json) {
        log_message(LOG_ERROR, "Invalid response JSON");
        return NULL;
    }
    
    cJSON *root = cJSON_Parse(response_json);
    if (!root) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr) {
            log_message(LOG_ERROR, "Error parsing JSON: %s", error_ptr);
        } else {
            log_message(LOG_ERROR, "Error parsing JSON");
        }
        return NULL;
    }
    
    // Navigate to the content field: choices[0].message.content
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        log_message(LOG_ERROR, "Invalid response format: missing or empty 'choices' array");
        cJSON_Delete(root);
        return NULL;
    }
    
    cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
    if (!first_choice) {
        log_message(LOG_ERROR, "Invalid response format: empty first choice");
        cJSON_Delete(root);
        return NULL;
    }
    
    cJSON *message = cJSON_GetObjectItem(first_choice, "message");
    if (!message) {
        log_message(LOG_ERROR, "Invalid response format: missing 'message'");
        cJSON_Delete(root);
        return NULL;
    }
    
    cJSON *content = cJSON_GetObjectItem(message, "content");
    if (!content || !cJSON_IsString(content) || !content->valuestring) {
        log_message(LOG_ERROR, "Invalid response format: missing or invalid 'content'");
        cJSON_Delete(root);
        return NULL;
    }
    
    // Extract the command string
    char *command = strdup(content->valuestring);
    cJSON_Delete(root);
    
    if (!command) {
        log_message(LOG_ERROR, "Failed to allocate memory for command");
        return NULL;
    }
    
    // Trim leading and trailing whitespace
    char *start = command;
    while (*start && isspace((unsigned char)*start)) start++;
    
    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) *end-- = '\0';
    
    // If the command starts with triple backticks (markdown code block), remove them
    if (strncmp(start, "```", 3) == 0) {
        start += 3;
        // Skip the language identifier if present
        while (*start && !isspace((unsigned char)*start)) start++;
        while (*start && isspace((unsigned char)*start)) start++;
        
        // Find the closing backticks
        char *closing = strstr(start, "```");
        if (closing) {
            *closing = '\0';
        }
    }
    
    // If the command isn't at the beginning of the buffer, move it
    if (start != command) {
        char *result = strdup(start);
        free(command);
        return result;
    }
    
    return command;
}

bool ai_load_config(const char *config_path) {
    if (!config_path) {
        config_path = DEFAULT_CONFIG_PATH;
    }
    
    FILE *file = fopen(config_path, "r");
    if (!file) {
        // Nếu file không tồn tại, tạo file mới với cấu hình mặc định
        if (errno == ENOENT) {
            log_message(LOG_INFO, "Config file not found at %s. Creating default config.", config_path);
            return ai_save_default_config(config_path);
        }
        
        log_message(LOG_ERROR, "Failed to open config file: %s (%s)", config_path, strerror(errno));
        return false;
    }
    
    // Đọc nội dung file
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size == 0) {
        fclose(file);
        log_message(LOG_ERROR, "Config file is empty: %s", config_path);
        return false;
    }
    
    char *json_str = (char *)malloc(file_size + 1);
    if (!json_str) {
        fclose(file);
        log_message(LOG_ERROR, "Memory allocation failed when reading config");
        return false;
    }
    
    size_t read_size = fread(json_str, 1, file_size, file);
    json_str[read_size] = '\0';
    fclose(file);
    
    // Parse JSON
    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    
    if (!root) {
        log_message(LOG_ERROR, "Failed to parse config file as JSON: %s", config_path);
        return false;
    }
    
    // Đọc API key
    cJSON *api_key = cJSON_GetObjectItemCaseSensitive(root, "api_key");
    if (cJSON_IsString(api_key) && api_key->valuestring) {
        safe_strcpy(ai_config.api_key, api_key->valuestring, sizeof(ai_config.api_key));
    }
    
    cJSON_Delete(root);
    ai_config.loaded = true;
    
    log_message(LOG_INFO, "AI config loaded successfully from %s", config_path);
    return true;
}

bool ai_save_default_config(const char *config_path) {
    if (!config_path) {
        config_path = DEFAULT_CONFIG_PATH;
    }
    
    // Đảm bảo thư mục cha tồn tại
    char dir_path[256];
    safe_strcpy(dir_path, config_path, sizeof(dir_path));
    
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (!ensure_directory_exists(dir_path)) {
            return false;
        }
    }
    
    // Tạo JSON cấu hình
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        log_message(LOG_ERROR, "Failed to create JSON object for config");
        return false;
    }
    
    cJSON_AddStringToObject(root, "api_key", "YOUR_API_KEY_HERE");
    
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (!json_str) {
        log_message(LOG_ERROR, "Failed to convert config to JSON string");
        return false;
    }
    
    // Ghi file
    FILE *file = fopen(config_path, "w");
    if (!file) {
        log_message(LOG_ERROR, "Failed to create config file: %s (%s)", config_path, strerror(errno));
        free(json_str);
        return false;
    }
    
    size_t written = fwrite(json_str, 1, strlen(json_str), file);
    free(json_str);
    fclose(file);
    
    if (written == 0) {
        log_message(LOG_ERROR, "Failed to write to config file: %s", config_path);
        return false;
    }
    
    log_message(LOG_INFO, "Default AI config saved to %s", config_path);
    return true;
}

bool ai_update_api_key(const char *api_key, const char *config_path) {
    if (!api_key) {
        return false;
    }
    
    if (!config_path) {
        config_path = DEFAULT_CONFIG_PATH;
    }
    
    // Cập nhật trong bộ nhớ
    safe_strcpy(ai_config.api_key, api_key, sizeof(ai_config.api_key));
    
    // Đọc cấu hình hiện tại nếu chưa được tải
    if (!ai_config.loaded) {
        ai_load_config(config_path);
    }
    
    // Tạo JSON cấu hình
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        log_message(LOG_ERROR, "Failed to create JSON object for config");
        return false;
    }
    
    cJSON_AddStringToObject(root, "api_key", ai_config.api_key);
    
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (!json_str) {
        log_message(LOG_ERROR, "Failed to convert config to JSON string");
        return false;
    }
    
    // Ghi file
    FILE *file = fopen(config_path, "w");
    if (!file) {
        log_message(LOG_ERROR, "Failed to open config file for writing: %s (%s)", config_path, strerror(errno));
        free(json_str);
        return false;
    }
    
    size_t written = fwrite(json_str, 1, strlen(json_str), file);
    free(json_str);
    fclose(file);
    
    if (written == 0) {
        log_message(LOG_ERROR, "Failed to write to config file: %s", config_path);
        return false;
    }
    
    log_message(LOG_INFO, "AI API key updated successfully in %s", config_path);
    return true;
}

const char* ai_get_api_key() {
    // Kiểm tra xem cấu hình đã được tải chưa
    if (!ai_config.loaded) {
        ai_load_config(NULL);
    }
    
    // Nếu API key đã được cấu hình, trả về từ cấu hình
    if (ai_config.api_key[0] != '\0' && strcmp(ai_config.api_key, "YOUR_API_KEY_HERE") != 0) {
        return ai_config.api_key;
    }
    
    // Nếu không có hoặc không hợp lệ, kiểm tra trong biến môi trường (để tương thích ngược)
    const char *env_api_key = getenv("DEEPSEEK_API_KEY");
    if (env_api_key && strlen(env_api_key) > 0) {
        // Cập nhật vào cấu hình để sử dụng sau này
        ai_update_api_key(env_api_key, NULL);
        return ai_config.api_key; // Trả về từ cấu hình để đảm bảo nhất quán
    }
    
    // Thử đọc lại cấu hình một lần nữa để đảm bảo
    ai_load_config(NULL);
    
    // Kiểm tra lại sau khi đọc lại
    if (ai_config.api_key[0] != '\0' && strcmp(ai_config.api_key, "YOUR_API_KEY_HERE") != 0) {
        return ai_config.api_key;
    }
    
    log_message(LOG_ERROR, "Không thể tìm thấy API key trong cấu hình hoặc biến môi trường");
    return NULL;
}

char* ai_generate_command(const SystemMetrics *metrics, const char *goal) {
    if (!metrics || !goal) {
        log_message(LOG_ERROR, "Invalid parameters for AI command generation");
        return NULL;
    }
    
    // Xác định API key mỗi lần, đảm bảo luôn đọc từ file cấu hình
    ai_load_config(NULL);
    
    // Get API key
    const char *api_key = ai_get_api_key();
    if (!api_key) {
        log_message(LOG_ERROR, "DeepSeek API key not found in config file or environment variable");
        log_message(LOG_INFO, "Please set your API key using 'taskscheduler set-api-key YOUR_API_KEY' command");
        return NULL;
    }
    
    // Convert metrics to JSON
    char *metrics_json = system_metrics_to_json(metrics);
    if (!metrics_json) {
        log_message(LOG_ERROR, "Failed to convert system metrics to JSON");
        return NULL;
    }
    
    // Call Deepseek API
    char *api_response = ai_call_deepseek_api(api_key, metrics_json, goal);
    free(metrics_json); // Free the metrics JSON
    
    if (!api_response) {
        log_message(LOG_ERROR, "Failed to call Deepseek API");
        return NULL;
    }
    
    // Parse the response to get the command
    char *command = ai_parse_deepseek_response(api_response);
    free(api_response); // Free the API response
    
    if (!command || strlen(command) == 0) {
        log_message(LOG_ERROR, "Failed to parse Deepseek API response or empty command returned");
        if (command) free(command);
        return NULL;
    }
    
    // Log the generated command
    log_message(LOG_INFO, "AI generated command: %s", command);
    
    return command;
} 