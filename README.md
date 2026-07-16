ESP32-Based Universal PLC Protocol Bridge
Overview

The ESP32-Based Universal PLC Protocol Bridge is an industrial edge-computing gateway designed to connect legacy Programmable Logic Controllers (PLCs) with modern Industrial Internet of Things (IIoT) platforms. The project enables older PLCs that communicate using Modbus RTU over serial interfaces to exchange data with cloud applications and monitoring systems through the lightweight MQTT messaging protocol.

Instead of replacing existing PLC infrastructure, this project demonstrates how an inexpensive ESP32 microcontroller can act as an intelligent protocol translator while maintaining deterministic communication, low latency, and secure data exchange. The work was completed as the final graduate project for EE 697 – Graduate Project in the Department of Electrical and Computer Engineering at the University of Alabama at Birmingham.

Background

Many industrial facilities continue to operate PLCs that were installed years or even decades ago. These controllers are extremely reliable but communicate through serial protocols such as:

Modbus RTU
RS-232
RS-485
Profibus

Modern Industrial IoT systems, however, use communication protocols such as:

MQTT
OPC UA
REST APIs
Cloud services

Because these communication standards are incompatible, factories often face a significant challenge when attempting to integrate legacy automation equipment with modern monitoring and analytics platforms.

Commercial protocol gateways are available but are often:

expensive
vendor locked
proprietary
difficult to customize
inaccessible for small manufacturers

This project proposes a low-cost, open, and extensible alternative using an ESP32 microcontroller.

Problem Statement

The objective of this project is to design a vendor-neutral industrial communication gateway capable of translating Modbus RTU messages into MQTT topics while maintaining predictable communication performance.

The project investigates whether a low-cost embedded platform can satisfy industrial communication requirements without requiring expensive hardware replacements.

The research addresses three primary questions:

Can an ESP32 provide deterministic communication suitable for industrial systems?
Can Modbus RTU data be translated into MQTT with low latency?
Can secure write operations be implemented without compromising system responsiveness?
Project Objectives

The project was designed with the following goals:

Read Modbus holding registers from a CLICK PLC.
Translate serial Modbus messages into MQTT telemetry.
Display live system information on an OLED display.
Measure communication latency and timing consistency.
Validate data integrity throughout the communication pipeline.
Provide a secure mechanism for authorized PLC write operations.
Demonstrate a scalable architecture suitable for Industry 4.0 applications.
System Architecture
              CLICK PLC
        (Modbus RTU over RS-232)
                    │
                    │
             MAX3232 Converter
          (RS232 ↔ TTL Level Shift)
                    │
                    │ UART
                    ▼
        ESP32 Heltec WiFi LoRa 32 V3
        ├───────────────────────────────┐
        │ Modbus RTU Master             │
        │ Protocol Translation Engine   │
        │ MQTT Client                   │
        │ OLED Status Display           │
        │ Latency Measurement           │
        │ Register Validation           │
        └───────────────────────────────┘
                    │
                 Wi-Fi Network
                    │
                    ▼
          Mosquitto MQTT Broker
                    │
                    ▼
 Dashboard • SCADA • Cloud • Analytics
Hardware Components

The hardware implementation consists of:

Component	Purpose
CLICK PLC C0-00DD1-D	Generates Modbus register data
ESP32 Heltec WiFi LoRa 32 V3	Executes protocol translation
MAX3232 Level Converter	Converts RS-232 voltage levels to TTL
OLED Display	Displays communication status and register values
Mosquitto Broker	Receives MQTT telemetry
Windows PC	MQTT server and monitoring
Software Technologies
C++
Arduino Framework
PlatformIO
Modbus RTU
MQTT
Mosquitto
OLED SSD1306 Library
CLICK PLC Programming Software
Working Principle

The communication pipeline follows these steps:

1. PLC Data Generation

The CLICK PLC continuously updates holding registers using ladder logic.

DS1 stores a constant reference value.
DS2 continuously increments to verify data freshness.
A heartbeat output confirms PLC operation.
2. Modbus Communication

The ESP32 acts as a Modbus RTU Master and periodically polls the PLC through the RS-232 serial interface.

Each communication cycle includes:

Request transmission
CRC verification
Response validation
Register decoding
3. Protocol Translation

After receiving valid Modbus frames, the ESP32 converts register values into structured MQTT messages.

Example:

Holding Register 400001

↓

Value = 1

↓

MQTT Topic

plc/holding/400001

↓

Payload

{
   "value":1,
   "timestamp":123456789
}
4. MQTT Communication

The translated data is transmitted over Wi-Fi to a Mosquitto MQTT broker.

The MQTT broker enables:

Remote monitoring
Dashboard integration
Cloud connectivity
Future OPC UA integration
5. OLED Feedback

The onboard OLED display provides real-time diagnostics including:

Wi-Fi connection status
MQTT connection status
Latest Modbus register values
Communication latency
Error messages
6. Performance Monitoring

The ESP32 continuously measures:

Communication latency
Average latency
95th percentile latency
Jitter
Packet timing consistency

These metrics are published through MQTT and displayed locally.

PLC Ladder Logic

The PLC ladder program generates deterministic test data for validating communication.

The logic performs three simple functions:

Turns on output Y001 continuously to verify PLC execution.
Copies the constant value 1 into register DS1.
Continuously increments DS2 using a math instruction.

These registers serve as known reference values, making it possible to verify that the ESP32 correctly reads, processes, and publishes Modbus data without corruption.

Experimental Results

The completed system successfully demonstrated:

Stable Modbus RTU communication
Successful RS-232 to MQTT protocol conversion
Correct PLC register acquisition
Reliable MQTT publishing
Deterministic communication timing
Successful OLED visualization
End-to-end communication validation across the PLC, ESP32, serial console, OLED display, and MQTT broker
Skills Demonstrated

This project integrates multiple engineering disciplines, including:

Embedded Systems
Industrial Automation
PLC Programming
Modbus RTU
MQTT
Serial Communication
IoT Systems
Edge Computing
Network Communication
Real-Time Systems
Hardware Integration
Performance Evaluation
Industrial Protocol Design
