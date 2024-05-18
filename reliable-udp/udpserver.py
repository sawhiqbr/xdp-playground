""" ---------------------- SENDER ---------------------- """
""" Sequence numbers will start from 1 and go up to whatever the total number of chunks is """
""" Please comment your code thoroughly, on what your implementation does rather than how. """
import socket
import hashlib
import threading
import os
import time
import sys
import queue
import random

BUFFER_SIZE           = 1024
THREAD_COUNT          = 256
THREAD_SLEEP          = 0.15
### WARNING! PLEASE SET THESE VALUES ACCORDING TO THE REPORT BEFORE RUNNING ###

CLIENT_ADDRESS_PORT   = None
LOCAL_IP              = "server"
LOCAL_PORT            = 20002
CURRENT_DIRECTORY     = os.getcwd()
SOCKET_TIMEOUT        = 0.5
TOTAL_CHUNKS_ALL      = 0
FILES                 = [f"{size}-{i}" for size in ["small", "large"] for i in range(10)]
ALL_DONE              = False
packets               = {}
packets_acked         = {}
packets_lock          = threading.Lock()
terminate_event       = threading.Event()

# Create a UDP socket at Server side
UDPServerSocket = socket.socket(family=socket.AF_INET, type=socket.SOCK_DGRAM)
UDPServerSocket.settimeout(SOCKET_TIMEOUT)
UDPServerSocket.bind((LOCAL_IP, LOCAL_PORT))

# Making packets ready to send
sequence_number = 0
for file_name in FILES:
    # Read file data
    with open(os.path.join(CURRENT_DIRECTORY, "objects", f"{file_name}.obj"), 'rb') as file:
        file_data = file.read()

    # Calculate the chunk size based on the buffer size and subtract 15 bytes for additional information
    chunk_size = BUFFER_SIZE - 15

    # Calculate the total number of chunks required to send the file data
    total_chunks = len(file_data) // chunk_size + 1

    # Update the total number of chunks for all files
    TOTAL_CHUNKS_ALL += total_chunks

    # Split the file data into chunks and create packets for each chunk
    for i in range(0, len(file_data), chunk_size):
        sequence_number += 1
        message = file_data[i:i+chunk_size]
        packet = file_name.encode() +  sequence_number.to_bytes(4, 'big') + total_chunks.to_bytes(4, 'big') + message
        
        # Store the packet and mark it as not acknowledged
        packets[sequence_number] = packet
        packets_acked[sequence_number] = False

# This code block defines a function called `packet_sender` and creates multiple threads to execute this function.
# The `packet_sender` function is responsible for sending and resending packets that have not been acknowledged by the client.
# Each thread is assigned a range of sequence numbers for which it is responsible.
# The function continuously checks if the packets within its range have been acknowledged and resends them if necessary.
# The threads run concurrently with the main program until all packets have been acknowledged or the termination event is set.

responsible_area = TOTAL_CHUNKS_ALL // THREAD_COUNT + 1
print(f"Testing with BUFFER_SIZE: {BUFFER_SIZE} and THREAD_COUNT: {THREAD_COUNT} and RESPONSIBLE_AREA: {responsible_area}")
responsible = [i for i in range(1, TOTAL_CHUNKS_ALL + 1, responsible_area)]

def packet_sender(UDPServerSocket, responsible):

    responsible_packets = {i: packets.get(i) for i in range(responsible, responsible + responsible_area)}
    responsible_packets_acked = {i: False for i in range(responsible, responsible + responsible_area)}

    while not all(responsible_packets_acked.values()) and not terminate_event.is_set():
        for sequence in responsible_packets:
            if responsible_packets_acked[sequence]:
                continue

            with packets_lock:
                if sequence not in packets:
                    responsible_packets_acked[sequence] = True
                    continue

                # print(f"Sending packet with sequence number {sequence}")
                UDPServerSocket.sendto(packets[sequence], CLIENT_ADDRESS_PORT)
            time.sleep(THREAD_SLEEP)

selective_send_threads = [threading.Thread(target=packet_sender, args=(UDPServerSocket, i, ), name=f"Packet Resender {i}") for i in range(1, TOTAL_CHUNKS_ALL + 1, responsible_area)]



# Initialize the total number of acknowledged packets and the timeout counter
# Start a loop that will run until the terminate_event is set
acked_total = 0
timeout_counter = 0
while not terminate_event.is_set():
    # If the client's address is not set, set it and start the selective_send_threads
    # If the timeout counter reaches 5 and the client's address is set, set the terminate_event
    try:
        ack_packet, address = UDPServerSocket.recvfrom(4)
        timeout_counter = 0
        if not CLIENT_ADDRESS_PORT:
            CLIENT_ADDRESS_PORT = address
            for thread in selective_send_threads:
                thread.start()
    except socket.timeout:
        timeout_counter += 1
        if timeout_counter >= 5 and CLIENT_ADDRESS_PORT:
            terminate_event.set()
        continue

    # Convert the ACK packet to an integer sequence number
    # If the ACK sequence number is 0, continue to the next iteration of the loop
    # If an ACK is received, mark the corresponding packet as acknowledged and stop its timer
    # If all packets have been acknowledged, close the server socket, set the ALL_DONE flag, set the terminate_event, and break the loop

    ack_sequence = int.from_bytes(ack_packet, 'big')
    if ack_sequence == 0:
        continue
    
    with packets_lock:
        if ack_sequence in packets:
            del packets[ack_sequence]
            acked_total += 1

    if acked_total == TOTAL_CHUNKS_ALL:
        UDPServerSocket.close()
        ALL_DONE = True
        terminate_event.set()
        break
                
    if terminate_event.is_set():
        break

print("Everything complete")