# Smart Helmet IoT System

An embedded safety and security network for motorcycles utilizing low-latency ESP-NOW communication. This system prevents a bike's ignition from starting unless the rider is wearing a helmet and is verified sober. It also features automatic engine cutoff and audible/visual alerts if an accident or impact occurs.

## Hardware Components
* **Microcontrollers:** 2x ESP32 Dev Kits (NodeMCU)
* **Helmet Unit Sensors:** 
  * HC-SR04 Ultrasonic Sensor (Helmet detection)
  * MQ3 Alcohol Gas Sensor (Sobriety detection)
  * MPU6050 6-Axis IMU (Crash/Fall detection)
* **Bike Unit Components:**
  * 16x2 I2C LCD Display (Diagnostic HUD)
  * Active-Low 5V Relay Module (Ignition Interlock Motor)
  * 5V Piezo Buzzer (Emergency siren)

  THE WHOLE LOGIC
  1. Helmet Side (Transmitter Logic)
Usage Verification: The HC-SR04 ultrasonic sensor continuously fires single-shot distance sweeps. If a barrier is detected under 12 cm, the rider is flagged as wearing the helmet.

Sobriety Analysis: Once helmet presence is confirmed, the system initializes the analog MQ3 sensor streams through a software moving-average smoothing window to eliminate raw electrical noise. If the values exceed a threshold of 1000, the rider is flagged as intoxicated.

Crash Tracking: The MPU6050 reads acceleration vector forces. If a sudden shock spike (> 1.8g) or a free-fall drop (< 0.7g) is calculated, an emergency fall flag is instantly flipped.

2. Bike Side (Receiver Logic)
Ignition Interlock Control: The bike ESP32 decrypts incoming structural telemetry frames every 500ms. If the helmet is worn AND the rider is safe, the active-low relay switches on, completing the ignition line (RUN). If either condition fails, the relay shuts off (STOP).

Emergency Override (Crash Mode): If the inbound packet signals a crash flag (fallStatus == 1), the receiver overrides normal telemetry execution, kills the engine instantly, triggers a high-visibility flashing warning layout across the 16x2 display, and generates a pulsing alarm through the piezo buzzer.

Link Supervision Timeout: If the bike side stops receiving data packets for more than 3 seconds, it assumes the helmet has disconnected or lost power, and automatically kills the engine for safety.

📌 Pin Configurations
Helmet Unit (TX)
Trigger Pin -> GPIO 5

Echo Pin    -> GPIO 18 (with internal pulldown)

MQ3 Analog  -> GPIO 34

I2C SDA     -> GPIO 21 (MPU6050)

I2C SCL     -> GPIO 22 (MPU6050)

Bike Unit (RX)
Ignition Relay -> GPIO 27

Alarm Buzzer   -> GPIO 26

I2C SDA        -> GPIO 21 (LCD Display)

I2C SCL        -> GPIO 22 (LCD Display)

  
  
  
