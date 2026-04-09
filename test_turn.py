import sys
import time
sys.path.append("/home/pi/DOLY-DIY/SDK/examples/python/DriveControl")
import doly_drive as drive
import doly_helper as helper

print("Init drive")
drive.init()
print("Rotating 45 (toForward=True)")
drive.go_rotate(1, 45.0, True, 30, True, True, 0, False, True)
time.sleep(2)
print("Rotating -45 (toForward=True)")
drive.go_rotate(2, -45.0, True, 30, True, True, 0, False, True)
time.sleep(3)
print("Rotating 45 (toForward=False)")
drive.go_rotate(3, 45.0, True, 30, False, True, 0, False, True)
time.sleep(2)
drive.dispose(False)
