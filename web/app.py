#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
from flask import Flask, render_template, request, redirect, url_for, flash, jsonify
from datetime import datetime
import time
import uuid
import werkzeug.utils
import json

# Thêm thư mục gốc vào sys.path để import các module
current_dir = os.path.dirname(os.path.abspath(__file__))
parent_dir = os.path.dirname(current_dir)
if parent_dir not in sys.path:
    sys.path.append(parent_dir)

# Import TaskAPI từ api
from web.api.task_api import TaskAPI

# Directory for storing uploaded scripts
UPLOAD_FOLDER = os.path.join(os.path.dirname(parent_dir), "bin", "uploads")
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

# Allowed extensions for script files
ALLOWED_EXTENSIONS = {'sh', 'py', 'pl', 'js', 'rb', 'bat', 'cmd'}

# Khởi tạo Flask app
app = Flask(__name__)
app.secret_key = '42ae8dfd4c3c74024ff5beed3b0c0e76'
app.config['UPLOAD_FOLDER'] = UPLOAD_FOLDER
app.config['MAX_CONTENT_LENGTH'] = 1 * 1024 * 1024  # 1MB limit

# Khởi tạo TaskAPI
task_api = TaskAPI()

# Đảm bảo data_dir nằm trong thư mục bin để phù hợp với backend C
data_dir = os.path.join(parent_dir, "bin", "data")
os.makedirs(data_dir, exist_ok=True)

# Helper functions
def format_timestamp(timestamp):
    """Format unix timestamp thành chuỗi datetime dễ đọc"""
    if not timestamp or timestamp == 0:
        return "Chưa thiết lập"
    return datetime.fromtimestamp(timestamp).strftime('%d/%m/%Y %H:%M:%S')

def get_frequency_name(frequency):
    """Chuyển đổi mã tần suất thành tên"""
    frequency_names = {
        0: "Một lần",
        1: "Hàng ngày",
        2: "Hàng tuần",
        3: "Hàng tháng",
        4: "Tùy chỉnh"
    }
    return frequency_names.get(frequency, "Không xác định")

def get_schedule_type_name(schedule_type):
    """Chuyển đổi mã kiểu lịch thành tên"""
    schedule_type_names = {
        0: "Thủ công",
        1: "Theo khoảng thời gian",
        2: "Theo biểu thức Cron"
    }
    return schedule_type_names.get(schedule_type, "Không xác định")

def get_dependency_behavior_name(behavior):
    """Chuyển đổi mã hành vi phụ thuộc thành tên"""
    behavior_names = {
        0: "Chạy nếu bất kỳ tác vụ phụ thuộc nào thành công",
        1: "Chạy chỉ khi tất cả tác vụ phụ thuộc thành công",
        2: "Chạy nếu bất kỳ tác vụ phụ thuộc nào hoàn thành",
        3: "Chạy sau khi tất cả tác vụ phụ thuộc hoàn thành"
    }
    return behavior_names.get(behavior, "Không xác định")

@app.template_filter('format_timestamp')
def _format_timestamp(timestamp):
    return format_timestamp(timestamp)

# Template context processor để thêm helper function
@app.context_processor
def utility_processor():
    def now():
        return datetime.now()
    return dict(now=now)

# Trang chủ - Hiển thị danh sách tác vụ
@app.route('/')
def index():
    tasks = task_api.get_all_tasks()
    
    # Bổ sung thông tin khác cho tác vụ
    for task in tasks:
        task['next_run_time_fmt'] = format_timestamp(task.get('next_run_time', 0))
        task['last_run_time_fmt'] = format_timestamp(task.get('last_run_time', 0))
        task['frequency_name'] = get_frequency_name(task.get('frequency', 0))
        task['schedule_type_name'] = get_schedule_type_name(task.get('schedule_type', 0))
    
    return render_template('index.html', tasks=tasks)

# Hiển thị form tạo tác vụ mới
@app.route('/task/new')
def new_task():
    return render_template('task_form.html', task=None, action="create")

# Tạo tác vụ mới
@app.route('/task/create', methods=['POST'])
def create_task():
    task_data = {
        'name': request.form.get('name', ''),
        'command': request.form.get('command', ''),
        'working_dir': request.form.get('working_dir', ''),
        'enabled': True
    }
    
    # Xử lý kiểu lịch trình
    schedule_type = int(request.form.get('schedule_type', 0))
    task_data['schedule_type'] = schedule_type
    
    if schedule_type == 1:  # Interval
        interval_minutes = int(request.form.get('interval', 1))
        task_data['interval'] = interval_minutes * 60  # Chuyển phút sang giây
    elif schedule_type == 2:  # Cron
        task_data['cron_expression'] = request.form.get('cron_expression', '')
    
    # Xử lý chế độ thực thi
    exec_mode = int(request.form.get('exec_mode', 0))
    task_data['exec_mode'] = exec_mode
    
    if exec_mode == 1:  # Script mode
        script_mode = request.form.get('script_mode', 'content')
        
        if script_mode == 'file':
            script_file = request.form.get('script_file', '')
            if script_file:
                task_data['script_file'] = script_file
        else:
            script_content = request.form.get('script_content', '')
            if script_content:
                task_data['script_content'] = script_content
    
    # Thêm thời gian chạy tối đa nếu có
    max_runtime = request.form.get('max_runtime')
    if max_runtime:
        task_data['max_runtime'] = int(max_runtime)
    
    # Thêm tác vụ
    task_id = task_api.add_task(task_data)
    
    if task_id > 0:
        flash('Tác vụ đã được tạo thành công!', 'success')
        return redirect(url_for('view_task', task_id=task_id))
    else:
        flash('Không thể tạo tác vụ. Vui lòng thử lại!', 'error')
        return render_template('task_form.html', task=task_data, action="create")

# Xem chi tiết tác vụ
@app.route('/task/<int:task_id>')
def view_task(task_id):
    task = task_api.get_task(task_id)
    if task:
        # Thêm các thông tin định dạng vào task
        task['creation_time_fmt'] = format_timestamp(task.get('creation_time', 0))
        task['next_run_time_fmt'] = format_timestamp(task.get('next_run_time', 0))
        task['last_run_time_fmt'] = format_timestamp(task.get('last_run_time', 0))
        
        # Thêm các tên mô tả
        task['schedule_type_name'] = get_schedule_type_name(task.get('schedule_type', 0))
        task['frequency_name'] = get_frequency_name(task.get('frequency', 0))
        task['dependency_behavior_name'] = get_dependency_behavior_name(task.get('dep_behavior', 0))
        
        # Xử lý mã kết quả exit_code
        exit_code = task.get('exit_code', -1)
        if exit_code == 0:
            task['exit_code_desc'] = f"Thành công ({exit_code})"
        elif exit_code == -1:
            if task.get('last_run_time', 0) == 0:
                task['exit_code_desc'] = "Chưa chạy"
            else:
                task['exit_code_desc'] = f"Lỗi ({exit_code})"
        else:
            task['exit_code_desc'] = f"Lỗi ({exit_code})"
        
        # Lấy danh sách tên của các tác vụ phụ thuộc (nếu có)
        dependency_names = {}
        if task.get('dependencies'):
            for dep_id in task['dependencies']:
                dep_task = task_api.get_task(dep_id)
                if dep_task:
                    dependency_names[dep_id] = dep_task['name']
                else:
                    dependency_names[dep_id] = f"Tác vụ {dep_id}"
        
        return render_template('task_detail.html', task=task, dependency_names=dependency_names)
    else:
        flash('Không tìm thấy tác vụ với ID này!', 'error')
        return redirect(url_for('index'))

# Hiển thị form chỉnh sửa tác vụ
@app.route('/task/<int:task_id>/edit')
def edit_task(task_id):
    task = task_api.get_task(task_id)
    if task:
        return render_template('task_form.html', task=task, action="update")
    else:
        flash('Không tìm thấy tác vụ với ID này!', 'error')
        return redirect(url_for('index'))

# Cập nhật tác vụ
@app.route('/task/<int:task_id>/update', methods=['POST'])
def update_task(task_id):
    task = task_api.get_task(task_id)
    if not task:
        flash('Không tìm thấy tác vụ với ID này!', 'error')
        return redirect(url_for('index'))
    
    task_data = {
        'id': task_id,
        'name': request.form.get('name', ''),
        'command': request.form.get('command', ''),
        'working_dir': request.form.get('working_dir', ''),
        'enabled': task.get('enabled', True)
    }
    
    # Xử lý kiểu lịch trình
    schedule_type = int(request.form.get('schedule_type', 0))
    task_data['schedule_type'] = schedule_type
    
    if schedule_type == 1:  # Interval
        interval_minutes = int(request.form.get('interval', 1))
        task_data['interval'] = interval_minutes * 60  # Chuyển phút sang giây
    elif schedule_type == 2:  # Cron
        task_data['cron_expression'] = request.form.get('cron_expression', '')
    
    # Xử lý chế độ thực thi
    exec_mode = int(request.form.get('exec_mode', 0))
    task_data['exec_mode'] = exec_mode
    
    if exec_mode == 1:  # Script mode
        script_mode = request.form.get('script_mode', 'content')
        
        if script_mode == 'file':
            script_file = request.form.get('script_file', '')
            if script_file:
                task_data['script_file'] = script_file
        else:
            script_content = request.form.get('script_content', '')
            if script_content:
                task_data['script_content'] = script_content
    
    # Thêm thời gian chạy tối đa nếu có
    max_runtime = request.form.get('max_runtime')
    if max_runtime:
        task_data['max_runtime'] = int(max_runtime)
    
    # Cập nhật tác vụ
    success = task_api.update_task(task_data)
    
    if success:
        flash('Tác vụ đã được cập nhật thành công!', 'success')
        return redirect(url_for('index'))
    else:
        flash('Không thể cập nhật tác vụ. Vui lòng thử lại!', 'error')
        return render_template('task_form.html', task=task_data, action="update")

# Xóa tác vụ
@app.route('/task/<int:task_id>/delete', methods=['POST'])
def delete_task(task_id):
    success = task_api.delete_task(task_id)
    if success:
        flash('Tác vụ đã được xóa thành công!', 'success')
    else:
        flash('Không thể xóa tác vụ. Vui lòng thử lại!', 'error')
    return redirect(url_for('index'))

# Chạy tác vụ ngay lập tức
@app.route('/task/<int:task_id>/run', methods=['POST'])
def run_task(task_id):
    success = task_api.execute_task(task_id)
    if success:
        flash('Tác vụ đã được kích hoạt thành công!', 'success')
    else:
        flash('Không thể kích hoạt tác vụ. Vui lòng thử lại!', 'error')
    return redirect(url_for('view_task', task_id=task_id))

# Bật/tắt tác vụ
@app.route('/task/<int:task_id>/toggle', methods=['POST'])
def toggle_task(task_id):
    task = task_api.get_task(task_id)
    if not task:
        flash('Không tìm thấy tác vụ với ID này!', 'error')
        return redirect(url_for('index'))
    
    # Cập nhật tác vụ với trạng thái đã đảo ngược
    task_data = task.copy()
    task_data['enabled'] = not task.get('enabled', True)
    
    success = task_api.update_task(task_data)
    
    if success:
        status = "bật" if task_data['enabled'] else "tắt"
        flash(f'Tác vụ đã được {status} thành công!', 'success')
    else:
        flash('Không thể thay đổi trạng thái tác vụ. Vui lòng thử lại!', 'error')
    
    return redirect(url_for('view_task', task_id=task_id))

# Hiển thị thống kê
@app.route('/stats')
def stats():
    tasks = task_api.get_all_tasks()
    
    # Tính toán các thống kê
    total_tasks = len(tasks)
    enabled_tasks = sum(1 for task in tasks if task.get('enabled', False))
    scheduled_tasks = sum(1 for task in tasks if task.get('next_run_time', 0) > 0)
    completed_tasks = sum(1 for task in tasks if task.get('last_run_time', 0) > 0)
    
    # Tính toán thống kê trạng thái
    tasks_by_status = {
        'success': sum(1 for task in tasks if task.get('exit_code', -1) == 0 and task.get('last_run_time', 0) > 0),
        'failed': sum(1 for task in tasks if task.get('exit_code', -1) != 0 and task.get('exit_code', -1) != -1),
        'never_run': sum(1 for task in tasks if task.get('last_run_time', 0) == 0)
    }
    
    # Phân loại nhiệm vụ theo tần suất
    tasks_by_frequency = {}
    for task in tasks:
        freq = task.get('frequency', 0)
        freq_name = get_frequency_name(freq)
        tasks_by_frequency[freq_name] = tasks_by_frequency.get(freq_name, 0) + 1
    
    # Đảm bảo tasks_by_frequency có tất cả các loại tần suất với ít nhất là 0
    for freq in range(5):  # Có 5 loại tần suất từ 0-4
        freq_name = get_frequency_name(freq)
        if freq_name not in tasks_by_frequency:
            tasks_by_frequency[freq_name] = 0
    
    # Truyền tất cả biến cần thiết cho template
    return render_template('stats.html', 
                          total_tasks=total_tasks,
                          enabled_tasks=enabled_tasks,
                          scheduled_tasks=scheduled_tasks,
                          completed_tasks=completed_tasks,
                          tasks_by_status=tasks_by_status,
                          tasks_by_frequency=tasks_by_frequency)

# Hàm kiểm tra xem file có phần mở rộng được cho phép không
def allowed_file(filename):
    return '.' in filename and filename.rsplit('.', 1)[1].lower() in ALLOWED_EXTENSIONS

# API để upload file script
@app.route('/api/upload_script', methods=['POST'])
def upload_script():
    if 'script_file' not in request.files:
        return jsonify({'success': False, 'message': 'Không có file nào được gửi lên'})
    
    file = request.files['script_file']
    
    if file.filename == '':
        return jsonify({'success': False, 'message': 'Không có file nào được chọn'})
    
    if file and allowed_file(file.filename):
        # Tạo tên file duy nhất để tránh xung đột
        filename = str(uuid.uuid4()) + '_' + werkzeug.utils.secure_filename(file.filename)
        file_path = os.path.join(app.config['UPLOAD_FOLDER'], filename)
        
        try:
            # Lưu file và set quyền thực thi
            file.save(file_path)
            os.chmod(file_path, 0o755)  # Thêm quyền thực thi
            
            return jsonify({
                'success': True, 
                'file_path': file_path,
                'original_filename': file.filename
            })
        except Exception as e:
            return jsonify({'success': False, 'message': f'Lỗi khi lưu file: {str(e)}'})
    
    return jsonify({'success': False, 'message': 'Loại file không được hỗ trợ'})

# Xử lý trang lỗi 404
@app.errorhandler(404)
def page_not_found(e):
    return render_template('error.html', error=e), 404

# Xử lý trang lỗi 500
@app.errorhandler(500)
def server_error(e):
    return render_template('error.html', error=e), 500

if __name__ == '__main__':
    app.run(debug=True, host='0.0.0.0', port=5000) 