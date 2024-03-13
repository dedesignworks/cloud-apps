#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include "Cloud.h"
#include "File.h"

#define MAX_FILE_COUNT 1024
#define DEFAULT_CONFIGURATION_PATH "/etc/cloud-apps/cloud.conf"

typedef void (*SignalHandler_t)(int);

typedef struct sConfigurationSetting {
    char *name;
    char *value;
} ConfigurationSetting;

static bool mExit = false;
static int mExitCode;
static bool mOptionFileSpecified = false;
static bool mOptionListSpecified = false;
static FileInfo mFiles[MAX_FILE_COUNT];
static int mFileCount = 0;
static int mFilesInProgressCount = 0;
static int mFileSendSuccessCount = 0;
static int mFileSendFailCount = 0;
static bool mDisableCleanup = false;
static CloudConnectionStatus mConnectionStatus = CLOUD_CONNECTION_DISCONNECTED_UNKNOWN;
static char mStringData[512];
static CloudConnectParams mCloudConnectParams;

static int ParseArguments(int argc, char *argv[]);
static int ReadConfigurationFile(const char *filename);
static int ParseConfigFile(const char *filename);
static int ValidateConfigurationSetting(ConfigurationSetting *setting);
static void ProcessConfigurationSetting(ConfigurationSetting *setting, CloudConnectParams *params);
static void RegisterSignalHandler(SignalHandler_t signalHandler);
static void SignalHandler(int signum);
static void CloudEventHandler(CloudEvent evt, void *data);
static void msleep(unsigned int milliseconds);

typedef enum eAppState {
    APP_STATE_IDLE,
    APP_STATE_CONNECTING,
    APP_STATE_CONNECTED,
    APP_STATE_SENDINPROGRESS,
} AppState;

static AppState mState = APP_STATE_IDLE;
static void AppStateMachine(void);
static void ExitAction(int exitCode);
static void CleanUp(void);

int main(int argc, char *argv[])
{
    /* Process command line arguments. Exit with error if failed. */
    if (ParseArguments(argc, argv) != 0) {
        return -1;
    }

    /* Register handler to catch system signals such as CTRL+C. */
    RegisterSignalHandler(SignalHandler);

    if (Cloud_Initialize() != 0) {
        return -1;
    }

    Cloud_RegisterEventHandler(CloudEventHandler);

    mExitCode = 0;

    while (!mExit) {
        AppStateMachine();
        Cloud_Task();
        msleep(1);
    }

    Cloud_Deinitialize();
    CleanUp();

    return mExitCode;
}

static void PrintUsage(void)
{
    /* clang-format off */
    static const char *usageString = "Usage: cloud-send [options]\n"
                                     "\n"
                                     "Optional options:\n"
                                     "  -c FILE, --conf-file FILE\n"
                                     "                           Configuration file.\n"
                                     "\n"
                                     "  -C FILE, --connection-string FILE\n"
                                     "                           File that contains the connection string.\n"
                                     "\n"
                                     "  -f FILE, --file FILE     File to send.\n"
                                     "  -l FILE, --list FILE     File that contains a list of files to send.\n"
                                     "  -g, --no-clean-up        Disable file clean up.\n"
                                     "  -h, --help               Print this message and exit.\n";
    /* clang-format on */

    printf("%s", usageString);
}

static int ParseArguments(int argc, char *argv[])
{
    int opt = 0;
    int res = 0;

    /* clang-format off */
    static struct option long_options[] = {
        {"connection-string", required_argument, 0, 'C'},
        {"conf-file", required_argument, 0, 'c'},
        {"file", required_argument, 0, 'f'},
        {"list", required_argument, 0, 'l'},
        {"no-clean-up", no_argument, 0, 'g'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0},
    };
    /* clang-format on */

    int long_index = 0;
    bool connectionStringOk = false;
    bool configFileOk = false;

    while ((opt = getopt_long(argc, argv, "c:C:f:l:gh", long_options, &long_index)) != -1) {
        switch (opt) {
            case 'c':
                if (File_Validate(optarg) == 0 &&
                    File_Read(optarg, mCloudConnectParams.key, 4096) == 0) {
                    mCloudConnectParams.isX509 = false;
                    connectionStringOk = true;
                } else {
                    printf("Failed to read connection string file %s.\n", optarg);
                    exit(-1);
                }
                break;

            case 'C':
                if (ReadConfigurationFile(optarg) == 0) {
                    configFileOk = true;
                } else {
                    exit(-1);
                }
                break;

            case 'f':
                /* validate file */
                if (File_Validate(optarg) == 0) {
                    mOptionFileSpecified = true;
                    strncpy(mFiles[0].filename, optarg, FILE_MAX_STRING_LENGTH - 1);
                    mFiles[0].sendStatus = false;
                    mFileCount = 1;
                } else {
                    printf("File %s doesn't exist\n", optarg);
                    exit(-1);
                }
                break;

            case 'l':
                /* validate file */
                if (File_Validate(optarg) == 0) {
                    mOptionListSpecified = true;
                    mFileCount = File_ReadList(optarg, mFiles, MAX_FILE_COUNT);
                } else {
                    printf("File %s doesn't exist\n", optarg);
                    exit(-1);
                }
                break;

            case 'g':
                mDisableCleanup = true;
                break;

            case 'h':
                PrintUsage();
                exit(0);
                break;

            default:
                PrintUsage();
                exit(-1);
                break;
        }
    }

    /* Read configuration file from default location */
    if (configFileOk == false && connectionStringOk == false) {
        configFileOk = (ReadConfigurationFile(DEFAULT_CONFIGURATION_PATH) == 0);

        if (!configFileOk) {
            printf("Default configuration file %s not found\n", DEFAULT_CONFIGURATION_PATH);
            exit(-1);
        }
    }

    /* Validate mandatory options */
    res = 0;

    if (configFileOk == false && connectionStringOk == false) {
        res = -1;
        printf("Option --conf-file/-c is required\n");
    } else if (mOptionFileSpecified && mOptionListSpecified) {
        res = -1;
        printf("Options --file/-f and --list/-l cannot be specified at the same time\n");
    }

    return res;
}

static int ReadConfigurationFile(const char *filename)
{
    int res;
    if (File_Validate(filename) == 0 && ParseConfigFile(filename) == 0) {
        res = 0;
    } else {
        res = -1;
        printf("Failed to read configuration file %s.\n", optarg);
    }
    return res;
}

static int ParseConfigFile(const char *filename)
{
    FILE *file = fopen(filename, "r");
    int res = 0;

    if (file == NULL) {
        perror("Error opening file");
        return -1;
    }

    char line[512];

    while (fgets(line, sizeof(line), file)) {
        /* Remove trailing newline character */
        line[strcspn(line, "\n")] = '\0';

        /* Ignore empty lines and lines starting with '#' */
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        /* Find the '=' character to split setting name and value */
        char *delimiter = strchr(line, '=');

        if (delimiter == NULL) {
            fprintf(stderr, "Invalid line format: %s\n", line);
            fclose(file);
            return -1;
        }

        /* Extract setting name and value */
        /* Replace '=' with '\0' to split string */
        *delimiter = '\0';
        char *name = line;
        char *value = delimiter + 1;

        /* Trim leading and trailing spaces from setting value */
        while (*value && (*value == ' ' || *value == '\t')) {
            value++;
        }

        size_t len = strlen(value);

        while (len > 0 && (value[len - 1] == ' ' || value[len - 1] == '\t')) {
            value[--len] = '\0';
        }

        /* Allocate memory for configuration setting and store values */
        ConfigurationSetting *setting = malloc(sizeof(ConfigurationSetting));
        if (setting == NULL) {
            perror("Memory allocation failed");
            fclose(file);
            return -1;
        }

        setting->name = strdup(name);
        setting->value = strdup(value);
        res = ValidateConfigurationSetting(setting);

        if (res == 0) {
            ProcessConfigurationSetting(setting, &mCloudConnectParams);
        }

        /* Don't forget to free memory for name and value */
        free(setting->name);
        free(setting->value);
        free(setting);

        if (res != 0) {
            break;
        }
    }

    fclose(file);
    mCloudConnectParams.isX509 = strlen(mCloudConnectParams.cert) && strlen(mCloudConnectParams.key);
    return res;
}

static int ValidateConfigurationSetting(ConfigurationSetting *setting)
{
    int res = 0;

    if (strcmp("HostName", setting->name) == 0) {
        res |= strlen(setting->value) == 0;
    } else if (strcmp("DeviceId", setting->name) == 0) {
        res |= strlen(setting->value) == 0;
    } else if (strcmp("CertFile", setting->name) == 0) {
        res |= strlen(setting->value) == 0;
        res |= File_Validate(setting->value);
    } else if (strcmp("KeyFile", setting->name) == 0) {
        res |= strlen(setting->value) == 0;
        res |= File_Validate(setting->value);
    } else {
        printf("Ignoring unknown configuration: %s\n", setting->name);
    }

    return res ? -1 : 0;
}

static void ProcessConfigurationSetting(ConfigurationSetting *setting, CloudConnectParams *params)
{
    if (strcmp("HostName", setting->name) == 0) {
        strcpy(params->hostname, setting->value);
    } else if (strcmp("DeviceId", setting->name) == 0) {
        strcpy(params->deviceId, setting->value);
    } else if (strcmp("CertFile", setting->name) == 0) {
        File_Read(setting->value, params->cert, 4096);
    } else if (strcmp("KeyFile", setting->name) == 0) {
        File_Read(setting->value, params->key, 4096);
    }
}

static void RegisterSignalHandler(SignalHandler_t signalHandler)
{
    /* Register signal handler */
    signal(SIGINT, signalHandler);
}

static void SignalHandler(int signum)
{
    mExit = true;
}

static void CloudEventHandler(CloudEvent evt, void *data)
{
    switch (evt) {
        case CLOUD_EVENT_CONNECTIONSTATUSCHANGED:
            mConnectionStatus = *((CloudConnectionStatus *)data);
            break;

        case CLOUD_EVENT_SENDDATASUCCEEDED:
            if (mFilesInProgressCount) {
                mFilesInProgressCount--;
            }

            FileInfo_SetSendStatus((FileInfo *)data, true);
            mFileSendSuccessCount++;
            break;

        case CLOUD_EVENT_SENDDATAFAILED:
            if (mFilesInProgressCount) {
                mFilesInProgressCount--;
            }

            FileInfo_SetSendStatus((FileInfo *)data, false);
            mFileSendFailCount++;
            break;

        default:
            break;
    }
}

void msleep(unsigned int milliseconds)
{
    time_t seconds = milliseconds / 1000;
    long nsRemainder = (milliseconds % 1000) * 1000000;
    struct timespec timeToSleep = {seconds, nsRemainder};
    nanosleep(&timeToSleep, NULL);
}

static void AppStateMachine(void)
{
    switch (mState) {
        case APP_STATE_IDLE:
            if ((mOptionFileSpecified || mOptionListSpecified) && mFileCount) {
                if (Cloud_Connect(&mCloudConnectParams) == 0) {
                    mConnectionStatus = CLOUD_CONNECTION_DISCONNECTED_UNKNOWN;
                    mState = APP_STATE_CONNECTING;
                } else {
                    ExitAction(-1);
                }
            }
            break;

        case APP_STATE_CONNECTING:
            if (mConnectionStatus != CLOUD_CONNECTION_DISCONNECTED_UNKNOWN) {
                if (mConnectionStatus == CLOUD_CONNECTION_CONNECTED) {
                    mState = APP_STATE_CONNECTED;
                } else {
                    ExitAction(-1);
                }
            }
            break;

        case APP_STATE_CONNECTED:
            for (size_t i = 0; i < mFileCount; i++) {
                if (File_Read(mFiles[i].filename, mStringData, sizeof(mStringData)) == 0) {
                    if (Cloud_SendData(mStringData, &mFiles[i]) == 0) {
                        mFilesInProgressCount++;
                    } else {
                        printf("Failed to send %s\n", mFiles[i].filename);
                    }
                } else {
                    printf("Failed to read %s\n", mFiles[i].filename);
                }
            }

            if (mFilesInProgressCount) {
                mFileSendSuccessCount = 0;
                mFileSendFailCount = 0;
                mState = APP_STATE_SENDINPROGRESS;
            } else {
                ExitAction(-1);
            }
            break;

        case APP_STATE_SENDINPROGRESS:
            if (mFilesInProgressCount == 0) {
                printf("Sent %d files. OK: %d, NOK: %d\n", mFileCount, mFileSendSuccessCount, mFileSendFailCount);
                ExitAction(0);
            }
            break;

        default:
            break;
    }
}

static void ExitAction(int exitCode)
{
    mOptionFileSpecified = false;
    mOptionListSpecified = false;
    mExit = true;
    mExitCode = exitCode;
    mState = APP_STATE_IDLE;
}

static void CleanUp(void)
{
    if (mDisableCleanup) {
        return;
    }

    /* Clean up files here */
    File_CleanList(mFiles, mFileCount);
}
