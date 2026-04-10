import SerialControl
import time
import os
from doly_commands import DolyCommand, from_byte, name_for_byte

# ---------------------------------------------------------------------------
# helper: load hex‑to‑command table from the markdown document
# ---------------------------------------------------------------------------

def load_command_table(path: str) -> dict[int, str]:
    """Parse the command list markdown and return a mapping 0xNN->name."""
    table: dict[int, str] = {}
    if not os.path.isfile(path):
        return table

    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("=")
            if len(parts) < 2:
                continue
            name = parts[0].strip()
            rest = parts[1]
            for token in rest.split():
                if token.startswith("0x"):
                    try:
                        value = int(token, 16)
                        table[value] = name
                    except ValueError:
                        pass
                    break
    return table

# load the table once at module import
COMMAND_FILE = os.path.join(os.path.dirname(__file__), "docs", "命令.md")
HEX_TO_COMMAND = load_command_table(COMMAND_FILE)


def on_byte(byte):
    # 方式 1: 使用枚举转换，便于逻辑判断
    cmd = from_byte(byte)
    
    if cmd:
        print(f"Python received command: {cmd.name} (0x{byte:02x})")
        
        # 最佳实践：使用 match 或 if 进行分支判断
        if cmd == DolyCommand.iHeyDoly or cmd.name == 'iHeyDoly' or cmd.name == 'iHeyDoly':
            print("Action: Waking up Doly...")
        elif getattr(cmd, 'name', None) == 'ActDance' or getattr(cmd, 'name', None) == 'ActDance':
            print("Action: Starting dance move!")
        else:
            # 示例：若需要检测枚举以外的情况，可查询解析得到的名称
            name = name_for_byte(byte)
            if name == 'ActStop':
                print("Action: Immediate stop triggered (from md mapping).")
        # ... 其他业务逻辑
    else:
        # 方式 2: 如果枚举中不存在，回退到查表 (用于动态加载的新命令)
        cmd_name = HEX_TO_COMMAND.get(byte)
        if cmd_name:
            print(f"Python received raw command: {cmd_name} (0x{byte:02x})")
        else:
            print(f"Python received unknown byte: 0x{byte:02x}")


def main():
    cfg = SerialControl.SerialConfig()
    # cfg.device = "/dev/ttyUSB0"
    # cfg.baud = 115200
    
    service = SerialControl.SerialService()
    if not service.init(cfg):
        print("Failed to init")
        return

    service.set_handler(on_byte)
    
    print("Starting SerialControl Python test (5 seconds)...")
    service.start()
    
    time.sleep(60)
    
    print("Stopping...")
    service.stop()

if __name__ == "__main__":
    main()
