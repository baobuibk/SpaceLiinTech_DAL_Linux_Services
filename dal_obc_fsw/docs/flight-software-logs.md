# Xem log flight software trên OBC

`dal_obc_fsw` chạy dưới systemd; log chuẩn ra và log lỗi được ghi vào journal của
`dal_obc_fsw.service`. Sau khi cài gói deploy, chế độ verbose được bật và các
bản dump housekeeping định kỳ được ẩn theo mặc định.

## Theo dõi và tra cứu

Chạy trên board với quyền root (hoặc thêm `sudo` trước `journalctl`):

```sh
journalctl -u dal_obc_fsw.service -f -o short-iso
```

Lệnh trên hiển thị log mới theo thời gian thực, kèm dấu thời gian. Nhấn
`Ctrl+C` để thoát. Để xem 200 dòng gần nhất của lần boot hiện tại hoặc giới
hạn theo thời gian:

```sh
journalctl -u dal_obc_fsw.service -b -n 200 --no-pager
journalctl -u dal_obc_fsw.service --since "10 minutes ago" --no-pager
```

Lọc các lệnh console, frame RS-422, lỗi và quá trình tải dữ liệu EXP:

```sh
journalctl -u dal_obc_fsw.service -b -o cat --no-pager | grep -E '\[CONSOLE\]|\[RX\]|\[TX\]|\[EXP-DATA\]|ERROR|failed'
```

`[CONSOLE] RX/TX` là lệnh và phản hồi qua Unix socket của flight software;
`[RX]/[TX]` là frame của giao diện TMTC. `[EXP-DATA]` báo việc tải, lưu hoặc
thử lại dữ liệu từ EXP. Log `[EXP]`/`[PDU]` không có tiền tố `[fsw]` khi chạy
`tools/fsw_rig.py` là log từ thiết bị giả lập trên host, không phải journal
trên board.

## Chọn mức chi tiết

File `/etc/default/dal-obc-fsw` chứa hai biến sau sau khi chạy `make install`:

```sh
DAL_OBC_VERBOSE=1
DAL_OBC_QUIET_HOUSEKEEPING=1
```

Đặt `DAL_OBC_QUIET_HOUSEKEEPING=0` để xem cả frame và lệnh housekeeping định
kỳ. Đặt `DAL_OBC_VERBOSE=0` để tắt các bản dump chi tiết. Sau khi sửa file,
áp dụng cấu hình và kiểm tra service:

```sh
systemctl restart dal_obc_fsw.service
systemctl status dal_obc_fsw.service --no-pager
```

Một lần `make -C /home/root/deploy install` tiếp theo sẽ đặt cả hai biến về
`1` và khởi động lại service. Nếu journal không có log mới, kiểm tra trạng
thái service và file cấu hình trước khi kiểm tra cổng UART hoặc RS-422.
