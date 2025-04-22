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

#endif /* AI_H */ 