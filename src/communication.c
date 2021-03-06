#include "conf.h"
#include "communication.h"
#include "log.h"
#include "server.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>

//------------------------------------------------------------------------------------------------

extern uint32_t CLIENT_AEM;
extern struct timeval CLIENT_AEM_CONN_START_LIST[CLIENT_AEM_LIST_LENGTH][MAX_CONNECTIONS_WITH_SAME_CLIENT];
extern struct timeval CLIENT_AEM_CONN_END_LIST[CLIENT_AEM_LIST_LENGTH][MAX_CONNECTIONS_WITH_SAME_CLIENT];
extern uint8_t CLIENT_AEM_CONN_N_LIST[CLIENT_AEM_LIST_LENGTH];

extern pthread_mutex_t messagesBufferLock, activeDevicesLock, availableThreadsLock, messagesStatsLock, logEventLock;
extern MessagesStats messagesStats;

extern pthread_t communicationThreads[ COMMUNICATION_WORKERS_MAX ];
extern uint8_t communicationThreadsAvailable;

extern Message MESSAGES_BUFFER[ MESSAGES_SIZE ];

//------------------------------------------------------------------------------------------------

/// \brief Datetime transmitter loop. Transmits current datetime on each new connection.
void communication_datetime_listener_worker(void)
{
    struct timeval tv;
    struct timezone tz;

    int server_socket_fd;
    int client_socket_fd;
    int status;
    struct sockaddr_in serverAddress;
    struct sockaddr_in clientAddress;

    // Create the server ( parent ) socket
    server_socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket_fd < 0)
        error(server_socket_fd, "ERROR opening socket");

    // Build the server's Internet address
    bzero((char *)&serverAddress, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = inet_addr( aem2ip( CLIENT_AEM ) );
    serverAddress.sin_port = htons((unsigned short) ( SOCKET_PORT + 1 ) );  // next port for setup only

    /* setsockopt: Handy debugging trick that lets
     * us rerun the server immediately after we kill it;
     * otherwise we have to wait about 20 secs.
     * Eliminates "ERROR on binding: Address already in use" error.
     */
    status = 1;
    if ( setsockopt( server_socket_fd, SOL_SOCKET, SO_REUSEPORT, (const void *)&status, sizeof(int) ) < 0 )
        perror("setsockopt ( SO_REUSEPORT )");

    struct linger lin;
    lin.l_onoff = 0;
    lin.l_linger = 0;
    if ( setsockopt( server_socket_fd, SOL_SOCKET, SO_LINGER, (const void *)&lin, sizeof(struct linger) ) < 0 )
        perror("setsockopt ( SO_LINGER )");

    // Associate the parent socket with a port
    if ( ( status = bind( server_socket_fd, (struct sockaddr *)&serverAddress, sizeof(struct sockaddr_in) ) < 0 ) )
        error(status, "ERROR on binding");

    listen( server_socket_fd, SOCKET_LISTEN_QUEUE_LEN );

    while (1)
    {
        client_socket_fd = accept(server_socket_fd, (struct sockaddr *) &clientAddress, &(socklen_t){ sizeof( struct sockaddr_in ) } );
        if (client_socket_fd < 0)
            error(client_socket_fd, "ERROR on accept");

        pthread_mutex_lock( &logEventLock );

            //  - get client address
            char ip[INET_ADDRSTRLEN];
            inet_ntop( AF_INET, &( clientAddress.sin_addr ), ip, INET_ADDRSTRLEN );

            //  - close read stream
            shutdown( client_socket_fd, SHUT_RD );

            //  - get time
            gettimeofday( &tv, &tz );

            //  - send time
            send( client_socket_fd, &tv, sizeof(struct timeval), 0 );

            //  - close write stream
            shutdown( client_socket_fd, SHUT_WR );
            close( client_socket_fd );
        
        pthread_mutex_unlock( &logEventLock );

        fprintf( stdout, "SENT DATETIME TO CLIENT ( current timestamp = %ld )\n", tv.tv_sec );
    }
}

/// \brief Receive current "timeofday" from reference device ( with AEM = 0001 )
/// \return TRUE on success, FALSE on failure
bool communication_datetime_receiver()
{
    struct timeval tv = {0, 0};
    int socket_fd;
    bool result;

    uint64_t previous_now;
    uint64_t new_now;

    // Initialize socket file descriptor
    socket_fd = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

    do
    {
        // Try connecting
        if ( true == socket_connect( (int32_t) socket_fd, SETUP_DATETIME_AEM, SOCKET_PORT + 1 ) )
        {
            log_event_start( "datetime", SETUP_DATETIME_AEM, CLIENT_AEM );

            //  - close write stream
            shutdown( socket_fd, SHUT_WR );

            //  - get time
            result = read( socket_fd, &tv, sizeof(struct timeval) ) == sizeof(struct timeval);

            //  - get time
            if ( result )
            {
                previous_now = (uint64_t) time(NULL);

                // Set timezone
                setenv( "TZ", "Europe/Athens", 1 );
                tzset();

                // Set timeval
                settimeofday( &tv, NULL );

                new_now = (uint64_t) time(NULL);
                log_event_message_datetime( previous_now, new_now );
            }
            else
            {
                fprintf( stderr, "ERROR\n" );
            }

            //  - close read stream
            shutdown( socket_fd, SHUT_RD );
            close( socket_fd );

            log_event_stop();

            fprintf( stdout, "RECEIVED DATETIME FROM SERVER ( current timestamp = %ld )\n", tv.tv_sec );
            break;
        }
    }
    while( 1 );

    return result;
}

/// \brief Handle communication staff with connected device ( POSIX thread compatible function ).
/// \param thread_args pointer to communicate_args_t type
void communication_worker(void *thread_args)
{
    CommunicationWorkerArgs *args = (CommunicationWorkerArgs *) thread_args;
    bool deviceExists;

    // Check if there is an active connection with given device
    deviceExists = devices_exists( args->connected_device );

    pthread_mutex_lock( &logEventLock );
        log_event_start( "connection", args->server ? CLIENT_AEM : args->connected_device.AEM,
                         args->server ? args->connected_device.AEM : CLIENT_AEM );

        // If no active connection with given device exists
        if ( !deviceExists && CLIENT_AEM_CONN_N_LIST[ args->connected_device.aemIndex ] <= MAX_CONNECTIONS_WITH_SAME_CLIENT )
        {
            // Update active devices
            pthread_mutex_lock( &activeDevicesLock );
                devices_push( args->connected_device );
            pthread_mutex_unlock( &activeDevicesLock );

            gettimeofday( &(CLIENT_AEM_CONN_START_LIST[args->connected_device.aemIndex][CLIENT_AEM_CONN_N_LIST[ args->connected_device.aemIndex ]]), NULL );

            // If device is server, act as transmitter, else act as receiver.
            //  - forward communication
            if ( args->server )
            {
                communication_transmitter_worker( args->connected_socket_fd, args->connected_device );
                shutdown( args->connected_socket_fd, SHUT_WR );

                communication_receiver_worker( args->connected_socket_fd, args->connected_device );
                shutdown( args->connected_socket_fd, SHUT_RD );
            }
            //  - reverse communication
            else
            {
                communication_receiver_worker( args->connected_socket_fd, args->connected_device );
                shutdown( args->connected_socket_fd, SHUT_RD );

                communication_transmitter_worker( args->connected_socket_fd, args->connected_device );
                shutdown( args->connected_socket_fd, SHUT_WR );
            }

            // Update connection time stats
            gettimeofday( &(CLIENT_AEM_CONN_END_LIST[args->connected_device.aemIndex][CLIENT_AEM_CONN_N_LIST[ args->connected_device.aemIndex ]]), NULL );

            // Update connection time stats
            CLIENT_AEM_CONN_N_LIST[ args->connected_device.aemIndex ]++;

            // Update active devices
            pthread_mutex_lock( &activeDevicesLock );
                devices_remove( args->connected_device );
            pthread_mutex_unlock( &activeDevicesLock );
        }
        else
        {
            fprintf( stderr, deviceExists ?
                "Active connection with device found: AEM = %04d. Skipping...":
                "Max no. of connections with device reached: AEM = %04d. Skipping...", args->connected_device.AEM
            );
        }

        // Close Socket
        close( args->connected_socket_fd );

        // Update number of threads ( since, if this function is called in a new thread, then that thread was detached )
        if ( args->concurrent )
        {
            pthread_mutex_lock( &availableThreadsLock );
            communicationThreadsAvailable++;
            pthread_mutex_unlock( &availableThreadsLock );
        }

        // Finalize event log
        log_event_stop();
    pthread_mutex_unlock( &logEventLock );
}

/// \brief Receiver sub-worker of communication worker ( POSIX thread compatible function ).
/// \param connectedSocket socket file descriptor with connected device
/// \param connectedDevice connected device that will send messages
void communication_receiver_worker(int32_t connectedSocket, Device connectedDevice)
{
    Message message;
    char messageSerialized[MESSAGE_SERIALIZED_LEN];

    next_loop:
    while ( read( connectedSocket, messageSerialized, MESSAGE_SERIALIZED_LEN ) == MESSAGE_SERIALIZED_LEN )
    {
        // Reconstruct message
        explode( &message, "_", messageSerialized );

        // Check for duplicates
        for ( uint16_t message_i = 0; message_i < MESSAGES_SIZE; message_i++ )
        {
            if ( 1 == isMessageEqual(message, MESSAGES_BUFFER[message_i] ) )
                goto next_loop;

            if (MESSAGES_BUFFER[message_i].created_at == 0 )
                break;
        }

        // Update message's transmitted devices to include sender ( so as not to send back )
        message.transmitted_devices[ connectedDevice.aemIndex ] = 1;

        // Store in $MESSAGES_BUFFER buffer
        pthread_mutex_lock( &messagesBufferLock );
            CLIENT_AEM == message.recipient ?
                inbox_push( &message, &connectedDevice ):
                messages_push( &message );
        pthread_mutex_unlock( &messagesBufferLock );

        // Update stats
        pthread_mutex_lock( &messagesStatsLock );
            messagesStats.received++;
        pthread_mutex_unlock( &messagesStatsLock );

        // Log received message
        log_event_message( "received", &message );
    }
}

/// \brief Transmitter sub-worker of communication worker ( POSIX thread compatible function ).
/// \param connectedSocket socket file descriptor with connected device
/// \param connectedDevice connected device that will receive messages
void communication_transmitter_worker(int32_t connectedSocket, Device connectedDevice)
{
    char messageSerialized[MESSAGE_SERIALIZED_LEN + 1];

    for ( uint16_t message_i = 0; message_i < MESSAGES_SIZE; message_i++ )
    {
        if (-1 == connectedDevice.aemIndex )
        {
            error(-1, "connectedDevice.aemIndex equals -1. Exiting...");
        }

        if (MESSAGES_BUFFER[message_i].created_at > 0
            && 0 == MESSAGES_BUFFER[message_i].transmitted_devices[ connectedDevice.aemIndex ]
            && 0 == MESSAGES_BUFFER[message_i].transmitted_to_recipient
        )
        {
            // ASSERTION
            if ( CLIENT_AEM == MESSAGES_BUFFER[message_i].recipient )
                error( -1, "communication_transmitter_worker(): \"Assertion CLIENT_AEM == MESSAGES_BUFFER[message_i].recipient\" failed" );

            // Serialize
            implode("_", MESSAGES_BUFFER[message_i], messageSerialized );

            // Transmit
            send(connectedSocket, messageSerialized , MESSAGE_SERIALIZED_LEN, 0 );

            // Update Status in $MESSAGES_BUFFER buffer
            pthread_mutex_lock( &messagesBufferLock );
                MESSAGES_BUFFER[message_i].transmitted = 1;
                MESSAGES_BUFFER[message_i].transmitted_devices[ connectedDevice.aemIndex ] = 1;
                    if (connectedDevice.AEM == MESSAGES_BUFFER[message_i].recipient )
                    {
                        MESSAGES_BUFFER[message_i].transmitted_to_recipient = 1;
                    }
            pthread_mutex_unlock( &messagesBufferLock );

            // Update stats
            pthread_mutex_lock( &messagesStatsLock );
                messagesStats.transmitted++;
                if (connectedDevice.AEM == MESSAGES_BUFFER[message_i].recipient )
                {
                    messagesStats.transmitted_to_recipient++;
                }
            pthread_mutex_unlock( &messagesStatsLock );

            log_event_message( "transmitted", &MESSAGES_BUFFER[message_i] );
        }
    }
}
