use dal_pdu_gui::{
    map::{self, Bank},
    rtu::{crc16, decode_read, frame, read_response},
};
use std::{
    io::{self, Read},
    time::Duration,
};
struct Fragments {
    data: Vec<u8>,
    index: usize,
}
impl Read for Fragments {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        if self.index == self.data.len() {
            return Ok(0);
        }
        buf[0] = self.data[self.index];
        self.index += 1;
        Ok(1)
    }
}
fn response(bytes: Vec<u8>, function: u8) -> Result<Vec<u8>, String> {
    read_response(
        &mut Fragments {
            data: bytes,
            index: 0,
        },
        1,
        function,
        Duration::from_millis(100),
    )
}
#[test]
fn standard_crc_vector_and_zero_based_offset() {
    assert_eq!(crc16(b"123456789"), 0x4b37);
    assert_eq!(
        frame(1, 3, &[0, 0, 0, 10]),
        vec![1, 3, 0, 0, 0, 10, 0xc5, 0xcd]
    );
    let rows = map::rows();
    assert_eq!(
        rows.iter().find(|r| r.address == 40100).unwrap().offset(),
        99
    );
    assert_eq!(
        rows.iter().find(|r| r.address == 30020).unwrap().offset(),
        19
    );
}
#[test]
fn partial_reads_crc_exception_address_and_truncation() {
    assert_eq!(
        response(frame(1, 3, &[4, 0x50, 0x44, 0x55, 0]), 3).unwrap(),
        vec![4, 0x50, 0x44, 0x55, 0]
    );
    let mut bad = frame(1, 3, &[2, 0, 1]);
    bad[3] ^= 1;
    assert!(response(bad, 3).unwrap_err().contains("CRC"));
    assert!(response(frame(1, 0x83, &[2]), 3)
        .unwrap_err()
        .contains("illegal address"));
    assert!(response(frame(2, 3, &[2, 0, 1]), 3).is_err());
    assert!(response(frame(1, 4, &[2, 0, 1]), 3).is_err());
    assert!(response(vec![1, 3, 2, 0], 3).is_err());
    assert!(response(vec![1, 3, 255], 3)
        .unwrap_err()
        .contains("Oversized"));
}
#[test]
fn words_and_bit_packing_are_checked() {
    assert_eq!(
        decode_read(3, 2, &[4, 0x12, 0x34, 0xab, 0xcd]).unwrap(),
        vec![0x1234, 0xabcd]
    );
    assert_eq!(decode_read(1, 3, &[1, 5]).unwrap(), vec![1, 0, 1]);
    assert!(decode_read(4, 2, &[2, 0, 1]).is_err());
}
#[test]
fn firmware_write_constraints_and_atomic_pairs() {
    let rows = map::rows();
    let get = |addr| rows.iter().find(|r| r.address == addr).unwrap();
    assert!(get(40001).parse("805").is_err());
    assert!(get(40001).parse("20010").is_err());
    assert_eq!(get(40001).parse("12000").unwrap(), vec![12000]);
    assert_eq!(get(40031).parse("-12.3").unwrap(), vec![(-123i16) as u16]);
    assert!(get(40031).parse("NaN").is_err());
    assert!(get(40031).parse("1.23").is_err());
    assert!(get(40030).parse("2").is_err());
    assert!(get(40030).parse("4").is_ok());
    assert!(get(40036).parse("57600").is_err());
    let baud = get(40036).parse("115200").unwrap();
    assert_eq!(baud, vec![1, 49664]);
    assert_eq!(
        get(40036).write_request(&baud),
        (16, vec![0, 35, 0, 2, 4, 0, 1, 0xc2, 0])
    );
    assert_eq!(get(1).write_request(&[1]), (5, vec![0, 0, 0xff, 0]));
    assert!(get(40105).parse("2").is_err());
    assert!(get(40040).parse("4294967295").is_err());
    assert!(get(40060).parse("1").is_err());
    assert_eq!(get(40060).parse("0xB007").unwrap(), vec![0xb007]);
}
#[test]
fn telemetry_sentinels_scaling_and_complete_map() {
    let rows = map::rows();
    let get = |addr| rows.iter().find(|r| r.address == addr).unwrap();
    assert_eq!(get(30021).display(&[(-123i16) as u16]), "-12.3");
    assert!(get(30021).display(&[0x8000]).contains("Unavailable"));
    assert!(get(30024).display(&[0xffff]).contains("Unavailable"));
    assert_eq!(get(30035).display(&[123]), "1230");
    assert_eq!(get(40100).display(&[0x5044, 0x5500]), "PDU");
    assert_eq!(get(40020).display(&[1, 0]), "65536");
    let mut addresses = std::collections::BTreeSet::new();
    for row in rows.iter().filter(|r| r.bank == Bank::Input) {
        for address in row.address..row.address + row.count {
            assert!(addresses.insert(address));
        }
    }
    assert_eq!(addresses, (30020..=30049).collect());
}
