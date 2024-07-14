import socket

client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

# client.connect(("192.168.200.142", 80))
# client.send(b"GET / HTTP/1.1\r\nHost: 192.168.200.142\r\n\r\n")

client.connect(("192.168.200.142", 64989))
client.send(b"GET / HTTP/1.1\r\nHost: 192.168.200.142\r\n\r\n")

print(client.recv(4096))