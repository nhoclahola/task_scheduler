/**
 * @file ai.h
 * @brief Định nghĩa các hàm và cấu trúc liên quan đến xử lý AI
 */

#ifndef AI_H
#define AI_H

#include <stdbool.h>
#include <stdlib.h>

// Khai báo forward declaration cho SystemMetrics
struct SystemMetrics;
typedef struct SystemMetrics SystemMetrics;

typedef struct {
    char *data;
    size_t size;
} AIResponse;

// Định nghĩa cấu trúc dữ liệu cho kết quả phân tích lịch
typedef struct {
    bool is_cron;                // True nếu kết quả là biểu thức cron, false nếu là interval
    union {
        char cron[128];          // Biểu thức cron nếu is_cron = true
        int interval_minutes;    // Khoảng thời gian theo phút nếu is_cron = false
    };
    char explanation[256];       // Giải thích về lịch trình đã phân tích
} AIScheduleAnalysis;

// Cấu trúc dữ liệu chứa tất cả thông tin tác vụ được tạo bởi AI
typedef struct {
    bool is_script;              // True nếu là script, false nếu là lệnh
    char content[8192];          // Nội dung lệnh hoặc script
    
    bool is_cron;                // True nếu lịch trình là cron, false nếu là interval
    union {
        char cron[128];          // Biểu thức cron nếu is_cron = true
        int interval_minutes;    // Khoảng thời gian theo phút nếu is_cron = false
    };
    
    char schedule_description[256]; // Mô tả lịch trình bằng ngôn ngữ tự nhiên
    char suggested_name[128];    // Tên đề xuất cho tác vụ
} AIGeneratedTask;

/**
 * Gọi Deepseek API với các tham số đã cho
 * 
 * @param api_key Deepseek API key
 * @param system_state_json Chuỗi JSON chứa thông tin trạng thái hệ thống
 * @param user_prompt Lệnh/mục tiêu của người dùng
 * @return Chuỗi JSON phản hồi được cấp phát động (người gọi phải giải phóng), NULL nếu thất bại
 */
char* ai_call_deepseek_api(const char *api_key, const char *system_state_json, const char *user_prompt);

/**
 * Phân tích phản hồi của Deepseek API để trích xuất lệnh cần thực thi
 * 
 * @param response_json Phản hồi JSON từ Deepseek API
 * @return Chuỗi lệnh được cấp phát động (người gọi phải giải phóng), NULL nếu thất bại
 */
char* ai_parse_deepseek_response(const char *response_json);

/**
 * Khởi tạo cấu hình AI từ file
 * 
 * @param config_path Đường dẫn đến file cấu hình, NULL để sử dụng đường dẫn mặc định
 * @return true nếu thành công, false nếu thất bại
 */
bool ai_load_config(const char *config_path);

/**
 * Lưu cấu hình AI mặc định vào file
 * 
 * @param config_path Đường dẫn đến file cấu hình, NULL để sử dụng đường dẫn mặc định
 * @return true nếu thành công, false nếu thất bại
 */
bool ai_save_default_config(const char *config_path);

/**
 * Cập nhật API key trong cấu hình AI
 * 
 * @param api_key API key mới
 * @param config_path Đường dẫn đến file cấu hình, NULL để sử dụng đường dẫn mặc định
 * @return true nếu thành công, false nếu thất bại
 */
bool ai_update_api_key(const char *api_key, const char *config_path);

/**
 * Lấy API key từ cấu hình AI hoặc biến môi trường
 * 
 * @return API key hoặc NULL nếu không tìm thấy
 */
const char* ai_get_api_key();

/**
 * Tạo lệnh shell dựa trên trạng thái hệ thống và mục tiêu
 * 
 * @param metrics Thông số hệ thống để sử dụng trong quá trình tạo lệnh
 * @param goal Mục tiêu/yêu cầu cần thực hiện
 * @return Chuỗi lệnh được tạo ra, NULL nếu thất bại
 */
char* ai_generate_command(const SystemMetrics *metrics, const char *goal);

/**
 * Tạo lệnh/script dựa vào mô tả ngôn ngữ tự nhiên. AI đóng vai trò như một 
 * agent để tạo shell command hoặc script thực hiện nhiệm vụ được mô tả
 * 
 * @param description Mô tả nhiệm vụ bằng ngôn ngữ tự nhiên
 * @param is_script Tham số đầu ra, true nếu kết quả là script, false nếu là lệnh
 * @param cmd_or_script Tham số đầu ra, buffer để lưu lệnh hoặc script
 * @param buffer_size Kích thước của buffer
 * @return true nếu thành công, false nếu thất bại
 */
bool ai_generate_shell_solution(const char *description, bool *is_script, 
                              char *cmd_or_script, size_t buffer_size);

/**
 * Sử dụng AI để phân tích mô tả lịch trình bằng ngôn ngữ tự nhiên và trả về
 * biểu thức cron hoặc khoảng thời gian thích hợp
 * 
 * @param description Mô tả lịch trình bằng ngôn ngữ tự nhiên
 * @param result Kết quả phân tích được lưu vào đây
 * @return true nếu thành công, false nếu thất bại
 */
bool ai_analyze_schedule(const char *description, AIScheduleAnalysis *result);

/**
 * Tạo tác vụ hoàn chỉnh từ mô tả ngôn ngữ tự nhiên, bao gồm cả lệnh/script và lịch trình
 * Hàm này sẽ tạo ra tất cả thông tin cần thiết để tạo một tác vụ mới, chỉ từ mô tả
 * 
 * @param description Mô tả tác vụ bằng ngôn ngữ tự nhiên
 * @param result Kết quả được lưu vào đây
 * @return true nếu thành công, false nếu thất bại
 */
bool ai_generate_complete_task(const char *description, AIGeneratedTask *result);

/**
 * Tạo script wrapper cho notify-send
 * 
 * Hàm này tạo một script nhỏ để bọc notify-send, kiểm tra xem lệnh có tồn tại không
 * và sử dụng echo làm phương án dự phòng nếu không có notify-send
 * 
 * @param buffer Buffer để lưu script
 * @param buffer_size Kích thước buffer
 * @param title Tiêu đề thông báo
 * @param message Nội dung thông báo
 * @return true nếu thành công, false nếu thất bại
 */
bool ai_create_notify_wrapper(char *buffer, size_t buffer_size, 
                            const char *title, const char *message);

#endif /* AI_H */ 