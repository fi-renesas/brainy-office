#include "sensor_temperature_thread.h"
#include "commons.h"
#include "math.h"

void sensor_humidity_temperature_thread_entry ( void );
unsigned int sensor_temperature_formatDataForCloudPublish ( const event_sensor_payload_t * const eventPtr,
                                                            char * payload, size_t payloadLength );

//double g_temperatureCelsius = -40.0, g_temperatureFahrenheit = -40.0, g_humidityPercent = 0.0; // minimum values sensor can read
extern double g_temperatureCelsius, g_temperatureFahrenheit, g_humidityPercent; // minimum values sensor can read

ULONG g_sensor_temperature_thread_wait = 100;

const double HIH_DIVISOR = 0x3FFE;

/* Sensor Temperature (HIH6030-021-001 & MS5637-02BA03-50) Thread entry function */
void sensor_temperature_thread_entry ( void )
{
    ssp_err_t err;
    sf_message_header_t * message;
    ssp_err_t msgStatus;

    uint8_t wbuf [] =
    { 0 };
    uint8_t rbuf [ 4 ];
    uint8_t hihReadStatus = 0;
    uint16_t hihHumidityValue, hihTemperartureValue;
//    double hihHumidity, hihTempeCelcius, hihTempFahrenheit;
    double hihTempQuotient, hihHumidityQuotient;
    // impossible minimum values for sensor to detect
    double prevTemperatureCelsius = -200.0;
    double prevHumidityPercent = -100.0;

    double difference = 0.0;
    double tempThresholdInCelsius = 0.2;
    double humidityThresholdInPercent = 3.5;

    const uint8_t sensor_temperature_thread_id = getUniqueThreadId ();
    registerSensorForCloudPublish ( sensor_temperature_thread_id, NULL );

    err = g_i2c_device_hih6030_temp.p_api->open ( g_i2c_device_hih6030_temp.p_ctrl, g_i2c_device_hih6030_temp.p_cfg );
    APP_ERR_TRAP (err)

    while ( 1 )
    {
        msgStatus = messageQueuePend ( &sensor_temperature_thread_message_queue, (void **) &message,
                                       g_sensor_temperature_thread_wait );

        if ( msgStatus == SSP_SUCCESS )
        {
            switch ( message->event_b.class_code )
            {

                case SF_MESSAGE_EVENT_CLASS_SYSTEM :
                    //TODO: anything related to System Message Processing goes here
                    if ( message->event_b.code == SF_MESSAGE_EVENT_SYSTEM_CLOUD_AVAILABLE )
                    {
                        registerSensorForCloudPublish ( sensor_temperature_thread_id,
                                                        sensor_temperature_formatDataForCloudPublish );
                        // Fire the latest data snapshot event, just for the good faith
                        postSensorEventMessage ( sensor_temperature_thread_id, SF_MESSAGE_EVENT_SENSOR_NEW_DATA,
                                                 &g_temperatureFahrenheit );
                    }
                    else if ( message->event_b.code == SF_MESSAGE_EVENT_SYSTEM_CLOUD_DISCONNECTED )
                    {
                        registerSensorForCloudPublish ( sensor_temperature_thread_id, NULL );
                    }
                    else if ( message->event_b.code == SF_MESSAGE_EVENT_SYSTEM_BUTTON_S4_PRESSED )
                    {
                        postSensorEventMessage ( sensor_temperature_thread_id, SF_MESSAGE_EVENT_SENSOR_NEW_DATA,
                                                 &g_temperatureFahrenheit );
                    }
                    break;
            }

            messageQueueReleaseBuffer ( (void **) &message );
        }

        ( (sci_i2c_instance_ctrl_t *) g_i2c_device_hih6030_temp.p_ctrl )->info.slave = 0x27;
        err = g_i2c_device_hih6030_temp.p_api->write ( g_i2c_device_hih6030_temp.p_ctrl, wbuf, 1, false, 10 );
        APP_ERR_TRAP (err)
        tx_thread_sleep ( 5 );
        err = g_i2c_device_hih6030_temp.p_api->read ( g_i2c_device_hih6030_temp.p_ctrl, rbuf, 4, false, 10 );
        APP_ERR_TRAP (err)
        err = g_i2c_device_hih6030_temp.p_api->close ( g_i2c_device_hih6030_temp.p_ctrl );
        APP_ERR_TRAP (err)

        hihReadStatus = ( uint8_t ) ( ( rbuf [ 0 ] & 0xC0 ) >> 6 );
        hihHumidityValue = ( uint16_t ) ( ( ( rbuf [ 0 ] & 0x3F ) << 8 ) + ( rbuf [ 1 ] ) ); // 14-bit value
        hihHumidityQuotient = hihHumidityValue / HIH_DIVISOR;
        g_humidityPercent = hihHumidityQuotient * 100;
        difference = fabs ( g_humidityPercent - prevHumidityPercent );
        if ( difference > humidityThresholdInPercent )
        {
            // post sensor message
            postSensorEventMessage ( sensor_temperature_thread_id, SF_MESSAGE_EVENT_SENSOR_NEW_DATA,
                                     &g_humidityPercent );
            prevHumidityPercent = g_humidityPercent;
            tx_thread_sleep ( 50 ); // sleep for a second, so that this message is processed first and then move to temperature
        }

        hihTemperartureValue = ( uint16_t ) ( ( ( rbuf [ 2 ] << 8 ) + rbuf [ 3 ] ) >> 2 ); // 14-bit value
        hihTempQuotient = hihTemperartureValue / HIH_DIVISOR;
        g_temperatureCelsius = hihTempQuotient * 165.0 - 40.0;
        g_temperatureFahrenheit = g_temperatureCelsius * 1.8 + 32.0;

        difference = fabs ( g_temperatureCelsius - prevTemperatureCelsius );
        if ( difference > tempThresholdInCelsius )
        {
            // post Sensor Message
            postSensorEventMessage ( sensor_temperature_thread_id, SF_MESSAGE_EVENT_SENSOR_NEW_DATA,
                                     &g_temperatureFahrenheit );

            prevTemperatureCelsius = g_temperatureCelsius;
            tx_thread_sleep ( 50 ); // sleep for a second, so that this message is processed first
        }

        err = g_i2c_device_hih6030_temp.p_api->open ( g_i2c_device_hih6030_temp.p_ctrl,
                                                      g_i2c_device_hih6030_temp.p_cfg );
        APP_ERR_TRAP (err)
    }
}

unsigned int sensor_temperature_formatDataForCloudPublish ( const event_sensor_payload_t * const eventPtr,
                                                            char * payload, size_t payloadLength )
{
    int bytesProcessed = 0;
    if ( eventPtr != NULL )
    {
        if ( ( eventPtr->dataPointer ) == &g_temperatureFahrenheit )
        {
            // it's a temperature publish event
            bytesProcessed = snprintf ( payload, payloadLength,
                                        "{\"temperature\":{\"fahrenheit\": %.3f,\"celsius\": %.3f}}",
                                        g_temperatureFahrenheit, g_temperatureCelsius );
        }
        else if ( ( eventPtr->dataPointer ) == &g_humidityPercent )
        {
            // it's a humidity publish event
            bytesProcessed = snprintf ( payload, payloadLength, "{\"relative_humidity\":{\"percent\":%.3f}}",
                                        g_humidityPercent );
        }
    }

    if ( bytesProcessed < 0 )
    {
        return 0;
    }

    return (unsigned int) bytesProcessed;
}
