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

typedef void (*SignalHandler_t)(int);

typedef struct sConfigurationSetting {
    char *name;
    char *value;
} ConfigurationSetting;

static bool mExit = false;
static int mExitCode;
static char mStringData[512];
static bool mInProgress;
static bool mRegistrationResult;
static CloudConnectParams mCloudConnectParams;

static int ParseArguments(int argc, char *argv[]);
static int ParseConfigFile(const char *filename);
static int ValidateConfigurationSetting(ConfigurationSetting *setting);
static void ProcessConfigurationSetting(ConfigurationSetting *setting, CloudConnectParams *params);
static void RegisterSignalHandler(SignalHandler_t signalHandler);
static void SignalHandler(int signum);
static void CloudEventHandler(CloudEvent evt, void *data);
static void msleep(unsigned int milliseconds);

typedef enum eAppState {
    APP_STATE_IDLE,
    APP_STATE_REGISTERING,
} AppState;

static AppState mState = APP_STATE_IDLE;
static void AppStateMachine(void);
static void ExitAction(int exitCode);

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

    return mExitCode;
}

static void PrintUsage(void)
{
    static const char *usageString = "Usage: cloud-provision --conf-file/-c [FILE] [options]\n"
                                     "\n"
                                     "Mandatory options:\n"
                                     "  -c FILE, --conf-file FILE\n"
                                     "                           Configuration file."
                                     "\n"
                                     "Optional options:\n"
                                     "  -h, --help               Print this message and exit.\n";

    printf("%s", usageString);
}

static int ParseArguments(int argc, char *argv[])
{
    int opt = 0;
    int res = 0;

    /* clang-format off */
    static struct option long_options[] = {
        {"conf-file", required_argument, 0, 'c'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0},
    };
    /* clang-format on */

    int long_index = 0;
    bool configFileOk = false;

    while ((opt = getopt_long(argc, argv, "c:h", long_options, &long_index)) != -1) {
        switch (opt) {
            case 'c':
                if (File_Validate(optarg) == 0 && ParseConfigFile(optarg) == 0) {
                    configFileOk = true;
                } else {
                    printf("Failed to read configuration file %s.\n", optarg);
                    exit(-1);
                }
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

    /* Validate mandatory options */
    res = 0;

    if (configFileOk == false) {
        res = -1;
        printf("Option --conf-file/-c is required\n");
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

    printf("%s\n", setting->name);

    if (strcmp("HostName", setting->name) == 0) {
        res |= strlen(setting->value) == 0;
    } else if (strcmp("DPSEndPoint", setting->name) == 0) {
        res |= strlen(setting->value) == 0;
    } else if (strcmp("DPSIdScope", setting->name) == 0) {
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
    } else if (strcmp("DPSEndPoint", setting->name) == 0) {
        strcpy(params->dpsEndPoint, setting->value);
    } else if (strcmp("DPSIdScope", setting->name) == 0) {
        strcpy(params->dpsIdScope, setting->value);
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
        case CLOUD_EVENT_REGISTRATIONSUCCEEDED:
            ExitAction(0);
            break;

        case CLOUD_EVENT_REGISTRATIONFAILED:
            ExitAction(-1);
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
            if (Cloud_Register(&mCloudConnectParams) == 0) {
                mState = APP_STATE_REGISTERING;
            } else {
                ExitAction(-1);
            }
            break;

        case APP_STATE_REGISTERING:
            break;

        default:
            break;
    }
}

static void ExitAction(int exitCode)
{
    mExit = true;
    mExitCode = exitCode;
    mState = APP_STATE_IDLE;
}
