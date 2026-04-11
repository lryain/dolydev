source /home/pi/dolydev/.venv/bin/activate
cd /home/pi/DOLY-DIY/SDK/examples/python/SerialControl/source
pip install .
cd ..
LD_LIBRARY_PATH="/home/pi/DOLY-DIY/SDK/lib:$LD_LIBRARY_PATH" python3 example.py