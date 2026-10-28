# Accumulator-Monitoring-System

Accumulator monitoring dashboard firmware for a Formula Student electric vehicle.

This project uses an Adafruit Feather M4 CAN Express to receive BMS CAN frames, decode accumulator data, and display live values, diagnostics, and fault states on a Nextion HMI touchscreen.

This repository contains the Arduino/C++ firmware and Nextion HMI files for an accumulator monitoring dashboard.

The dashboard receives BMS CAN messages, decodes key accumulator parameters, and displays them across a multi-page touchscreen interface. It was tested using a Microchip CAN BUS Analyzer by manually transmitting CAN frames and validating the values shown on the Nextion display.

The system displays:

State of Charge (SOC)

Pack voltage

Pack current

Peak pack current

Maximum cell temperature

Average pack/cell temperature

Hottest cell ID

Cell voltage data

CAN/BMS communication status

Warning and fault states
