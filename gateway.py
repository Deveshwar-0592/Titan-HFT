import websocket
import json
import socket
import struct

# Configuration
BINANCE_WS = "wss://stream.binance.com:9443/ws/btcusdt@trade"
UDP_IP = "127.0.0.1"
UDP_PORT = 9999

# Setup UDP Socket (Sender)
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

print(f"Connecting to {BINANCE_WS}...")
print(f"Forwarding to C++ Engine at {UDP_IP}:{UDP_PORT}")

def on_message(ws, message):
    try:
        data = json.loads(message)
        
        # Extract Binance Data
        # "p" = Price (Float String), "q" = Qty (Float String)
        # "m" = True if buyer is maker (Sell), False if buyer is taker (Buy)
        price_float = float(data['p'])
        qty_float = float(data['q'])
        is_buyer_maker = data['m']

        # Logic: If Buyer is Maker, Taker is Seller -> Trade is SELL
        side = 1 if is_buyer_maker else 0 # 0=BUY, 1=SELL
        
        # Convert to Integers for C++ Engine
        # (Multiply by 100 to keep 2 decimal places precision)
        price_int = int(price_float * 100) 
        qty_int = int(qty_float * 1000)   # Multiply by 1000 for partial BTC
        
        # Generate a fake Order ID (just use time or counter)
        order_id = data['t'] # Trade ID from Binance

        # PACKING BINARY DATA (Must match C++ Struct!)
        # Format: Q (uint64), I (uint32), I (uint32), I (uint32 - using int for enum)
        # Total size = 8 + 4 + 4 + 4 = 20 bytes (plus padding to 32)
        # We align to 32 bytes to match C++ alignas(32) if needed, 
        # but simpler to just match the raw data fields.
        
        # Let's match the C++ OrderRequest struct:
        # uint64_t id; uint32_t price; uint32_t qty; Side side;
        # Python Struct: Q = 8 bytes, I = 4 bytes, I = 4 bytes, i = 4 bytes (enum)
        packet = struct.pack("QIIi", order_id, price_int, qty_int, side)
        
        sock.sendto(packet, (UDP_IP, UDP_PORT))
        # print(f"Sent: {price_int} {side}") # Uncomment to debug

    except Exception as e:
        print(f"Error: {e}")

def on_error(ws, error):
    print(f"Error: {error}")

def on_close(ws, close_status_code, close_msg):
    print("### Closed ###")

def on_open(ws):
    print("### Connected to Binance ###")

# Start WebSocket
ws = websocket.WebSocketApp(BINANCE_WS,
                            on_open=on_open,
                            on_message=on_message,
                            on_error=on_error,
                            on_close=on_close)

ws.run_forever()