""" --------------------- RECEIVER --------------------- """
import socket
import os 
import time
import hashlib

HOST               = socket.gethostbyname('server')
PORT               = 17000
CURRENT_DIRECTORY  = os.getcwd()
BUFFER_SIZE        = 1024

# Ensure the directory exists
os.makedirs(os.path.join(CURRENT_DIRECTORY, "objects_received_tcp"), exist_ok=True)

# Delete all files inside the folder
folder_path = os.path.join(CURRENT_DIRECTORY, "objects_received_tcp")
for file_name in os.listdir(folder_path):
    file_path = os.path.join(folder_path, file_name)
    if os.path.isfile(file_path):
        os.remove(file_path)


def receive_file(conn, filename):
    global RECEIVED_BYTES
    # Receive the file size first
    file_size_data = conn.recv(8)
    if not file_size_data:
        return  # Handle case where connection is closed
    file_size = int.from_bytes(file_size_data, 'big')

    # Receive the file data

    with open(os.path.join(CURRENT_DIRECTORY, "objects_received_tcp", f"{filename}"), 'wb') as file:
        file.truncate(0)  # Clear existing content
        remaining = file_size
        while remaining > 0:
            chunk_size = BUFFER_SIZE if remaining >= BUFFER_SIZE else remaining
            data = conn.recv(chunk_size)
            if not data:
                break  # Handle case where connection is closed unexpectedly
            file.write(data)
            remaining -= len(data)
    conn.sendall(b'File received')


    # Calculate MD5 hash
    with open(os.path.join(CURRENT_DIRECTORY, "objects_received_tcp", f"{filename}"), 'rb') as f:
        file_data = f.read()
        calculated_hash = hashlib.md5(file_data).hexdigest()

    # Read the stored MD5 hash
    with open(os.path.join(CURRENT_DIRECTORY, "objects", f"{filename}.md5"), 'r') as md5_file:
        stored_hash = md5_file.read().strip()

    # Compare hashes
    if stored_hash == calculated_hash:
        pass
    else:
        print(f"File {filename} corrupted during transfer")

for i in range(30):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((HOST, PORT + i))
        starttime = time.time()
        for i in range(10):
            receive_file(s, f"large-{i}.obj")
            receive_file(s, f"small-{i}.obj")

        print(f"{time.time() - starttime}")
        s.close()

        time.sleep(1)