import cv2
import numpy as np
import sys

path = sys.argv[1]
img = cv2.imread(path, cv2.IMREAD_COLOR)
if img is None:
    print('cannot open', path); sys.exit(1)

h,w = img.shape[:2]
print('size', w, 'x', h)
gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
blur = cv2.GaussianBlur(gray, (5,5), 0)
th = cv2.adaptiveThreshold(blur,255,cv2.ADAPTIVE_THRESH_GAUSSIAN_C, cv2.THRESH_BINARY,51,10)
# morphology
k = max(3, min(63, int(max(1, w/60))))
kern = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (k,k))
th = cv2.morphologyEx(th, cv2.MORPH_CLOSE, kern)
th = cv2.medianBlur(th,3)

contours, _ = cv2.findContours(th, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
print('contours', len(contours))
for i,c in enumerate(contours):
    area = cv2.contourArea(c)
    peri = cv2.arcLength(c, True)
    approx = cv2.approxPolyDP(c, 0.01*peri, True)
    convex = cv2.isContourConvex(approx)
    bound = cv2.boundingRect(c)
    rect = cv2.minAreaRect(c)
    (rw,rh) = rect[1]
    short_edge = min(rw,rh)
    long_edge = max(rw,rh)
    solidity = area / (bound[2]*bound[3]) if bound[2]*bound[3]>0 else 0
    aspect = short_edge/long_edge if long_edge>0 else 0
    print(f'contour {i}: area={area:.1f} approx={len(approx)} convex={convex} solidity={solidity:.3f} short={short_edge:.1f} long={long_edge:.1f} aspect={aspect:.3f} bound={bound}')

cv2.imwrite('/tmp/inspect_th.png', th)
print('wrote /tmp/inspect_th.png')
