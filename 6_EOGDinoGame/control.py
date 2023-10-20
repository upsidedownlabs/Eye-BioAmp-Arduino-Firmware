# Human Computer Interface (HCI) 
# game controller for games like dino and flappy bird

import serial
import time
import pyautogui

# return time in ms
def milis():
    return int(round(time.time() * 1000))

# Arduino serial port interface
ser = serial.Serial('COM4', 115200, timeout=1)

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
    except Exception as e:
        print(e, "\nBlink now...")
        continue