# Ugly path hack, but apparently needed to get this file to recognize the parent module
import sys
import os
sys.path.insert(0, os.path.abspath('..'))

from SensorData import SensorData
from socket import *
from LightPet.common.write_config_header import load_config_fromfile
from google.protobuf.message import DecodeError


def main():
    # First, load the config from the config file
    config = load_config_fromfile('common/configuration.json')

    # Break out the values from the config object into handy variables
    udp_port = config["UDP_PORT"]
    server_advertisement_message = config["SERVER_SERVICE_MESSAGE"]

    # Next, setup the socket according to these config parameters
    udpSocket = socket(AF_INET, SOCK_DGRAM)
    udpSocket.setsockopt(SOL_SOCKET, SO_REUSEADDR, 1)
    udpSocket.setsockopt(SOL_SOCKET, SO_BROADCAST, 1)
    udpSocket.bind(('', udp_port))

    # Send a server advertisement
    udpSocket.sendto(server_advertisement_message.encode('ASCII'), ("10.0.0.255", udp_port))

    # Receive messages and output them
    while True:
        data, addr = udpSocket.recvfrom(512)

        # Get that data into an object
        data_object = SensorData()
        try:
            data_object.ParseFromString(data)
            print(str(data_object))
        except DecodeError:
            print("Received non-protobuf encoded message (probably an advertisement)")

if __name__ == '__main__':
    main()
