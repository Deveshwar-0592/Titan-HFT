import socket
import struct
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import collections

# Configuration
MCAST_GRP = '239.0.0.1'
MCAST_PORT = 5000
BUFFER_SIZE = 100

# Setup UDP Multicast Socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(('', MCAST_PORT))
mreq = struct.pack("4sl", socket.inet_aton(MCAST_GRP), socket.INADDR_ANY)
sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

# Data Stores
prices = collections.deque(maxlen=BUFFER_SIZE)
volumes = collections.deque(maxlen=BUFFER_SIZE)
x_vals = collections.deque(maxlen=BUFFER_SIZE)

# Setup Plot
fig, (ax1, ax2) = plt.subplots(2, 1, sharex=True)
fig.suptitle('Titan-HFT Live Feed')

def animate(i):
    try:
        # Non-blocking receive
        sock.settimeout(0.01)
        data, _ = sock.recvfrom(1024)
        
        # Unpack C++ struct {uint32_t price, uint32_t qty}
        # 'II' stands for 2 Unsigned Integers (4 bytes each)
        price, qty = struct.unpack('II', data[:8])
        
        prices.append(price)
        volumes.append(qty)
        x_vals.append(len(x_vals) + 1)
        
        # Clear and Redraw
        ax1.cla()
        ax2.cla()
        
        ax1.plot(x_vals, prices, label='Price', color='blue')
        ax1.set_ylabel('Price ($)')
        ax1.legend(loc='upper left')
        
        ax2.bar(x_vals, volumes, label='Volume', color='green', alpha=0.5)
        ax2.set_ylabel('Volume')
        ax2.legend(loc='upper left')
        
    except socket.timeout:
        pass # No data received this frame
    except Exception as e:
        print(f"Error: {e}")

ani = FuncAnimation(fig, animate, interval=50)
plt.show()