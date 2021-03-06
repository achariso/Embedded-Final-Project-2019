#include "conf.h"
#include "log.h"
#include "utils.h"
#include <sys/time.h>

//------------------------------------------------------------------------------------------------

extern uint32_t CLIENT_AEM;
extern struct timeval CLIENT_AEM_CONN_START_LIST[CLIENT_AEM_LIST_LENGTH][MAX_CONNECTIONS_WITH_SAME_CLIENT];
extern struct timeval CLIENT_AEM_CONN_END_LIST[CLIENT_AEM_LIST_LENGTH][MAX_CONNECTIONS_WITH_SAME_CLIENT];
extern uint8_t CLIENT_AEM_CONN_N_LIST[CLIENT_AEM_LIST_LENGTH];

extern uint32_t executionTimeRequested;
extern MessagesStats messagesStats;

extern Message MESSAGES_BUFFER[ MESSAGES_SIZE ];
extern InboxMessage INBOX[ INBOX_SIZE ];
extern messages_head_t inboxHead;

//------------------------------------------------------------------------------------------------

static FILE *jsonFilePointer;

struct timeval lastEventStart, lastEventStop;

/// \brief Logs the start of a new event in session.json file
/// \param type
/// \param server
/// \param client
void log_event_start( const char* type, uint32_t server, uint32_t client )
{
    gettimeofday( &lastEventStart, NULL );

    fprintf( jsonFilePointer, "{\"occured_at\": \"%s\", \"type\": \"%s\", \"server\": \"%u\", \"client\": \"%u\", \"messages\": [",
            timestamp2ftime( (uint64_t) time(NULL), "%H:%M:%S" ), type,
            server, client );
}

/// \brief Logs $message to session.json file
/// \param action
/// \param message
void log_event_message( const char* action, const Message* message )
{
    fprintf( jsonFilePointer, "{\"saved_at\": \"%s\", \"action\": \"%s\", \"sender\": \"%u\", \"recipient\": \"%u\", \"created_at\": \"%s\", \"body\": \"%s\", \"transmitted\": \"%s\", \"transmitted_devices\": \"%s\", \"transmitted_to_recipient\": \"%s\"},",
     timestamp2ftime( (uint64_t) time(NULL), "%FT%TZ" ), action,
         message->sender, message->recipient, timestamp2ftime( message->created_at, "%FT%TZ" ), message->body,
         message->transmitted == 1 ? "TRUE" : "FALSE", getTransmittedDevicesString( message ),
         message->transmitted_to_recipient == 1 ? "TRUE" : "FALSE"
     );
}

/// \brief Logs datetime syncing to session.json file
/// \param previous_now
/// \param new_now
void log_event_message_datetime( uint64_t previous_now, uint64_t new_now )
{
    fprintf( jsonFilePointer, "{\"saved_at\": \"%s\", \"action\": \"%s\", \"previous_now\": \"%s\", \"new_now\": \"%s\"},",
             timestamp2ftime( (uint64_t) time(NULL), "%FT%TZ" ), "datetime",
             timestamp2ftime( previous_now, "%FT%TZ" ), timestamp2ftime( new_now, "%FT%TZ" ) );
}

/// \brief Logs the end of a new event in session.json file
void log_event_stop(void)
{
    gettimeofday( &lastEventStop, NULL );

    double duration = (double) ( lastEventStop.tv_sec - lastEventStart.tv_sec ) * 1000 +
            (double) ( lastEventStop.tv_usec - lastEventStart.tv_usec ) / 1000;

    removeTrailingCommaFromJson();
    fprintf( jsonFilePointer, "], \"duration\": \"%f ms\"},", duration );
}

/// Append end of session message and closes log file pointer.
/// \param executionTimeRequested
void log_tearDown(const double executionTimeActual)
{
    if ( ALSO_LOG_TO_STDOUT )
    {
        fprintf(stdout, "\n/*\n"
                        "|--------------------------------------------------------------------------\n"
                        "| end: NEW SESSION\n"
                        "|--------------------------------------------------------------------------\n"
                        "|\n"
                        "| Duration Actual     : %lf secs\n"
                        "| Duration Requested  : %u secs\n"
                        "| Devices Connected   : %d\n"
                        "|\n"
                        "| Messages Produced   : %u ( avg. delay = %.03f min )\n"
                        "| Messages Received   : %u (for me: %u)\n"
                        "| Messages Transmitted: %u (to recipient: %u)\n"
                        "|\n"
                        "*/\n\n\n",
                executionTimeActual, executionTimeRequested, 0,
                messagesStats.produced, messagesStats.producedDelayAvg,
                messagesStats.received, messagesStats.received_for_me,
                messagesStats.transmitted, messagesStats.transmitted_to_recipient );
    }

    removeTrailingCommaFromJson();
    fprintf( jsonFilePointer, "], \"duration\": \"%f s\", \"end\": \"%s\", \"stats\": { \"produced\": \"%d\", \"received\": \"%d\", \"received_for_me\": \"%d\", \"transmitted\": \"%d\", \"transmitted_to_recipient\": \"%d\", \"producedDelayAvg\": \"%.2fmin\", \"devices\": [",
            executionTimeActual, timestamp2ftime( (uint64_t) time(NULL), "%FT%TZ" ),
            messagesStats.produced, messagesStats.received, messagesStats.received_for_me,
            messagesStats.transmitted, messagesStats.transmitted_to_recipient, messagesStats.producedDelayAvg );

    // Inspect connections
    if ( ALSO_LOG_TO_STDOUT )
        fprintf( stdout, "\n\n-------------------- start: DEVICES INSPECTION --------------------\n" );

    for ( uint32_t device_i = 0; device_i < CLIENT_AEM_LIST_LENGTH; device_i++ )
    {
        uint32_t aem = CLIENT_AEM_LIST[device_i];

        if ( ALSO_LOG_TO_STDOUT )
            fprintf( stdout, "\t- %04d\n", aem );

        if ( 0 < CLIENT_AEM_CONN_N_LIST[device_i] )
        {
            fprintf( jsonFilePointer, "{ \"aem\": \"%04d\", \"connections\": [", aem );

            double averageDuration = 0.0;
            for ( uint8_t n = 0; n < CLIENT_AEM_CONN_N_LIST[device_i]; n++ )
            {
                double duration = (double)( CLIENT_AEM_CONN_END_LIST[device_i][n].tv_sec - CLIENT_AEM_CONN_START_LIST[device_i][n].tv_sec ) * 1e3 +
                        (double)( CLIENT_AEM_CONN_END_LIST[device_i][n].tv_usec - CLIENT_AEM_CONN_START_LIST[device_i][n].tv_usec ) * 1e-3;

                if ( ALSO_LOG_TO_STDOUT )
                    fprintf( stdout, "\t\t start: %lf | end %lf ( duration: %lfms )\n",
                        CLIENT_AEM_CONN_START_LIST[device_i][n].tv_sec + CLIENT_AEM_CONN_START_LIST[device_i][n].tv_usec * 1e-6,
                        CLIENT_AEM_CONN_END_LIST[device_i][n].tv_sec + CLIENT_AEM_CONN_END_LIST[device_i][n].tv_usec * 1e-6,
                        duration
                    );

                fprintf( jsonFilePointer, "{\"start\": \"%s.%03d\", \"end\": \"%s.%03d\", \"duration\": \"%.2fms\" },",
                    timestamp2ftime( CLIENT_AEM_CONN_START_LIST[device_i][n].tv_sec, "%H:%M:%S" ), (int)(CLIENT_AEM_CONN_START_LIST[device_i][n].tv_usec * 1e-3),
                    timestamp2ftime( CLIENT_AEM_CONN_END_LIST[device_i][n].tv_sec, "%H:%M:%S" ), (int)(CLIENT_AEM_CONN_END_LIST[device_i][n].tv_usec * 1e-3),
                    duration
                );

                averageDuration += duration;
            }

            removeTrailingCommaFromJson();
            fprintf( jsonFilePointer, "], \"average_duration\": \"%.2fms\"},", averageDuration / (double) CLIENT_AEM_CONN_N_LIST[device_i] );
        }
    }

    if ( ALSO_LOG_TO_STDOUT )
        fprintf( stdout, "\n-------------------- end: DEVICES INSPECTION --------------------\n\n" );

    // Finalize & close json file pointer
    removeTrailingCommaFromJson();
    fprintf( jsonFilePointer, "], \"buffer_messages\": [" );

    for ( uint16_t message_i = 0; message_i < MESSAGES_SIZE; message_i++ )
    {
        Message message = MESSAGES_BUFFER[message_i];
        if ( 0 == message.created_at ) continue;

        fprintf( jsonFilePointer, "{\"sender\": \"%u\", \"recipient\": \"%u\", \"created_at\": \"%s\", \"body\": \"%s\"},",
             message.sender, message.recipient, timestamp2ftime( message.created_at, "%FT%TZ" ), message.body
         );
    }

    removeTrailingCommaFromJson();
    fprintf( jsonFilePointer, "], \"inbox_messages\": [" );

    for (uint16_t inbox_message_i = 0; inbox_message_i < inboxHead; inbox_message_i++ )
    {
        #define inboxMessage INBOX[inbox_message_i]
        if ( 0 == inboxMessage.created_at ) continue;

        fprintf( jsonFilePointer, "{\"sender\": \"%u\", \"created_at\": \"%s\", \"saved_at\": \"%s\", \"body\": \"%s\", \"first_sender\": \"%u\"},",
                 inboxMessage.sender,
                 timestamp2ftime( inboxMessage.created_at, "%FT%TZ" ),
                 timestamp2ftime( inboxMessage.saved_at, "%FT%TZ" ),
                 inboxMessage.body, inboxMessage.first_sender
        );
    }

    removeTrailingCommaFromJson();
    fprintf( jsonFilePointer, "]}}" );
    fclose( jsonFilePointer );
}

/// Creates / Opens file and add the new session messages
/// \param jsonFileName
void log_tearUp(const char *jsonFileName)
{
    // Check if session.json file exists
    remove( jsonFileName );
    jsonFilePointer = fopen( jsonFileName, "w+" );

    const char* nowAsString = timestamp2ftime( (uint64_t) time(NULL), "%FT%TZ" );

    if ( ALSO_LOG_TO_STDOUT )
    {
        fprintf(stdout, "/*\n"
                             "|--------------------------------------------------------------------------\n"
                             "| start: NEW SESSION\n"
                             "|--------------------------------------------------------------------------\n"
                             "|\n"
                             "| Date    : %s\n"
                             "| Client  : %s\n"
                             "| FileName: %s\n"
                             "|\n"
                             "*/\n\n", nowAsString, aem2ip( CLIENT_AEM ), jsonFileName );
    }

    // JSON file start
    fprintf( jsonFilePointer, "{\"start\": \"%s\", \"client_aem\": \"%d\", \"requested_duration\":\"%u secs\", \"events\": [",
            nowAsString, CLIENT_AEM, executionTimeRequested );
}

/// \brief Removes last character from session.json file
void removeTrailingCommaFromJson(void)
{
    fpos_t lastFilePosition;
    fpos_t endFilePosition;

    // Get end file position
    fgetpos( jsonFilePointer, &endFilePosition );

    // Go to last character's position
    if ( -1 == fseek( jsonFilePointer, -1L, SEEK_CUR ) )
    {
        perror( "removeTrailingCommaFromJson(): fseek" );
    }

    // Get last character's position in session.json file
    fgetpos( jsonFilePointer, &lastFilePosition );

    // Set new write position to last character's position to overwrite character ( if it was comma )
    if ( -1 == fsetpos( jsonFilePointer, ',' == (char) fgetc( jsonFilePointer ) ? &lastFilePosition : &endFilePosition ) )
    {
        perror( "removeTrailingCommaFromJson(): fsetpos()" );
    }
}