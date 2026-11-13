# Hướng dẫn sử dụng

Đây là ứng dụng riêng; không thay đổi chương trình Camera. Chỉ đọc dữ liệu nguồn.

1. Mở CameraFtpSync.exe. Chọn **Duyệt** hoặc nhập thư mục nguồn. Nhập bộ lọc (ví dụ `lsc*.txt`), loại trừ và tùy chọn thư mục con.
2. Nhập host, tài khoản, mật khẩu, cổng và thư mục FTP tuyệt đối đã tồn tại. Có thể dán URL rồi bấm **Phân tích URL**; URL không được có tài khoản/mật khẩu. `ftp://` được chọn mặc định nâng cấp TLS explicit; `ftps://` là TLS implicit. SFTP không được hỗ trợ.
3. Chọn ExplicitFTPS (khuyên dùng), ImplicitFTPS hoặc PlainFTP. PlainFTP yêu cầu đánh dấu chấp nhận dữ liệu/mật khẩu không mã hóa. Không được tắt xác minh chứng chỉ. Với CA nội bộ, quản trị viên phải cài CA đáng tin vào Windows đúng phạm vi tài khoản.
4. Đặt Station ID riêng cho từng máy. Giữ **Tách theo Station ID**. Bấm **Xem đích** để kiểm tra. Tắt tách chỉ khi bạn là chủ duy nhất của đích và đã xác nhận. Hai máy không được ghi cùng một tên file.
5. Chọn chu kỳ 1/5/10/15/30/60 hoặc nhập 1–1440 phút. Thời gian ổn định 5–300 giây. Lần quét đầu ghi nhận file; lần quét sau mới đủ điều kiện nếu đã ổn định. **Đồng bộ ngay** có thể dùng lại sau thời gian ổn định.
6. `ReadBackSHA256` kiểm tra nội dung sau upload. Từ bản 1.0.1, tùy chọn cũ `Size` vẫn kiểm tra SHA-256 của cả file tạm trước publish và file final sau publish; không có đường thay thế chỉ dựa vào kích thước. Không gọi SIZE là xác minh mật mã.
7. Chọn **Lưu mật khẩu** chỉ khi muốn lưu trong Windows Credential Manager. Không chọn thì mật khẩu chỉ tồn tại trong phiên. Bỏ chọn rồi **Lưu / Áp dụng** sẽ xóa credential đã lưu. INI không chứa mật khẩu.
8. **Lưu / Áp dụng**, **Test kết nối**, rồi tùy chọn **Test ghi/đổi tên**. Test kết nối chỉ đăng nhập/liệt kê thư mục gốc; test ghi tạo file thử duy nhất và có hộp xác nhận. Nếu test thất bại, xem log và kiểm tra các file `.write-test`/`.rename-test` còn lại.
9. Bấm **Start / Tiếp tục** để bật lịch; **Tạm dừng** dừng lịch nhưng cho lượt hiện tại hoàn tất. **Đồng bộ ngay / Force Verify** thực hiện một lượt kể cả khi lịch tạm dừng. Mọi lượt luôn hash đầy đủ. **Dừng / sửa cấu hình** hủy lượt hiện tại rồi cho phép sửa các trường.
10. Máy chủ FTP không bảo đảm atomic replace. Phải chấp nhận **khoảng trống khi đổi tên** trước khi tự cập nhật file đã tồn tại. Nếu phía đọc yêu cầu tên final luôn tồn tại, không bật lựa chọn này; cần giải pháp phía máy chủ.

File không đổi không được upload và không tạo backup. File bị cắt ngắn, sửa phần đã xác nhận, remote thay đổi hoặc file remote lạ sẽ báo Conflict. Không xóa file để bỏ qua cảnh báo. Với thay thế thủ công: dừng, kiểm tra cả hai bản và journal; nhập đúng tên file tương đối vào trường cuối; bấm **Thay thế + backup** và xác nhận. Bản cũ có hậu tố `^yyyyMMddHHmmss.<unique-id>` (giờ UTC 24h). Chỉ backup được app ghi nhận mới được dọn khi bật tùy chọn retention.

`Pending` khác `Success`: file đang ghi/không đọc được vẫn chờ. Log có UTC, mức và danh mục; **Mở log/history** mở thư mục log, **Xuất log** lưu các sự kiện đang hiển thị. Lịch sử lâu hơn nằm trong log ngày. Tiến độ upload hiển thị byte.

Nút X ẩn xuống tray; nhấp biểu tượng để mở lại. **Exit** dừng đồng bộ. Đăng xuất hoặc tắt máy cũng dừng app. Khởi động cùng Windows mặc định tắt và chỉ bật theo lựa chọn của bạn.

Ảnh chụp GUI thật chưa có vì máy phát triển không cài MFC. Xem `GUI_SMOKE_CHECKLIST.md` để chụp các màn hình bắt buộc trên máy nghiệm thu; không sử dụng sơ đồ hay mockup thay cho ảnh nghiệm thu.

Khi nâng từ 1.0.0 lên 1.0.1: đọc `REVIEW_1.0.1.md`, xử lý hết journal đang dang dở trước khi đổi bản. Định danh job mới tách theo tài khoản và chế độ TLS. Không xóa state/journal để vượt qua cảnh báo.
