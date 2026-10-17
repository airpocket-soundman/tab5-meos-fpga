"""Reset a board that is on USB-Serial/JTAG, the way esptool's HardReset does (RTS = EN).
The 0.2 s pauses matter on the ESP32-P4; the 0.1 s the first version of these tools used worked on the S3 only."""
import time

def reset_board(ser):
    ser.dtr = False
    ser.rts = True            # EN low
    time.sleep(0.2)
    ser.rts = False
    time.sleep(0.2)
