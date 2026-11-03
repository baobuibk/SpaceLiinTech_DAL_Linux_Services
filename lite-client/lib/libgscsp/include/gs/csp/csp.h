#ifndef LIBGSCSP_INCLUDE_GS_CSP_CSP_H
#define LIBGSCSP_INCLUDE_GS_CSP_CSP_H
/* Copyright (c) 2013-2018 GomSpace A/S. All rights reserved. */
/**
   @file

   Extensions to standard libcsp.
*/

#include <gs/csp/error.h>
#include <gs/csp/port.h>
#include <gs/csp/rtable.h>
#include <csp/csp.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
   Default CSP timeout (mS).

   Default timeout value for communicating with a satellite, based on round-trip time of approximately 500 mS.
*/
#define GS_CSP_TIMEOUT 3000

/**
   Check if address is a valid CSP adddress.
   @param[in] address CSP address to verify
   @return \a true if address is valid.
*/
bool gs_csp_is_address_valid(uint8_t address);
    
/**
   gscsp and csp configuration.
*/
typedef struct {
    /**
       Forward CSP logs to GomSpace log (libutil).
    */
    bool use_gs_log;

    /**
       Use command line options.
       Configure interfaces specified on the command line.
       @note Only supported on Linux - ignored on platforms without command line options.
    */
    bool use_command_line_options;

    /**
       Size of a CSP buffer (in bytes).
       @see csp_buffer_init().
    */
    size_t csp_buffer_size;
    /**
       Number of CSP buffers to allocate.
       @see csp_buffer_init().
    */
    size_t csp_buffers;

    /**
       CSP address of the system
    */
    uint8_t address;
    /**
       Host name, returned by the #CSP_CMP_IDENT request.
       @note String must remain valid as long as the application is running.
    */
    const char *hostname;
    /**
       Model, returned by the #CSP_CMP_IDENT request.
       @note String must remain valid as long as the application is running.
    */
    const char *model;
    /**
       Revision, returned by the #CSP_CMP_IDENT request
       @note String must remain valid as long as the application is running.
    */
    const char *revision;

} gs_csp_conf_t;

/**
   Get default CSP configuration for server systems.
   @param[in] conf user supplied configuration struct.
*/
void gs_csp_conf_get_defaults_server(gs_csp_conf_t * conf);

/**
   Get default CSP configuration for embedded systems.
   @param[in] conf user supplied configuration struct.
*/
void gs_csp_conf_get_defaults_embedded(gs_csp_conf_t * conf);

/**
   Initialize CSP.

   Wraps initialization of libcsp, by calling following functions
   - hooks CSP log system into GomSpace log system.
   - initializes CSP buffers by calling csp_buffer_init()
   - initializes CSP by calling csp_init()
   - configure interfaces specificed on command line
   - configure routing table specificed on command line, or default if only one interface is present.

   @param[in] conf configuration.
   @return_gs_error_t
*/
gs_error_t gs_csp_init(const gs_csp_conf_t * conf);
    
/**
   Perform an entire request/reply transaction,

   Copies both input buffer and reply to output buffer.
   Also makes the connection and closes it again
   Differ from 'csp_transaction()' by limiting input buffer size when input length is unknown

   @param[in] prio CSP Prio
   @param[in] dest CSP Dest
   @param[in] port CSP Port
   @param[in] timeout timeout in ms
   @param[in] tx_buf pointer to outgoing data buffer
   @param[in] tx_len length of request to send
   @param[out] rx_buf pointer to incoming data buffer
   @param[in] rx_max_len length of expected reply (input buffer size)
   @param[out] rx_len pointer to length of reply
   @param[in] opts connection options.
   @return GS_ERROR_OVERFLOW when input length larger than buffer - it still updates rx_buf
   @return GS_ERROR_IO when 'csp_send()' or 'csp_read()' fails
   @return GS_ERROR_ALLOC when 'csp_get_buffer()' fails
   @return_gs_error_t
 */
gs_error_t gs_csp_transaction2(uint8_t prio,
                               uint8_t dest,
                               uint8_t port,
                               uint32_t timeout,
                               const void * tx_buf,
                               size_t tx_len,
                               void * rx_buf,
                               size_t rx_max_len,
                               size_t * rx_len,
                               uint32_t opts);

/**
   Perform an entire request/reply transaction,

   @param[in] prio CSP Prio
   @param[in] dest CSP Dest
   @param[in] port CSP Port
   @param[in] timeout timeout in ms
   @param[in] tx_buf pointer to outgoing data buffer
   @param[in] tx_len length of request to send
   @param[out] rx_buf pointer to incoming data buffer
   @param[in] rx_max_len length of expected reply (input buffer size)
   @param[out] rx_len pointer to length of reply
   @return GS_ERROR_OVERFLOW when input length larger than buffer - it still updates rx_buf
   @return GS_ERROR_IO when 'csp_send()' or 'csp_read()' fails
   @return GS_ERROR_ALLOC when 'csp_get_buffer()' fails
   @return_gs_error_t
   @see gs_csp_transaction2()
*/
static inline gs_error_t gs_csp_transaction(uint8_t prio,
                                            uint8_t dest,
                                            uint8_t port,
                                            uint32_t timeout,
                                            const void * tx_buf,
                                            size_t tx_len,
                                            void * rx_buf,
                                            size_t rx_max_len,
                                            size_t * rx_len)
{
    return gs_csp_transaction2(prio, dest, port, timeout, tx_buf, tx_len, rx_buf, rx_max_len, rx_len, 0);
}

/**
   Use an existing connection to perform a transaction.

   This is only possible if the next packet is on the same port and destination!
   Differ from 'csp_transaction_persistent()' by limiting input buffer size when input length is unknown

   @param[in] conn pointer to connection structure
   @param[in] timeout timeout in ms
   @param[in] tx_buf pointer to outgoing data buffer
   @param[in] tx_len length of request to send
   @param[out] rx_buf pointer to incoming data buffer
   @param[in] rx_max_len length of expected reply (input buffer size)
   @param[out] rx_len pointer to length of reply
   @return GS_ERROR_OVERFLOW when input length larger than buffer - it still updates rx_buf
   @return GS_ERROR_IO when 'csp_send()' or 'csp_read()' fails
   @return GS_ERROR_ALLOC when 'csp_get_buffer()' fails
   @return_gs_error_t
 */
gs_error_t gs_csp_transaction_persistent(csp_conn_t * conn,
                                         uint32_t timeout,
                                         const void * tx_buf,
                                         size_t tx_len,
                                         void * rx_buf,
                                         size_t rx_max_len,
                                         size_t * rx_len);

#ifdef __cplusplus
}
#endif
#endif
