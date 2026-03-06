"""
## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com"""

import smtplib
from email.mime.text import MIMEText

try:
    msg = MIMEText("Test")
    msg['Subject'] = "Test"
    msg['From'] = "47129927@qq.com"
    msg['To'] = "47129927@qq.com"
    
    with smtplib.SMTP_SSL("smtp.qq.com", 465, timeout=10) as server:
        server.set_debuglevel(1)
        server.login("47129927@qq.com", "")
        server.send_message(msg)
    print("✅ Success!")
except Exception as e:
    print(f"❌ Failed: {e}")
    import traceback
    traceback.print_exc()

