# Ugly path hack, but apparently needed to get this file to recognize the parent module
import sys
import os
sys.path.insert(0, os.path.abspath('..'))

from curio.socket import *
import curio
import logging
import json
import datetime

from SensorData import SensorData
from LightPet.common.write_config_header import load_config_fromfile
from google.protobuf.message import DecodeError

# How often to advertise that we are a server
SERVER_ADVERTISEMENT_TIMEOUT = 10

async def advertise(socket, message, port):
    """Advertise the service on the given socket then wait"""
    logger = logging.getLogger(__name__)
    while True:
        await socket.sendto(message.encode('ASCII'), ("10.0.0.255", port))
        logger.info("Sent server advertisement")
        await curio.sleep(SERVER_ADVERTISEMENT_TIMEOUT)

async def data_handler(data):
    logger = logging.getLogger(__name__)
    # Get that data into an object
    data_object = SensorData()
    try:
        data_object.ParseFromString(data)
        logger.info(json.dumps(data_object.asDict()), extra={'data': True})
    except DecodeError:
        logger.info("Received non-protobuf encoded message (probably an advertisement)")

def filter_data(record):
    # Filters records that don't have the data attribute set to true
    try:
        return record.data
    except AttributeError:
        return False

async def main():
    # Set up a logger for this module
    logging.basicConfig(level=logging.INFO)

    logger = logging.getLogger(__name__)
    fh = logging.FileHandler(datetime.datetime.utcnow().isoformat() + 'lightpet.log')
    fh.addFilter(filter_data)
    logger.addHandler(fh)

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

    # Spawn the server advertisements
    advertisement_task = await curio.spawn(advertise(udpSocket, server_advertisement_message, udp_port))

    # Receive messages and output them
    while True:
        data, _ = await udpSocket.recvfrom(512)
        # Spawn a data_handler task to handle this data
        await curio.spawn(data_handler(data))

if __name__ == '__main__':
    curio.run(main())
