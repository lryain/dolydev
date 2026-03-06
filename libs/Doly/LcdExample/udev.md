# Udev Rules for Doly LCD

sudo nano /etc/udev/rules.d/99-doly-lcd.rules
KERNEL=="doly_lcd", OWNER="pi", GROUP="pi", MODE="0660"


sudo udevadm control --reload
sudo udevadm trigger --name-match=doly_lcd




