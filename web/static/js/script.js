/* Custom JavaScript for Task Scheduler */

// Cập nhật ngày giờ theo định dạng Việt Nam
function formatDate(date) {
    return date.toLocaleString('vi-VN', {
        day: '2-digit',
        month: '2-digit',
        year: 'numeric',
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit'
    });
}

// Function để hiển thị thông báo toast
function showToast(message, type = 'success') {
    // Tạo toast container nếu chưa tồn tại
    if (!document.querySelector('.toast-container')) {
        const toastContainer = document.createElement('div');
        toastContainer.className = 'toast-container position-fixed top-0 end-0 p-3';
        document.body.appendChild(toastContainer);
    }
    
    // Tạo toast
    const toastId = 'toast-' + Date.now();
    const toastHTML = `
        <div id="${toastId}" class="toast" role="alert" aria-live="assertive" aria-atomic="true">
            <div class="toast-header ${type === 'success' ? 'bg-success text-white' : 'bg-danger text-white'}">
                <strong class="me-auto">${type === 'success' ? 'Thành công' : 'Lỗi'}</strong>
                <small>${formatDate(new Date())}</small>
                <button type="button" class="btn-close btn-close-white" data-bs-dismiss="toast" aria-label="Close"></button>
            </div>
            <div class="toast-body">
                ${message}
            </div>
        </div>
    `;
    
    // Thêm toast vào container
    document.querySelector('.toast-container').innerHTML += toastHTML;
    
    // Hiển thị toast
    const toastElement = document.getElementById(toastId);
    const toast = new bootstrap.Toast(toastElement, { delay: 5000 });
    toast.show();
    
    // Xóa toast khỏi DOM sau khi ẩn
    toastElement.addEventListener('hidden.bs.toast', function () {
        toastElement.remove();
    });
}

// Quản lý form submission với AJAX
document.addEventListener('DOMContentLoaded', function() {
    // Xử lý các nút có thuộc tính data-confirm
    document.querySelectorAll('[data-confirm]').forEach(button => {
        button.addEventListener('click', function(e) {
            if (!confirm(this.getAttribute('data-confirm'))) {
                e.preventDefault();
                e.stopPropagation();
            }
        });
    });
    
    // Xử lý ẩn flash messages sau một khoảng thời gian
    document.querySelectorAll('.alert').forEach(alert => {
        setTimeout(() => {
            const closeBtn = alert.querySelector('.btn-close');
            if (closeBtn) {
                closeBtn.click();
            }
        }, 5000);
    });
    
    // Hiển thị đồng hồ hiện tại ở footer (nếu có phần tử)
    const clockElement = document.getElementById('current-time');
    if (clockElement) {
        function updateClock() {
            clockElement.textContent = formatDate(new Date());
        }
        updateClock();
        setInterval(updateClock, 1000);
    }
    
    // Helper function để chuyển đổi định dạng thời gian từ giây sang chuỗi dễ đọc
    window.formatDuration = function(seconds) {
        if (!seconds || seconds <= 0) return "0 giây";
        
        const days = Math.floor(seconds / 86400);
        seconds %= 86400;
        const hours = Math.floor(seconds / 3600);
        seconds %= 3600;
        const minutes = Math.floor(seconds / 60);
        seconds %= 60;
        
        let result = [];
        if (days > 0) result.push(`${days} ngày`);
        if (hours > 0) result.push(`${hours} giờ`);
        if (minutes > 0) result.push(`${minutes} phút`);
        if (seconds > 0) result.push(`${seconds} giây`);
        
        return result.join(', ');
    };
});

// Xử lý AJAX form submission (nếu cần trong tương lai)
function submitFormAjax(formElement, successCallback, errorCallback) {
    const formData = new FormData(formElement);
    
    fetch(formElement.action, {
        method: formElement.method,
        body: formData,
        headers: {
            'X-Requested-With': 'XMLHttpRequest'
        }
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            if (successCallback) successCallback(data);
            else showToast(data.message || 'Thao tác thành công!', 'success');
        } else {
            if (errorCallback) errorCallback(data);
            else showToast(data.message || 'Đã xảy ra lỗi!', 'error');
        }
    })
    .catch(error => {
        console.error('Error:', error);
        if (errorCallback) errorCallback({ message: 'Lỗi kết nối máy chủ' });
        else showToast('Lỗi kết nối máy chủ', 'error');
    });
    
    return false;
} 