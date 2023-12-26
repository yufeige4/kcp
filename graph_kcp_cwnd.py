import matplotlib.pyplot as plt
import numpy as np
import math

# 定义k的值
mss = 250
sst = 20

def calculate(k,x):
    return math.floor(float(x)+float(k)/float(x)+1.0/16.0)

i = 0
increment = []
cwnd = []
cwnd.append(1)
increment.append(mss)

curr_cwnd = 1
curr_incre = mss

def calculate(incre,mss):
    result = int(mss*mss/incre) + int(mss/16)
    return result

ticks = 500

plt.figure(figsize=(10, 6))

while(i<ticks):
    if(curr_cwnd<sst):
        curr_cwnd += 1
        curr_incre += mss
        plt.scatter(i, curr_cwnd, color='green')
    else:
        if(curr_incre<mss):
            curr_incre = mss
        curr_incre += calculate(curr_incre,mss)
        if((curr_cwnd+1)*mss<=curr_incre):
            curr_cwnd = int((curr_incre+mss-1)/mss)
        plt.scatter(i, curr_cwnd, color='blue')
    increment.append(curr_incre)
    cwnd.append(curr_cwnd)
    i+=1


plt.scatter([], [], color='green', label='Slow Start')
plt.scatter([], [], color='blue', label='Congestion Control')

plt.xlabel('ticks')
plt.ylabel('cwnd')
plt.title('KCP Congestion Control Strategy Curve')
plt.grid(True)
plt.legend()

plt.show()