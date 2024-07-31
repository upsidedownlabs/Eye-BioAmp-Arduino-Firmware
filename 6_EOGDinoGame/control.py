# Human Computer Interface (HCI) 
# Game controller for games like dino and flappy bird

import serial
import time
import pyautogui
from DetectComPort import find_responsive_port

# return time in ms
def milis():
    return int(round(time.time() * 1000))

# Arduino serial port interface
ser = serial.Serial(find_responsive_port(), 115200, timeout=1)

# Timing variable
timer = milis()

latency =  24
                              
# Infinite loop
while True:
    try:
        # Process serial data
        data = ser.readline().decode('utf-8').strip()
        # Debounce
        if((milis() - timer) > latency):
            timer = milis()
            # Virtual spacebar
            if(int(data)):
                pyautogui.press('space')  
                print("jump")
        ser.flushInput()
    except Exception :
        print("\nBlink now...")
        continue  