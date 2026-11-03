#![cfg(unix)]
use dal_pdu_gui::console::{Console, Event};
use serialport::SerialPort;
use std::{
    io::{Read, Write},
    thread,
    time::Duration,
};

#[test]
fn console_sends_one_cr_terminated_command_and_streams_reply() {
    let (mut master, slave) = serialport::TTYPort::pair().unwrap();
    master.set_timeout(Duration::from_secs(2)).unwrap();
    let server = thread::spawn(move || {
        let mut request = [0; 5];
        master.read_exact(&mut request).unwrap();
        assert_eq!(&request, b"help\r");
        master
            .write_all(b"help\r\nAvailable commands\r\n> ")
            .unwrap();
        thread::sleep(Duration::from_millis(200));
    });
    let console = Console::start(slave.name().unwrap(), 9600);
    assert!(matches!(
        console.events.recv_timeout(Duration::from_secs(2)).unwrap(),
        Event::Connected
    ));
    assert!(console.send("help\n").is_err());
    console.send("help").unwrap();
    let mut output = Vec::new();
    while !output.ends_with(b"> ") {
        match console.events.recv_timeout(Duration::from_secs(2)).unwrap() {
            Event::Output(bytes) => output.extend(bytes),
            Event::Failed(error) => panic!("{error}"),
            Event::Connected => {}
        }
    }
    assert_eq!(output, b"help\r\nAvailable commands\r\n> ");
    server.join().unwrap();
}
