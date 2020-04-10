/**
 * @file
 * @author Edward A. Lee (eal@berkeley.edu)
 *
 * @section LICENSE
Copyright (c) 2020, The University of California at Berkeley.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 * @section DESCRIPTION
 * Utility functions for a federate in a federated execution.
 */


#include <stdio.h>
#include <stdlib.h>
#include <errno.h>      // Defined perror(), errno
#include <sys/socket.h>
#include <netinet/in.h> // Defines struct sockaddr_in
#include <unistd.h>     // Defines read(), write(), and close()
#include <netdb.h>      // Defines gethostbyname().
#include <strings.h>    // Defines bzero().
#include <pthread.h>
#include <assert.h>
#include "util.c"       // Defines error() and swap_bytes_if_big_endian().
#include "rti.h"        // Defines TIMESTAMP.
#include "reactor.h"    // Defines instant_t.

// Error messages.
char* ERROR_SENDING_HEADER = "ERROR sending header information to federate via RTI";
char* ERROR_SENDING_MESSAGE = "ERROR sending message to federate via RTI";
char* ERROR_UNRECOGNIZED_MESSAGE_TYPE = "ERROR Received from RTI an unrecognized message type";

/** The socket descriptor for this federate to communicate with the RTI.
 *  This is set by connect_to_rti(), which must be called before other
 *  functions are called.
 */
int rti_socket = -1;

/** Send the specified message to the specified port in the
 *  specified federate via the RTI. The port should be an
 *  input port of a reactor in the destination federate.
 *  This version does not include the timestamp in the message.
 *  The caller can reuse or free the memory after this returns.
 *  @param port The ID of the destination port.
 *  @param federate The ID of the destination federate.
 *  @param length The message length.
 *  @param message The message.
 */
void send_via_rti(unsigned int port, unsigned int federate, size_t length, unsigned char* message) {
    assert(port < 65536);
    assert(federate < 65536);
    unsigned char buffer[9];
    buffer[0] = MESSAGE;
    // NOTE: Send messages little endian, not big endian.
    buffer[1] = port & 0xff;
    buffer[2] = port & 0xff00;
    buffer[3] = federate & 0xff;
    buffer[4] = federate & 0xff00;
    buffer[5] = length & 0xff;
    buffer[6] = length & 0xff00;
    buffer[7] = length & 0xff0000;
    buffer[8] = length & 0xff000000;

    write_to_socket(rti_socket, 9, buffer);
    write_to_socket(rti_socket, length, message);
}

/** Send the specified timestamped message to the specified port in the
 *  specified federate via the RTI. The port should be an
 *  input port of a reactor in the destination federate.
 *  This version does include the timestamp in the message.
 *  The caller can reuse or free the memory after this returns.
 *  @param port The ID of the destination port.
 *  @param federate The ID of the destination federate.
 *  @param length The message length.
 *  @param message The message.
 */
void send_via_rti_timed(unsigned int port, unsigned int federate, size_t length, unsigned char* message) {
    assert(port < 65536);
    assert(federate < 65536);
    unsigned char buffer[17];
    // First byte identifies this as a timed message.
    buffer[0] = TIMED_MESSAGE;
    // FIXME: The following encoding ops should become generic utilities in util.c.
    // Next two bytes identify the destination port.
    // NOTE: Send messages little endian, not big endian.
    encode_ushort(port, &(buffer[1]));

    // Next two bytes identify the destination federate.
    encode_ushort(federate, &(buffer[3]));

    // The next four bytes are the message length.
    encode_int(length, &(buffer[5]));

    // Next 8 bytes are the timestamp.
    instant_t current_time = get_logical_time();

    encode_ll(current_time, &(buffer[9]));
    // printf("DEBUG: Sending message with timestamp %lld.\n", current_time);

    write_to_socket(rti_socket, 17, buffer);
    write_to_socket(rti_socket, length, message);
}

/** Connect to the RTI at the specified host and port and return
 *  the socket descriptor for the connection. If this fails, the
 *  program exits. If it succeeds, it sets the rti_socket global
 *  variable to refer to the socket for communicating with the RTI.
 *  @param id The assigned ID of the federate.
 *  @param hostname A hostname, such as "localhost".
 *  @param port A port number.
 */
void connect_to_rti(int id, char* hostname, int port) {
    // Repeatedly try to connect, one attempt every 2 seconds, until
    // either the program is killed, the sleep is interrupted,
    // or the connection succeeds.
    int result = -1;
    int count_retries = 0;
    while (result < 0) {
        // Create an IPv4 socket for TCP (not UDP) communication over IP (0).
        rti_socket = socket(AF_INET , SOCK_STREAM , 0);
        if (rti_socket < 0) error("ERROR on federate creating socket to RTI");

        struct hostent *server = gethostbyname(hostname);
        if (server == NULL) {
            fprintf(stderr,"ERROR, no such host for RTI.\n");
            exit(1);
        }
        // Server file descriptor.
        struct sockaddr_in server_fd;

        // Zero out the server_fd struct.
        bzero((char *) &server_fd, sizeof(server_fd));

        // Set up the server_fd fields.
        server_fd.sin_family = AF_INET;    // IPv4
        bcopy((char *)server->h_addr,
             (char *)&server_fd.sin_addr.s_addr,
             server->h_length);
        // Convert the port number from host byte order to network byte order.
        server_fd.sin_port = htons(port);

        result = connect(
            rti_socket,
            (struct sockaddr *)&server_fd,
            sizeof(server_fd));
        if (result < 0) {
            count_retries++;
            if (count_retries > CONNECT_NUM_RETRIES) {
                fprintf(stderr, "Failed to connect to the RTI after %d retries. Giving up.\n",
                        CONNECT_NUM_RETRIES);
                exit(2);
            }
            printf("Could not connect to RTI at %s, port %d. Will try again every %d seconds.\n",
                    hostname, port, CONNECT_RETRY_INTERVAL);
            // Wait CONNECT_RETRY_INTERVAL seconds.
            struct timespec wait_time = {(time_t)CONNECT_RETRY_INTERVAL, 0L};
            struct timespec remaining_time;
            if (nanosleep(&wait_time, &remaining_time) != 0) {
                // Sleep was interrupted.
                continue;
            }
        }
    }
    printf("Federate: connected to RTI at %s, port %d.\n", hostname, port);

    // Notify the RTI of the ID of this federate.
    // Send the message type first.
    unsigned char message_marker = FED_ID;
    int bytes_written = write(rti_socket, &message_marker, 1);
    // FIXME: Retry rather than exit.
    if (bytes_written < 0) error("ERROR sending federate ID to RTI");

    // Send the ID.
    int message = swap_bytes_if_big_endian_int(id);

    bytes_written = write(rti_socket, (void*)(&message), sizeof(int));
    if (bytes_written < 0) error("ERROR sending federate ID to RTI");
}

/** Send the specified timestamp to the RTI and wait for a response.
 *  The specified timestamp should be current physical time of the
 *  federate, and the response will be the designated start time for
 *  the federate. This proceedure blocks until the response is
 *  received from the RTI.
 *  @param my_physical_time The physical time at this federate.
 *  @return The designated start time for the federate.
 */
instant_t get_start_time_from_rti(instant_t my_physical_time) {
    // Send the timestamp marker first.
    unsigned char message_marker = TIMESTAMP;
    int bytes_written = write(rti_socket, &message_marker, 1);
    // FIXME: Retry rather than exit.
    if (bytes_written < 0) error("ERROR sending message ID to RTI");

    // Send the timestamp.
    long long message = swap_bytes_if_big_endian_ll(my_physical_time);

    bytes_written = write(rti_socket, (void*)(&message), sizeof(long long));
    if (bytes_written < 0) error("ERROR sending message to RTI");

    // Get a reply.
    // Buffer for message ID plus timestamp.
    int buffer_length = sizeof(long long) + 1;
    unsigned char buffer[buffer_length];

    // Read bytes from the socket. We need 9 bytes.
    read_from_socket(rti_socket, 9, &(buffer[0]));
    // printf("DEBUG: federate read 9 bytes.\n");

    // First byte received is the message ID.
    if (buffer[0] != TIMESTAMP) {
        fprintf(stderr,
                "ERROR: Federate expected a TIMESTAMP message from the RTI. Got %u (see rti.h).\n",
                buffer[0]);
        exit(1);
    }

    instant_t timestamp = swap_bytes_if_big_endian_ll(*((long long*)(&(buffer[1]))));
    printf("Federate: starting timestamp is: %lld\n", timestamp);

    return timestamp;
}

/**
 * Placeholder for a generated function that returns a pointer to the
 * trigger_t struct for the action corresponding to the specified port ID.
 * @param port_id The port ID.
 * @return A pointer to a trigger_t struct or null if the ID is out of range.
 */
trigger_t* __action_for_port(int port_id);

/** Handle a message being received from a remote federate via the RTI.
 *  @param buffer The buffer to read.
 */
void handle_message(unsigned char* buffer) {
    // Read the header.
    read_from_socket(rti_socket, 8, buffer);
    // Extract the header information.
    unsigned short port_id;
    unsigned short federate_id;
    unsigned int length;
    extract_header(buffer, &port_id, &federate_id, &length);
    // printf("DEBUG: Federate receiving message to port %d to federate %d of length %d.\n", port_id, federate_id, length);

    // Read the payload.
    // Allocate memory for the message contents.
    unsigned char* message_contents = malloc(length);
    read_from_socket(rti_socket, length, message_contents);
    // printf("DEBUG: Message received by federate: %s.\n", message_contents);

    schedule_value(__action_for_port(port_id), 0LL, message_contents, length);
    // printf("DEBUG: Called schedule\n");
}

/** Handle a timestamped message being received from a remote federate via the RTI.
 *  This will read the timestamp, which is appended to the header,
 *  and calculate an offset to pass to the schedule function.
 *  @param buffer The buffer to read.
 */
void handle_timed_message(unsigned char* buffer) {
    // Read the header.
    read_from_socket(rti_socket, 8, buffer);
    // Extract the header information.
    unsigned short port_id;
    unsigned short federate_id;
    unsigned int length;
    extract_header(buffer, &port_id, &federate_id, &length);
    // printf("DEBUG: Federate receiving message to port %d to federate %d of length %d.\n", port_id, federate_id, length);

    // Read the timestamp.
    read_from_socket(rti_socket, 8, buffer + 8);
    instant_t timestamp = extract_ll(buffer + 8);
    // printf("DEBUG: Message timestamp: %lld.\n", timestamp);

    // Read the payload.
    // Allocate memory for the message contents.
    unsigned char* message_contents = malloc(length);
    read_from_socket(rti_socket, length, message_contents);
    // printf("DEBUG: Message received by federate: %s.\n", message_contents);

    // Acquire the one mutex lock to prevent logical time from advancing
    // between the time we get logical time and the time we call schedule().
    pthread_mutex_lock(&mutex);

    interval_t delay = timestamp - get_logical_time();
    schedule_value(__action_for_port(port_id), delay, message_contents, length);
    // printf("DEBUG: Called schedule with delay %lld.\n", delay);

    pthread_mutex_unlock(&mutex);
}

/** Thread that listens for inputs from the RTI.
 *  When a physical message arrives, this calls schedule.
 */
void* listen_to_rti(void* args) {
    // Buffer for incoming messages.
    // This does not constrain the message size
    // because the message will be put into malloc'd memory.
    unsigned char buffer[BUFFER_SIZE];

    // Listen for messages from the federate.
    while (1) {
        // Read one byte to get the message type.
        read_from_socket(rti_socket, 1, buffer);
        switch(buffer[0]) {
        case MESSAGE:
            handle_message(buffer + 1);
            break;
        case TIMED_MESSAGE:
            handle_timed_message(buffer + 1);
            break;
        default:
            // printf("DEBUG: Erroneous message type: %d\n", buffer[0]);
            error(ERROR_UNRECOGNIZED_MESSAGE_TYPE);
        }
    }
    return NULL;
}

/** Synchronize the start with other federates via the RTI.
 *  This initiates a connection with the RTI, then
 *  sends the current logical time to the RTI and waits for the
 *  RTI to respond with a specified time.
 *  It then waits for physical time to match the specified time,
 *  sets current logical time to the time returned by the RTI,
 *  and then returns.
 *  @param id The assigned ID of the federate.
 *  @param hostname The name of the RTI host, such as "localhost".
 *  @param port The port used by the RTI.
 */
void synchronize_with_other_federates(int id, char* hostname, int port) {

    // printf("DEBUG: Federate synchronizing with other federates.\n");

    // Connect to the RTI. This sets rti_socket.
    connect_to_rti(id, hostname, port);

    // Reset the start time to the coordinated start time for all federates.
    current_time = get_start_time_from_rti(get_physical_time());

    start_time = current_time;

    if (duration >= 0LL) {
        // A duration has been specified. Recalculate the stop time.
        stop_time = current_time + duration;
    }

    // Start a thread to listen for incoming messages from the RTI.
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, listen_to_rti, NULL);

    // If --fast was not specified, wait until physical time matches
    // or exceeds the start time.
    wait_until(current_time);
    // printf("DEBUG: Done waiting for start time %lld.\n", current_time);
    // printf("DEBUG: Physical time is ahead of current time by %lld.\n", get_physical_time() - current_time);

    // Reinitialize the physical start time to match the current physical time.
    // This will be the same on each federate.
    physical_start_time = current_time;

    /* To make it different on each federate, use this:
    struct timespec actualStartTime;
    clock_gettime(CLOCK_REALTIME, &actualStartTime);
    physical_start_time = actualStartTime.tv_sec * BILLION + actualStartTime.tv_nsec;
    */
}
