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
# Play overlay
cmd = {'action':'play_overlay_sequence_sync','sequence':'hearts','side':'LEFT','loop':False,'fps':20,'speed':1.0}
print('sending overlay cmd',cmd)
sock.send_json(cmd)
resp = sock.recv_json()
print('overlay resp:', json.dumps(resp, ensure_ascii=False, indent=2))
# Wait a little then play behavior
time.sleep(0.2)
cmd2 = {'action':'play_behavior','behavior':'ANIMATION_HAPPY','level':1}
print('sending behavior cmd',cmd2)
sock.send_json(cmd2)
resp2 = sock.recv_json()
print('behavior resp:', json.dumps(resp2, ensure_ascii=False, indent=2))
# Wait to allow playback
print('sleeping 3s')
for i in range(6):
    time.sleep(0.5)
    print('.', end='', flush=True)
print('\ndone')