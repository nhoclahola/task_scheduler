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
    
    // Log the full response for debugging
    log_message(LOG_DEBUG, "Raw API response: %s", response_json);
    
    cJSON *root = cJSON_Parse(response_json);
    if (!root) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr) {
            log_message(LOG_ERROR, "Error parsing JSON: %s", error_ptr);
        } else {
            log_message(LOG_ERROR, "Error parsing JSON");
        }
        
        // Try to find JSON directly in the response
        log_message(LOG_DEBUG, "Attempting to extract JSON object from response text");
        const char *start = strchr(response_json, '{');
        const char *end = strrchr(response_json, '}');
        
        if (start && end && end > start) {
            size_t json_len = end - start + 1;
            char *json_content = (char *)malloc(json_len + 1);
            
            if (json_content) {
                memcpy(json_content, start, json_len);
                json_content[json_len] = '\0';
                
                log_message(LOG_DEBUG, "Extracted potential JSON: %s", json_content);
                
                // Check if this is valid JSON
                cJSON *json_test = cJSON_Parse(json_content);
                if (json_test) {
                    // Check if it has the required fields for an AI task
                    if (cJSON_GetObjectItem(json_test, "is_script") && 
                        cJSON_GetObjectItem(json_test, "content") &&
                        cJSON_GetObjectItem(json_test, "schedule")) {
                        
                        log_message(LOG_INFO, "Successfully extracted valid task JSON from response");
                        cJSON_Delete(json_test);
                        return json_content;
                    }
                    cJSON_Delete(json_test);
                }
                
                free(json_content);
            }
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
    
    // Log the command for debugging
    log_message(LOG_DEBUG, "Parsed command: %s", start);
    
    // Step 1: If response is already a JSON object (starts with {), return as is
    if (start[0] == '{') {
        // Make sure it's valid JSON
        cJSON *json_test = cJSON_Parse(start);
        if (json_test) {
            cJSON_Delete(json_test);
            
            // If the command isn't at the beginning of the buffer, move it
            if (start != command) {
                char *result = strdup(start);
                free(command);
                return result;
            }
            
            return command;
        }
    }
    
    // If the command starts with triple backticks (markdown code block), remove them
    if (strncmp(start, "```", 3) == 0) {
        start += 3;
        
        // Check for "json" language identifier
        if (strncmp(start, "json", 4) == 0) {
            start += 4;
        }
        
        // Skip any whitespace
        while (*start && isspace((unsigned char)*start)) start++;
        
        // Find the closing backticks
        char *closing = strstr(start, "```");
        if (closing) {
            *closing = '\0';
        }
    }
    
    // Try to find JSON object in the content
    char *json_start = strchr(start, '{');
    char *json_end = strrchr(start, '}');
    
    if (json_start && json_end && json_end > json_start) {
        // Extract the JSON part
        size_t json_len = json_end - json_start + 1;
        char *json_part = (char *)malloc(json_len + 1);
        if (json_part) {
            strncpy(json_part, json_start, json_len);
            json_part[json_len] = '\0';
            
            // Test if valid JSON
            cJSON *json_test = cJSON_Parse(json_part);
            if (json_test) {
                cJSON_Delete(json_test);
                free(command);
                return json_part;
            }
            free(json_part);
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

bool ai_generate_shell_solution(const char *description, bool *is_script, 
                              char *cmd_or_script, size_t buffer_size) {
    if (!description || !is_script || !cmd_or_script || buffer_size == 0) {
        log_message(LOG_ERROR, "Invalid parameters for AI shell solution generation");
        return false;
    }
    
    // Xác định API key
    ai_load_config(NULL);
    const char *api_key = ai_get_api_key();
    if (!api_key) {
        log_message(LOG_ERROR, "DeepSeek API key not found in config file or environment variable");
        log_message(LOG_INFO, "Please set your API key using 'taskscheduler set-api-key YOUR_API_KEY' command");
        return false;
    }
    
    // Chuẩn bị system prompt để định hướng AI tạo shell solution
    const char *system_prompt = 
        "Vai trò của bạn là một chuyên gia Linux sẽ tạo ra chính xác một trong hai thứ sau dựa trên yêu cầu:\n"
        "1. Một câu lệnh shell đơn giản cho các nhiệm vụ đơn giản; hoặc\n"
        "2. Một shell script đầy đủ cho các nhiệm vụ phức tạp hơn.\n\n"
        "Hãy phân tích yêu cầu và quyết định xem nên tạo lệnh đơn giản hay script đầy đủ.\n"
        "Nếu tạo script, hãy bắt đầu bằng dòng #!/bin/bash hoặc shebang thích hợp và bao gồm xử lý lỗi.\n"
        "Ưu tiên các công cụ phổ biến như df, du, free, notify-send, v.v.\n"
        "KHÔNG bao gồm giải thích, chỉ cung cấp lệnh shell hoặc shell script hoàn chỉnh.\n"
        "Nếu yêu cầu cần gửi thông báo, hãy ưu tiên sử dụng notify-send.\n"
        "Không sử dụng đường dẫn tuyệt đối không cần thiết. Viết code đầy đủ, không rút gọn.";
    
    // Gọi API DeepSeek để tạo shell command/script
    char *api_response = ai_call_deepseek_api(api_key, system_prompt, description);
    if (!api_response) {
        log_message(LOG_ERROR, "Failed to call DeepSeek API for shell solution generation");
        return false;
    }
    
    // Phân tích phản hồi để lấy lệnh/script
    char *shell_solution = ai_parse_deepseek_response(api_response);
    free(api_response);
    
    if (!shell_solution || strlen(shell_solution) == 0) {
        log_message(LOG_ERROR, "Failed to parse DeepSeek API response or empty response returned");
        if (shell_solution) free(shell_solution);
        return false;
    }
    
    // Xác định xem đây là script hay lệnh đơn
    *is_script = false;
    
    // Kiểm tra xem có phải là script không (bắt đầu bằng #! hoặc chứa nhiều dòng)
    if (strncmp(shell_solution, "#!", 2) == 0 || strchr(shell_solution, '\n') != NULL) {
        *is_script = true;
    }
    
    // Sao chép kết quả vào buffer đầu ra
    if (strlen(shell_solution) >= buffer_size) {
        log_message(LOG_ERROR, "Shell solution exceeds buffer size (%zu vs %zu)", 
                   strlen(shell_solution), buffer_size);
        free(shell_solution);
        return false;
    }
    
    safe_strcpy(cmd_or_script, shell_solution, buffer_size);
    free(shell_solution);
    
    log_message(LOG_INFO, "AI generated %s: %s", 
               *is_script ? "script" : "command", 
               strlen(cmd_or_script) > 50 ? "(script too long to display in log)" : cmd_or_script);
    
    return true;
}

bool ai_generate_complete_task(const char *description, AIGeneratedTask *result) {
    if (!description || !result) {
        log_message(LOG_ERROR, "Invalid parameters for AI task generation");
        return false;
    }
    
    // Initialize result
    memset(result, 0, sizeof(AIGeneratedTask));
    
    // Load AI configuration and get API key
    ai_load_config(NULL);
    const char *api_key = ai_get_api_key();
    if (!api_key) {
        log_message(LOG_ERROR, "DeepSeek API key not found in config file or environment variable");
        log_message(LOG_INFO, "Please set your API key using 'taskscheduler set-api-key YOUR_API_KEY' command");
        return false;
    }
    
    // Prepare system prompt for complete task generation
    const char *system_prompt = 
        "You are a scheduling expert that generates complete task configurations for a Linux task scheduler.\n\n"
        "IMPORTANT: Your response MUST ONLY be a valid JSON object with NO additional text, code formatting, or explanations.\n\n"
        "Given a natural language task description, generate the following:\n"
        "1. A suitable shell command or script to accomplish the task\n"
        "2. An appropriate schedule (either cron expression or interval in minutes)\n"
        "3. A brief description of the schedule for human understanding\n"
        "4. A suggested task name that is concise but descriptive\n\n"
        
        "Return your answer in EXACTLY this JSON format with NO additional text:\n"
        "{\n"
        "  \"is_script\": true/false,\n"
        "  \"content\": \"the shell command or script content\",\n"
        "  \"is_cron\": true/false,\n"
        "  \"schedule\": \"cron expression or interval in minutes as a number\",\n"
        "  \"schedule_description\": \"runs at 9 AM every weekday\",\n"
        "  \"suggested_name\": \"backup_home_directory\"\n"
        "}\n\n"
        
        "For shell commands/scripts:\n"
        "- If the task is simple, use a single command (is_script: false)\n"
        "- If the task is complex, use a complete shell script with shebang (is_script: true)\n"
        "- For scripts, include proper error handling and use best practices\n"
        "- Prefer standard Linux tools like df, du, free, notify-send, etc.\n\n"
        
        "For scheduling:\n"
        "- If the task needs a specific time/day pattern, use a cron expression (is_cron: true)\n"
        "- If the task needs simple interval, use minutes (is_cron: false)\n"
        "- For complex schedules, use the 5-field cron format (minute hour day month weekday)\n"
        "- Parse the natural language description to identify time intervals ('every 5 minutes', 'daily at 2pm', etc.)\n\n"
        
        "Examples:\n"
        "1. For 'check disk space every 10 minutes and notify if less than 10% free', return ONLY:\n"
        "{\n"
        "  \"is_script\": true,\n"
        "  \"content\": \"#!/bin/bash\\n\\nDISK_PATH=\\\"/\\\"\\nTHRESHOLD=10\\n\\nFREE_PERCENT=$(df -h \\\"$DISK_PATH\\\" | grep -v Filesystem | awk '{print 100-$5}')\\n\\nif [ \\\"$FREE_PERCENT\\\" -lt \\\"$THRESHOLD\\\" ]; then\\n    notify-send \\\"Disk Space Alert\\\" \\\"Only $FREE_PERCENT% free on $DISK_PATH\\\"\\n    exit 1\\nfi\\n\\nexit 0\\n\",\n"
        "  \"is_cron\": false,\n"
        "  \"schedule\": 10,\n"
        "  \"schedule_description\": \"Runs every 10 minutes\",\n"
        "  \"suggested_name\": \"disk_space_monitor\"\n"
        "}\n\n"
        
        "2. For 'backup home directory to /mnt/backup every day at 2am', return ONLY:\n"
        "{\n"
        "  \"is_script\": true,\n"
        "  \"content\": \"#!/bin/bash\\n\\nBACKUP_SOURCE=\\\"$HOME\\\"\\nBACKUP_DEST=\\\"/mnt/backup\\\"\\nBACKUP_DATE=$(date +%Y-%m-%d)\\n\\n# Check if destination exists\\nif [ ! -d \\\"$BACKUP_DEST\\\" ]; then\\n    echo \\\"Backup destination not found: $BACKUP_DEST\\\"\\n    exit 1\\nfi\\n\\n# Create backup\\ntar -czf \\\"$BACKUP_DEST/home_backup_$BACKUP_DATE.tar.gz\\\" \\\"$BACKUP_SOURCE\\\" 2>/dev/null\\n\\nif [ $? -eq 0 ]; then\\n    echo \\\"Backup completed successfully\\\"\\n    exit 0\\nelse\\n    echo \\\"Backup failed\\\"\\n    exit 1\\nfi\\n\",\n"
        "  \"is_cron\": true,\n"
        "  \"schedule\": \"0 2 * * *\",\n"
        "  \"schedule_description\": \"Runs at 2:00 AM every day\",\n"
        "  \"suggested_name\": \"daily_home_backup\"\n"
        "}\n\n"
        
        "REMINDER: Your response must be a valid JSON object ONLY. Do not include ANY explanations, markdown formatting, or anything outside the JSON object.";
    
    // Call DeepSeek API to generate the complete task
    char *api_response = ai_call_deepseek_api(api_key, system_prompt, description);
    if (!api_response) {
        log_message(LOG_ERROR, "Failed to call DeepSeek API for complete task generation");
        return false;
    }
    
    // Parse the response
    char *json_response = ai_parse_deepseek_response(api_response);
    free(api_response);
    
    if (!json_response || strlen(json_response) == 0) {
        log_message(LOG_ERROR, "Failed to parse DeepSeek API response or empty response returned");
        if (json_response) free(json_response);
        return false;
    }
    
    log_message(LOG_DEBUG, "AI generated complete task: %s", json_response);
    
    // Parse JSON response
    cJSON *root = cJSON_Parse(json_response);
    free(json_response);
    
    if (!root) {
        log_message(LOG_ERROR, "Failed to parse complete task JSON");
        return false;
    }
    
    // Extract is_script field
    cJSON *is_script_json = cJSON_GetObjectItem(root, "is_script");
    if (cJSON_IsBool(is_script_json)) {
        result->is_script = cJSON_IsTrue(is_script_json);
    } else {
        log_message(LOG_ERROR, "Invalid or missing 'is_script' field in AI response");
        cJSON_Delete(root);
        return false;
    }
    
    // Extract content field
    cJSON *content_json = cJSON_GetObjectItem(root, "content");
    if (cJSON_IsString(content_json) && content_json->valuestring) {
        safe_strcpy(result->content, content_json->valuestring, sizeof(result->content));
        
        // Validate content (must not be empty)
        if (strlen(result->content) == 0) {
            log_message(LOG_ERROR, "Empty content returned by AI");
            cJSON_Delete(root);
            return false;
        }
    } else {
        log_message(LOG_ERROR, "Invalid or missing 'content' field in AI response");
        cJSON_Delete(root);
        return false;
    }
    
    // Extract is_cron field
    cJSON *is_cron_json = cJSON_GetObjectItem(root, "is_cron");
    if (cJSON_IsBool(is_cron_json)) {
        result->is_cron = cJSON_IsTrue(is_cron_json);
    } else {
        log_message(LOG_ERROR, "Invalid or missing 'is_cron' field in AI response");
        cJSON_Delete(root);
        return false;
    }
    
    // Extract schedule field
    cJSON *schedule_json = cJSON_GetObjectItem(root, "schedule");
    if (schedule_json) {
        if (result->is_cron) {
            // If it's a cron expression
            if (cJSON_IsString(schedule_json) && schedule_json->valuestring) {
                safe_strcpy(result->cron, schedule_json->valuestring, sizeof(result->cron));
                
                // Validate cron expression (basic validation)
                if (strlen(result->cron) == 0) {
                    log_message(LOG_ERROR, "Empty cron expression returned by AI");
                    cJSON_Delete(root);
                    return false;
                }
            } else {
                log_message(LOG_ERROR, "Invalid cron expression format in AI response");
                cJSON_Delete(root);
                return false;
            }
        } else {
            // If it's an interval
            if (cJSON_IsNumber(schedule_json)) {
                result->interval_minutes = schedule_json->valueint;
                
                // Validate interval (must be positive)
                if (result->interval_minutes <= 0) {
                    log_message(LOG_ERROR, "Invalid interval value (%d) returned by AI", 
                               result->interval_minutes);
                    cJSON_Delete(root);
                    return false;
                }
            } else if (cJSON_IsString(schedule_json) && schedule_json->valuestring) {
                // Handle the case where the number is returned as a string
                result->interval_minutes = atoi(schedule_json->valuestring);
                
                // Validate interval (must be positive)
                if (result->interval_minutes <= 0) {
                    log_message(LOG_ERROR, "Invalid interval value (%d) returned by AI", 
                               result->interval_minutes);
                    cJSON_Delete(root);
                    return false;
                }
            } else {
                log_message(LOG_ERROR, "Invalid interval format in AI response");
                cJSON_Delete(root);
                return false;
            }
        }
    } else {
        log_message(LOG_ERROR, "Missing 'schedule' field in AI response");
        cJSON_Delete(root);
        return false;
    }
    
    // Extract schedule_description field
    cJSON *schedule_desc_json = cJSON_GetObjectItem(root, "schedule_description");
    if (cJSON_IsString(schedule_desc_json) && schedule_desc_json->valuestring) {
        safe_strcpy(result->schedule_description, schedule_desc_json->valuestring, 
                  sizeof(result->schedule_description));
    } else {
        // Default description if not provided
        if (result->is_cron) {
            safe_strcpy(result->schedule_description, "Custom schedule based on cron expression", 
                      sizeof(result->schedule_description));
        } else {
            snprintf(result->schedule_description, sizeof(result->schedule_description), 
                    "Runs every %d minutes", result->interval_minutes);
        }
    }
    
    // Extract suggested_name field
    cJSON *name_json = cJSON_GetObjectItem(root, "suggested_name");
    if (cJSON_IsString(name_json) && name_json->valuestring) {
        safe_strcpy(result->suggested_name, name_json->valuestring, sizeof(result->suggested_name));
    } else {
        // Default name based on first 20 characters of description
        int desc_len = strlen(description);
        int max_len = desc_len > 20 ? 20 : desc_len;
        strncpy(result->suggested_name, description, max_len);
        result->suggested_name[max_len] = '\0';
        
        // Replace spaces with underscores for a valid name
        for (int i = 0; i < max_len; i++) {
            if (result->suggested_name[i] == ' ' || !isalnum(result->suggested_name[i])) {
                result->suggested_name[i] = '_';
            }
        }
    }
    
    // Clean up and return success
    cJSON_Delete(root);
    
    // Log the result
    log_message(LOG_INFO, "AI generated task: %s, Schedule: %s, Name: %s", 
               result->is_script ? "Script" : "Command",
               result->is_cron ? result->cron : "Interval-based",
               result->suggested_name);
    
    return true;
} 