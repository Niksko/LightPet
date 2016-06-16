all: client server

client: common/configuration.json
	python common/write_config_header.py common/configuration.json
	mv configuration.h clientSrc/LightPetClient/

.PHONY: clean

clean:
	rm clientSrc/LightPetClient/configuration.h
