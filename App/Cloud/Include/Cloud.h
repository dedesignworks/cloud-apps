#ifndef CLOUD_H
#define CLOUD_H

#include <stdbool.h>

typedef enum eCloudEvent {
    CLOUD_EVENT_CONNECTIONSTATUSCHANGED,
    CLOUD_EVENT_SENDDATASUCCEEDED,
    CLOUD_EVENT_SENDDATAFAILED,
    CLOUD_EVENT_REGISTRATIONSUCCEEDED,
    CLOUD_EVENT_REGISTRATIONFAILED,
} CloudEvent;

typedef enum eCloudConnectionStatus {
    CLOUD_CONNECTION_CONNECTED,
    CLOUD_CONNECTION_DISCONNECTED_UNKNOWN,
    CLOUD_CONNECTION_DISCONNECTED_EXPIRED_SAS_TOKEN,
    CLOUD_CONNECTION_DISCONNECTED_DEVICE_DISABLED,
    CLOUD_CONNECTION_DISCONNECTED_BAD_CREDENTIAL,
    CLOUD_CONNECTION_DISCONNECTED_RETRY_EXPIRED,
    CLOUD_CONNECTION_DISCONNECTED_NO_NETWORK,
    CLOUD_CONNECTION_DISCONNECTED_COMMUNICATION_ERROR,
    CLOUD_CONNECTION_DISCONNECTED_NO_PING_RESPONSE,
    CLOUD_CONNECTION_DISCONNECTED_QUOTA_EXCEEDED,
} CloudConnectionStatus;

typedef struct sCloudConnectParams {
    char hostname[1024];
    char dpsEndPoint[1024];
    char dpsIdScope[32];
    char deviceId[1024];
    char cert[4096];
    char key[4096];
    bool isX509;
} CloudConnectParams;

typedef void (*Cloud_EventHandler)(CloudEvent evt, void *data);

int Cloud_Initialize(void);
void Cloud_Deinitialize(void);
void Cloud_RegisterEventHandler(Cloud_EventHandler eventHandler);
int Cloud_Connect(CloudConnectParams *params);
int Cloud_Register(CloudConnectParams *params);
void Cloud_Task(void);
int Cloud_SendData(const char *data, void *contextData);

#endif