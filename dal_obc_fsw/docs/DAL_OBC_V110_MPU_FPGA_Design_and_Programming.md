# DAL OBC V1.1.0: MPU–FPGA Interface and Programming Guide

Document date: 13 October 2026  
Hardware reference: BEE-DAL OBC schematic, revision 1.1.0  
Scope: SAMA7G54 MPU, Artix-7 FPGA, configuration Flash, parameter EEPROM, and FPGA programming.

## 1. Evidence and scope

This document is based on `DAL_OBC_V110.pdf`, principally sheets 11 (SAMA7G54 PIO), 28 (FPGA supplies), 29 (FPGA banks and configuration), and 30 (Flash/EEPROM). The enlarged CFGBVS screenshot supplied with the review confirms the CFGBVS pull-up connection.

Connections below describe the schematic, not a verified assembled PCB. Actual fitted components, PCB continuity, supply voltages, MPU pin multiplexing, and software behavior must be checked during bring-up. Suggested protocols and update policies are implementation recommendations, not functions already established by the schematic.

**Correction to the earlier review:** CFGBVS is connected to the 3.3 V FPGA I/O rail through a 4.7 kΩ pull-up. It is not floating. No CFGBVS repair is indicated by the supplied schematic detail.

## 2. Architecture

| Reference | Device | Purpose |
|---|---|---|
| U3 | Microchip SAMA7G54 | Host MPU and application processor |
| U11 | AMD/Xilinx XC7A35T-1FTG256C | Artix-7 FPGA with volatile SRAM configuration |
| U18 | Infineon/Cypress S25FL128LAGMFI010 | 128-Mbit serial NOR Flash containing FPGA boot images |
| U17 | onsemi CAT93C46BHU4I-GT3 | 1-Kbit Microwire EEPROM for parameters |
| U13 | TI SN3257-Q1 | Four-channel switch selecting MPU or FPGA access to Flash |
| U14 | TI SN3257-Q1 | Four-channel switch selecting MPU or FPGA access to EEPROM |
| Q7 | MMBT2222A | Inverting MPU-controlled pull-down for PROGRAM_B |
| J2 | Eight-pin header | FPGA JTAG connection |

The FPGA configuration is lost when power is removed. U18 retains the boot bitstream. U17 is a separate parameter memory and is not the FPGA boot ROM.

The architecture supports two independent activities:

1. **Configuration:** the FPGA reads its bitstream from U18, or receives a temporary configuration through JTAG.
2. **Application communication:** the running FPGA exchanges data with the MPU over UART and four GPIO signals.

The switched SPI connections are MPU-to-memory access paths. They do not constitute a direct MPU-to-FPGA application SPI bus.

## 3. Direct MPU–FPGA pin connections

### 3.1 UART and optional synchronous clock

Net names are retained exactly as shown. TX/RX labels follow the MPU-side signal direction; the FPGA HDL must assign the opposite receive/transmit role.

| Schematic net | MPU port / function | MPU ball | FPGA ball | FPGA bank | Required HDL role |
|---|---|---|---|---|---|
| FPGA_USART_TX | PC23 / FLEXCOM7_TX | F3 | T7 | 14 | Input: UART RX from MPU |
| FPGA_USART_RX | PC24 / FLEXCOM7_RX | C2 | R8 | 14 | Output: UART TX to MPU |
| FPGA_USART_CLK | PB25 / FLEXCOM7_CLK | J14 | P8 | 14 | Direction determined by synchronous USART configuration |

R147 and R148 provide 22 Ω series resistance on the MPU UART data lines; R502 provides 22 Ω on the optional clock line.

For ordinary asynchronous UART, implement only RX and TX. Configure matching baud rate, data length, parity, and stop bits on both ends. The presence of the clock wire does not require synchronous operation.

Sheet 29 also provides a separate FPGA console through J10 and FPGA pins T15/T14. That console is distinct from the MPU–FPGA UART and can be used for independent diagnostics if implemented in HDL.

### 3.2 General-purpose handshake signals

| Net | MPU port | MPU ball | FPGA ball | Bank | Series resistors |
|---|---|---|---|---|---|
| MPU_FPGA_IO_0 | PD1 | F4 | R7 | 14 | R370 and R374, each 22 Ω |
| MPU_FPGA_IO_1 | PD2 | H12 | R6 | 14 | R371 and R375, each 22 Ω |
| MPU_FPGA_IO_2 | PD3 | B15 | T5 | 14 | R372 and R376, each 22 Ω |
| MPU_FPGA_IO_3 | PD8 | B13 | R5 | 14 | R373 and R377, each 22 Ω |

These are configurable FPGA I/O, not a predefined parallel bus. Define one owner and direction per signal before enabling outputs.

A possible assignment is IO_0 = FPGA-ready, IO_1 = FPGA interrupt, IO_2 = MPU request, and IO_3 = acknowledgement. This is a proposal only. FPGA-ready implemented in user logic is not a substitute for the dedicated configuration DONE signal when configuration fails.

Signals entering an unrelated clock domain require synchronization. Use a two-flop synchronizer for stable single-bit levels; use an appropriate handshake or asynchronous FIFO for multi-bit data. Do not synchronize each bit independently and assume coherent words.

### 3.3 MPU-controlled FPGA reconfiguration

| Segment | Connection |
|---|---|
| MPU output | PD0, ball G3 |
| External net | CPU_FPGA_PROGRAM_B, through R315 (22 Ω) |
| Transistor input | Q7 base through R357 (10 kΩ), with R358 (47 kΩ) pull-down |
| Transistor output | Q7 collector drives PROGRAM_B |
| FPGA input | PROGRAM_B, ball L9 |

Q7 inverts the MPU signal:

| PD0 | Q7 | PROGRAM_B | Result |
|---|---|---|---|
| Low | Off | High through pull-up | FPGA may configure and run |
| High | On | Low | FPGA configuration is cleared/held in reset |

Name software operations according to their effect, such as `assert_fpga_program_reset()` and `release_fpga_program_reset()`. A high MPU output asserts reset despite the `_B` suffix in the external net name.

PROGRAM_B does not merely reset application registers. It destroys the current FPGA configuration and initiates a new configuration sequence when released. Use an application reset register or dedicated user I/O for routine logic reset.

## 4. Configuration Flash and ownership switch

### 4.1 MPU Flash interface

| MPU net | Port / function shown on sheet 11 | Connection |
|---|---|---|
| SAMA7_FLASH_SPI_MOSI/MD0 | PB3 / FLEXCOM11_MOSI | U13 S3A |
| SAMA7_FLASH_SPI_MISO/MD1 | PB4 / FLEXCOM11_MISO | U13 S4A |
| SAMA7_FLASH_SPI_MCLK | PB5 / FLEXCOM11_SPCK | U13 S2A |
| SAMA7_FLASH_SPI_MCSN | PB6 / FLEXCOM11_NPCS1 | U13 S1A |
| SAMA7_FLASH_SPI_FPGA_SEL | PB18 / GPIO | U13 SEL |

Treat these as schematic assignments. Confirm peripheral mux selections against the SAMA7G54 datasheet and the deployed device tree or bare-metal initialization. In particular, retain the schematic's NPCS1 assignment when selecting the chip-select channel.

### 4.2 FPGA Flash interface

| FPGA function / net | FPGA ball | Route to U18 |
|---|---|---|
| CCLK / FPGA_FLASH_SPI_MCLK | E8 | R243 → U13 S2B → D2 → SCK, pin 6 |
| FCS_B / FPGA_FLASH_SPI_MCSN | L13 | R197 → U13 S1B → D1 → CS#, pin 1 |
| D00_MOSI / FPGA_FLASH_SPI_MOSI/MD0 | J13 | R206 → U13 S3B → D3 → SI/IO0, pin 5 |
| D01_DIN / FPGA_FLASH_SPI_MISO/MD1 | J14 | R204 → U13 S4B → D4 → SO/IO1, pin 2 |
| D02 / FPGA_FLASH_SPI_MD2 | K15 | R202 → R266 → WP#/IO2, pin 3 |
| D03 / FPGA_FLASH_SPI_MD3 | K16 | R201 → R265 → IO3/RESET#, pin 7 |

The FPGA supports a four-data-line configuration connection. **Only CS, SCK, IO0, and IO1 pass through U13. IO2 and IO3 remain directly connected to the FPGA.** The MPU can therefore access Flash in conventional SPI x1 mode; the shown wiring does not provide MPU QSPI x4 access.

During MPU Flash access, ensure FPGA IO2/IO3 do not drive conflicting levels. Holding PROGRAM_B low during a controlled update is the simplest ownership policy, but verify actual pin levels on hardware and preserve required Flash WP#/RESET# behavior.

### 4.3 U13 selection logic

U13 is a bidirectional four-channel signal switch, not an SPI controller or automatic bus arbiter.

| nEN | SEL | Connection |
|---|---|---|
| 0 | 0 | MPU branch A ↔ Flash |
| 0 | 1 | FPGA branch B ↔ Flash |
| 1 | Either | All switched channels disconnected |

R254 pulls nEN low, enabling the switch. R250 pulls SEL high, selecting the FPGA by default. R248/R249 pull the source-side Flash chip-select lines high.

At power-up, keep PB18 high-impedance or high so the FPGA retains ownership. Configure bootloader and operating-system GPIO initialization consistently. An early software pull-down on PB18 can interrupt automatic FPGA boot.

Before switching ownership, stop transactions and deassert chip select. The mux does not coordinate access requests or wait for an SPI transaction to finish.

## 5. EEPROM connection and protocol

### 5.1 MPU side

| Net | MPU port / function | U14 branch |
|---|---|---|
| SAMA7_CONF_SPI_MCLK | PA26 / FLEXCOM5_SPCK | S2A |
| SAMA7_CONF_SPI_MCSN | PA27 / FLEXCOM5_NPCS0 | S1A |
| SAMA7_CONF_SPI_MOSI/MD0 | PA28 / FLEXCOM5_MOSI | S3A |
| SAMA7_CONF_SPI_MISO/MD1 | PA29 / FLEXCOM5_MISO | S4A |
| SAMA7_CONF_SPI_FPGA_SEL | PD27 / GPIO | SEL |

### 5.2 FPGA side and EEPROM destination

| FPGA net | FPGA ball | U14 branch | U17 destination |
|---|---|---|---|
| FPGA_CONF_SPI_CS | N13 | S1B | CS, pin 1 |
| FPGA_CONF_SPI_CLK | P14 | S2B | SK, pin 2 |
| FPGA_CONF_SPI_MOSI | R16 | S3B | DI, pin 3 |
| FPGA_CONF_SPI_MISO | P16 | S4B | DO, pin 4 |

R255 enables U14 by pulling nEN low. R253 selects the FPGA by default. PD27 low grants MPU access; PD27 high grants FPGA access.

U17 uses **Microwire**, with active-high CS. The MPU net name MCSN must not be interpreted as the actual EEPROM selection polarity. ORG, pin 6, is grounded, selecting 128 × 8-bit organization. Implement the device's start bit, opcode, address length, write-enable command, and ready/busy handling. A normal SPI NOR driver will not work.

Use GPIO-controlled active-high CS and a compatible serial transfer implementation, or bit-bang the interface. Verify whether the chosen MPU SPI driver supports the required transfer lengths and CS polarity. Pull-ups on CS should be reviewed for intentional Microwire idle behavior; they do not provide the usual active-low SPI deselection.

This EEPROM can hold board identification, calibration, or small application settings. It cannot store an Artix-7 bitstream.

## 6. FPGA configuration straps and status

| Signal | FPGA ball | Schematic behavior |
|---|---|---|
| M0 | M9 | High through R244, 1 kΩ |
| M1 | M10 | Low through R246, 1 kΩ |
| M2 | M11 | Low through R247, 1 kΩ |
| CFGBVS | E7 | High through 4.7 kΩ pull-up to +3.3V_FPGA_IO |
| PROGRAM_B | L9 | Pull-up; Q7 and pushbutton can pull low |
| INIT_B | K10 | 4.7 kΩ pull-up |
| DONE | H10 | R241, 4.7 kΩ pull-up; also drives Q16 LED circuit |

M[2:0] = 001 selects **Master SPI** configuration. R359/R360/R361 are shown as DNP alternative straps; verify the assembly population rather than assuming all drawn resistors are fitted.

CFGBVS high is appropriate for 3.3 V configuration I/O. Verify the corresponding VCCO supplies on the PCB. Set matching implementation properties in Vivado; a software constraint does not correct a wrong physical supply.

INIT_B and DONE are not shown as direct MPU inputs in the reviewed connection group. Adding them to MPU GPIO would permit deterministic configuration monitoring and error reporting.

DONE controls PNP Q16. DONE low turns Q16 on and lights D2; DONE high turns it off. D2 is therefore an inverse-DONE indicator, not a conventional illuminated-ready LED. Confirm this behavior during bring-up.

### Automatic boot sequence

1. FPGA and Flash supplies become valid.
2. PROGRAM_B is released high and mode straps are sampled.
3. The FPGA completes initialization, reflected by INIT_B.
4. U13 defaults to the FPGA branch.
5. FPGA configuration logic drives CCLK and reads U18.
6. A valid image completes configuration and startup; DONE indicates completion.
7. User logic starts and establishes application-level communication with the MPU.

The external oscillator X1 supplies the user FPGA clock at N14. It is distinct from configuration CCLK. Releasing PROGRAM_B does not cause the MPU UART to deliver a bitstream in this design.

## 7. FPGA development workflow

Use Vivado with support for the exact XC7A35T device, FTG256 package, and speed grade -1.

1. Create an RTL project targeting `xc7a35tftg256-1`.
2. Implement clock/reset logic, UART, a command decoder, status registers, and the application logic.
3. Define package pins and I/O standards in XDC.
4. Add timing constraints and clock-domain crossing handling.
5. Run synthesis, implementation, timing analysis, and design-rule checks.
6. Generate a bitstream for the intended programming path.
7. Validate a minimal design through JTAG before attempting Flash boot.

Suggested initial test: UART version response, a heartbeat output, and a controlled LED. Keep external outputs inactive until configuration and application initialization are complete.

### Example XDC fragment

The port names below are examples and must match the HDL top-level ports. The UART directions intentionally differ from the schematic net suffixes.

```tcl
# FPGA receives the MPU's TX signal on T7.
set_property PACKAGE_PIN T7 [get_ports mpu_uart_rx]
set_property PACKAGE_PIN R8 [get_ports mpu_uart_tx]
set_property IOSTANDARD LVCMOS33 [get_ports {mpu_uart_rx mpu_uart_tx}]

set_property PACKAGE_PIN R7 [get_ports mpu_fpga_io_0]
set_property PACKAGE_PIN R6 [get_ports mpu_fpga_io_1]
set_property PACKAGE_PIN T5 [get_ports mpu_fpga_io_2]
set_property PACKAGE_PIN R5 [get_ports mpu_fpga_io_3]
set_property IOSTANDARD LVCMOS33 [get_ports {mpu_fpga_io_0 mpu_fpga_io_1 mpu_fpga_io_2 mpu_fpga_io_3}]

set_property CFGBVS VCCO [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]
# Begin Flash boot validation with x1.
set_property BITSTREAM.CONFIG.SPI_BUSWIDTH 1 [current_design]
```

Do not apply LVCMOS33 to every bank indiscriminately. Select standards compatible with each bank's actual supply. Add the user clock constraint using the fitted X1 frequency, not a guessed value. Dedicated configuration and JTAG pins are handled differently from ordinary application ports.

## 8. Programming method A: direct JTAG configuration

### J2 pinout

| J2 pin | Signal | FPGA ball / purpose |
|---|---|---|
| 1 | +3.3V_FPGA_IO | Cable target-voltage reference |
| 2 | GND | Ground |
| 3 | GND | Ground |
| 4 | TDI | N7 |
| 5 | TCK | L7 |
| 6 | Unconnected in schematic | Do not assume a function |
| 7 | TDO | N8 |
| 8 | TMS | M7 |

Use an adapter matching this pinout. J2 must not be assumed to match a standard vendor cable connector. The voltage reference pin is not an instruction to power the board from the programmer.

Procedure:

1. Power the board and verify FPGA rails.
2. Connect a compatible JTAG programmer with common ground and correct voltage reference.
3. Open Vivado Hardware Manager and connect to the target.
4. Confirm that the detected device is XC7A35T.
5. Program the generated `.bit` file.
6. Verify DONE/startup and test the UART and handshake outputs.

This writes volatile FPGA configuration SRAM. The design does not survive power loss unless a boot image is also programmed into U18. JTAG configuration does not depend on U13 transferring SPI signals.

## 9. Programming method B: persistent Flash image through JTAG

This normally uses an FPGA-resident programming helper to access the external Flash through FPGA pins.

1. Validate direct JTAG programming first.
2. Generate a configuration-memory image for U18, using the intended SPI width and boot address.
3. Keep PB18 high and U13 on the FPGA branch.
4. In Hardware Manager, select a configuration-memory device matching the actual S25FL128L variant and supported tool flow.
5. Erase, program, and verify the chosen image region.
6. Remove/reapply power and verify autonomous Flash boot.

S25FL-L supports 7-Series configuration, but tool support and JEDEC-ID handling must be confirmed for the exact Flash variant. Do not assume an S25FL-S selection is interchangeable without checking command/register compatibility and the programming flow.

Use `.bit` for direct FPGA programming. Use a correctly generated `.bin` or `.mcs` for Flash programming. Do not write a `.bit` container header into Flash as though it were the intended raw boot image. An `.mcs` file is an Intel HEX representation and must be decoded by a suitable programmer rather than written as ASCII text.

Begin with SPI x1 and a conservative supported configuration clock. Introduce x4 only after checking Quad Enable, IO2/IO3 behavior, reset configuration, read command, dummy cycles, and power-on register defaults. A valid x4 bitstream alone does not guarantee the Flash is in the required state.

## 10. Programming method C: MPU-managed Flash update

The present wiring supports **MPU programming of Flash followed by FPGA self-configuration**. It does not establish an MPU-driven Slave Serial or SelectMAP configuration interface.

### Controlled update procedure

1. Validate the candidate image, target FPGA, size, address range, and integrity metadata.
2. Stop FPGA activity and place connected actuators/outputs in the required state.
3. Assert PROGRAM_B: set PD0 high.
4. Deassert the MPU Flash chip select.
5. Set PB18 low to connect U13 to the MPU.
6. Read Flash JEDEC ID and status/configuration registers.
7. Confirm write-protection and addressing settings.
8. Erase only the intended sectors.
9. Program in supported page-sized chunks, issuing write-enable as required and polling completion with bounded timeouts.
10. Read back the image and verify it against the source.
11. Leave MPU chip select inactive and end all transactions.
12. Set PB18 high to return Flash ownership to the FPGA.
13. Release PROGRAM_B: set PD0 low.
14. Wait for successful configuration using DONE/INIT_B if available.
15. Perform a version and readiness handshake over UART before resuming operation.

Use PROGRAM_B assertion/release timings from UG470 and the FPGA data sheet. Do not select an arbitrary pulse width as a guaranteed requirement.

If verification fails, do not release the FPGA to an image known to be invalid. Preserve a recovery route and report the update failure. Firmware must use timeouts for erase/program completion and FPGA readiness.

### Linux implementation considerations

If the SAMA7G54 runs Linux, configure the correct FLEXCOM SPI/UART pinmux and select-line GPIOs in the device tree. Use an appropriate SPI NOR/MTD driver or a carefully controlled programming service. Treat `/dev/spidev*` and `/dev/mtd*` paths as platform-specific; this schematic does not determine their numbers.

Serialize all Flash operations and mux changes under one ownership mechanism. Do not allow the kernel Flash driver, a user-space updater, and another application to issue independent transactions. Keep bootloader and Linux GPIO defaults consistent so kernel initialization does not steal the Flash during FPGA configuration.

## 11. FPGA access to Flash after configuration

The same boot Flash may be accessed by user logic after startup, provided ownership and image-region protection are enforced.

CCLK at E8 is a dedicated configuration pin. To drive it from user logic after configuration, use the 7-Series **STARTUPE2** primitive, including the appropriate user clock output and tri-state controls. Do not treat E8 as a normal GPIO output in RTL/XDC.

Use a SPI controller for CS and data signals and verify the post-configuration ownership of shared configuration pins. Account for STARTUPE2 clock-path timing when selecting the Flash access frequency.

The EEPROM interface uses ordinary FPGA I/O and needs a Microwire controller, not STARTUPE2.

## 12. Recommended software and HDL partitioning

| Layer | Responsibilities |
|---|---|
| MPU board interface | UART, GPIO, SPI pinmux, mux selection, PROGRAM_B inversion |
| Flash driver | Identify, read, erase, program, status polling, protection |
| EEPROM driver | Microwire transactions, organization, write enable, completion |
| FPGA update service | Image validation, ownership sequence, readback, reconfiguration, recovery |
| FPGA application protocol | Version, status, commands, events, error reporting |
| FPGA HDL | UART, registers, synchronizers, application logic, heartbeat |

Provide CLI commands for bring-up, such as `fpga status`, `fpga version`, `flash id`, `flash verify`, and `fpga reload`. Put the actual ownership and update sequence in a reusable service rather than embedding it separately in each command.

The application protocol should include framing, message length, command identifier, sequence number, integrity check, and explicit error responses where appropriate. Define timeouts and behavior when the FPGA is unconfigured or reloading.

## 13. Recovery and bring-up acceptance criteria

Recommended validation order:

1. Check fitted straps and power rails, including configuration-bank voltages.
2. Verify the confirmed CFGBVS pull-up and PROGRAM_B inversion.
3. Detect and program the FPGA through JTAG.
4. Confirm MPU UART direction and GPIO continuity with a minimal test design.
5. Read Flash ID from the MPU with controlled ownership.
6. Program a known image and verify its bytes.
7. Boot FPGA autonomously from Flash with the MPU select line left high.
8. Exercise an MPU-driven update and reload.
9. Validate Microwire EEPROM reads/writes separately.
10. Test interrupted updates and recovery before enabling field updates.

For field updates, reserve a protected golden image and a separate candidate region. Define how a valid golden image selects or falls back from a candidate using the supported MultiBoot mechanism. Merely storing two images does not implement automatic fallback. Ensure the image-selection metadata can tolerate interrupted writes.

A successful boot should establish both configuration completion and application readiness. DONE alone does not prove the expected design/version is running or that external peripherals are initialized.

## 14. Findings and open checks

| Item | Assessment / action |
|---|---|
| CFGBVS | Confirmed pull-up to 3.3 V; earlier floating-pin finding withdrawn |
| Configuration mode | M[2:0]=001, consistent with Master SPI |
| Default Flash owner | FPGA, through SEL pull-up; nEN correctly pulled low |
| MPU programming path | Supported through SPI x1 Flash access and PROGRAM_B reload |
| IO2/IO3 ownership | Direct FPGA connections; verify levels during MPU updates |
| DONE and INIT_B feedback | Recommend dedicated MPU inputs for robust monitoring |
| DONE LED | Inverse indication due to PNP driver |
| EEPROM protocol | Microwire, active-high CS; use a dedicated driver |
| Peripheral multiplexing | Confirm against SAMA7G54 datasheet and actual software configuration |
| External output behavior | Define inactive behavior during reset, reconfiguration, and power sequencing |
| Flash programmer support | Confirm exact S25FL128L identification and tool compatibility |
| Layout/timing | Not established by this schematic review |

## 15. References

- Board schematic: `DAL_OBC_V110.pdf`, revision 1.1.0, sheets 11, 28–30.
- Supplied enlarged CFGBVS schematic screenshot: `image(20261013-153328).png`.
- AMD, *7 Series FPGAs Configuration User Guide*, UG470: https://docs.amd.com/v/u/en-US/ug470_7Series_Config
- TI, *SN3257-Q1 data sheet*, selection and enable truth table: https://www.ti.com/lit/gpn/SN3257-Q1
- onsemi, *CAT93C46B: EEPROM Serial 1-Kb Microwire*: https://www.onsemi.com/pdf/datasheet/cat93c46b-d.pdf
- Infineon, *Using Cypress S25FL-L SPI Flash with Xilinx 7 Series FPGAs*: https://community.infineon.com/t5/Knowledge-Base-Articles/Using-Cypress-S25FL-L-SPI-Flash-with-Xilinx-7-Series-FPGAs/ta-p/248701
- AMD, *AXI Quad SPI: Enable STARTUPE2 Primitive Parameter*: https://docs.amd.com/r/en-US/pg153-axi-quad-spi/Enable-STARTUPE2-Primitive-Parameter
