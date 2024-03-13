# cloud-apps

## Building the Application

1.  Open project in VSCode and open the project in a container.
    The container provides all the neccessary dependencies needed for this project.

2.  Once the container is running, open a bash terminal and enter the following command to configure the build environment:

        cmake -S . -B build

4.  Build the application:

        cmake --build build/

    Add `-j` option to use more CPUs for building:

        cmake --build build/ -j$(nproc)

    The corresponding application binaries will be in the following directories:

    | Application  | Directory                         |
    |--------------|-----------------------------------|
    | `cloud-send` | `build/App/cloud-send/cloud-send` |

## Applications

### `cloud-send`

The `cloud-send` application can be used to send sensor data files to the Azure IoT Hub.
This application has the following features:

- Single data files can be sent.
- Multiple data files can be sent in one commandline call. Up to 1000 files can be sent.
- The connection string needs to be passed into the application via a file as a commandline argument.

#### Usage

    Usage: cloud-send --connection-string/-c [FILE] [options]

    Mandatory options:
    -c FILE, --connection-string FILE
                            File that contains the connection string.
    Optional options:
    -f FILE, --file FILE     File to send.
    -l FILE, --list FILE     File that contains a list of files to send.
    -g, --no-clean-up        Disable file clean up.
    -h, --help               Print this message and exit.
