# Automated Cartesian XY System

A fully functional, 2-axis Cartesian robot built with aluminum V-Slot profiles, driven by NEMA 17 stepper motors, and controlled by an STM32 microcontroller. The project features hardware-generated motion profiles, closed-loop position verification via incremental encoders, and a custom Python/PyQt6 desktop application with a real-time Digital Twin.

## 🛠️ Hardware & Mechanical Design
The physical structure was optimized for rigidity and smooth motion, utilizing standard V-Slot linear rails and custom 3D-printed parts designed in **Autodesk Fusion 360**. 
* **X-Axis:** Driven by a GT2 timing belt. The return path of the belt is smartly concealed inside the aluminum profile channel.
* **Y-Axis:** Driven by a T8 lead screw with an anti-backlash nut for high precision and self-locking capabilities.
* **Actuators & Sensors:** NEMA 17 stepper motors powered by industrial TB6600 drivers (24V). Position feedback is provided by dual-channel incremental encoders.
* **Power Management & EMI:** The system safely separates the 24V motor power supply from the 5V logic via a step-down converter. To mitigate electromagnetic interference (EMI) and ensure stable encoder readings, shielded cables were used for the motors.
* **5V-Tolerant Integration:** 5V sensors (encoders and endstops) interface directly with the 3.3V STM32 by utilizing its native 5V-tolerant (FT) hardware pins, eliminating the need for external logic level shifters.

## 💻 Embedded Firmware (STM32 / C)
The firmware is written in C using HAL libraries and is built around a robust, interrupt-driven **Finite State Machine (FSM)** ensuring non-blocking execution.
* **Hardware Motion Ramps:** A 10 kHz base timer (`TIM6`) generates smooth trapezoidal acceleration and deceleration profiles, eliminating mechanical vibrations and preventing step loss.
* **Encoder Mode:** Hardware timers (`TIM2`, `TIM3`) are configured in quadrature encoder mode for lossless step counting. A custom software delta algorithm prevents integer overflow during extended operations.
* **DMA UART Communication:** Host communication runs entirely in the background using `ReceiveToIdle_DMA`, preventing CPU stalls while parsing incoming telemetry and commands.
* **Safety & Homing:** Hardware-debounced limit switches connected to EXTI lines serve dual purposes: emergency stops (E-STOP) during standard operation, and zero-point references during the homing procedure.

## 🖥️ PC Application (Python / PyQt6)
The machine is controlled via a custom Human-Machine Interface (HMI) built with **PyQt6**.
* **Asynchronous Serial:** Communication with the STM32 is offloaded to a background `QThread` to keep the GUI fully responsive.
* **Real-Time Digital Twin:** Utilizing `pyqtgraph`, the application dynamically plots the robot's physical position in a 2D workspace based on live telemetry data from the encoders.
* **Closed-Loop Verification:** The software implements a basic closed-loop mechanism. After reaching the target, the system compares the encoder reading with the commanded position and automatically triggers a corrective movement if the error exceeds the defined tolerance.

## 🚀 Future Improvements (To Do)
While the current prototype is fully operational, several upgrades are planned:
* **Two-Stage Homing:** Improve the homing sequence by backing off the limit switch and re-approaching at a minimal speed to achieve a highly precise machine zero.
* **S-Curve Motion Profiles:** Replace the current trapezoidal ramps with S-curves to further reduce motor acoustic noise ("humming") and mechanical stress.
* **Real-Time PID Control:** Upgrade the closed-loop system from post-move verification to active, real-time error correction using a PID controller.
* **Dedicated PCB:** Design a custom printed circuit board (e.g., an STM32 shield) to replace wire-to-wire connections, integrating logic, power distribution, and opto-isolators in one module.
* **Waypoint Memory:** Implement a feature in the Python GUI to save, load, and execute complex paths consisting of multiple coordinates.

## 👨‍💻 Authors
* **Jakub Muszyński** * **Kacper Nele** *Developed as an engineering project at the Poznań University of Technology (Automatic Control and Robotics).*
<img width="2048" height="1536" alt="efb0cb9e-aebe-48f0-bcd3-bfdc1dcb67ef" src="https://github.com/user-attachments/assets/95f7441a-1b2b-4f1a-ad47-454151590be1" />
