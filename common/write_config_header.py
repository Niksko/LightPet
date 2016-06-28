# Load a JSON configuration file and write it's contents to a c header file
import json
import sys
from contextlib import redirect_stdout


def load_config_fromfile(filename):
    with open(filename, "r") as config_file:
        config = json.load(config_file)
        # Get the top level config object
        config = config["config"]

    return config


def main():
    # Check that we got an argument
    if len(sys.argv) < 2:
        raise Exception("Invalid number of arguments passed. Expected one argument for input filename.")
    # Get the configuration file name
    filename = sys.argv[1]

    # Get the config object
    config = load_config_fromfile(filename=filename)

    with open('./configuration.h', "w") as output_file:
        with redirect_stdout(output_file):
            # Define a string that will serve as our format string
            format_string = "#define {} \"{}\""
            format_integer = "#define {} {}"

            # Iterate through the key value pairs in the config object and print them to the file
            for key, value in config.items():
                # Format them differently depending on whether the value is a number or a string
                if isinstance(value, str):
                    print(format_string.format(key, value))
                elif isinstance(value, int):
                    print(format_integer.format(key, value))
                else:
                    raise TypeError("Configuration value was of invalid type: {}".format(type(value)))

if __name__ == '__main__':
    main()
