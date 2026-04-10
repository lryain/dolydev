"""
example.py

It demonstrates:
- Writing IO pin state
- Reading IO pin state

"""

import time
import doly_helper as helper
import doly_io as io

def main():

    # Version
    try:
        print(f"[info] IoControl Version: {io.get_version():.3f}")
        print("[error] 请先手动关闭 Doly Drive 服务：sudo systemctl stop drive-service.service")
    except AttributeError:
        pass

    # *** IMPORTANT *** 
    # Stop doly service if running,
    # otherwise instance of libraries cause conflict	
    if helper.stop_doly_service() < 0: 
        print("[error] Doly service stop failed")
        return -1
    
    # INFORMATION
    print("[info] Before testing, connect IO port 0 and port 1 (pin_0 <-> pin_1).")

    # write pin state => HIGH 
    io_5 = 5 # IO port pin_0 前挡大灯
    # io.write_pin(1, io_5, io.GpioState.High)
    # # read IO pin state
    # # state = io.read_pin(2, io_5);
    # # print(f"[info] Read Pin:{io_5} State:{state}")
    # time.sleep(0.2)
    # write pin state => LOW 
    io.write_pin(1, io_5, io.GpioState.Low)
    # # read IO pin state
    state = io.read_pin(2, io_5);
    print(f"[info] Read Pin:{io_5} State:{state}")

    # 前挡大灯 闪烁3次
    for i in range(3):
        io.write_pin(1, io_5, io.GpioState.High)
        time.sleep(1.5)
        io.write_pin(1, io_5, io.GpioState.Low)
        time.sleep(0.3)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
