import socket, struct, time

print("Connecting to Lithos MMORPG Server on port 6900...")
s = socket.socket()
s.connect(('127.0.0.1', 6900))

# 構造 RO 0x0064 CA_LOGIN 封包 (總長 55 bytes)
# 2 bytes Header (0x0064) + 24 bytes Account + 24 bytes Pass + 5 bytes junk
header = struct.pack('<H', 0x0064) # Little Endian
account = b'admin_rocks\x00' + b'\x00' * 12
password = b'lithos_mmorpg\x00' + b'\x00' * 10
junk = b'12345'

pkt = header + account + password + junk
print(f"Sending RO Packet 0x0064 (Len: {len(pkt)})...")
s.send(pkt)

time.sleep(1)
s.close()
print("Done.")
