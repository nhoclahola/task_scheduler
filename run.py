#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
import time
import signal
import subprocess
import threading
import logging
import argparse
from datetime import datetime
import webbrowser

# Thiết lập logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)

logger = logging.getLogger('task_scheduler')

# Đường dẫn đến thư mục gốc của ứng dụng
BASE_DIR = os.path.dirname(os.path.abspath(__file__))

def get_binary_path():
    """Xác định đường dẫn đến file thực thi"""
    bin_dir = os.path.join(BASE_DIR, "bin")
    binary = os.path.join(bin_dir, "taskscheduler")
    
    # Kiểm tra trên Windows
    if sys.platform.startswith('win'):
        binary += ".exe"
    
    return binary

def start_backend(simulation_mode=False):
    """Khởi động backend C"""
    if simulation_mode:
        logger.info("Chạy ở chế độ giả lập - không khởi động backend C")
        os.environ['TASK_SCHEDULER_SIMULATION'] = '1'
        return None
    
    # Đường dẫn đến binary
    binary = get_binary_path()
    logger.info(f"Binary path: {binary}")
    
    # Đường dẫn đến thư mục bin
    bin_dir = os.path.join(BASE_DIR, "bin")
    
    # Đường dẫn đến thư mục data
    # Đảm bảo thư mục data nằm trong thư mục bin để phù hợp với cách chạy của binary C
    data_dir = os.path.join(bin_dir, "data")
    os.makedirs(data_dir, exist_ok=True)
    
    # Đường dẫn đến thư mục lib
    lib_dir = os.path.join(BASE_DIR, "lib")
    
    # Kiểm tra xem binary có tồn tại không
    if not os.path.exists(binary):
        logger.error(f"Không tìm thấy binary tại {binary}")
        logger.info("Chuyển sang chế độ giả lập")
        os.environ['TASK_SCHEDULER_SIMULATION'] = '1'
        return None
    
    # Kiểm tra quyền thực thi
    if not os.access(binary, os.X_OK):
        try:
            os.chmod(binary, 0o755)
            logger.info(f"Đã thêm quyền thực thi cho {binary}")
        except Exception as e:
            logger.error(f"Không thể thiết lập quyền thực thi cho {binary}: {e}")
            logger.info("Chuyển sang chế độ giả lập")
            os.environ['TASK_SCHEDULER_SIMULATION'] = '1'
            return None
    
    # Backend đã được tích hợp vào API qua chế độ tương tác
    # Không cần khởi động process riêng ở đây
    logger.info("Backend sẽ được khởi động tự động qua TaskAPI")
    return None

def start_webapp():
    """Khởi động ứng dụng web Flask"""
    web_dir = os.path.join(BASE_DIR, "web")
    
    # Nếu chạy từ PyInstaller bundle
    if getattr(sys, 'frozen', False):
        app_path = os.path.join(sys._MEIPASS, "web")
    else:
        app_path = web_dir
        
    logger.info(f"Khởi động ứng dụng web từ {app_path}")
    
    try:
        # Import Flask app
        sys.path.append(web_dir)
        from app import app
        
        # Lấy địa chỉ IP và port cho ứng dụng web
        host = '127.0.0.1'  # Localhost
        port = 5000
        
        # Khởi động ứng dụng web
        threading.Thread(target=lambda: app.run(host=host, port=port, debug=False), daemon=True).start()
        logger.info(f"Ứng dụng web đã được khởi động tại http://{host}:{port}")
        
        # Mở trình duyệt
        webbrowser.open(f"http://{host}:{port}")
        
        return True
    except Exception as e:
        logger.error(f"Không thể khởi động ứng dụng web: {e}")
        return False

def signal_handler(sig, frame):
    """Xử lý tín hiệu ngắt"""
    logger.info("Đang tắt Task Scheduler...")
    sys.exit(0)

def print_banner():
    """Hiển thị banner khi khởi động"""
    banner = """
    ===============================================
      _____         _      ____      _              _       _           
     |_   _|_ _ ___| | __ / ___|  __| |_   _  ___  _| |_   _| | ___ _ __ 
       | |/ _` / __| |/ / \___ \ / _` | | | |/ _ \/ | | | | | |/ _ \ '__|
       | | (_| \__ \   <   ___) | (_| | |_| |  __/_| | |_| | |  __/ |   
       |_|\__,_|___/_|\_\ |____/ \__,_|\__, |\___(_|_|\__,_|_|\___|_|   
                                       |___/                             
    ===============================================
    """
    print(banner)
    print(f"Phiên bản: 1.0.0")
    print(f"Thời gian: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("===============================================")

def main():
    """Hàm chính để khởi động hệ thống Task Scheduler"""
    # Phân tích tham số dòng lệnh
    parser = argparse.ArgumentParser(description='Task Scheduler System')
    parser.add_argument('--simulation', action='store_true', help='Chạy ở chế độ giả lập')
    parser.add_argument('--no-web', action='store_true', help='Không khởi động giao diện web')
    args = parser.parse_args()
    
    # Hiển thị banner
    print_banner()
    
    # Đăng ký xử lý tín hiệu ngắt
    signal.signal(signal.SIGINT, signal_handler)
    
    # Khởi động backend C
    backend_process = start_backend(simulation_mode=args.simulation)
    
    # Khởi động ứng dụng web
    if not args.no_web:
        if not start_webapp():
            logger.error("Không thể khởi động hệ thống Task Scheduler")
            sys.exit(1)
    
    # Giữ chương trình chạy
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        logger.info("Nhận được tín hiệu ngắt. Đang tắt...")
    finally:
        logger.info("Đang tắt Task Scheduler...")

if __name__ == "__main__":
    main() 