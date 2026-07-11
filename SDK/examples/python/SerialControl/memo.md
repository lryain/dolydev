source /home/pi/dolydev/.venv/bin/activate
cd /home/pi/dolydev/SDK/examples/python/SerialControl/source
pip install .
cd ..
LD_LIBRARY_PATH="/home/pi/dolydev/SDK/lib:$LD_LIBRARY_PATH" python3 example.py