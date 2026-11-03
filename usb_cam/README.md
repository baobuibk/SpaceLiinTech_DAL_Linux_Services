# DAL USB camera capture

This project copies the `usb-cam` Yocto recipe's Python source and camera
configuration. `target` embeds both in one executable shell wrapper at
`build-target/usb_cam`. `deploy` stages it at `service/deploy/usb_cam/usb_cam`.

```sh
make -C service/usb_cam target
make -C service/usb_cam deploy
```

The service-level `make -C service deploy` also stages this command. Install
the staged file at `/home/root/tools/bin/usb_cam` on the OBC. It needs Python
3, `v4l2-ctl`, `uvcvideo`, and access to the USB camera GPIO controls.

The bundled map is in `files/usb-cam.json`. The board's SEL1–SEL0 positions
`00`, `01`, `10`, and `11` select cameras 1–4, respectively. Because the JSON
stores `[SEL0, SEL1]`, its entries are `[0,0]`, `[1,0]`, `[0,1]`, and `[1,1]`.
An external map can be supplied with `--config` without rebuilding. The
program leaves selected camera power and switch state in place after use.
The switch OE is active low: the command sets `en_switch_usb=1` while changing
SEL0/SEL1, then sets `en_switch_usb=0` and checks it before using the camera.

The flight software's `uvc_cfg <camera> set led_pwm <0..100>` stores CAMERA_LED
duty in byte 31 of that camera's 32-byte profile (field mask bit 12). The
`usb_cam` tool preserves this field while applying and reading V4L2 settings;
it does not control PWM itself. During `uvc_ctl <camera> capture <request_id>`,
the flight software sets PWM before invoking `usb_cam` and turns it off after
the capture. Profiles live in `/var/lib/dal/uvc/cameraN.bin`; set
`DAL_UVC_PROFILE_DIR` to override that directory in a test environment.
