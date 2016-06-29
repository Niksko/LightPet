#include <pb_common.h>
#include <pb.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "sensorData.pb.h"
#include "configuration.h"

#include <TaskScheduler.h>
#include <Wire.h>
#include <SI7021.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

// The following file should #define two values, WIFI_SSID and WIFI_PASSWORD, the ssid and pass for the network you
// want to connect to
#include "wifi.credentials.h"

// Relevant pin definitions. MUX pins are as per datasheet
#define MUX_A_PIN D3
#define MUX_B_PIN D4
#define SCL_PIN D1
#define SDA_PIN D2
#define ANALOG_PIN A0

// Rates at which data is read from sensors. Values are in Hz
#define MICROPHONE_SAMPLE_RATE 20
#define LIGHT_SAMPLE_RATE 20
#define TEMP_HUMIDITY_SAMPLE_RATE 5

// Define how much to buffer our data arrays by, in case of timing errors causing them to fill before they're emptied
#define DATA_BUFFER_SIZE 3

// Just to avoid magic numbers, define the ms in a second
#define MS_PER_SECOND 1000

// Define how often to generate a data packet. Value in Hz
#define DATA_SEND_RATE 1

// Define how often to check for received UDP packets. Value in Hz
#define DATA_RECEIVE_RATE 50

// Define the size of the output buffer
#define OUTPUT_BUFFER_SIZE 512

// Define how often we advertise ourselves in seconds
#define ADVERTISEMENT_RATE 10

// Define the max size of our udp packet
#define UDP_MAX_SIZE 1024

// Variables used to store data values read from sensors
uint16_t microphoneData[MICROPHONE_SAMPLE_RATE + DATA_BUFFER_SIZE];
uint16_t lightData[LIGHT_SAMPLE_RATE + DATA_BUFFER_SIZE];
uint32_t temperatureData[TEMP_HUMIDITY_SAMPLE_RATE + DATA_BUFFER_SIZE];
uint32_t humidityData[TEMP_HUMIDITY_SAMPLE_RATE + DATA_BUFFER_SIZE];
si7021_env envData;

// Variables used to store the size of valid data in the sensor data arrays
size_t microphoneDataSize = 0;
size_t lightDataSize = 0;
size_t temperatureDataSize = 0;
size_t humidityDataSize = 0;

// Used to read from Si7021 sensor
SI7021 sensor;

// Variable used to hold data from protobuf encode
uint8_t outputBuffer[512];
size_t message_length;
bool message_status;
pb_ostream_t outputStream;
SensorData outputData;

// Function prototypes for task callback functions
void readMicCallback();
void readLightCallback();
void readTempHumidityCallback();
void sendDataPacketCallback();
void sendClientServiceMessageCallback();
void listenForUDPPacketCallback();

// function prototype for ntpUnixTime
unsigned long ntpUnixTime(WiFiUDP &udp);

// Task objects used for task scheduling
Task readMic(MS_PER_SECOND / MICROPHONE_SAMPLE_RATE, TASK_FOREVER, &readMicCallback);
Task readLight(MS_PER_SECOND / LIGHT_SAMPLE_RATE, TASK_FOREVER, &readLightCallback);
Task readTempHumidity(MS_PER_SECOND / TEMP_HUMIDITY_SAMPLE_RATE, TASK_FOREVER, &readTempHumidityCallback);
Task sendDataPacket(MS_PER_SECOND / DATA_SEND_RATE, TASK_FOREVER, &sendDataPacketCallback);
Task sendClientServiceMessage(MS_PER_SECOND * ADVERTISEMENT_RATE, TASK_FOREVER, &sendClientServiceMessageCallback);
Task listenForUDPPacket(MS_PER_SECOND / DATA_RECEIVE_RATE, TASK_FOREVER, &listenForUDPPacketCallback);

// Set up a scheduler to schedule all of these tasks
Scheduler taskRunner;

// An IPAddress to hold the computed broadcast IP
IPAddress broadcastIP;

// An IPAddress to hold the server that we will send our UDP packets to
IPAddress serverIP(0, 0, 0, 0);

// A UDP instance
WiFiUDP udp;

// Create a variable to hold the Unix time during setup, as well as a variable to hold the current millis when
// we got this time. This way we can compute current NTP time by comparison
unsigned long NTPSyncTime;
unsigned long NTPSyncMillis;

// Set up the server magic string in a constant string variable
const char serverMagicString[] = SERVER_SERVICE_MESSAGE;

void setup() {
  // Set up serial
  Serial.begin(38400);

  // Set mux pins to output mode
  pinMode(MUX_A_PIN, OUTPUT);
  pinMode(MUX_B_PIN, OUTPUT);

  // Start the I2C connection with the si7021
  sensor.begin(SDA_PIN, SCL_PIN);

  // Connect to the wifi
  Serial.print("Connecting to:");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  // Wait until the wifi is connected
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // Compute the broadcast IP
  broadcastIP = ~WiFi.subnetMask() | WiFi.gatewayIP();

  // Start up the UDP service
  udp.begin(UDP_PORT);

  // Add all the tasks to the runner and enable them
  taskRunner.addTask(readMic);
  taskRunner.addTask(readLight);
  taskRunner.addTask(readTempHumidity);
  taskRunner.addTask(sendDataPacket);
  taskRunner.addTask(sendClientServiceMessage);
  taskRunner.addTask(listenForUDPPacket);
  readMic.enable();
  readLight.enable();
  readTempHumidity.enable();
  sendDataPacket.enable();
  sendClientServiceMessage.enable();
  listenForUDPPacket.enable();

  // Get the time from an NTP server. For the moment we will just do this once, but it might make sense to do this
  // as a task every so often to prevent clock drift
  static WiFiUDP timeUDP;
  NTPSyncTime = ntpUnixTime(timeUDP);
  NTPSyncMillis = millis();
}

// Helper function that configures the input select lines of the MUX so that the mic is feeding data to A0
void select_microphone() {
  digitalWrite(MUX_A_PIN, HIGH);
  digitalWrite(MUX_B_PIN, LOW);
}

// Helper function that configures the input select lines of the MUX so that the light sensor is feeding data to A0
void select_light() {
  digitalWrite(MUX_A_PIN, LOW);
  digitalWrite(MUX_B_PIN, HIGH);
}

void readMicCallback() {
  select_microphone();
  microphoneData[microphoneDataSize] = analogRead(ANALOG_PIN);
  microphoneDataSize = microphoneDataSize + 1;
}

void readLightCallback() {
  select_light();
  lightData[lightDataSize] = analogRead(ANALOG_PIN);
  lightDataSize = lightDataSize + 1;
}

void readTempHumidityCallback() {
  envData = sensor.getHumidityAndTemperature();
  temperatureData[temperatureDataSize] = envData.celsiusHundredths;
  humidityData[humidityDataSize] = envData.humidityBasisPoints;
  temperatureDataSize = temperatureDataSize + 1;
  humidityDataSize = humidityDataSize + 1;
}

// Send the special UDP packaet advertising this device as a light pet client device
void sendClientServiceMessageCallback() {
  udp.beginPacket(broadcastIP, UDP_PORT);
  udp.write(CLIENT_SERVICE_MESSAGE);
  udp.endPacket();
}

void listenForUDPPacketCallback() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    // Read the packet into a packetBuffer
    char packetBuffer[UDP_MAX_SIZE];
    int readLength = udp.read(packetBuffer, UDP_MAX_SIZE);
    // Null terminate the packet data so that we can do string comparison below
    packetBuffer[readLength] = 0;

    // Depending on the contents, take different action
    if (strcmp(packetBuffer, serverMagicString) == 0) {
      serverIP = udp.remoteIP();
    }
    else {
      // Only accept packets from the IP matching our light pet server
      if (serverIP == udp.remoteIP()) {
        // The message is a data message. For the moment just print it, but eventually this will
        // be where we decode the protobuf format and update state based on the data
        for (int i = 0; i < readLength; i++) {
          Serial.print(packetBuffer[i]);
          Serial.print(" ");
        }
        Serial.print("\n");
      }
    }
  }
}

void sendDataPacketCallback() {
  // Zero our sensor data object
  outputData = SensorData_init_zero;
  // Set some fields in the output data
  // Compute the current NTP time with currentmillis - syncmillis + synctime * 1000 (since synctime is in seconds)
  // FIXME: This will have issues if millis overflows back to 0. This should probably trigger an NTP sync. Set this
  // up once we have a ntp sync task
  outputData.timestamp = millis() - NTPSyncMillis + NTPSyncTime * 1000;
  outputData.has_timestamp = true;

  outputData.temperatureSampleRate = TEMP_HUMIDITY_SAMPLE_RATE;
  outputData.has_temperatureSampleRate = true;

  outputData.humiditySampleRate = TEMP_HUMIDITY_SAMPLE_RATE;
  outputData.has_humiditySampleRate = true;

  outputData.audioSampleRate = MICROPHONE_SAMPLE_RATE;
  outputData.has_audioSampleRate = true;

  outputData.lightSampleRate = LIGHT_SAMPLE_RATE;
  outputData.has_lightSampleRate = true;

  outputData.chipID = ESP.getChipId();
  outputData.has_chipID = true;

  // Setup the callback functions for the variable length packed data segments
  outputData.temperatureData.funcs.encode = &encodeTemperatureData;
  outputData.humidityData.funcs.encode = &encodeHumidityData;
  outputData.audioData.funcs.encode = &encodeAudioData;
  outputData.lightData.funcs.encode = &encodeLightData;
 
  // Setup the output stream for our protobuf encode to output to our outputBuffer
  outputStream = pb_ostream_from_buffer(outputBuffer, sizeof(outputBuffer));

  // Encode the data to the stream
  message_status = pb_encode(&outputStream, SensorData_fields, &outputData);
  message_length = outputStream.bytes_written;

  // Depending on the status, write a different thing to the serial monitor
  if (!message_status) {
    Serial.println("Encoding failed");
    Serial.println(PB_GET_ERROR(&outputStream));
  }
  else {
    // If we have a real server, not just the default 0.0.0.0 IP
    if (serverIP != IPAddress(0, 0, 0, 0)) {
      // Send the encoded data from the output buffer
      udp.beginPacket(serverIP, UDP_PORT);
      udp.write(outputBuffer, message_length);
      udp.endPacket();
    }
    else {
      // Just print to serial
      for (int i = 0; i < message_length; i++) {
        Serial.print(outputBuffer[i]);
        Serial.print(" ");
      }
      Serial.print("\n");
    }
  }

  // Reset the values that we use to track the size of our data arrays to zero
  temperatureDataSize = 0;
  humidityDataSize = 0;
  lightDataSize = 0;
  microphoneDataSize = 0;
}

bool encodeTemperatureData(pb_ostream_t *stream, const pb_field_t *field, void * const *arg) {
  // First, set up a substream that we will use to figure out the length of our data
  pb_ostream_t dummySubstream = PB_OSTREAM_SIZING;
  size_t dataLength;

  // Write all of the data from the temperature data array to the dummy substream, then check the bytes written
  for (int i = 0; i < temperatureDataSize; i++) {
    pb_encode_varint(&dummySubstream, (uint64_t) temperatureData[i]);
  }

  // Get the bytes written
  dataLength = dummySubstream.bytes_written;

  // Now we can actually do the encoding on the real stream
  // First encode the tag and field type. Since this is a packed array, we use the PB_WT_STRING type to denote that
  // it is length delimited
  if (!pb_encode_tag(stream, PB_WT_STRING, field->tag)) {
    return false;
  }

  // Next encode the data length we just found
  if (!pb_encode_varint(stream, (uint64_t) dataLength)) {
    return false;
  }

  // Finally encode the data as varints
  for (int i = 0; i < temperatureDataSize; i++) {
    if (! pb_encode_varint(stream, (uint64_t) temperatureData[i])) {
      return false;
    }
  }

  return true;
}

bool encodeHumidityData(pb_ostream_t *stream, const pb_field_t *field, void * const *arg) {
  // First, set up a substream that we will use to figure out the length of our data
  pb_ostream_t dummySubstream = PB_OSTREAM_SIZING;
  size_t dataLength;

  // Write all of the data from the temperature data array to the dummy substream, then check the bytes written
  for (int i = 0; i < humidityDataSize; i++) {
    pb_encode_varint(&dummySubstream, (uint64_t) humidityData[i]);
  }

  // Get the bytes written
  dataLength = dummySubstream.bytes_written;

  // Now we can actually do the encoding on the real stream
  // First encode the tag and field type. Since this is a packed array, we use the PB_WT_STRING type to denote that
  // it is length delimited
  if (!pb_encode_tag(stream, PB_WT_STRING, field->tag)) {
    return false;
  }

  // Next encode the data length we just found
  if (!pb_encode_varint(stream, (uint64_t) dataLength)) {
    return false;
  }

  // Finally encode the data as varints
  for (int i = 0; i < humidityDataSize; i++) {
    if (! pb_encode_varint(stream, (uint64_t) humidityData[i])) {
      return false;
    }
  }

  return true;
}

bool encodeLightData(pb_ostream_t *stream, const pb_field_t *field, void * const *arg) {
  // First, set up a substream that we will use to figure out the length of our data
  pb_ostream_t dummySubstream = PB_OSTREAM_SIZING;
  size_t dataLength;

  // Write all of the data from the temperature data array to the dummy substream, then check the bytes written
  for (int i = 0; i < lightDataSize; i++) {
    pb_encode_varint(&dummySubstream, (uint64_t) lightData[i]);
  }

  // Get the bytes written
  dataLength = dummySubstream.bytes_written;

  // Now we can actually do the encoding on the real stream
  // First encode the tag and field type. Since this is a packed array, we use the PB_WT_STRING type to denote that
  // it is length delimited
  if (!pb_encode_tag(stream, PB_WT_STRING, field->tag)) {
    return false;
  }

  // Next encode the data length we just found
  if (!pb_encode_varint(stream, (uint64_t) dataLength)) {
    return false;
  }

  // Finally encode the data as varints
  for (int i = 0; i < lightDataSize; i++) {
    if (! pb_encode_varint(stream, (uint64_t) lightData[i])) {
      return false;
    }
  }

  return true;
}

bool encodeAudioData(pb_ostream_t *stream, const pb_field_t *field, void * const *arg) {
  // First, set up a substream that we will use to figure out the length of our data
  pb_ostream_t dummySubstream = PB_OSTREAM_SIZING;
  size_t dataLength;

  // Write all of the data from the temperature data array to the dummy substream, then check the bytes written
  for (int i = 0; i < microphoneDataSize; i++) {
    pb_encode_varint(&dummySubstream, (uint64_t) microphoneData[i]);
  }

  // Get the bytes written
  dataLength = dummySubstream.bytes_written;

  // Now we can actually do the encoding on the real stream
  // First encode the tag and field type. Since this is a packed array, we use the PB_WT_STRING type to denote that
  // it is length delimited
  if (!pb_encode_tag(stream, PB_WT_STRING, field->tag)) {
    return false;
  }

  // Next encode the data length we just found
  if (!pb_encode_varint(stream, (uint64_t) dataLength)) {
    return false;
  }

  // Finally encode the data as varints
  for (int i = 0; i < microphoneDataSize; i++) {
    if (! pb_encode_varint(stream, (uint64_t) microphoneData[i])) {
      return false;
    }
  }

  return true;
}

/*
 * © Francesco Potortì 2013 - GPLv3 - Revision: 1.13
 *
 * Send an NTP packet and wait for the response, return the Unix time
 *
 * To lower the memory footprint, no buffers are allocated for sending
 * and receiving the NTP packets.  Four bytes of memory are allocated
 * for transmision, the rest is random garbage collected from the data
 * memory segment, and the received packet is read one byte at a time.
 * The Unix time is returned, that is, seconds from 1970-01-01T00:00.
 */

unsigned long inline ntpUnixTime(WiFiUDP &udp)
{
  static int udpInited = udp.begin(123); // open socket on arbitrary port

  const char timeServer[] = "pool.ntp.org";  // NTP server

  // Only the first four bytes of an outgoing NTP packet need to be set
  // appropriately, the rest can be whatever.
  const long ntpFirstFourBytes = 0xEC0600E3; // NTP request header

  // Fail if WiFiUdp.begin() could not init a socket
  if (! udpInited)
    return 0;

  // Clear received data from possible stray received packets
  udp.flush();

  // Send an NTP request
  if (! (udp.beginPacket(timeServer, 123) // 123 is the NTP port
   && udp.write((byte *)&ntpFirstFourBytes, 48) == 48
   && udp.endPacket()))
    return 0;       // sending request failed

  // Wait for response; check every pollIntv ms up to maxPoll times
  const int pollIntv = 150;   // poll every this many ms
  const byte maxPoll = 15;    // poll up to this many times
  int pktLen;       // received packet length
  for (byte i=0; i<maxPoll; i++) {
    if ((pktLen = udp.parsePacket()) == 48)
      break;
    delay(pollIntv);
  }
  if (pktLen != 48)
    return 0;       // no correct packet received

  // Read and discard the first useless bytes
  // Set useless to 32 for speed; set to 40 for accuracy.
  const byte useless = 40;
  for (byte i = 0; i < useless; ++i)
    udp.read();

  // Read the integer part of sending time
  unsigned long time = udp.read();  // NTP time
  for (byte i = 1; i < 4; i++)
    time = time << 8 | udp.read();

  // Round to the nearest second if we want accuracy
  // The fractionary part is the next byte divided by 256: if it is
  // greater than 500ms we round to the next second; we also account
  // for an assumed network delay of 50ms, and (0.5-0.05)*256=115;
  // additionally, we account for how much we delayed reading the packet
  // since its arrival, which we assume on average to be pollIntv/2.
  time += (udp.read() > 115 - pollIntv/8);

  // Discard the rest of the packet
  udp.flush();

  return time - 2208988800ul;   // convert NTP time to Unix time
}

void loop() {
  taskRunner.execute();
}
