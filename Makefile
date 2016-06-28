# Define the path to the nanopb generator for the protoc compiler
# You will probably need to modify this to point to the correct directory
NANOPB_GENERATOR_PATH = ../nanopb/generator/protoc-gen-nanopb

CLIENT_SRC_DIR = clientSrc/LightPetClient
SERVER_SRC_DIR = serverSrc
COMMON_DIR = common


all: client server

client: client_config client_protobuf

client_config: $(COMMON_DIR)/configuration.json
	python $(COMMON_DIR)/write_config_header.py $(COMMON_DIR)/configuration.json
	mv configuration.h $(CLIENT_SRC_DIR)

client_protobuf: $(COMMON_DIR)/sensorData.proto
	protoc -I=$(COMMON_DIR) --plugin=protoc-gen-nanopb=$(NANOPB_GENERATOR_PATH) --nanopb_out=$(CLIENT_SRC_DIR) $(COMMON_DIR)/sensorData.proto

server: server_protobuf

server_protobuf: $(COMMON_DIR)/sensorData.proto
	protoc -I=$(COMMON_DIR) --python_out=$(SERVER_SRC_DIR) $(COMMON_DIR)/sensorData.proto

.PHONY: clean

clean:
	-rm $(CLIENT_SRC_DIR)/configuration.h
	-rm $(SERVER_SRC_DIR)/sensorData_pb2.py
	-rm $(CLIENT_SRC_DIR)/sensorData.pb.*
