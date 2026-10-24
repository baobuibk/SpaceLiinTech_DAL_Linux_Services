//! Exercise the real serial backend against an in-process RTU slave on a PTY.
#![cfg(unix)]
use dal_pdu_gui::{
    rtu::{crc16, frame, Client},
    worker::{Config, Event, Worker},
};
use serialport::SerialPort;
use std::{
    io::{Read, Write},
    thread,
    time::{Duration, Instant},
};

#[test]
fn real_serial_reads_writes_exceptions_and_bad_acknowledgments() {
    let (mut master, slave) = serialport::TTYPort::pair().unwrap();
    master.set_timeout(Duration::from_secs(2)).unwrap();
    let path = slave.name().unwrap();
    let server = thread::spawn(move || {
        for (function, payload) in [
            (3, vec![4, 0x50, 0x44, 0x55, 0]),
            (6, vec![0, 0, 0x2e, 0xe0]),
            (16, vec![0, 35, 0, 2]),
            (5, vec![0, 0, 0, 0]),
            (0x84, vec![2]),
        ] {
            let mut request = vec![0; if function == 16 { 13 } else { 8 }];
            master.read_exact(&mut request).unwrap();
            let n = request.len();
            assert_eq!(
                crc16(&request[..n - 2]),
                u16::from_le_bytes([request[n - 2], request[n - 1]])
            );
            // Byte-at-a-time replies exercise partial reads through the actual driver.
            for byte in frame(1, function, &payload) {
                master.write_all(&[byte]).unwrap();
            }
        }
        thread::sleep(Duration::from_millis(100));
    });
    let mut client = Client::open(&path, 115200, 1, Duration::from_millis(500)).unwrap();
    assert_eq!(client.read(3, 99, 2).unwrap(), vec![0x5044, 0x5500]);
    client.write(6, &[0, 0, 0x2e, 0xe0]).unwrap();
    client.write(16, &[0, 35, 0, 2, 4, 0, 1, 0xc2, 0]).unwrap();
    assert!(client
        .write(5, &[0, 0, 0xff, 0])
        .unwrap_err()
        .contains("acknowledgment"));
    assert!(client
        .read(4, 0, 1)
        .unwrap_err()
        .contains("illegal address"));
    server.join().unwrap();
}

#[test]
fn background_worker_polls_supported_ranges_and_can_stop() {
    let (mut master, slave) = serialport::TTYPort::pair().unwrap();
    master.set_timeout(Duration::from_secs(2)).unwrap();
    let path = slave.name().unwrap();
    let server = thread::spawn(move || loop {
        let mut request = [0; 8];
        if master.read_exact(&mut request).is_err() {
            break;
        }
        let function = request[1];
        assert!((1..=4).contains(&function), "Monitoring must never write");
        let offset = u16::from_be_bytes([request[2], request[3]]);
        let count = u16::from_be_bytes([request[4], request[5]]);
        if function == 3 {
            assert!(matches!(
                (offset, count),
                (0, 1)
                    | (19, 2)
                    | (21, 2)
                    | (29..=34, 1)
                    | (35, 2)
                    | (39, 2)
                    | (49, 1)
                    | (59, 1)
                    | (99, 2)
                    | (101..=105, 1)
                    | (106, 2)
                    | (108, 1)
            ));
        }
        let bytes = if function <= 2 {
            (count as usize).div_ceil(8)
        } else {
            count as usize * 2
        };
        let mut payload = vec![bytes as u8];
        payload.resize(bytes + 1, 0);
        if master.write_all(&frame(1, function, &payload)).is_err() {
            break;
        }
    });
    let worker = Worker::start(Config {
        port: path,
        baud: 115200,
        unit: 1,
        timeout_ms: 100,
    });
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        assert!(Instant::now() < deadline, "No snapshot received");
        if let Event::Snapshot(values, _) =
            worker.events.recv_timeout(Duration::from_secs(2)).unwrap()
        {
            assert!(values.iter().all(Result::is_ok));
            break;
        }
    }
    drop(worker);
    drop(slave);
    server.join().unwrap();
}

#[test]
fn a_lost_write_reply_times_out_without_retransmitting() {
    let (mut master, slave) = serialport::TTYPort::pair().unwrap();
    master.set_timeout(Duration::from_millis(300)).unwrap();
    let path = slave.name().unwrap();
    let server = thread::spawn(move || {
        let mut request = [0; 8];
        master.read_exact(&mut request).unwrap();
        assert_eq!(request[1], 6);
        let mut extra = [0; 1];
        assert!(
            master.read(&mut extra).is_err(),
            "Write must not be retransmitted"
        );
    });
    let mut client = Client::open(&path, 115200, 1, Duration::from_millis(100)).unwrap();
    let start = Instant::now();
    assert!(client
        .write(6, &[0, 29, 0, 1])
        .unwrap_err()
        .contains("timeout"));
    assert!(start.elapsed() < Duration::from_secs(1));
    server.join().unwrap();
}
