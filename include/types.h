#ifndef FINAL_TYPES_H
#define FINAL_TYPES_H

#include <stdint.h>
#include "conf.h"

#define error(status, msg) do { errno = status; perror(msg); exit(EXIT_FAILURE); } while (0)

// start: Server.h
typedef uint16_t messages_head_t;

// start: Utils.h
typedef struct device_t {
    uint32_t AEM;
    int32_t aemIndex;
} Device;

typedef struct message_t {
    // Necessary fields
    uint32_t sender;                    // ΑΕΜ αποστολέα:       uint32
    uint32_t recipient;                 // ΑΕΜ παραλήπτη:       uint32
    uint64_t created_at;                // Χρόνος δημιουργίας:  uint64 ( Linux timestamp - 10 digits at the time of writing )
    char body[MESSAGE_BODY_LEN];        // Κείμενο μηνύματος:   ASCII[256]

    // Metadata
    uint8_t transmitted;                // If the message was actually transmitted from this device
    uint8_t transmitted_devices[CLIENT_AEM_LIST_LENGTH];    // Boolean array, 1 if i-th device has received the message,
                                                            // 0 otherwise
    uint8_t transmitted_to_recipient;
} Message;

typedef struct inbox_message_t {
    // Necessary fields
    uint32_t sender;                    // ΑΕΜ αποστολέα:       uint32
    uint64_t created_at;                // Χρόνος δημιουργίας:  uint64 ( Linux timestamp - 10 digits at the time of writing )
    uint64_t saved_at;                  // Χρόνος αποθήκευσης:  uint64 ( Linux timestamp - 10 digits at the time of writing )
    char body[MESSAGE_BODY_LEN];        // Κείμενο μηνύματος:   ASCII[256]

    // Metadata
    uint32_t first_sender;              // ΑΕΜ της συσκευής που μετέδωσε το μήνυμα
} InboxMessage;

/* pthread function arguments pointer */
typedef struct communication_worker_args_t {

    Device connected_device;
    int32_t connected_socket_fd;
    bool concurrent;
    bool server;

} CommunicationWorkerArgs;
// end

// start: Log.h
typedef struct messages_stats_t {

    // Total
    uint16_t produced;
    uint16_t received;
    uint16_t received_for_me;
    uint16_t transmitted;
    uint16_t transmitted_to_recipient;

    // Time
    float producedDelayAvg;

} MessagesStats;
// end

// start: Client.h

// end

#endif //FINAL_TYPES_H
