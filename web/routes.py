@app.route('/task/<int:task_id>/run', methods=['POST'])
def run_task(task_id):
    """Chạy tác vụ ngay lập tức"""
    if not is_authenticated():
        return redirect(url_for('login'))
    
    # Thiết lập chế độ chờ đến khi task hoàn thành
    task_api.set_run_mode('wait_complete')
    
    # Thực thi task
    success = task_api.execute_task(task_id)
    
    # Khôi phục lại chế độ interactive
    task_api.set_run_mode('interactive')
    
    if success:
        flash('Tác vụ đã được thực thi thành công!', 'success')
    else:
        flash('Không thể thực thi tác vụ. Vui lòng kiểm tra nhật ký.', 'danger')
    
    return redirect(url_for('view_task', task_id=task_id))
    
 