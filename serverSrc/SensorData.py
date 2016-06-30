from sensorData_pb2 import SensorData as ProtoData
from google.protobuf.message import DecodeError

import datetime


class SensorData:

    def __init__(self):
        self.protobuf_object = ProtoData()

    def ParseFromString(self, data):
        try:
            self.protobuf_object.ParseFromString(data)
        except DecodeError:
            raise

    @property
    def timestamp(self):
        # Convert to a utc timestamp, dividing by 1000
        return datetime.datetime.utcfromtimestamp(self.protobuf_object.timestamp/1000)

    @property
    def temperatureSampleRate(self):
        return self.protobuf_object.temperatureSampleRate

    @property
    def humiditySampleRate(self):
        return self.protobuf_object.humiditySampleRate

    @property
    def audioSampleRate(self):
        return self.protobuf_object.audioSampleRate

    @property
    def lightSampleRate(self):
        return self.protobuf_object.lightSampleRate

    @property
    def temperatureData(self):
        return [i/100 for i in self.protobuf_object.temperatureData]

    @property
    def humidityData(self):
        return [i/100 for i in self.protobuf_object.humidityData]

    @property
    def audioData(self):
        return list(self.protobuf_object.audioData)

    @property
    def lightData(self):
        return list(self.protobuf_object.lightData)

    @property
    def chipID(self):
        return self.protobuf_object.chipID

    def asDict(self):
        d = {}
        d['time'] = self.timestamp.timestamp()  # Get unix time so that it can be parsed as JSON
        d['temperatureSampleRate'] = self.temperatureSampleRate
        d['humiditySampleRate'] = self.humiditySampleRate
        d['audioSampleRate'] = self.audioSampleRate
        d['lightSampleRate'] = self.lightSampleRate
        d['temperatureData'] = self.temperatureData
        d['humidityData'] = self.humidityData
        d['audioData'] = self.audioData
        d['lightData'] = self.lightData
        d['chipID'] = self.chipID
        return d

    def __repr__(self):
        return str(self.asDict())

    def __str__(self):
        returnVal = 'Time: {}\n'.format(str(self.timestamp))
        returnVal += 'Sample rates: {}Hz (temp and humidity), {}Hz (audio), {}Hz (light)\n'.format(
            self.temperatureSampleRate,
            self.audioSampleRate,
            self.lightSampleRate)
        returnVal += 'Temperature data (degrees C): {}\n'.format(self.temperatureData)
        returnVal += 'Humidity data (%): {}\n'.format(self.humidityData)
        returnVal += 'Audio data: {}\n'.format(self.audioData)
        returnVal += 'Light data: {}\n'.format(self.lightData)
        return returnVal
