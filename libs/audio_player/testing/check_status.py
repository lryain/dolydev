import zmq, time, json
ctx=zmq.Context()
sub=ctx.socket(zmq.SUB); sub.connect('ipc:///tmp/doly_audio_player_status.sock'); sub.setsockopt_string(zmq.SUBSCRIBE,'status.audio.playback')
start=time.time(); printed=0
while time.time()-start<5:
    try:
        topic=sub.recv_string(flags=zmq.NOBLOCK)
        data=sub.recv_string()
        j=json.loads(data)
        print('STATUS', j)
    except zmq.Again:
        time.sleep(0.05)

print('--- tail last 120 lines ---')
# import subprocess
# print(subprocess.check_output(['tail','-n','120','/tmp/audio_player_run.log']).decode())
