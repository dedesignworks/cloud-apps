#include "Cloud.h"
#include <stdio.h>
#include <stdlib.h>

#include "iothub.h"
#include "iothub_device_client_ll.h"
#include "iothub_client_options.h"
#include "azure_prov_client/prov_device_ll_client.h"
#include "azure_prov_client/prov_security_factory.h"
#include "azure_prov_client/prov_transport_mqtt_client.h"
#include "iothub_message.h"
#include "iothub_client_version.h"
#include "azure_c_shared_utility/threadapi.h"
#include "azure_c_shared_utility/tickcounter.h"
#include "azure_c_shared_utility/crt_abstractions.h"
#include "azure_c_shared_utility/shared_util_options.h"
#include "iothubtransportmqtt.h"

static IOTHUB_DEVICE_CLIENT_LL_HANDLE mIoTClient = NULL;
static PROV_DEVICE_LL_HANDLE mProvisioningDevice = NULL;
static PROV_DEVICE_TRANSPORT_PROVIDER_FUNCTION prov_transport = NULL;
static bool mIsInit = false;
static bool mIsConnected = false;
static Cloud_EventHandler mEventHandler = NULL;

static int SetOptions(CloudConnectParams *params);
static int SetProvisioningDeviceOptions(CloudConnectParams *params);
static void SendCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void *userContextCallback);
static void ConnectionStatusCallback(IOTHUB_CLIENT_CONNECTION_STATUS result,
                                     IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason, void *user_context);
static void RegisterDeviceCallback(PROV_DEVICE_RESULT register_result, const char *iothub_uri, const char *device_id,
                                   void *user_context);
static void RegistrationStatusCallback(PROV_DEVICE_REG_STATUS reg_status, void *user_context);
static CloudConnectionStatus TranslateIoTClientConnectionStatus(IOTHUB_CLIENT_CONNECTION_STATUS status,
                                                                IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason);

int Cloud_Initialize(void)
{
    int res = IoTHub_Init();
    mIsInit = (res == 0);
    mIoTClient = NULL;
    mIsConnected = false;
    return res;
}

void Cloud_Deinitialize(void)
{
    IoTHubDeviceClient_LL_Destroy(mIoTClient);
    mIoTClient = NULL;
    Prov_Device_LL_Destroy(mProvisioningDevice);
    mProvisioningDevice = NULL;
    mIsConnected = false;
    mIsInit = false;
}

void Cloud_RegisterEventHandler(Cloud_EventHandler eventHandler)
{
    mEventHandler = eventHandler;
}

int Cloud_Connect(CloudConnectParams *params)
{
    char connectionString[1024];

    if (mIsConnected || mIoTClient != NULL) {
        return -1;
    }

    params->isX509 ? snprintf(connectionString, sizeof(connectionString), "HostName=%s;DeviceId=%s;x509=true",
                              params->hostname, params->deviceId)
                   : snprintf(connectionString, sizeof(connectionString), "%s", params->key);

    /* Create the iothub handle */
    mIoTClient = IoTHubDeviceClient_LL_CreateFromConnectionString(connectionString, MQTT_Protocol);

    if (mIoTClient == NULL) {
        printf("Failure creating IotHub device. Hint: Check your connection string.\n");
        return -1;
    }

    /* Set any option that are necessary. For available options please see the iothub_sdk_options.md documentation */
    if (SetOptions(params) != 0) {
        printf("Failure in setting options.\n");
        return -1;
    }

    IoTHubDeviceClient_LL_SetConnectionStatusCallback(mIoTClient, ConnectionStatusCallback, NULL);
    return 0;
}

int Cloud_Register(CloudConnectParams *params)
{
    (void)prov_dev_security_init(SECURE_DEVICE_TYPE_X509);
    (void)printf("Provisioning API Version: %s\r\n", Prov_Device_LL_GetVersionString());
    (void)printf("Iothub API Version: %s\r\n", IoTHubClient_GetVersionString());

    if ((mProvisioningDevice =
             Prov_Device_LL_Create(params->dpsEndPoint, params->dpsIdScope, Prov_Device_MQTT_Protocol)) == NULL) {
        (void)printf("failed calling Prov_Device_LL_Create\r\n");
    }

    /* Set any option that are necessary. For available options please see the iothub_sdk_options.md documentation */
    if (SetProvisioningDeviceOptions(params) != 0) {
        printf("Failure in setting options.\n");
        return -1;
    }

    if (Prov_Device_LL_Register_Device(mProvisioningDevice, RegisterDeviceCallback, NULL, RegistrationStatusCallback,
                                       NULL) != PROV_DEVICE_RESULT_OK) {
        (void)printf("failed calling Prov_Device_LL_Register_Device\r\n");
        return -1;
    }
}

void Cloud_Task(void)
{
    if (mIoTClient) {
        IoTHubDeviceClient_LL_DoWork(mIoTClient);
    }

    if (mProvisioningDevice) {
        Prov_Device_LL_DoWork(mProvisioningDevice);
    }
}

int Cloud_SendData(const char *data, void *contextData)
{
    if (mIoTClient == NULL) {
        return -1;
    }

    IOTHUB_MESSAGE_HANDLE msgHandle = IoTHubMessage_CreateFromString(data);

    if (msgHandle == NULL) {
        return -1;
    }

    /* Set ContentEncoding and ContentType accordingly */
    (void)IoTHubMessage_SetContentTypeSystemProperty(msgHandle, "application/json");
    (void)IoTHubMessage_SetContentEncodingSystemProperty(msgHandle, "utf-8");

    int res =
        (IoTHubDeviceClient_LL_SendEventAsync(mIoTClient, msgHandle, SendCallback, contextData) == IOTHUB_CLIENT_OK)
            ? 0
            : -1;

    IoTHubMessage_Destroy(msgHandle);
    return res;
}

static int SetOptions(CloudConnectParams *params)
{
    bool traceOn = true;
    bool urlEncodeOn = true;
    int res = 0;

    res |= IoTHubDeviceClient_LL_SetOption(mIoTClient, OPTION_LOG_TRACE, &traceOn) != IOTHUB_CLIENT_OK;

#ifdef SET_TRUSTED_CERT_IN_SAMPLES
    /* Setting the Trusted Certificate. This is only necessary on systems without built in certificate stores. */
    res |= IoTHubDeviceClient_LL_SetOption(mIoTClient, OPTION_TRUSTED_CERT, certificates) != IOTHUB_CLIENT_OK;
#endif // SET_TRUSTED_CERT_IN_SAMPLES

    /* Setting the auto URL Encoder (recommended for MQTT). Please use this option unless you are URL Encoding inputs
     * yourself. ONLY valid for use with MQTT */
    res |= IoTHubDeviceClient_LL_SetOption(mIoTClient, OPTION_AUTO_URL_ENCODE_DECODE, &urlEncodeOn) != IOTHUB_CLIENT_OK;

    if (params->isX509) {
        res |= IoTHubDeviceClient_LL_SetOption(mIoTClient, OPTION_X509_CERT, params->cert) != IOTHUB_CLIENT_OK;
        res |= IoTHubDeviceClient_LL_SetOption(mIoTClient, OPTION_X509_PRIVATE_KEY, params->key) != IOTHUB_CLIENT_OK;
    }

    return res;
}

static int SetProvisioningDeviceOptions(CloudConnectParams *params)
{
    bool traceOn = true;
    int res = 0;

    res |= Prov_Device_LL_SetOption(mProvisioningDevice, PROV_OPTION_LOG_TRACE, &traceOn) != PROV_DEVICE_RESULT_OK;

    if (params->isX509) {
        res |= Prov_Device_LL_SetOption(mProvisioningDevice, OPTION_X509_CERT, params->cert) != PROV_DEVICE_RESULT_OK;
        res |= Prov_Device_LL_SetOption(mProvisioningDevice, OPTION_X509_PRIVATE_KEY, params->key) !=
               PROV_DEVICE_RESULT_OK;
        res |= Prov_Device_LL_SetOption(mProvisioningDevice, PROV_REGISTRATION_ID, params->deviceId) !=
               PROV_DEVICE_RESULT_OK;
    }

    return res;
}

static void SendCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void *userContextCallback)
{
    CloudEvent evt =
        result == IOTHUB_CLIENT_CONFIRMATION_OK ? CLOUD_EVENT_SENDDATASUCCEEDED : CLOUD_EVENT_SENDDATAFAILED;

    if (mEventHandler) {
        mEventHandler(evt, userContextCallback);
    }
}

static void ConnectionStatusCallback(IOTHUB_CLIENT_CONNECTION_STATUS result,
                                     IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason, void *user_context)
{
    (void)user_context;
    CloudEvent evt = CLOUD_EVENT_CONNECTIONSTATUSCHANGED;
    CloudConnectionStatus s = TranslateIoTClientConnectionStatus(result, reason);
    mIsConnected = (result == IOTHUB_CLIENT_CONNECTION_AUTHENTICATED);

    if (mEventHandler) {
        mEventHandler(evt, &s);
    }
}

static void RegisterDeviceCallback(PROV_DEVICE_RESULT register_result, const char *iothub_uri, const char *device_id,
                                   void *user_context)
{
    (void)iothub_uri;
    (void)device_id;
    (void)user_context;

    if (mEventHandler) {
        mEventHandler(register_result == PROV_DEVICE_RESULT_OK ? CLOUD_EVENT_REGISTRATIONSUCCEEDED
                                                               : CLOUD_EVENT_REGISTRATIONFAILED,
                      NULL);
    }
}

static void RegistrationStatusCallback(PROV_DEVICE_REG_STATUS reg_status, void *user_context)
{
    (void)user_context;
    (void)reg_status;
}

static CloudConnectionStatus TranslateIoTClientConnectionStatus(IOTHUB_CLIENT_CONNECTION_STATUS status,
                                                                IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason)
{
    CloudConnectionStatus s = CLOUD_CONNECTION_DISCONNECTED_UNKNOWN;

    if (status == IOTHUB_CLIENT_CONNECTION_AUTHENTICATED) {
        s = CLOUD_CONNECTION_CONNECTED;
    } else {
        switch (reason) {
            case IOTHUB_CLIENT_CONNECTION_EXPIRED_SAS_TOKEN:
                s = CLOUD_CONNECTION_DISCONNECTED_EXPIRED_SAS_TOKEN;
                break;

            case IOTHUB_CLIENT_CONNECTION_DEVICE_DISABLED:
                s = CLOUD_CONNECTION_DISCONNECTED_DEVICE_DISABLED;
                break;

            case IOTHUB_CLIENT_CONNECTION_BAD_CREDENTIAL:
                s = CLOUD_CONNECTION_DISCONNECTED_BAD_CREDENTIAL;
                break;

            case IOTHUB_CLIENT_CONNECTION_RETRY_EXPIRED:
                s = CLOUD_CONNECTION_DISCONNECTED_RETRY_EXPIRED;
                break;

            case IOTHUB_CLIENT_CONNECTION_NO_NETWORK:
                s = CLOUD_CONNECTION_DISCONNECTED_NO_NETWORK;
                break;

            case IOTHUB_CLIENT_CONNECTION_COMMUNICATION_ERROR:
                s = CLOUD_CONNECTION_DISCONNECTED_COMMUNICATION_ERROR;
                break;

            case IOTHUB_CLIENT_CONNECTION_NO_PING_RESPONSE:
                s = CLOUD_CONNECTION_DISCONNECTED_NO_PING_RESPONSE;
                break;

            case IOTHUB_CLIENT_CONNECTION_QUOTA_EXCEEDED:
                s = CLOUD_CONNECTION_DISCONNECTED_QUOTA_EXCEEDED;
                break;

            default:
                break;
        }
    }

    return s;
}
