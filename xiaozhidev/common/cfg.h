#ifndef __CFG_H
#define __CFG_H

#define UI_PORT_UP    5678      /* GUI向control_center的这个端口上传UI信息 */
#define UI_PORT_DOWN  5679      /* control_center向GUI的这个端口下发UI信息 */
#define WAKEUP_WORD_DETECTION_AUDIO_PORT_UP  5682  /*sound_app向ai_app的这个端口上传用于检测唤醒词的音频*/
#define WAKEUP_WORD_DETECTION_AUDIO_PORT_DOWN 5683 /*ai_app向sound_app的这个端口下发唤醒词信息*/
#define WAKEUP_WORD_DETECTION_CONTROL_PORT_UP  5684  /*sound_app向ai_app的这个端口上传用于检测唤醒词的控制信息*/
#define WAKEUP_WORD_DETECTION_CONTROL_PORT_DOWN 5685 /*ai_app向sound_app的这个端口下发唤醒词控制信息*/


#define CFG_FILE "/etc/xiaozhi.cfg"

#endif