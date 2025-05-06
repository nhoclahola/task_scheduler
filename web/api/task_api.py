import os
import json
import subprocess
import time
import threading
import queue
import pty
import fcntl
import re
import select
from datetime import datetime

class TaskAPI:
    """API wrapper cho các lệnh của taskscheduler qua dòng lệnh"""
    
    def __init__(self):
        """Khởi tạo API wrapper"""
        # Đường dẫn đến thư mục gốc của dự án
        self.base_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        
        # Đường dẫn đến thư mục bin
        self.bin_dir = os.path.join(self.base_dir, "bin")
        
        # Đường dẫn đến file thực thi
        self.bin_path = os.path.join(self.bin_dir, "taskscheduler")
        
        # Đường dẫn đến thư mục data - Điều chỉnh để trỏ đến thư mục data trong bin
        # Đây là thư mục mà binary C sẽ sử dụng
        self.data_dir = os.path.join(self.bin_dir, "data")
        
        # Đường dẫn đến thư mục lib
        self.lib_dir = os.path.join(self.base_dir, "lib")
        
        # Chế độ giả lập - chỉ kiểm tra file binary, không kiểm tra thư viện
        self.simulation_mode = os.environ.get('TASK_SCHEDULER_SIMULATION') == '1' or not os.path.exists(self.bin_path)
        
        # Biến để theo dõi quá trình tương tác
        self.process = None
        self.master_fd = None
        self.slave_fd = None
        self.interactive_lock = threading.Lock()
        
        # Cache cho task data và các thông tin khác
        self._cache = {}
        
        # Cache cho output của lệnh view để tránh gọi lại nhiều lần
        self._last_view_output = ""
        self._last_view_task_id = -1
        
        if self.simulation_mode:
            print("WARNING: Running in simulation mode - no taskscheduler binary available")
            self.next_id = 1
            self.tasks = {}
        else:
            # Đảm bảo thư mục data tồn tại
            os.makedirs(self.data_dir, exist_ok=True)
            
            # Thiết lập quyền thực thi cho binary nếu cần
            if not os.access(self.bin_path, os.X_OK):
                try:
                    os.chmod(self.bin_path, 0o755)
                    print(f"Added execute permission to {self.bin_path}")
                except Exception as e:
                    print(f"Warning: Could not set execute permission on {self.bin_path}: {e}")
            
            # In thông tin về đường dẫn dữ liệu
            print(f"Data directory for the task scheduler: {self.data_dir}")
            print(f"Database file path: {os.path.join(self.data_dir, 'tasks.db')}")
            
            # Khởi động quá trình tương tác với binary
            self._start_interactive_process()
    
    def _start_interactive_process(self):
        """Khởi động quá trình tương tác với binary"""
        if self.simulation_mode:
            return
        
        try:
            # Dọn dẹp các tài nguyên cũ nếu có
            if self.process and self.process.poll() is None:
                print(f"Debug: Terminating existing process before starting new one")
                try:
                    self.process.terminate()
                    self.process.wait(timeout=2)
                except Exception as e:
                    print(f"Debug: Error terminating old process: {e}")
                    try:
                        self.process.kill()
                    except:
                        pass
            
            # Đóng các file descriptor cũ nếu có
            if self.master_fd:
                try:
                    os.close(self.master_fd)
                except Exception as e:
                    print(f"Debug: Error closing old master_fd: {e}")
            
            if self.slave_fd:
                try:
                    os.close(self.slave_fd)
                except Exception as e:
                    print(f"Debug: Error closing old slave_fd: {e}")
            
            # Reset các biến
            self.master_fd = None
            self.slave_fd = None
            self.process = None
            
            # Tạo môi trường cho lệnh
            env = os.environ.copy()
            
            # Thiết lập thư mục làm việc là thư mục bin
            working_dir = self.bin_dir
            
            # Kiểm tra xem binary có tồn tại và có quyền thực thi không
            if not os.path.exists(self.bin_path):
                print(f"Error: Binary file not found at {self.bin_path}")
                self.simulation_mode = True
                return
            
            if not os.access(self.bin_path, os.X_OK):
                try:
                    os.chmod(self.bin_path, 0o755)
                    print(f"Added execute permission to {self.bin_path}")
                except Exception as e:
                    print(f"Error: Could not set execute permission on {self.bin_path}: {e}")
                    self.simulation_mode = True
                    return
            
            # Khởi tạo pty để tương tác với binary
            self.master_fd, self.slave_fd = pty.openpty()
            
            # Thiết lập chế độ không chặn cho master
            flags = fcntl.fcntl(self.master_fd, fcntl.F_GETFL)
            fcntl.fcntl(self.master_fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)
            
            # Chạy binary trong tiến trình con, không cần chỉ định -D vì binary sẽ tự tìm trong thư mục hiện tại
            cmd = ["./taskscheduler", "-i"]
            print(f"Starting interactive process with command: {' '.join(cmd)} in {working_dir}")
            
            self.process = subprocess.Popen(
                cmd,
                stdin=self.slave_fd,
                stdout=self.slave_fd,
                stderr=self.slave_fd,
                cwd=working_dir,
                env=env,
                start_new_session=True
            )
            
            # Đợi cho binary khởi động và hiển thị dấu nhắc
            time.sleep(1)
            
            # Đọc đầu ra ban đầu để xác nhận binary đã khởi động
            initial_output = self._read_output(timeout=3.0)
            print(f"Initial output from binary: {initial_output}")
            
            # Kiểm tra xem binary có đang chạy không
            if self.process.poll() is not None:
                print(f"Binary exited prematurely with code {self.process.returncode}")
                self.simulation_mode = True
            else:
                print("Interactive mode started successfully")
                
                # Xóa cache để đảm bảo dữ liệu mới
                self._cache = {}
                self._last_view_task_id = -1
                self._last_view_output = None
                
        except Exception as e:
            print(f"Error starting interactive process: {e}")
            self.simulation_mode = True
            
            # Dọn dẹp nếu có lỗi
            if self.master_fd:
                try:
                    os.close(self.master_fd)
                except:
                    pass
            if self.slave_fd:
                try:
                    os.close(self.slave_fd)
                except:
                    pass
            if self.process and self.process.poll() is None:
                try:
                    self.process.terminate()
                except:
                    pass
            
            self.master_fd = None
            self.slave_fd = None
            self.process = None
    
    def _read_output(self, timeout=10.0, command=None):
        """Đọc đầu ra từ binary tương tác, với timeout tùy chỉnh"""
        if self.simulation_mode or not self.master_fd:
            return ""
        
        output = ""
        end_time = time.time() + timeout
        
        # Điều chỉnh timeout dựa trên loại lệnh
        if command:
            if 'ai-create' in command or 'ai-generate' in command:
                # Tăng thời gian chờ cho lệnh AI
                end_time = time.time() + 30.0
                print(f"Debug: Using extended timeout (30s) for AI command")
            elif command.startswith('run '):
                # Tăng thời gian chờ cho lệnh run task
                end_time = time.time() + 20.0
                print(f"Debug: Using extended timeout (20s) for run command")
            elif command.startswith('view '):
                # Tăng thời gian chờ cho lệnh view
                end_time = time.time() + 15.0
                print(f"Debug: Using extended timeout (15s) for view command")
        
        waiting_for_prompt = True
        last_output_time = time.time()
        
        while time.time() < end_time and waiting_for_prompt:
            try:
                # Kiểm tra xem có dữ liệu để đọc không
                readable, _, _ = select.select([self.master_fd], [], [], 0.1)
                if self.master_fd in readable:
                    chunk = os.read(self.master_fd, 4096).decode('utf-8', errors='replace')
                    if not chunk:  # Kết nối đã đóng
                        break
                    output += chunk
                    last_output_time = time.time()
                    
                    # Kiểm tra xem đã nhận được kết quả hoàn chỉnh chưa
                    if command:
                        if 'ai-create' in command or 'ai-generate' in command:
                            # Điều kiện đặc biệt cho lệnh AI
                            if "Suggested name:" in output or "Task created" in output or "Done" in output:
                                waiting_for_prompt = False
                                print(f"Debug: Detected AI response completion: {output[-100:]}")
                                break
                        elif command.startswith('run '):
                            # Điều kiện đặc biệt cho lệnh run
                            if "Executing task" in output and "at " in output:
                                # Đã bắt đầu thực thi, có thể kết thúc đọc
                                print(f"Debug: Detected run command execution start")
                                waiting_for_prompt = False
                                break
                        
                    # Kiểm tra xem có dấu nhắc cuối cùng không
                    if output.strip().endswith(">"):
                        waiting_for_prompt = False
                        break
                else:
                    # Không có dữ liệu sẵn sàng, chờ một chút
                    time.sleep(0.2)
                    
                    # Nếu đã có output và không có dữ liệu mới trong 3 giây, có thể kết thúc
                    if output and time.time() - last_output_time > 3.0:
                        print(f"Debug: No new data for 3 seconds after receiving some output, considering command complete")
                        waiting_for_prompt = False
                        break
            except Exception as e:
                print(f"Error reading from process: {e}")
                break
        
        # Thử đọc thêm một lần nữa để chắc chắn có tất cả đầu ra
        try:
            readable, _, _ = select.select([self.master_fd], [], [], 0.5)
            if self.master_fd in readable:
                chunk = os.read(self.master_fd, 8192).decode('utf-8', errors='replace')
                if chunk:
                    output += chunk
        except:
            pass
        
        if time.time() >= end_time:
            print(f"Warning: Timeout waiting for output from command {command}")
        
        return output
    
    def _send_command(self, command):
        """Gửi lệnh đến binary tương tác và đợi phản hồi"""
        if self.simulation_mode or not self.master_fd or not self.process or self.process.poll() is not None:
            return None, ""
        
        with self.interactive_lock:
            try:
                # Kiểm tra xem process có còn sống không
                if self.process.poll() is not None:
                    print(f"Debug: Interactive process has exited (code: {self.process.returncode}), restarting...")
                    self._start_interactive_process()
                    # Nếu vẫn không khởi động được, trả về lỗi
                    if self.process.poll() is not None:
                        return -1, "Interactive process could not be started"
                
                # Gửi lệnh kèm theo ký tự xuống dòng
                print(f"Sending to binary: '{command}'")
                os.write(self.master_fd, f"{command}\n".encode('utf-8'))
                
                # Đợi phản hồi - truyền thêm lệnh để xác định thời gian chờ
                output = self._read_output(timeout=20.0, command=command)
                print(f"Raw output from binary: '{output}'")
                
                # Kiểm tra xem output có rỗng hoặc quá ngắn không
                if not output or (len(output.strip()) <= len(command) + 2 and command.strip() in output):
                    print(f"Debug: Output is too short or empty, binary might be stuck")
                    # Thử khởi động lại process
                    try:
                        if self.process and self.process.poll() is None:
                            print(f"Debug: Terminating unresponsive process")
                            self.process.terminate()
                            self.process.wait(timeout=2)
                        
                        # Khởi động lại
                        self._start_interactive_process()
                        if self.process.poll() is None:
                            print(f"Debug: Process restarted, trying command again after 1 second")
                            time.sleep(1)
                            os.write(self.master_fd, f"{command}\n".encode('utf-8'))
                            output = self._read_output(timeout=15.0, command=command)
                            print(f"Raw output after restart: '{output}'")
                    except Exception as e:
                        print(f"Debug: Error during process restart: {e}")
                
                # Phân tích kết quả để xác định thành công hay thất bại
                if "Error" in output or "error" in output or "Unknown command" in output or "Usage:" in output:
                    return 1, output
                
                # Ngay cả khi không nhận được output đầy đủ, cũng trả về những gì có
                return 0, output
            except Exception as e:
                print(f"Error sending command: {e}")
                return -1, str(e)
    
    def _run_command(self, *args):
        """Chạy lệnh taskscheduler với các đối số được cung cấp
        
        Args:
            *args: Các đối số của lệnh cần thực thi
            
        Returns:
            tuple: (exit_code, output)
                - exit_code: 0 nếu thành công, khác 0 nếu có lỗi
                - output: Chuỗi đầu ra từ lệnh
        
        Lưu ý: 
            - Hàm này tự động xử lý các tham số để đảm bảo định dạng đúng
            - Đối với các lệnh đặc biệt như set-api-key, hàm sẽ truyền tham số nguyên vẹn
            - Nếu tham số đã có dấu ngoặc kép, sẽ không thêm dấu ngoặc kép nữa
            - Nếu tham số có khoảng trắng nhưng không có dấu ngoặc kép, sẽ thêm dấu ngoặc kép
        """
        if self.simulation_mode:
            # Không chạy lệnh thực tế trong chế độ giả lập
            return None, ""
        
        # In log args for debugging
        print(f"Debug: _run_command args: {args}")
        
        # Handle arguments properly - build a proper command string
        # If an argument already has quotes, keep them
        processed_args = []
        
        # Kiểm tra nếu là lệnh đặc biệt như set-api-key
        is_special_command = args and args[0] in ["set-api-key"]
        
        for i, arg in enumerate(args):
            if not isinstance(arg, str):
                arg = str(arg)
                
            # Đối với tham số dạng API key, khóa, token, v.v., không bọc trong ngoặc kép
            if is_special_command and i > 0:
                processed_args.append(arg)
                continue
                
            # If the argument already has quotes, don't add more
            if (arg.startswith('"') and arg.endswith('"')) or (arg.startswith("'") and arg.endswith("'")):
                processed_args.append(arg)
            # If the argument has spaces but no quotes, add quotes
            elif ' ' in arg:
                processed_args.append(f'"{arg}"')
            else:
                processed_args.append(arg)
        
        # In the interactive mode, we'll send a properly formatted command string
        command = " ".join(processed_args)
        print(f"Debug: Formatted command: {command}")
        return self._send_command(command)
    
    def get_all_tasks(self):
        """Lấy tất cả các tác vụ"""
        if self.simulation_mode:
            # Trả về dữ liệu mẫu trong chế độ giả lập
            return list(self.tasks.values()) or [
                {
                    'id': 1,
                    'name': 'Sample Task 1',
                    'command': 'echo "Hello World"',
                    'working_dir': '/tmp',
                    'creation_time': int(time.time() - 3600),
                    'next_run_time': int(time.time() + 1800),
                    'last_run_time': int(time.time() - 1800),
                    'frequency': 1,  # FREQUENCY_DAILY
                    'interval': 86400,  # 1 day in seconds
                    'schedule_type': 1,  # SCHEDULE_INTERVAL
                    'enabled': True,
                    'exit_code': 0,
                    'max_runtime': 60,
                    'dependencies': [],
                    'dep_behavior': 0,
                    'exec_mode': 0,  # EXEC_COMMAND
                    'cron_expression': ''
                },
                {
                    'id': 2,
                    'name': 'Sample Task 2',
                    'command': 'ls -la',
                    'working_dir': '/home',
                    'creation_time': int(time.time() - 7200),
                    'next_run_time': int(time.time() + 3600),
                    'last_run_time': 0,
                    'frequency': 2,  # FREQUENCY_WEEKLY
                    'interval': 604800,  # 1 week in seconds
                    'schedule_type': 1,  # SCHEDULE_INTERVAL
                    'enabled': False,
                    'exit_code': -1,
                    'max_runtime': 120,
                    'dependencies': [1],
                    'dep_behavior': 1,
                    'exec_mode': 0,  # EXEC_COMMAND
                    'cron_expression': ''
                }
            ]
        
        # Chạy lệnh "list" trong chế độ tương tác
        exit_code, output = self._run_command("list")
        
        if exit_code != 0 or not output.strip():
            print(f"Error listing tasks: {output}")
            return []
        
        try:
            # Kiểm tra nếu không có tác vụ nào
            if "No tasks found" in output:
                return []
            
            # Nếu không tìm thấy các chuỗi đặc trưng của danh sách tác vụ
            if "Task List:" not in output and "ID:" not in output:
                print(f"Unexpected output format: {output}")
                return []
            
            # Parse output để lấy danh sách tác vụ
            tasks = []
            current_task = None
            
            # Phân tích output từ lệnh list
            lines = output.split('\n')
            for line in lines:
                line = line.strip()
                
                # Bỏ qua dòng trống và dòng đặc biệt
                if not line or line == "----------" or line == "Task List:" or line == ">":
                    # Nếu đang có một task và gặp separator, thêm task vào danh sách
                    if current_task and line == "----------":
                        tasks.append(current_task)
                        current_task = None
                    continue
                
                # Bắt đầu task mới
                if line.startswith("ID:"):
                    if current_task:
                        tasks.append(current_task)
                    
                    try:
                        task_id = int(line.split(":")[1].strip())
                        current_task = {'id': task_id}
                    except (ValueError, IndexError):
                        print(f"Error parsing task ID from line: {line}")
                        continue
                    
                # Phân tích các trường thông tin khác    
                elif current_task and ":" in line:
                    try:
                        key, value = line.split(":", 1)
                        key = key.strip().lower().replace(" ", "_")
                        value = value.strip()
                        
                        # Chuyển đổi các trường thành kiểu dữ liệu phù hợp
                        if key == "id":
                            current_task[key] = int(value)
                        elif key == "name":
                            current_task[key] = value
                        elif key == "enabled":
                            current_task[key] = value.lower() == "yes"
                        elif key == "type":
                            if "command" in value.lower():
                                current_task["exec_mode"] = 0  # EXEC_COMMAND
                            elif "script" in value.lower():
                                current_task["exec_mode"] = 1  # EXEC_SCRIPT
                            elif "ai dynamic" in value.lower():
                                current_task["exec_mode"] = 2  # EXEC_AI_DYNAMIC
                        elif key == "command":
                            current_task[key] = value
                        elif key == "script" and "size:" in value.lower():
                            # Ghi nhận thông tin về script (kích thước)
                            current_task["script_size"] = value
                        elif key == "ai_prompt":
                            current_task[key] = value
                        elif key == "system_metrics":
                            current_task["system_metrics"] = value.split(",")
                        elif key == "schedule":
                            # Xử lý lịch trình
                            if "Manual" in value:
                                current_task["schedule_type"] = 0  # SCHEDULE_MANUAL
                            elif "Every" in value and "minutes" in value:
                                current_task["schedule_type"] = 1  # SCHEDULE_INTERVAL
                                # Trích xuất số phút
                                interval_match = re.search(r"Every (\d+) minutes", value)
                                if interval_match:
                                    minutes = int(interval_match.group(1))
                                    current_task["interval"] = minutes * 60  # Chuyển phút sang giây
                            elif "Cron:" in value:
                                current_task["schedule_type"] = 2  # SCHEDULE_CRON
                                # Trích xuất biểu thức cron
                                cron_match = re.search(r"Cron: (.+)", value)
                                if cron_match:
                                    current_task["cron_expression"] = cron_match.group(1).strip()
                        elif key == "working_directory" or key == "working_dir":
                            if "(default)" in value:
                                current_task["working_dir"] = ""
                            else:
                                current_task["working_dir"] = value
                        elif key == "max_runtime":
                            try:
                                runtime_match = re.search(r"(\d+) seconds", value)
                                if runtime_match:
                                    current_task["max_runtime"] = int(runtime_match.group(1))
                                else:
                                    current_task["max_runtime"] = 0
                            except:
                                current_task["max_runtime"] = 0
                        elif key == "created":
                            try:
                                dt = datetime.strptime(value, "%Y-%m-%d %H:%M:%S")
                                current_task["creation_time"] = int(dt.timestamp())
                            except:
                                current_task["creation_time"] = 0
                        elif key == "last_run":
                            if value == "Never":
                                current_task["last_run_time"] = 0
                            else:
                                try:
                                    dt = datetime.strptime(value, "%Y-%m-%d %H:%M:%S")
                                    current_task["last_run_time"] = int(dt.timestamp())
                                except:
                                    current_task["last_run_time"] = 0
                        elif key == "exit_code":
                            try:
                                current_task["exit_code"] = int(value)
                            except:
                                current_task["exit_code"] = -1
                        elif key == "next_run":
                            if value == "Not scheduled":
                                current_task["next_run_time"] = 0
                            else:
                                try:
                                    dt = datetime.strptime(value, "%Y-%m-%d %H:%M:%S")
                                    current_task["next_run_time"] = int(dt.timestamp())
                                except:
                                    current_task["next_run_time"] = 0
                        elif key == "dependencies":
                            try:
                                # Danh sách các task ID mà task này phụ thuộc
                                if value.isdigit():  # Một task ID duy nhất
                                    current_task["dependencies"] = [int(value)]
                                else:  # Nhiều task ID
                                    # Xử lý các trường hợp khác
                                    current_task["dependencies"] = []
                            except:
                                current_task["dependencies"] = []
                        elif key == "dependency_behavior":
                            try:
                                if "All Success" in value:
                                    current_task["dep_behavior"] = 1
                                elif "Any Complete" in value:
                                    current_task["dep_behavior"] = 2
                                elif "All Complete" in value:
                                    current_task["dep_behavior"] = 3
                                else:  # "Any Success" mặc định
                                    current_task["dep_behavior"] = 0
                            except:
                                current_task["dep_behavior"] = 0
                    except Exception as e:
                        print(f"Error parsing line '{line}': {e}")
            
            # Thêm task cuối cùng nếu có
            if current_task:
                tasks.append(current_task)
            
            # Đảm bảo tất cả các tasks có các trường cần thiết
            for task in tasks:
                # Thêm các trường mặc định nếu chưa có
                if "dependencies" not in task:
                    task["dependencies"] = []
                if "dep_behavior" not in task:
                    task["dep_behavior"] = 0
                if "frequency" not in task:
                    # Đặt frequency dựa vào schedule_type
                    if task.get("schedule_type") == 1:  # SCHEDULE_INTERVAL
                        interval = task.get("interval", 0)
                        if interval == 86400:  # 1 ngày
                            task["frequency"] = 1  # FREQUENCY_DAILY
                        elif interval == 604800:  # 1 tuần
                            task["frequency"] = 2  # FREQUENCY_WEEKLY
                        else:
                            task["frequency"] = 0  # FREQUENCY_ONCE
                    else:
                        task["frequency"] = 0  # FREQUENCY_ONCE
                
                # Đảm bảo có command hoặc script_content cho tác vụ script/command
                if task.get("exec_mode") == 0 and "command" not in task:
                    task["command"] = ""
                    
                # Đảm bảo có ai_prompt cho tác vụ AI Dynamic
                if task.get("exec_mode") == 2 and "ai_prompt" not in task:
                    task["ai_prompt"] = "AI Dynamic"
            
            return tasks
        except Exception as e:
            print(f"Error parsing task list: {e}")
            return []
    
    def _get_tasks_from_database(self):
        """Truy cập trực tiếp vào cơ sở dữ liệu để lấy danh sách tác vụ"""
        try:
            import sqlite3
            db_path = os.path.join(self.data_dir, "tasks.db")
            if not os.path.exists(db_path):
                print(f"Database file not found at {db_path}")
                return []
            
            conn = sqlite3.connect(db_path)
            conn.row_factory = sqlite3.Row
            cursor = conn.cursor()
            
            # Truy vấn để lấy tất cả các tác vụ
            cursor.execute("""
                SELECT * FROM tasks
            """)
            
            tasks = []
            for row in cursor.fetchall():
                task = {}
                for key in row.keys():
                    if key in ['id', 'frequency', 'schedule_type', 'exit_code', 'max_runtime', 'dep_behavior', 'exec_mode']:
                        task[key] = int(row[key])
                    elif key in ['creation_time', 'next_run_time', 'last_run_time', 'interval']:
                        task[key] = int(row[key])
                    elif key == 'enabled':
                        task[key] = bool(row[key])
                    else:
                        task[key] = row[key]
                
                # Chuẩn bị dependencies
                task['dependencies'] = []
                
                # Truy vấn để lấy phụ thuộc
                cursor.execute("""
                    SELECT dependency_id FROM dependencies WHERE task_id = ?
                """, (task['id'],))
                
                for dep_row in cursor.fetchall():
                    task['dependencies'].append(int(dep_row[0]))
                
                tasks.append(task)
            
            conn.close()
            return tasks
        except Exception as e:
            print(f"Error accessing database: {e}")
            return []
    
    def get_task(self, task_id, force_refresh=False):
        """Lấy thông tin chi tiết của một tác vụ theo ID
        
        Args:
            task_id (int): ID của tác vụ cần lấy thông tin
            force_refresh (bool): Làm mới cache nếu cần
            
        Returns:
            dict: Thông tin task hoặc None nếu không tìm thấy
        """
        if self.simulation_mode:
            return self.tasks.get(task_id)
        
        cache_key = f"task_{task_id}"
        
        # Kiểm tra cache
        if not force_refresh and cache_key in self._cache:
            print(f"Debug: Using cached task {task_id}")
            return self._cache[cache_key]
        
        # Gọi lệnh view - truyền đúng tham số
        exit_code, output = self._run_command("view", str(task_id))
        
        # Lưu lại output để sử dụng sau nếu cần
        self._last_view_output = output
        self._last_view_task_id = task_id
        
        print(f"Debug: View command output (first 50 chars): {output[:50]}...")
        
        # Kiểm tra trường hợp task không tồn tại - chỉ khi có thông báo lỗi cụ thể
        if "Task not found" in output and "ID:" not in output:
            print(f"Debug: Task {task_id} explicitly not found")
            return None
        
        # Kiểm tra xem có header "Task Details:" không - dấu hiệu chính của tác vụ tồn tại    
        if "Task Details:" not in output:
            print(f"Debug: No Task Details found for task {task_id}")
            return None
        
        # Kiểm tra sự tồn tại của thông tin ID và Name - những trường bắt buộc
        id_exists = re.search(r"ID:\s*(\d+)", output) is not None
        name_exists = re.search(r"Name:\s*(.+?)$", output, re.MULTILINE) is not None
        
        if not (id_exists and name_exists):
            print(f"Debug: Missing essential task fields (ID or Name) for task {task_id}")
            return None
            
        # Phân tích output để lấy thông tin task
        # Tạo task_data object ban đầu với ID
        task_data = {'id': task_id}
        
        # Loại bỏ dấu nhắc ">" ở cuối nếu có
        if output.strip().endswith(">"):
            output = output[:output.rfind(">")].strip()
        
        # Sử dụng regex để lấy các thông tin cơ bản
        name_match = re.search(r"Name:\s*(.+?)$", output, re.MULTILINE)
        if name_match:
            task_data["name"] = name_match.group(1).strip()
        else:
            print(f"Debug: Could not find Name field for task {task_id}")
            # Thử dùng phương thức test_view_output
            fallback_data = self.test_view_output(task_id)
            if fallback_data and 'name' in fallback_data:
                print(f"Debug: Using fallback task data from test_view_output")
                return fallback_data
            return None
            
        enabled_match = re.search(r"Enabled:\s*(Yes|No)", output, re.MULTILINE)
        if enabled_match:
            task_data["enabled"] = (enabled_match.group(1) == "Yes")
            
        # Xác định loại task (Command, Script hay AI Dynamic)
        type_match = re.search(r"Type:\s*(Script|Command|AI Dynamic)", output, re.MULTILINE)
        if type_match:
            type_value = type_match.group(1)
            if type_value == "Script":
                task_data["exec_mode"] = 1
            elif type_value == "Command":
                task_data["exec_mode"] = 0
            elif type_value == "AI Dynamic":
                task_data["exec_mode"] = 2
            
            # Tìm AI Prompt và System Metrics cho AI Dynamic
            ai_prompt_match = re.search(r"AI Prompt:\s*(.+?)$", output, re.MULTILINE) 
            if ai_prompt_match:
                task_data["ai_prompt"] = ai_prompt_match.group(1).strip()
            
            system_metrics_match = re.search(r"System Metrics:\s*(.+?)$", output, re.MULTILINE)
            if system_metrics_match:
                metrics_str = system_metrics_match.group(1).strip()
                # Lưu dưới dạng danh sách các metric
                task_data["system_metrics"] = [m.strip() for m in metrics_str.split(',')]
    
        # Lấy command nếu có
        command_match = re.search(r"Command:\s*(.+?)$", output, re.MULTILINE)
        if command_match:
            task_data["command"] = command_match.group(1).strip()
    
        # Lấy schedule - Xử lý trường hợp đặc biệt cho Cron
        schedule_match = re.search(r"Schedule:\s*(.+?)$", output, re.MULTILINE)
        if schedule_match:
            schedule_value = schedule_match.group(1).strip()
            
            # Xử lý đặc biệt cho Cron expression (Schedule: Cron: 0 10 * * *)
            if schedule_value.startswith("Cron:"):
                task_data["schedule_type"] = 2  # SCHEDULE_CRON
                task_data["cron_expr"] = schedule_value  # Lưu cả "Cron: 0 10 * * *"
                cron_expr_match = re.search(r"Cron:\s*(.+?)$", schedule_value)
                if cron_expr_match:
                    task_data["cron_expression"] = cron_expr_match.group(1).strip()
            else:
                task_data["cron_expr"] = schedule_value
                
                # Xác định schedule_type nếu là interval
                if "Every" in schedule_value and "minutes" in schedule_value:
                    task_data["schedule_type"] = 1  # SCHEDULE_INTERVAL
                    # Trích xuất số phút
                    interval_match = re.search(r"Every (\d+) minutes", schedule_value)
                    if interval_match:
                        minutes = int(interval_match.group(1))
                        task_data["interval"] = minutes * 60  # Chuyển phút sang giây
                else:
                    task_data["schedule_type"] = 0  # SCHEDULE_MANUAL
    
        # Lấy working directory
        working_dir_match = re.search(r"Working Directory:\s*(.+?)$", output, re.MULTILINE)
        if working_dir_match:
            working_dir = working_dir_match.group(1).strip()
            if working_dir == "(default)":
                task_data["working_dir"] = ""
            else:
                task_data["working_dir"] = working_dir
        
        # Lấy max runtime
        max_runtime_match = re.search(r"Max Runtime:\s*(\d+) seconds", output, re.MULTILINE)
        if max_runtime_match:
            task_data["max_runtime"] = int(max_runtime_match.group(1))
        else:
            task_data["max_runtime"] = 0
        
        # Lấy timestamps
        created_match = re.search(r"Created:\s*(.+?)$", output, re.MULTILINE)
        if created_match:
            try:
                dt = datetime.strptime(created_match.group(1).strip(), "%Y-%m-%d %H:%M:%S")
                task_data["creation_time"] = int(dt.timestamp())
            except:
                task_data["creation_time"] = 0
            
        last_run_match = re.search(r"Last Run:\s*(.+?)$", output, re.MULTILINE)
        if last_run_match:
            last_run_value = last_run_match.group(1).strip()
            if last_run_value == "Never" or last_run_value == "Never run before":
                task_data["last_run_time"] = 0
            else:
                try:
                    dt = datetime.strptime(last_run_value, "%Y-%m-%d %H:%M:%S")
                    task_data["last_run_time"] = int(dt.timestamp())
                except:
                    task_data["last_run_time"] = 0
                
        next_run_match = re.search(r"Next Run:\s*(.+?)$", output, re.MULTILINE)
        if next_run_match:
            next_run_value = next_run_match.group(1).strip()
            if next_run_value == "Not scheduled":
                task_data["next_run_time"] = 0
            else:
                try:
                    dt = datetime.strptime(next_run_value, "%Y-%m-%d %H:%M:%S")
                    task_data["next_run_time"] = int(dt.timestamp())
                except:
                    task_data["next_run_time"] = 0
                    
        # Exit Code
        exit_code_match = re.search(r"Exit Code:\s*(.+?)$", output, re.MULTILINE)
        if exit_code_match:
            try:
                task_data["exit_code"] = int(exit_code_match.group(1).strip())
            except:
                task_data["exit_code"] = -1
        else:
            task_data["exit_code"] = -1
            
        # Max Runtime
        max_runtime_match = re.search(r"Max Runtime:\s*(\d+) seconds", output, re.MULTILINE)
        if max_runtime_match:
            task_data["max_runtime"] = int(max_runtime_match.group(1))
        else:
            task_data["max_runtime"] = 0
            
        # Đảm bảo các trường cần thiết đều tồn tại
        if "dependencies" not in task_data:
            task_data["dependencies"] = []
            
        if "dep_behavior" not in task_data:
            task_data["dep_behavior"] = 0
            
        # Cache kết quả phân tích nếu thành công
        if task_id is not None and task_data and 'name' in task_data:
            cache_key = f"task_{task_id}"
            self._cache[cache_key] = task_data
            print(f"Debug: Successfully parsed and cached task {task_id} using fallback method")
            
        print(f"Debug: Task data from fallback method: {task_data}")
        return task_data
    
    def update_task(self, task_data):
        """Cập nhật một tác vụ"""
        if self.simulation_mode:
            # Cập nhật tác vụ trong chế độ giả lập
            task_id = task_data.get('id')
            if task_id and task_id in self.tasks:
                # Lưu giá trị last_run_time trước khi cập nhật
                last_run_time = self.tasks[task_id].get('last_run_time', 0)
                
                # Cập nhật task với dữ liệu mới
                self.tasks[task_id].update(task_data)
                
                # Đảm bảo giữ nguyên last_run_time nếu chỉ thay đổi trạng thái enabled
                if 'last_run_time' not in task_data and last_run_time > 0:
                    self.tasks[task_id]['last_run_time'] = last_run_time
                    print(f"Giữ nguyên last_run_time: {last_run_time} cho task {task_id}")
                
                return True
            return False
        
        task_id = task_data.get('id')
        if not task_id:
            print("Error: No task ID provided for update")
            return False
        
        # Kiểm tra xem đây có phải là yêu cầu bật/tắt task không
        if len(task_data) == 2 and 'id' in task_data and 'enabled' in task_data:
            print(f"Chỉ thay đổi trạng thái bật/tắt cho task {task_id}")
            
            # Sử dụng lệnh enable hoặc disable trực tiếp thay vì xóa và tạo lại
            enabled_status = "enable" if task_data['enabled'] else "disable"
            exit_code, output = self._run_command(enabled_status, str(task_id))
            
            if exit_code != 0:
                print(f"Error changing task status: {output}")
                return False
                
            print(f"Task {task_id} {enabled_status}d successfully")
            
            # Làm mới cache sau khi thay đổi trạng thái
            if self._last_view_task_id == task_id:
                self._last_view_task_id = -1  # Reset cache
                self._last_view_output = None
            
            # Đảm bảo AI Dynamic tasks luôn được bật
            if task_data.get('enabled') == False:
                current_task = self.get_task(task_id, force_refresh=True)
                if current_task and current_task.get('exec_mode') == 2:  # AI Dynamic
                    print(f"Task {task_id} là AI Dynamic task - đảm bảo nó được bật")
                    exit_code, output = self._run_command("enable", str(task_id))
                    if exit_code == 0:
                        cache_key = f"task_{task_id}"
                        if cache_key in self._cache:
                            self._cache[cache_key]['enabled'] = True
                        print(f"Đã bật lại AI Dynamic task {task_id}")
            
            return True
            
        # Lấy thông tin hiện tại của task trước khi cập nhật
        current_task = self.get_task(task_id)
        if not current_task:
            print(f"Error: Cannot find task {task_id} to update")
            return False
            
        # Sử dụng lệnh edit để cập nhật từng trường thay vì xóa và tạo lại
        success = True
        
        # Cập nhật tên
        if 'name' in task_data and task_data['name'] != current_task.get('name', ''):
            name = task_data['name'].replace('"', '\\"')  # Escape dấu ngoặc kép
            edit_cmd = f'edit {task_id} name "{name}"'
            exit_code, output = self._run_command(edit_cmd)
            if exit_code != 0:
                print(f"Error updating task name: {output}")
                success = False
        
        # Cập nhật lệnh nếu trong chế độ lệnh
        exec_mode = task_data.get('exec_mode', current_task.get('exec_mode', 0))
        
        if exec_mode == 0:  # Chế độ lệnh
            if 'command' in task_data and task_data['command'] != current_task.get('command', ''):
                command = task_data['command'].replace('"', '\\"')  # Escape dấu ngoặc kép
                if current_task.get('exec_mode', 0) != 0:
                    # Chuyển từ chế độ khác sang chế độ lệnh
                    convert_cmd = f'to-command {task_id} "{command}"'
                    exit_code, output = self._run_command(convert_cmd)
                else:
                    # Chỉ cập nhật lệnh
                    edit_cmd = f'edit {task_id} command "{command}"'
                    exit_code, output = self._run_command(edit_cmd)
                
                if exit_code != 0:
                    print(f"Error updating command: {output}")
                    success = False
        
        elif exec_mode == 1:  # Chế độ script
            # Xử lý nội dung script
            script_content = None
            
            if 'script_content' in task_data and task_data['script_content']:
                script_content = task_data['script_content'].strip()
            elif 'script_file' in task_data and task_data['script_file']:
                try:
                    with open(task_data['script_file'], 'r') as f:
                        script_content = f.read()
                except Exception as e:
                    print(f"Error reading script file: {e}")
            
            if script_content:
                # Kiểm tra xem có đang chuyển từ command sang script không
                if current_task.get('exec_mode', 0) == 0:
                    print("Chuyển từ command sang script - tạo lại task")
                    # Trong trường hợp này cần xóa task cũ và tạo lại để tránh lỗi
                    
                    # Lưu lại tất cả thông tin task hiện tại
                    new_task_data = current_task.copy()
                    
                    # Cập nhật với thông tin mới
                    new_task_data.update(task_data)
                    
                    # Đảm bảo exec_mode là script
                    new_task_data['exec_mode'] = 1
                    new_task_data['script_content'] = script_content
                    
                    # Xóa ID để tránh xung đột khi tạo mới
                    if 'id' in new_task_data:
                        del new_task_data['id']
                    
                    # Xóa task cũ
                    if not self.delete_task(task_id):
                        print(f"Error deleting old task when converting to script")
                        return False
                    
                    # Tạo task mới
                    new_task_id = self.add_task(new_task_data)
                    if new_task_id <= 0:
                        print(f"Error creating new task when converting to script")
                        return False
                    
                    # Khôi phục các dependencies nếu có
                    if 'dependencies' in current_task and current_task['dependencies']:
                        for dep_id in current_task['dependencies']:
                            if not self.add_dependency(new_task_id, dep_id):
                                print(f"Warning: Failed to add dependency {dep_id} to new task {new_task_id}")
                    
                    # Khôi phục dependency behavior nếu có
                    if 'dep_behavior' in current_task and current_task['dep_behavior'] != 0:
                        if not self.set_dependency_behavior(new_task_id, current_task['dep_behavior']):
                            print(f"Warning: Failed to set dependency behavior for new task {new_task_id}")
                    
                    # Khôi phục trạng thái enabled nếu task đã bị tắt
                    if not current_task.get('enabled', True):
                        self._run_command("disable", str(new_task_id))
                    
                    print(f"Đã chuyển đổi task {task_id} thành task mới với ID {new_task_id}")
                    return True
                else:
                    # Cùng là script mode, chỉ cập nhật nội dung
                    # Chuẩn hóa nội dung script
                    normalized_content = script_content.replace('\r\n', '\n')
                    if not normalized_content.strip().startswith('#!/'):
                        normalized_content = '#!/bin/bash\n' + normalized_content
                    
                    # Tạo file script tạm
                    import tempfile
                    
                    # Tạo thư mục scripts trong bin nếu chưa tồn tại
                    script_dir = os.path.join(self.base_dir, "bin", "scripts")
                    os.makedirs(script_dir, exist_ok=True)
                    
                    # Tạo file tạm với đuôi .sh để dễ nhận dạng
                    fd, temp_path = tempfile.mkstemp(suffix='.sh', prefix='script_', dir=script_dir)
                    
                    try:
                        # Ghi nội dung đã chuẩn hóa vào file
                        with os.fdopen(fd, 'w') as f:
                            f.write(normalized_content)
                        
                        # Cấp quyền thực thi cho file
                        os.chmod(temp_path, 0o777)
                        
                        # Sử dụng lệnh edit với trường script
                        edit_cmd = f'edit {task_id} script "{temp_path}"'
                        exit_code, output = self._run_command(edit_cmd)
                        
                        if exit_code != 0:
                            print(f"Error updating script content: {output}")
                            success = False
                        else:
                            print(f"Successfully updated script content for task {task_id}")
                    except Exception as e:
                        print(f"Error creating temporary script file: {e}")
                        success = False
        
        elif exec_mode == 2:  # Chế độ AI dynamic
            # Xử lý chế độ AI dynamic
            if 'ai_prompt' in task_data and 'system_metrics' in task_data:
                # Nếu đang chuyển từ chế độ khác sang AI dynamic, cần dùng cách tạo mới
                if current_task.get('exec_mode', 0) != 2:
                    print("Chuyển sang chế độ AI dynamic - tạo lại task")
                    
                    # Lưu lại tất cả thông tin task hiện tại
                    new_task_data = current_task.copy()
                    
                    # Cập nhật với thông tin mới
                    new_task_data.update(task_data)
                    
                    # Đảm bảo exec_mode là AI dynamic
                    new_task_data['exec_mode'] = 2
                    
                    # Xóa ID để tránh xung đột khi tạo mới
                    if 'id' in new_task_data:
                        del new_task_data['id']
                    
                    # Xóa task cũ
                    if not self.delete_task(task_id):
                        print(f"Error deleting old task when converting to AI dynamic")
                        return False
                    
                    # Tạo task mới
                    new_task_id = self.add_task(new_task_data)
                    if new_task_id <= 0:
                        print(f"Error creating new task when converting to AI dynamic")
                        return False
        
                    # Chuyển đổi task mới thành AI dynamic và đảm bảo nó được bật
                    ai_prompt = task_data['ai_prompt']
                    system_metrics = task_data['system_metrics']
                    if isinstance(system_metrics, list):
                        system_metrics = ','.join(system_metrics)
                    
                    if not self.convert_task_to_ai_dynamic(new_task_id, ai_prompt, system_metrics):
                        print(f"Error converting new task to AI dynamic mode")
                        # Không return False ở đây vì task đã được tạo thành công
                    
                    # Khôi phục các dependencies nếu có
                    if 'dependencies' in current_task and current_task['dependencies']:
                        for dep_id in current_task['dependencies']:
                            if not self.add_dependency(new_task_id, dep_id):
                                print(f"Warning: Failed to add dependency {dep_id} to new task {new_task_id}")
                    
                    # Khôi phục dependency behavior nếu có
                    if 'dep_behavior' in current_task and current_task['dep_behavior'] != 0:
                        if not self.set_dependency_behavior(new_task_id, current_task['dep_behavior']):
                            print(f"Warning: Failed to set dependency behavior for new task {new_task_id}")
                    
                    # Đảm bảo task mới được bật, ngay cả khi task cũ bị tắt
                    self._run_command("enable", str(new_task_id))
                    print(f"Enabled new AI Dynamic task {new_task_id}")
                    
                    print(f"Đã chuyển đổi task {task_id} thành task AI dynamic mới với ID {new_task_id}")
                    return True
                else:
                    # Đã là chế độ AI dynamic, chỉ cập nhật thông tin
                    ai_prompt = task_data['ai_prompt'].replace('"', '\\"')
                    system_metrics = task_data['system_metrics']
                    if isinstance(system_metrics, list):
                        system_metrics = ','.join(system_metrics)
                    system_metrics = system_metrics.replace('"', '\\"')
                    
                    convert_cmd = f'to-ai {task_id} "{ai_prompt}" "{system_metrics}"'
                    exit_code, output = self._run_command(convert_cmd)
                    
                    if exit_code != 0:
                        print(f"Error updating AI dynamic mode: {output}")
                        success = False
                    else:
                        # Đảm bảo task vẫn được bật sau khi cập nhật
                        enable_code, enable_output = self._run_command("enable", str(task_id))
                        if enable_code == 0:
                            print(f"Enabled AI Dynamic task {task_id} after updating")
        
        # Cập nhật lịch trình
        schedule_type = task_data.get('schedule_type', current_task.get('schedule_type', 0))
        
        if schedule_type == 1 and 'interval' in task_data:  # Interval scheduling
            interval_minutes = max(1, int(task_data.get('interval', 0) / 60))  # Convert seconds to minutes
            if interval_minutes != int(current_task.get('interval', 0) / 60):
                edit_cmd = f'edit {task_id} interval {interval_minutes}'
                exit_code, output = self._run_command(edit_cmd)
                if exit_code != 0:
                    print(f"Error updating interval: {output}")
                    success = False
        
        elif schedule_type == 2 and 'cron_expression' in task_data:  # Cron scheduling
            if task_data['cron_expression'] != current_task.get('cron_expression', ''):
                cron = task_data['cron_expression'].replace('"', '\\"')
                edit_cmd = f'edit {task_id} cron "{cron}"'
                exit_code, output = self._run_command(edit_cmd)
                if exit_code != 0:
                    print(f"Error updating cron expression: {output}")
                    success = False
        
        # Cập nhật thời gian chạy tối đa
        if 'max_runtime' in task_data and task_data['max_runtime'] != current_task.get('max_runtime', 0):
            edit_cmd = f'edit {task_id} runtime {task_data["max_runtime"]}'
            exit_code, output = self._run_command(edit_cmd)
            if exit_code != 0:
                print(f"Error updating max runtime: {output}")
                success = False
        
        # Cập nhật thư mục làm việc
        if 'working_dir' in task_data and task_data['working_dir'] != current_task.get('working_dir', ''):
            working_dir = task_data['working_dir'].replace('"', '\\"')
            edit_cmd = f'edit {task_id} dir "{working_dir}"'
            exit_code, output = self._run_command(edit_cmd)
            if exit_code != 0:
                print(f"Error updating working directory: {output}")
                success = False
        
        # Cập nhật hành vi phụ thuộc
        if 'dep_behavior' in task_data and task_data['dep_behavior'] != current_task.get('dep_behavior', 0):
            edit_cmd = f'edit {task_id} dep_behavior {task_data["dep_behavior"]}'
            exit_code, output = self._run_command(edit_cmd)
            if exit_code != 0:
                print(f"Error updating dependency behavior: {output}")
                success = False
        
        # Đảm bảo task được bật nếu ở chế độ AI Dynamic
        if exec_mode == 2 and success:
            enable_code, enable_output = self._run_command("enable", str(task_id))
            if enable_code == 0:
                print(f"Enabled AI Dynamic task {task_id} after updates")
        
        # Xóa cache để đảm bảo dữ liệu mới
        cache_key = f"task_{task_id}"
        if cache_key in self._cache:
            del self._cache[cache_key]
        
        # Kiểm tra lại task sau khi cập nhật
        updated_task = self.get_task(task_id, force_refresh=True)
        if updated_task and updated_task.get('exec_mode') == 2 and not updated_task.get('enabled', False):
            # Nếu task là AI Dynamic nhưng bị tắt, bật lại
            print(f"Task {task_id} là AI Dynamic task đã bị tắt - đang bật lại")
            exit_code, output = self._run_command("enable", str(task_id))
            if exit_code == 0:
                print(f"Đã bật lại AI Dynamic task {task_id}")
        
        return success
    
    def execute_task(self, task_id):
        """Thực thi một tác vụ ngay lập tức"""
        if self.simulation_mode:
            # Giả lập thực thi tác vụ
            if task_id in self.tasks:
                task = self.tasks[task_id]
                task['last_run_time'] = int(time.time())
                task['exit_code'] = 0
                return True
            return False
        
        print(f"Debug: Executing task {task_id} immediately")
        
        # Thực thi tác vụ sử dụng lệnh run
        exit_code, output = self._run_command("run", str(task_id))
        
        # In đầu ra chi tiết để debug
        print(f"Debug: Task run output: {output}")
        
        # Kiểm tra phản hồi để xác định thành công hay thất bại
        task_started = False
        if "Running task" in output and "Executing task" in output:
            task_started = True
            print(f"Debug: Task {task_id} started execution successfully")
        
        # Làm mới cache cho task này sau khi chạy
        # Đảm bảo không còn cache cho task này, để lần sau lấy dữ liệu mới
        cache_key = f"task_{task_id}"
        if cache_key in self._cache:
            del self._cache[cache_key]
            print(f"Debug: Cleared cache for task {task_id} after execution")
        
        # Đảm bảo view cache cũng được làm mới
        if self._last_view_task_id == task_id:
            self._last_view_task_id = -1
            self._last_view_output = None
        
        # Đợi một chút và thử lấy thông tin mới nhất của task
        try:
            time.sleep(1.0)
            # Lấy thông tin mới nhất của task
            updated_task = self.get_task(task_id, force_refresh=True)
            if updated_task:
                # Đánh dấu lệnh thành công nếu lấy được thông tin task mới
                print(f"Debug: Successfully retrieved updated task info after execution")
                return True
        except Exception as e:
            print(f"Debug: Error retrieving updated task info: {e}")
        
        # Trả về kết quả dựa trên phản hồi ban đầu nếu không lấy được thông tin cập nhật
        return task_started
    
    def get_api_key(self):
        """Get the configured API key"""
        if self.simulation_mode:
            return "SIMULATION_MODE_API_KEY"
        
        code, output = self._run_command("view-api-key")
        
        # Nếu không có output hoặc output rỗng
        if not output:
            return None
        
        # Trích xuất API key từ output
        # Output có định dạng: "Current API key: sk-8****ebdc"
        # hoặc "API key is configured and ready to use with AI-Dynamic tasks."
        api_key_match = re.search(r'Current API key: ([^\s]+)', output)
        if api_key_match:
            return api_key_match.group(1).strip()
        
        # Kiểm tra xem có thông báo API key đã được cấu hình không
        if "API key is configured and ready to use" in output:
            # Trả về giá trị placeholder để biết rằng API key đã được cấu hình
            return "CONFIGURED_API_KEY"
        
        # Nếu có lỗi hoặc không tìm thấy API key
        if "No API key is currently configured" in output or "Không thể tìm thấy API key" in output:
            return None
        
        # Mặc định trả về None nếu không tìm thấy API key
        return None

    def ai_generate_task(self, description):
        """Sử dụng AI để tạo một tác vụ dựa trên mô tả bằng ngôn ngữ tự nhiên
        
        Args:
            description (str): Mô tả tác vụ bằng ngôn ngữ tự nhiên
            
        Returns:
            dict: Kết quả tạo tác vụ với các thông tin cần thiết
        """
        if self.simulation_mode:
            # Trong chế độ giả lập, trả về dữ liệu mẫu
            return {
                'success': True,
                'content': 'df -h | awk \'NR>1 {print $5,$1}\' | while read size fs; do pct=$(echo $size | cut -d\'%\' -f1); if [ $pct -ge 90 ]; then echo "WARNING: $fs is $size full!"; fi; done',
                'is_script': False,
                'is_cron': False,
                'cron': '',
                'interval_minutes': 60,
                'schedule_description': 'Chạy mỗi giờ',
                'suggested_name': 'Kiểm tra ổ đĩa'
            }
        
        command = f'ai-create "{description}"'
        
        print(f"Sending AI create command with description: '{description}'")
        print(f"Sending to binary: '{command}'")
        
        # Thiết lập biến môi trường để thông báo cho backend biết đây là lệnh gọi từ API
        os.environ["FROM_TASKSCHEDULER_API"] = "1"
        
        output = ""
        try:
            # Khởi tạo quá trình tương tác
            with self.interactive_lock:
                try:
                    # Gửi lệnh kèm theo ký tự xuống dòng
                    os.write(self.master_fd, f"{command}\n".encode('utf-8'))
                    
                    # Đợi dữ liệu ban đầu với timeout 30 giây cho AI suy nghĩ
                    start_time = time.time()
                    initial_output = ""
                    confirmation_needed = False
                    
                    # Vòng lặp chính để đọc đầu ra
                    while time.time() - start_time < 45.0:  # Tăng timeout lên 45 giây
                        # Đọc dữ liệu có sẵn
                        readable, _, _ = select.select([self.master_fd], [], [], 1.0)
                        if self.master_fd in readable:
                            chunk = os.read(self.master_fd, 4096).decode('utf-8', errors='replace')
                            if not chunk:  # Kết nối đã đóng
                                break
                            
                            # Thêm vào output hiện tại
                            initial_output += chunk
                            
                            # In ra một phần của chunk để debug
                            print(f"Received data chunk (last 100 chars): {chunk[-100:] if len(chunk) > 100 else chunk}")
                            
                            # Kiểm tra nếu đã nhận được output đầy đủ
                            if "Do you want to create this task? (y/n):" in initial_output:
                                confirmation_needed = True
                                print("Confirmation prompt detected, sending 'y'")
                                time.sleep(0.5)  # Chờ một chút để đảm bảo binary đã sẵn sàng
                                os.write(self.master_fd, b"y\n")
                                time.sleep(1.0)  # Chờ để xử lý phản hồi
                                
                                # Đọc thêm đầu ra sau khi xác nhận
                                final_read_attempt = time.time()
                                while time.time() - final_read_attempt < 5.0:
                                    readable, _, _ = select.select([self.master_fd], [], [], 0.5)
                                    if self.master_fd in readable:
                                        additional_chunk = os.read(self.master_fd, 4096).decode('utf-8', errors='replace')
                                        if additional_chunk:
                                            initial_output += additional_chunk
                                            print(f"Additional output after confirmation: {additional_chunk}")
                                    else:
                                        # Không có dữ liệu mới, kiểm tra xem đã có đủ thông tin chưa
                                        if "Task added with ID:" in initial_output or "Task created successfully" in initial_output:
                                            print("Task creation confirmed")
                                            break
                        
                            # Nếu đã nhận được đủ thông tin cần thiết, có thể kết thúc sớm
                            if ("Suggested Name:" in initial_output and 
                                "Schedule:" in initial_output and 
                                "-----------------------------------------------------------" in initial_output):
                                # Đủ thông tin để parse, không cần đợi thêm
                                print("Received sufficient information to parse task")
                                if not confirmation_needed:
                                    # Kiểm tra xem có cần xác nhận không
                                    if "Do you want to create this task?" not in initial_output:
                                        break
                        else:
                            # Không có dữ liệu mới, chờ
                            time.sleep(0.5)
                    
                    # Lưu output sau khi đã xử lý
                    output = initial_output
                    print(f"Final output length: {len(output)} chars")
                
                except Exception as e:
                    print(f"Error in interactive AI command: {e}")
                    output = ""
        except Exception as e:
            print(f"Warning: Timeout or error waiting for output from command {command}: {str(e)}")
            # Thử lại một lần nữa với _run_command
            try:
                code, output = self._run_command("ai-create", f'"{description}"')
                if code != 0 or not output:
                    print(f"Error generating task: {str(e)}")
                    return {
                        'success': False,
                        'error': f'Lỗi khi tạo tác vụ: {str(e)}'
                    }
            except Exception as e2:
                print(f"Error in second attempt: {str(e2)}")
                return {
                    'success': False,
                    'error': f'Lỗi khi tạo tác vụ: {str(e2)}'
                }
        finally:
            # Xóa biến môi trường sau khi hoàn thành để không ảnh hưởng đến các lệnh khác
            if "FROM_TASKSCHEDULER_API" in os.environ:
                del os.environ["FROM_TASKSCHEDULER_API"]
        
        # Kiểm tra nếu output là một tuple (có thể xảy ra với _run_command)
        if isinstance(output, tuple) and len(output) >= 2:
            _, output = output
        
        print(f"Raw output from binary: '{output}'")
        
        # Kiểm tra lỗi từ AI
        if "[ERROR]" in output and "Failed to parse complete task JSON" in output:
            return {
                'success': False,
                'error': 'API của OpenAI không thể tạo task hợp lệ. Vui lòng thử lại với mô tả khác.'
            }
        
        if "Could not generate task from description." in output:
            return {
                'success': False,
                'error': 'Không thể tạo tác vụ từ mô tả này. Vui lòng thử mô tả rõ ràng hơn.'
            }
        
        # Xác định loại tác vụ (script hay command)
        is_script = False
        if "AI has generated a script based on your description:" in output:
            is_script = True
        elif "AI has generated a command based on your description:" in output:
            is_script = False
        else:
            # Kiểm tra xem có chứa "Script" trong thông tin schedule không
            if "Script" in output and "Schedule:" in output:
                is_script = True
        
        # Loại bỏ phần "Do you want to create this task? (y/n):" nếu có
        if "Do you want to create this task? (y/n):" in output:
            output = output.split("Do you want to create this task? (y/n):")[0].strip()
        
        # Trích xuất nội dung script/command
        content_lines = []
        in_content_section = False
        
        # Mẫu mới: "AI generated task: Script, Schedule: 0 2 * * *, Name: daily_www_backup"
        suggested_name = ""
        schedule_description = "Chạy theo lịch trình mặc định"
        cron_expression = ""
        is_cron = False
        interval_minutes = 60  # Mặc định 60 phút nếu không tìm thấy
        
        if "AI generated task:" in output:
            info_line = [line for line in output.split('\n') if "AI generated task:" in line]
            if info_line:
                info = info_line[0]
                if "Script" in info:
                    is_script = True
                
                # Trích xuất cron expression
                cron_match = re.search(r'Schedule: ([0-9*\s]+)', info)
                if cron_match:
                    cron_expression = cron_match.group(1).strip()
                    is_cron = True
                    # Phân tích biểu thức cron để xác định loại lịch trình
                    cron_parts = cron_expression.split()
                    if len(cron_parts) >= 2:
                        try:
                            hour = int(cron_parts[1])
                            schedule_description = f"Chạy mỗi ngày lúc {hour} giờ"
                        except ValueError:
                            schedule_description = f"Theo biểu thức Cron: {cron_expression}"
                
                # Trích xuất tên đề xuất
                name_match = re.search(r'Name: ([\w_-]+)', info)
                if name_match:
                    suggested_name = name_match.group(1).strip()
        
        # Phân tích nội dung output
        for line in output.split('\n'):
            # Tìm phần bắt đầu của nội dung
            if line.strip() == "-----------------------------------------------------------":
                if not in_content_section:
                    in_content_section = True
                    continue
                else:
                    in_content_section = False
                    break
            
            # Thu thập các dòng nội dung
            if in_content_section:
                content_lines.append(line)
        
        # Nếu không tìm thấy khu vực được đánh dấu rõ ràng, tìm kiếm theo cách khác
        if not content_lines:
            # Tìm kiếm nội dung sau "AI has generated a script/command based on your description:"
            if is_script:
                marker = "AI has generated a script based on your description:"
            else:
                marker = "AI has generated a command based on your description:"
            
            if marker in output:
                content_section = output.split(marker)[1].strip()
                
                # Lấy phần nội dung trước khi gặp "Schedule:" hoặc "Cron Expression:" hoặc dòng trống kép
                if "Schedule:" in content_section:
                    content_section = content_section.split("Schedule:")[0].strip()
                elif "Cron Expression:" in content_section:
                    content_section = content_section.split("Cron Expression:")[0].strip()
                elif "\n\n" in content_section:
                    content_section = content_section.split("\n\n")[0].strip()
                
                # Loại bỏ delimiter nếu có
                content_section = content_section.replace("-----------------------------------------------------------", "").strip()
                
                content_lines = content_section.split("\n")
        
        # Nếu vẫn không tìm thấy nội dung, tìm kiếm từ đầu ra chưa được xử lý
        if not content_lines and isinstance(output, str):
            # Tìm dòng bắt đầu với #!/bin/bash hoặc các ký tự phổ biến khác cho script
            lines = output.split('\n')
            start_index = -1
            end_index = -1
            
            for i, line in enumerate(lines):
                if line.strip().startswith("#!/bin/bash") or line.strip().startswith("#!/usr/bin/env"):
                    start_index = i
                    break
            
            if start_index >= 0:
                # Tìm dòng kết thúc (dòng trống sau script)
                for i in range(start_index + 1, len(lines)):
                    if not lines[i].strip() and i < len(lines) - 1 and not lines[i+1].strip():
                        end_index = i
                        break
                
                # Nếu không tìm thấy dòng kết thúc rõ ràng, tìm các marker khác
                if end_index < 0:
                    for i in range(start_index + 1, len(lines)):
                        if lines[i].strip() == "-----------------------------------------------------------" or \
                           "Schedule:" in lines[i] or "Cron Expression:" in lines[i]:
                            end_index = i - 1
                            break
                
                # Nếu vẫn không tìm thấy, lấy đến cuối
                if end_index < 0:
                    end_index = len(lines) - 1
                
                content_lines = lines[start_index:end_index+1]
        
        # Kết hợp các dòng nội dung
        if content_lines:
            content = '\n'.join(content_lines).strip()
        else:
            content = ""
        
        # Trích xuất thông tin lịch trình
        for line in output.split('\n'):
            if line.strip().startswith("Schedule:"):
                schedule_description = line.strip()[len("Schedule:"):].strip()
                break
        
        # Trích xuất biểu thức cron
        if not cron_expression:
            for line in output.split('\n'):
                if line.strip().startswith("Cron Expression:"):
                    cron_expression = line.strip()[len("Cron Expression:"):].strip()
                    is_cron = True
                    break
        
        # Phân tích biểu thức cron để xác định loại lịch trình
        if cron_expression:
            if re.match(r'^\d+\s+\d+\s+\*\s+\*\s+\*$', cron_expression):
                cron_parts = cron_expression.split()
                try:
                    hour = int(cron_parts[1])
                    schedule_description = f"Chạy mỗi ngày lúc {hour} giờ"
                except (ValueError, IndexError):
                    schedule_description = f"Theo biểu thức Cron: {cron_expression}"
        
        # Trích xuất tên đề xuất
        if not suggested_name:
            for line in output.split('\n'):
                if line.strip().startswith("Suggested Name:"):
                    suggested_name = line.strip()[len("Suggested Name:"):].strip()
                    break
                elif "Name:" in line and not "AI has generated" in line:
                    name_part = line.split("Name:")[1].strip()
                    # Lấy phần đầu tiên nếu có nhiều phần
                    suggested_name = name_part.split()[0].strip()
                    break
        
        # Nếu không tìm thấy tên đề xuất, đặt tên mặc định dựa trên mô tả
        if not suggested_name:
            words = description.split()
            if len(words) > 3:
                suggested_name = "_".join(words[:3]).lower()
            else:
                suggested_name = "_".join(words).lower()
            
            # Loại bỏ các ký tự không hợp lệ
            suggested_name = re.sub(r'[^a-zA-Z0-9_]', '', suggested_name)
            
            # Đảm bảo tên không quá dài
            if len(suggested_name) > 30:
                suggested_name = suggested_name[:30]
            
            # Thêm tiền tố để dễ nhận biết
            suggested_name = "task_" + suggested_name
        
        # Kiểm tra xem có tìm thấy nội dung hay không
        if not content:
            # Nếu không tìm thấy nội dung, trả về lỗi
            return {
                'success': False,
                'error': 'Không thể trích xuất nội dung từ phản hồi của AI'
            }
        
        return {
            'success': True,
            'content': content,
            'is_script': is_script,
            'is_cron': is_cron,
            'cron': cron_expression,
            'interval_minutes': interval_minutes,
            'schedule_description': schedule_description,
            'suggested_name': suggested_name,
            'raw_output': output  # Lưu toàn bộ đầu ra gốc để debug
        }

    def set_api_key(self, api_key):
        """Set the API key for AI services"""
        if self.simulation_mode:
            return True
        
        # Debug log
        print(f"Debug: Setting API key (masked): {api_key[:4]}...{api_key[-4:] if len(api_key) > 8 else '****'}")
        
        # Không cần bọc thêm trong dấu nháy kép, _run_command sẽ tự động làm điều này nếu cần
        code, output = self._run_command("set-api-key", api_key)
        
        # Kiểm tra kết quả
        print(f"Debug: set-api-key result: code={code}, output includes success: {'success' in output.lower()}")
        
        # Kiểm tra xem API key đã được cập nhật thành công chưa
        if code == 0 and ("updated successfully" in output or "saved successfully" in output):
            return True
        
        return False

    def ai_generate_command(self, goal, system_metrics):
        """Tạo lệnh dựa trên thông số hệ thống và mục tiêu"""
        if self.simulation_mode:
            # Trả về dữ liệu mẫu trong chế độ giả lập
            return {
                'success': True,
                'command': 'echo "Dynamic command based on metrics"'
            }
        
        # Gọi lệnh ai-generate với mục tiêu và metrics
        code, output = self._run_command("ai-generate", f'"{goal}"', f'"{system_metrics}"')
        
        if code == 0:
            # Trích xuất lệnh từ đầu ra
            command_match = re.search(r'Command:\s*`(.+?)`', output)
            if command_match:
                return {
                    'success': True,
                    'command': command_match.group(1).strip()
                }
            else:
                return {
                    'success': False,
                    'error': 'Không thể trích xuất lệnh từ kết quả'
                }
        else:
            return {
                'success': False,
                'error': output
            }

    def convert_task_to_ai_dynamic(self, task_id, goal, system_metrics):
        """Chuyển đổi task thành chế độ AI Dynamic và đảm bảo nó được bật"""
        if self.simulation_mode:
            return True
        
        # Lấy thông tin task trước khi chuyển đổi để xem trạng thái enabled
        current_task = self.get_task(task_id)
        was_enabled = current_task and current_task.get('enabled', True)
        
        # Gọi lệnh to-ai để chuyển đổi task
        code, output = self._run_command("to-ai", str(task_id), f'"{goal}"', f'"{system_metrics}"')
        
        success = code == 0
        
        if success:
            print(f"Debug: Successfully converted task {task_id} to AI Dynamic mode")
            
            # Đảm bảo task được bật sau khi chuyển đổi
            enable_result = self._run_command("enable", str(task_id))
            if enable_result[0] == 0:
                print(f"Debug: Enabled task {task_id} after converting to AI Dynamic mode")
                
                # Cập nhật cache
                cache_key = f"task_{task_id}"
                if cache_key in self._cache:
                    self._cache[cache_key]['enabled'] = True
                    print(f"Debug: Updated cache for task {task_id}, set enabled=True")
                
                # Làm mới view cache
                self._last_view_task_id = -1
                self._last_view_output = None
        else:
            print(f"Debug: Failed to convert task {task_id} to AI Dynamic mode: {output}")
        
        return success

    def get_available_system_metrics(self):
        """Lấy danh sách các system metrics khả dụng"""
        # Danh sách cố định của các metrics có sẵn
        return [
            {'id': 'cpu_load', 'name': 'Tải CPU', 'description': 'Phần trăm tải CPU'},
            {'id': 'mem_free', 'name': 'Bộ nhớ trống', 'description': 'Lượng bộ nhớ RAM còn trống'},
            {'id': 'mem_used', 'name': 'Bộ nhớ đã dùng', 'description': 'Lượng bộ nhớ RAM đã sử dụng'},
            {'id': 'disk:/', 'name': 'Ổ đĩa / (root)', 'description': 'Mức sử dụng ổ đĩa gốc'},
            {'id': 'disk:/home', 'name': 'Ổ đĩa /home', 'description': 'Mức sử dụng ổ đĩa /home'},
            {'id': 'disk:/var', 'name': 'Ổ đĩa /var', 'description': 'Mức sử dụng ổ đĩa /var'},
            {'id': 'load_avg', 'name': 'Tải trung bình', 'description': 'Chỉ số tải trung bình của hệ thống'},
            {'id': 'processes', 'name': 'Tiến trình', 'description': 'Thông tin về các tiến trình đang chạy'}
        ]

    def add_task(self, task_data):
        """Thêm một tác vụ mới"""
        if self.simulation_mode:
            # Tạo tác vụ mẫu trong chế độ giả lập
            task_data['id'] = self.next_id
            task_data['creation_time'] = int(time.time())
            task_data['exit_code'] = -1
            
            if 'schedule_type' in task_data and task_data['schedule_type'] == 1:  # SCHEDULE_INTERVAL
                # Tính thời gian chạy tiếp theo dựa trên interval
                task_data['next_run_time'] = int(time.time() + task_data.get('interval', 0))
            else:
                task_data['next_run_time'] = 0
            
            self.tasks[self.next_id] = task_data
            self.next_id += 1
            return task_data['id']
        
        # Xây dựng lệnh add trong chế độ tương tác
        # Dựa vào phân tích mã nguồn cli.c, cú pháp là: add <n> <command>
        # Sau đó mới đến các tùy chọn
        name = task_data.get('name', '').replace('"', '\\"')  # Escape dấu ngoặc kép trong tên
        command = task_data.get('command', '').replace('"', '\\"')  # Escape dấu ngoặc kép trong lệnh
        
        cmd_parts = [f'add "{name}"']
        
        # Xử lý chế độ thực thi
        exec_mode = task_data.get('exec_mode', 0)
        
        if exec_mode == 0:  # Chế độ lệnh
            # Thêm lệnh nếu có
            if command:
                cmd_parts.append(f'"{command}"')
        else:  # Chế độ script
            # Đối với tất cả các script, luôn lưu vào file và sử dụng -f
            if 'script_content' in task_data and task_data['script_content']:
                script_content = task_data.get('script_content').strip()
                
                if script_content:
                    # Tạo file script tạm thời từ nội dung
                    import tempfile
                    
                    # Tạo thư mục scripts trong bin nếu chưa tồn tại
                    script_dir = os.path.join(self.base_dir, "bin", "scripts")
                    os.makedirs(script_dir, exist_ok=True)
                    
                    # Tạo file tạm với đuôi .sh để dễ nhận dạng
                    fd, temp_path = tempfile.mkstemp(suffix='.sh', prefix='script_', dir=script_dir)
                    
                    try:
                        # Chuẩn hóa nội dung script
                        # Chuyển đổi các CRLF (Windows) thành LF (Unix) 
                        normalized_content = script_content.replace('\r\n', '\n')
                        
                        # Xử lý shebang nếu chưa có
                        if not normalized_content.strip().startswith('#!/'):
                            normalized_content = '#!/bin/bash\n' + normalized_content
                        
                        # Ghi nội dung đã chuẩn hóa vào file
                        with os.fdopen(fd, 'w') as f:
                            f.write(normalized_content)
                        
                        # Cấp quyền thực thi cho file (0777 cho phép tất cả người dùng thực thi)
                        os.chmod(temp_path, 0o777)
                        
                        print(f"Created script file with full permissions (0777): {temp_path}")
                        
                        # Thêm đường dẫn file vào lệnh
                        cmd_parts.append('-f')
                        cmd_parts.append(f'"{temp_path}"')
                        
                        print(f"Created script file from content: {temp_path}")
                    except Exception as e:
                        print(f"Error creating script file: {e}")
                        # Quay lại sử dụng lệnh trực tiếp
                        cmd_parts.append(f'"echo Error creating script: {str(e)}"')
                        exec_mode = 0
                else:
                    # Nếu nội dung trống, quay lại sử dụng lệnh trực tiếp
                    cmd_parts.append('"echo No script content provided"')
                    exec_mode = 0
            # Hoặc kiểm tra xem có script_file hay không
            elif 'script_file' in task_data and task_data['script_file']:
                script_file = task_data['script_file'].replace('"', '\\"')
                
                # Kiểm tra xem file có tồn tại và có nội dung không
                try:
                    if os.path.exists(script_file):
                        # Đọc nội dung file để kiểm tra
                        with open(script_file, 'r') as f:
                            script_content = f.read().strip()
                        
                        if script_content:
                            cmd_parts.append('-f')
                            cmd_parts.append(f'"{script_file}"')
                            print(f"Using script file: {script_file}")
                        else:
                            # File trống, quay lại sử dụng lệnh trực tiếp
                            cmd_parts.append('"echo Script file is empty"')
                            exec_mode = 0
                    else:
                        print(f"Script file not found: {script_file}")
                        # Quay lại sử dụng lệnh trực tiếp
                        cmd_parts.append(f'"echo Script file not found: {os.path.basename(script_file)}"')
                        exec_mode = 0
                except Exception as e:
                    print(f"Error checking script file: {e}")
                    # Quay lại sử dụng lệnh trực tiếp
                    cmd_parts.append(f'"echo Error with script file: {os.path.basename(script_file)}"')
                    exec_mode = 0
            else:
                # Không có nội dung script, quay lại sử dụng lệnh trực tiếp
                cmd_parts.append('"echo No script content provided"')
                exec_mode = 0
        
        # Xử lý lịch trình dựa trên loại
        schedule_type = task_data.get('schedule_type', 0)
        
        # Thêm các tùy chọn
        # Nếu có khoảng thời gian (interval)
        if schedule_type == 1 and task_data.get('interval'):
            # Chuyển đổi từ giây sang phút (backend chấp nhận phút)
            minutes = max(1, int(task_data.get('interval', 0) / 60))
            cmd_parts.append(f"-t {minutes}")
        
        # Nếu có biểu thức cron
        elif schedule_type == 2 and task_data.get('cron_expression'):
            cron = task_data.get('cron_expression', '').replace('"', '\\"')
            cmd_parts.append(f'-s "{cron}"')
        
        # Thêm thư mục làm việc nếu có
        if task_data.get('working_dir'):
            working_dir = task_data.get('working_dir', '').replace('"', '\\"')
            cmd_parts.append(f'-d "{working_dir}"')
        
        # Thêm thời gian chạy tối đa nếu có
        if task_data.get('max_runtime'):
            cmd_parts.append(f"-m {task_data.get('max_runtime')}")
        
        # Xóa cờ -y để cho phương pháp thay thế làm việc tốt hơn
        # cmd_parts.append("-y")
        
        # Kết hợp các phần tạo thành lệnh hoàn chỉnh
        command = ' '.join(cmd_parts)
        
        # Thực thi lệnh
        print(f"Sending command: {command}")
        
        # Phương pháp 1: Sử dụng _send_command và xử lý kết quả
        try:
            # Sử dụng interactive process để có thể trả lời prompt
            self._start_interactive_process()
            
            # Gửi lệnh add
            os.write(self.master_fd, f"{command}\n".encode())
            
            # Đọc output với timeout
            output = self._read_output(timeout=6.0)
            print(f"Initial output from add command: '{output}'")
            
            # Kiểm tra xem có confirmation prompt không
            if "Do you want to create this task? (y/n):" in output:
                # Tự động trả lời "y"
                os.write(self.master_fd, b"y\n")
                time.sleep(0.5)
                
                # Đọc kết quả sau khi xác nhận
                output = self._read_output(timeout=3.0)
                print(f"Output after confirmation: '{output}'")
            
            # Tìm task ID từ kết quả
            id_match = re.search(r"Task added with ID: (\d+)", output)
            if id_match:
                task_id = int(id_match.group(1))
                print(f"Task added successfully with ID: {task_id}")
                
                # Cập nhật trạng thái của tác vụ nếu cần
                if 'enabled' in task_data and not task_data['enabled']:
                    self._run_command("disable", str(task_id))
                
                return task_id
            
            # Nếu không tìm thấy ID nhưng không có lỗi rõ ràng
            if "Task creation cancelled" not in output and "Error" not in output:
                # Thử phương pháp 2: Lấy danh sách tác vụ để tìm ID mới nhất
                print("Using fallback method to find newly created task")
                tasks = self.get_all_tasks()
                if tasks:
                    max_id = max(task.get('id', 0) for task in tasks)
                    print(f"Latest task ID found: {max_id}")
                    return max_id
            
            # Phương pháp 3: Thử tạo lại task bằng lệnh add trực tiếp
            print("Task creation failed. Trying direct add command...")
            
            # Tạo lệnh add mới không có tùy chọn -y
            direct_command = ' '.join(cmd_parts)
            
            # Khởi động lại process để đảm bảo trạng thái sạch
            self._start_interactive_process()
            
            # Gửi lệnh và trả lời prompt
            os.write(self.master_fd, f"{direct_command}\n".encode())
            time.sleep(1.0)
            os.write(self.master_fd, b"y\n")
            time.sleep(1.0)
            
            # Đọc kết quả
            output = self._read_output(timeout=3.0)
            print(f"Direct command output: '{output}'")
            
            # Kiểm tra lại ID
            id_match = re.search(r"Task added with ID: (\d+)", output)
            if id_match:
                task_id = int(id_match.group(1))
                print(f"Task added successfully with ID: {task_id} (direct method)")
                return task_id
            
            # Kiểm tra một lần nữa danh sách tác vụ
            tasks = self.get_all_tasks()
            if tasks:
                max_id = max(task.get('id', 0) for task in tasks)
                print(f"Latest task ID found after direct command: {max_id}")
                return max_id
            
        except Exception as e:
            print(f"Error during task creation: {e}")
        
        print(f"All methods failed to create task")
        return -1

    def _extract_script_content(self, output):
        """Trích xuất nội dung script từ đầu ra của lệnh view hoặc script-content"""
        if not output:
                return None
            
        # Log output để debug
        print(f"Debug: _extract_script_content input length: {len(output)}")
        
        # Loại bỏ dấu nhắc ">" ở cuối nếu có
        if output.strip().endswith(">"):
            output = output[:output.rfind(">")].strip()
        
        # Chuỗi tìm kiếm cho phần Script
        script_markers = [
            "\nScript:\n",         # Format 1: Có dấu xuống dòng trước và sau
            "\nScript:",           # Format 2: Chỉ có dấu xuống dòng trước
            "Script:\n",           # Format 3: Chỉ có dấu xuống dòng sau
            "Script:"              # Format 4: Không có dấu xuống dòng
        ]
        
        script_pos = -1
        script_marker_used = ""
        
        # Thử tất cả các định dạng marker
        for marker in script_markers:
            pos = output.find(marker)
            if pos != -1:
                script_pos = pos
                script_marker_used = marker
                print(f"Debug: Found script marker '{script_marker_used}' at position {script_pos}")
                break
        
        if script_pos == -1:
            print("Debug: Script section not found in output (tried all markers)")
            return None
        
        # Tính vị trí bắt đầu của nội dung script
        script_start = script_pos + len(script_marker_used)
        
        # Đảm bảo bỏ qua các dòng trống ở đầu nếu có
        while script_start < len(output) and (output[script_start] == "\n" or output[script_start] == " "):
            script_start += 1
        
        # Tìm vị trí kết thúc của script (đầu mục tiếp theo hoặc hết file)
        next_section = -1
        
        # Danh sách các đầu mục có thể xuất hiện sau Script
        possible_sections = [
            "\nSchedule:",
            "\nWorking Directory:", 
            "\nMax Runtime:", 
            "\nCreated:", 
            "\nLast Run:",
            "\nNext Run:",
            "\nExit Code:",
            "\nDependencies:",
            "\nDependency Behavior:"
        ]
        
        for section in possible_sections:
            pos = output.find(section, script_start)
            if pos != -1 and (next_section == -1 or pos < next_section):
                next_section = pos
                print(f"Debug: Found end marker '{section}' at position {pos}")
        
        # Trích xuất nội dung script
        if next_section != -1:
            script_content = output[script_start:next_section].strip()
        else:
            script_content = output[script_start:].strip()
        
        # In thông tin debug
        if script_content:
            print(f"Debug: Extracted script content (first 50 chars): {script_content[:50]}...")
        else:
            print("Debug: Extracted script content is empty")
        
        # Nếu không tìm thấy nội dung hoặc nội dung quá ngắn, thử cách khác
        if not script_content or len(script_content) < 5:
            print("Debug: Using fallback line-by-line method to extract script content")
            
            # Kiểm tra từng dòng sau "Script:" để lấy nội dung script
            lines = output.split('\n')
            script_content_lines = []
            in_script_section = False
        
            # Tìm phần Script:
            for i, line in enumerate(lines):
                if "Script:" in line:
                    in_script_section = True
                    print(f"Debug: Found Script marker at line {i}: '{line}'")
                    # Bắt đầu từ dòng tiếp theo
                continue
                
                # Nếu đã vào phần script, thu thập nội dung
                if in_script_section:
                    # Kiểm tra nếu dòng hiện tại là một đầu mục mới
                    is_new_section = False
                    for section in possible_sections:
                        # Loại bỏ \n ở đầu để tìm kiếm chính xác
                        clean_section = section.strip()
                        if clean_section in line:
                            is_new_section = True
                            print(f"Debug: Found end section at line {i}: '{line}'")
                            break
                    
                    # Nếu là đầu mục mới, dừng thu thập
                    if is_new_section:
                        in_script_section = False
                        break
                    
                    # Thêm dòng vào nội dung script
                    script_content_lines.append(line)
            
            # Ghép các dòng script thành nội dung hoàn chỉnh
            if script_content_lines:
                script_content = '\n'.join(script_content_lines).strip()
                print(f"Debug: Fallback method extracted script content (first 50 chars): {script_content[:50]}...")
                
        # Final content check
        if script_content and len(script_content) > 5:
            print(f"Debug: Successfully extracted script content, length: {len(script_content)}")
            return script_content
        else:
            print("Debug: Failed to extract valid script content")
            return None
    
    def get_script_content(self, task_id):
        """Lấy nội dung script của tác vụ
        
        Args:
            task_id (int): ID của tác vụ cần lấy nội dung script
            
        Returns:
            str: Nội dung script hoặc None nếu không có
        """
        if self.simulation_mode:
            # Trả về dữ liệu mẫu trong chế độ giả lập
            task = self.tasks.get(task_id)
            if task and task.get('exec_mode') == 1:
                return "#!/bin/bash\necho 'Sample script content'"
            return None
        
        # Kiểm tra xem tác vụ có tồn tại không và có phải là script không
        task = self.get_task(task_id)
        if not task:
            print(f"Debug: Task {task_id} not found")
            return None
        
        if 'script_content' in task and task['script_content']:
            print(f"Debug: Found script content in task cache")
            return task['script_content']
        
        # Kiểm tra xem có đầu ra của lệnh view đã lưu trong cache không
        if self._last_view_task_id == task_id and self._last_view_output:
            print(f"Debug: Using cached view output for task {task_id}")
            script_content = self._extract_script_content(self._last_view_output)
            if script_content:
                # Cập nhật cache
                cache_key = f"task_{task_id}"
                if cache_key in self._cache:
                    self._cache[cache_key]['script_content'] = script_content
                print(f"Debug: Successfully extracted script content from cached view output")
                return script_content
            else:
                print(f"Debug: Could not extract script content from cached view output")
        
        # Sử dụng lệnh view để lấy nội dung task, script-content không tồn tại trong binary
        print(f"Debug: Getting script content for task {task_id} using view command")
        exit_code, output = self._run_command("view", str(task_id))
        
        # Lưu lại output để sử dụng sau nếu cần
        self._last_view_output = output
        self._last_view_task_id = task_id
        
        if exit_code != 0:
            print(f"Debug: Error getting task details for task {task_id}: {output}")
            return None

        # Trích xuất nội dung script từ output
        script_content = self._extract_script_content(output)
        
        if script_content:
            # Cập nhật cache
            cache_key = f"task_{task_id}"
            if cache_key in self._cache:
                self._cache[cache_key]['script_content'] = script_content
            print(f"Debug: Successfully extracted script content for task {task_id}")
            return script_content
        
        print(f"Debug: Could not extract script content for task {task_id}")
        return None

    def delete_task(self, task_id):
        """Xóa một tác vụ theo ID
        
        Args:
            task_id (int): ID của tác vụ cần xóa
            
        Returns:
            bool: True nếu xóa thành công, False nếu thất bại
        """
        if self.simulation_mode:
            # Xóa từ danh sách tác vụ mô phỏng
            if task_id in self.tasks:
                del self.tasks[task_id]
                print(f"Debug: (Simulation) Deleted task {task_id}")
                return True
            else:
                print(f"Debug: (Simulation) Task {task_id} not found")
                return False
        
        # Xóa tác vụ thật bằng lệnh remove
        print(f"Debug: Deleting task {task_id}")
        exit_code, output = self._run_command("remove", str(task_id))
        
        # Kiểm tra kết quả
        success = exit_code == 0 and ("Task successfully removed" in output or "Task removed" in output)
        
        if success:
            print(f"Debug: Successfully deleted task {task_id}")
            
            # Xóa task khỏi cache nếu có
            cache_key = f"task_{task_id}"
            if cache_key in self._cache:
                del self._cache[cache_key]
                print(f"Debug: Removed task {task_id} from cache")
            
            # Cập nhật lại danh sách tác vụ trong bộ nhớ
            self._task_list = None  # Reset để lần sau sẽ tải lại
        else:
            print(f"Debug: Failed to delete task {task_id}. Exit code: {exit_code}")
            print(f"Debug: Output: {output}")
            
            # Kiểm tra xem có lỗi cụ thể không
            if "does not exist" in output:
                print(f"Debug: Task {task_id} does not exist")
                # Xóa khỏi cache nếu có
                cache_key = f"task_{task_id}"
                if cache_key in self._cache:
                    del self._cache[cache_key]
                    print(f"Debug: Removed non-existent task {task_id} from cache")
                return True  # Xem như xóa thành công nếu task không tồn tại
        
        return success