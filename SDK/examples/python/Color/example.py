import doly_color

def test_color_logic():
    print("--- Doly Color SDK Test ---")
    
    # 1. Test ColorCode enum
    print(f"ColorCode.Red: {doly_color.ColorCode.Red}")
    print(f"ColorCode.Green: {doly_color.ColorCode.Green}")
    print(f"ColorCode.Blue: {doly_color.ColorCode.Blue}")

    # 2. Test Color from code
    red = doly_color.Color.from_code(doly_color.ColorCode.Red)
    print(f"Red Color object: {red} (String: {red.toString()})")

    # 3. Test Manual Color creation
    custom = doly_color.Color.get_color(100, 150, 200)
    print(f"Custom Color (100, 150, 200): {custom}")

    # 4. Test Hex to RGB
    hex_color = doly_color.Color.hex_to_rgb("#FFA500") # Orange
    print(f"Hex #FFA500 to Color: {hex_color}")

    # 5. Test Color Name
    name = doly_color.Color.get_color_name(doly_color.ColorCode.Gold)
    print(f"ColorCode.Gold name: {name}")

    # 6. Test LED Color (getting specific tone for LEDs)
    led_red = doly_color.Color.get_led_color(doly_color.ColorCode.Red)
    print(f"LED Red profile: {led_red}")

if __name__ == "__main__":
    try:
        test_color_logic()
        print("\n[Success] Color module test completed.")
    except Exception as e:
        print(f"\n[Error] Test failed: {e}")
