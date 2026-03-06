"""
## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com"""

import zmq, json, time
ctx = zmq.Context()
sock = ctx.socket(zmq.REQ)
sock.connect('ipc:///tmp/doly_eye_cmd.sock')
cmd = {'action':'play_overlay_sequence_sync','sequence':'hearts','side':'BOTH','loop':False,'fps':15}
sock.send_json(cmd)
resp = sock.recv_json()
print('RESP:',json.dumps(resp,ensure_ascii=False,indent=2))
# wait a bit for events
time.sleep(0.5)
print('\nTAIL event log:')
print(open('/tmp/overlay_events.log').read().strip())
print('\nTAIL engine log last 40 lines:')
import subprocess
print(subprocess.check_output(['tail','-n','40','/tmp/eye_zmq_service.log']).decode())