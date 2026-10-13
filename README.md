# Bucky ESP32 Touchscreen Mod

Turns an ESP32 into a standalone touchscreen-controlled Bluetooth keyboard- It is capable of injecting scripts. 

## Board Used

- **Model:** ESP32-2432S028R
- **Display:** 2.8-inch 320 × 240 TFT
- **Display controller:** ILI9341
- **Touch controller:** XPT2046 resistive touchscreen
- **Microcontroller:** ESP32-WROOM-32
- [Amazon](https://www.amazon.ca/ELEGOO-Display-Acrylic-Protector-Projects/dp/B0FJQJZYXG/ref=sr_1_5?crid=3GR6J1B67R0W6&dib=eyJ2IjoiMSJ9.eoTCAj9Xi5hjuVhlCTysKSR5ZmfGqjP_DJOC5h0yriOxAOtzzpBTIqygVCzwzXKbaQLQDjGKDsk4IoNHAGruKJfgGhkKIen5dAy8OsBuJgRbI7TIfZ7AcW0c5gd8YWxzqMUJOu_LbA3C6puKEPTkDpisUtCFgOYRZWdGu4DitTZJue65jCIdE1VJJ0zrJFt-2geOdC-Sv4Llj4YSCOMPyQ2RWToe0dfTE2JV1Ro2u2T40yohVTOnSa-jps6VN9bkLjlQLDrBoj4o4_8ukHGPfhhMhU8CBVr2p47A6bCAsfI.50UI7pDGTSVcwMMUEMf4VO_i0X2c-CJZkAIikaNRSgk&dib_tag=se&keywords=esp32%2Bdisplay&qid=1791918947&s=electronics&sprefix=esp32%2Celectronics%2C82&sr=1-5&th=1)

## Functions

- QWERTY keyboard
- Windows and keyboard shortcuts
- Run scripts
- Rename Bluetooth ID
- Reset Bluetooth address (Unpair)
- Turn the screen off while keeping Bluetooth active, Wake the screen with five taps

## Installation

### Install Arduino IDE  
Download and install the [Arduino IDE](https://www.arduino.cc/en/software) on your computer.  

### Install ESP32 Board Support  
1. Open **File** → **Preferences**  
2. Add the following URL to **Additional Board Manager URLs:**https://dl.espressif.com/dl/package_esp32_index.json
3. Open **Tools** → **Board** → **Boards Manager**, search for "ESP32", and install it.  

### Install Required Libraries  
1. Download the **ESP32-BLE-Keyboard** library from [GitHub](https://github.com/T-vK/ESP32-BLE-Keyboard).  
2. In Arduino IDE, go to **Sketch** → **Include Library** → **Add .ZIP Library**, then select the downloaded ZIP file.  
3: In the same manner: Add the included **TFT_eSPImod.zip** and **touchscreen** .zip files to the project. 

### Upload the Sketch

1. Open `buckyfork.ino` in Arduino IDE.
2. Select **ESP32 Dev Module**.
3. Select the board's COM port.
4. Click **Verify**.
5. Click **Upload**.
6. Restart the board after the upload completes.

### Pair the Keyboard

1. Open Bluetooth settings on the computer.
2. Choose **Add device**.
3. Select **Bluetooth**.
4. Select the name shown by Bucky.
5. Wait for the touchscreen status to show **CONNECTED**.

## Screen Controls

- **TYPE TEXT:** Compose and send text using the touchscreen keyboard.
- **QUICK KEYS:** Send common shortcuts such as Win+R, Win+D, Ctrl+C, and Alt+Tab.
- **SCRIPTS:** Run the included example scripts.
- **RENAME:** Change the Bluetooth device name and restart the ESP32.
- **RESET:** Remove saved BLE bonds, generate a new Bluetooth address, and restart.
- **SCREEN OFF:** Turn off the display backlight. Tap the screen five times within four seconds to wake it.

## Notes
- this is a work in progress

