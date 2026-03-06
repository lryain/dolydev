"""
邮件发送模块

用于发送照片、视频等附件到指定邮箱

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import smtplib
import logging
from email.mime.multipart import MIMEMultipart
from email.mime.text import MIMEText
from email.mime.image import MIMEImage
from email.mime.base import MIMEBase
from email import encoders
from pathlib import Path
from typing import Optional, List
import os

logger = logging.getLogger(__name__)


class EmailSender:
    """邮件发送器"""
    
    def __init__(self, smtp_config: dict):
        """
        初始化邮件发送器
        
        Args:
            smtp_config: SMTP 配置
                {
                    'smtp_server': 'smtp.gmail.com',
                    'smtp_port': 587,
                    'smtp_username': 'your_email@gmail.com',
                    'smtp_password': 'your_password',
                    'from_email': 'your_email@gmail.com',
                    'from_name': 'Doly Robot'
                }
        """
        self.smtp_server = smtp_config.get('smtp_server', 'smtp.gmail.com')
        self.smtp_port = smtp_config.get('smtp_port', 587)
        self.smtp_username = smtp_config.get('smtp_username', '')
        self.smtp_password = smtp_config.get('smtp_password', '')
        self.from_email = smtp_config.get('from_email', self.smtp_username)
        self.from_name = smtp_config.get('from_name', 'Doly Robot')
        
        self.enabled = all([self.smtp_server, self.smtp_username, self.smtp_password])
        
        if not self.enabled:
            logger.warning("[EmailSender] ⚠️ 邮件配置不完整，邮件发送功能已禁用")
        else:
            logger.info(f"[EmailSender] ✅ 邮件发送器初始化完成: {self.smtp_server}:{self.smtp_port}")
    
    def send_email(self,
                   to_addrs: List[str],
                   subject: str,
                   body: str,
                   attachments: Optional[List[str]] = None,
                   html: bool = False) -> bool:
        """
        发送邮件
        
        Args:
            to_addrs: 收件人列表
            subject: 邮件主题
            body: 邮件正文
            attachments: 附件文件路径列表
            html: 是否使用 HTML 格式
        
        Returns:
            是否发送成功
        """
        if not self.enabled:
            logger.error("[EmailSender] ❌ 邮件发送器未启用")
            return False
        
        if not to_addrs:
            logger.error("[EmailSender] ❌ 收件人列表为空")
            return False
        
        try:
            # 创建邮件
            msg = MIMEMultipart()
            msg['From'] = f"{self.from_name} <{self.from_email}>"
            msg['To'] = ', '.join(to_addrs)
            msg['Subject'] = subject
            
            # 添加正文
            if html:
                msg.attach(MIMEText(body, 'html', 'utf-8'))
            else:
                msg.attach(MIMEText(body, 'plain', 'utf-8'))
            
            # 添加附件
            if attachments:
                for file_path in attachments:
                    if not Path(file_path).exists():
                        logger.warning(f"[EmailSender] ⚠️ 附件不存在: {file_path}")
                        continue
                    
                    self._attach_file(msg, file_path)
            
            # 发送邮件
            logger.info(f"[EmailSender] 📤 正在发送邮件到: {to_addrs}")
            
            # ★★★ 根据端口选择 SMTP 或 SMTP_SSL ★★★
            if self.smtp_port == 465:
                # 465 端口使用 SSL
                with smtplib.SMTP_SSL(self.smtp_server, self.smtp_port) as server:
                    server.login(self.smtp_username, self.smtp_password)
                    server.send_message(msg)
            else:
                # 587/25 端口使用 STARTTLS
                with smtplib.SMTP(self.smtp_server, self.smtp_port) as server:
                    server.starttls()
                    server.login(self.smtp_username, self.smtp_password)
                    server.send_message(msg)
            
            logger.info(f"[EmailSender] ✅ 邮件发送成功: {subject}")
            return True
            
        except Exception as e:
            logger.error(f"[EmailSender] ❌ 邮件发送失败: {e}", exc_info=True)
            return False
    
    def send_photo(self,
                   to_addrs: List[str],
                   photo_path: str,
                   subject: Optional[str] = None,
                   message: Optional[str] = None) -> bool:
        """
        发送照片
        
        Args:
            to_addrs: 收件人列表
            photo_path: 照片路径
            subject: 邮件主题（默认使用文件名）
            message: 附加消息
        
        Returns:
            是否发送成功
        """
        if not Path(photo_path).exists():
            logger.error(f"[EmailSender] ❌ 照片文件不存在: {photo_path}")
            return False
        
        if subject is None:
            subject = f"📸 来自 Doly 的照片 - {Path(photo_path).name}"
        
        if message is None:
            message = f"这是 Doly 机器人拍摄的照片。\n\n文件名: {Path(photo_path).name}"
        
        return self.send_email(
            to_addrs=to_addrs,
            subject=subject,
            body=message,
            attachments=[photo_path]
        )
    
    def send_video(self,
                   to_addrs: List[str],
                   video_path: str,
                   subject: Optional[str] = None,
                   message: Optional[str] = None) -> bool:
        """
        发送视频
        
        Args:
            to_addrs: 收件人列表
            video_path: 视频路径
            subject: 邮件主题（默认使用文件名）
            message: 附加消息
        
        Returns:
            是否发送成功
        """
        if not Path(video_path).exists():
            logger.error(f"[EmailSender] ❌ 视频文件不存在: {video_path}")
            return False
        
        if subject is None:
            subject = f"🎥 来自 Doly 的视频 - {Path(video_path).name}"
        
        if message is None:
            message = f"这是 Doly 机器人录制的视频。\n\n文件名: {Path(video_path).name}"
        
        return self.send_email(
            to_addrs=to_addrs,
            subject=subject,
            body=message,
            attachments=[video_path]
        )
    
    def _attach_file(self, msg: MIMEMultipart, file_path: str) -> None:
        """
        添加附件到邮件
        
        Args:
            msg: 邮件对象
            file_path: 文件路径
        """
        file_path = Path(file_path)
        filename = file_path.name
        
        # 判断文件类型
        ext = file_path.suffix.lower()
        
        if ext in ['.jpg', '.jpeg', '.png', '.gif', '.bmp']:
            # 图片附件
            with open(file_path, 'rb') as f:
                img = MIMEImage(f.read())
                img.add_header('Content-Disposition', 'attachment', filename=filename)
                msg.attach(img)
        else:
            # 其他文件类型
            with open(file_path, 'rb') as f:
                part = MIMEBase('application', 'octet-stream')
                part.set_payload(f.read())
                encoders.encode_base64(part)
                part.add_header('Content-Disposition', f'attachment; filename={filename}')
                msg.attach(part)
        
        logger.debug(f"[EmailSender] 📎 已添加附件: {filename}")
