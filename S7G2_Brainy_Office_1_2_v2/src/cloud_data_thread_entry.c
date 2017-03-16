#include "cloud_data_thread.h"
#include "event_system_api.h"
#include "event_sensor_api.h"
#include "event_config_api.h"
#include "commons.h"

void processConfigMessage ( event_config_payload_t *configEventMsg );
void processSensorMessage ( event_sensor_payload_t *sensorEventMsg );
void processSystemMessage ( event_system_payload_t *systemEventMsg );

//TODO: This function needs refactoring ... break down the reading part and send the bits to M1 configuration
bool configureUSBCloudProvisioning ();

stringDataFunction registeredSenders [ MAX_THREAD_COUNT ] =
{ NULL };

ULONG cloud_data_thread_wait = 5;

stringDataFunction g_cloudPublishImpl = NULL;
stringDataFunction g_cloudConfigImpl = NULL;
stringDataFunction g_cloudInitImpl = NULL;

/* Cloud Data Thread entry function */
void cloud_data_thread_entry ( void )
{
    sf_message_header_t * message;
    ssp_err_t msgStatus;

    while ( 1 )
    {
        msgStatus = messageQueuePend ( &cloud_data_thread_message_queue, (void **) &message, cloud_data_thread_wait );

        if ( msgStatus == SSP_SUCCESS )
        {
            // TODO: Process System Message here
            switch ( message->event_b.class_code )
            {
                case SF_MESSAGE_EVENT_CLASS_CONFIG :
                    processConfigMessage ( (event_config_payload_t *) message );
                    break;

                case SF_MESSAGE_EVENT_CLASS_SENSOR :
                    processSensorMessage ( (event_sensor_payload_t *) message );
                    break;

                case SF_MESSAGE_EVENT_CLASS_SYSTEM :
                    processSystemMessage ( (event_system_payload_t *) message );
                    break;
            }

            messageQueueReleaseBuffer ( (void **) &message );
        }
        else if ( msgStatus != SSP_ERR_MESSAGE_QUEUE_EMPTY )
        {
            // if any error other than empty queue
        }
    }
}

stringDataFunction setCloudPublishingFunction ( stringDataFunction publishImpl, stringDataFunction configImpl,
                                                stringDataFunction initImpl )
{
    stringDataFunction existingPtr = g_cloudPublishImpl;
    g_cloudPublishImpl = publishImpl;
    g_cloudConfigImpl = configImpl;
    g_cloudInitImpl = initImpl;
    return existingPtr;
}

#define CURR_NET_STATUS_BIT_MASK    0x02
#define CURR_CLOUD_PROV_BIT_MASK    0x01
#define IS_CLOUD_AVAILABLE          ((g_cloudPublishImpl!=NULL)&&(g_cloudAvailabilityStatus==3))

int g_cloudAvailabilityStatus = 0;

void processConfigMessage ( event_config_payload_t *configEventMsg )
{
    switch ( configEventMsg->header.event_b.code )
    {
        case SF_MESSAGE_EVENT_CONFIG_CLOUD_PROVISION :
            break;

        case SF_MESSAGE_EVENT_CONFIG_SET_CLOUD_TRANSMITTER :
            break;
    }
}

#define BUFFER_LENGTH   256
char buffer [ BUFFER_LENGTH ];

void processSensorMessage ( event_sensor_payload_t *sensorEventMsg )
{
    uint8_t threadId = 0;
    switch ( sensorEventMsg->header.event_b.code )
    {
        case SF_MESSAGE_EVENT_SENSOR_NEW_DATA :
            if ( IS_CLOUD_AVAILABLE )
            {
                threadId = ( ( sensorEventMsg->sender->tx_thread_id ) % MAX_THREAD_COUNT );
                if ( registeredSenders [ threadId ] != NULL )
                {
                    // Get the data Payload
                    unsigned int dataLength = registeredSenders [ threadId ] ( buffer, BUFFER_LENGTH );

                    if ( dataLength > 0 )
                    {
                        // publish to Implemented Adapter
                        g_cloudPublishImpl ( buffer, dataLength );
                    }
                }
            }
            break;
    }
}

void processSystemMessage ( event_system_payload_t *systemEventMsg )
{
    bool existingCloudStatus = IS_CLOUD_AVAILABLE;

    switch ( systemEventMsg->header.event_b.code )
    {
        case SF_MESSAGE_EVENT_SYSTEM_NETWORK_AVAILABLE :
            g_cloudAvailabilityStatus |= CURR_NET_STATUS_BIT_MASK;
            break;

        case SF_MESSAGE_EVENT_SYSTEM_USB_STORAGE_READY :
            //TODO: Provision from USB
            if ( configureUSBCloudProvisioning () == true )
            {
                g_cloudAvailabilityStatus |= CURR_CLOUD_PROV_BIT_MASK;
            }
            else
            {
                g_cloudAvailabilityStatus &= ~CURR_CLOUD_PROV_BIT_MASK;
            }
            break;

        case SF_MESSAGE_EVENT_SYSTEM_NETWORK_DISCONNECTED :
            g_cloudAvailabilityStatus &= ~CURR_NET_STATUS_BIT_MASK;
            break;

        case SF_MESSAGE_EVENT_SYSTEM_USB_STORAGE_REMOVED :
            break;
    }

    if ( existingCloudStatus == false && IS_CLOUD_AVAILABLE )
    {
        // publish Cloud available system message
    }
    else if ( existingCloudStatus == true && !IS_CLOUD_AVAILABLE )
    {
        // publish Cloud NOT available system message
    }
}

#if (USBX_CONFIGURED)
    #define CONFIG_FILE "config.txt"
extern FX_MEDIA * gp_media;

bool configureUSBCloudProvisioning ()
{
    UINT keysRead = 0;
    FX_FILE file;
    UINT status = FX_SUCCESS;

    const ULONG bufferSize = 512;
    char configurationBuffer [ bufferSize ];
    ULONG bytesRead;

    if ( g_cloudConfigImpl != NULL )
    {
        status = fx_file_open ( gp_media, &file, CONFIG_FILE, FX_OPEN_FOR_READ );

        if ( status == FX_SUCCESS )
        {
            if ( fx_file_read ( &file, configurationBuffer, bufferSize, &bytesRead ) == FX_SUCCESS )
            {

                // bytesRead will contain the number of bytes read, from this call
                keysRead = g_cloudConfigImpl ( configurationBuffer, bytesRead );
            }

            fx_file_close ( &file );

            if ( keysRead > 0 && g_cloudInitImpl != NULL )
            {
                g_cloudInitImpl ( NULL, 0 );
            }
        }
    }

    return ( keysRead > 0 );
}
#else
bool configureUSBCloudProvisioning ()
{
    UINT keysRead = 0;
    const ULONG bufferSize = 512;
    char configurationBuffer [ bufferSize ];

    if ( g_cloudConfigImpl != NULL )
    {
        sprintf ( configurationBuffer, "name=desktop-kit-1\r\n"
                  "api_key=WG46JL5TPKJ3Q272SDCEJDJQGQ4DEOJZGM2GEZBYGRQTAMBQ\r\n"
                  "project_id=o3adQcvIn0Q\r\n"
                  "user_id=kHgc2feq1bg\r\n"
                  "password=Dec2kPok\r\n"
                  "host=mqtt2.mediumone.com\r\n"
                  "port=61619\r\n" );
        keysRead = g_cloudConfigImpl ( configurationBuffer, bufferSize );
        if ( keysRead > 0 && g_cloudInitImpl != NULL )
        {
            g_cloudInitImpl ( NULL, 0 );
        }
    }

    return ( keysRead > 0 );
}
#endif