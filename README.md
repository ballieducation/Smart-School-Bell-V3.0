# Industrial STM32 Smart School Bell V3.0
![Project Status](https://img.shields.io/badge/Status-Version%203.0-blue)
![Platform](https://img.shields.io/badge/Platform-STM32%20|%20Arduino%20IDE-orange)
An industrial-grade scheduling solution developed in the **Arduino IDE** for the **STM32F103 (Blue Pill)**. This project replaces outdated manual bells with a high-precision, automated system featuring localized **Punjabi Voice Alerts** and smart timetable generation.
STM32 Smart School Bell V3.0 – Industrial Grade Auto Timetable + Punjabi MP3 Voice Alerts + Multi-Schedule System + Special Exam Mode – Reliable, Crystal-Clear Announcements for Modern Punjabi Schools

## 🚀 Key Features
* **Industrial MCU:** High-performance STM32 core for maximum reliability.
* **Punjabi Voice Alerts:** Professional MP3 announcements for periods, prayers, and breaks.
* **Auto-Timetable Generation:** Input start time and duration; the system builds the schedule automatically.
* **Exam Mode:** Specialized timer with a visual progress bar on an 8x8 LED Matrix.
* **Multi-Timetable Storage:** Save up to 20 unique schedules in non-volatile I2C EEPROM.
* **Hardware Self-Test:** Built-in diagnostic suite to verify RTC, Relay, I2C, and Audio.

## 🛠 Hardware Components
* **MCU:** STM32F103C8T6 (Blue Pill)
* **Display:** 20x4 LCD (I2C) & 8x8 MAX7219 LED Matrix
* **Audio:** DFPlayer Mini & 5W Speaker
* **Timekeeping:** DS3231 RTC (via STM32RTC Library)
* **Storage:** AT24C32/256 EEPROM
* **Output:** 5V Opto-isolated Relay Module

## 🏗 Enclosure Design
The housing for this system was designed in **Solid Edge 2025 Community Edition** and is optimized for **3D Printing**.
* **Mounting:** Dedicated standoffs for STM32 and modules.
* **Interface:** Front-panel cutouts for 20x4 LCD and 3 tactile navigation buttons.

## 🔌 Connection Map (Pinout)
| Component | Pin | Function |
| :--- | :--- | :--- |
| **I2C SDA/SCL** | PB7 / PB6 | LCD & EEPROM |
| **Matrix DIN/CS/CLK**| PA7 / PA4 / PA5 | Visual Indicators |
| **Relay** | PA1 | Bell Control |
| **Buzzer** | PA2 | System Alerts |
| **Buttons** | PB14, PB13, PB12| Set, Up, Down |
| **MP3 RX/TX** | PA10 / PA9 | Audio Communication |

## 📂 Installation & Setup
1. **Arduino IDE:** Install the STM32duino board manager.
2. **Libraries:** Install `STM32RTC`, `LiquidCrystal_I2C`, `LedControl`, and `DFRobotDFPlayerMini`.
3. **SD Card:** Create an `/mp3` folder and upload tracks `0001.mp3` through `0014.mp3`.
4. **Flash:** Upload the `.ino` sketch using an ST-Link V2.

## 📜 Professional Summary
The STM32 Smart School Bell V3.0 provides a professional, high-tech environment for modern educational institutions. It ensures that no period is missed, and every announcement is delivered clearly in the local language, all within a robust, 3D-printed industrial enclosure.

---
Developed by **Balwinder Singh**

Platform: Arduino IDE / STM32duino
Enclosure Design: Solid Edge 2025

