/*********************************************************************
 *        _       _         _
 *  _ __ | |_  _ | |  __ _ | |__   ___
 * | '__|| __|(_)| | / _` || '_ \ / __|
 * | |   | |_  _ | || (_| || |_) |\__ \
 * |_|    \__|(_)|_| \__,_||_.__/ |___/
 *
 * www.rt-labs.com
 * Copyright 2019 rt-labs AB, Sweden.
 * See LICENSE file in the project root for full license information.
 ********************************************************************/

/**
 * \file
 * \brief Callback functions to be implemented by application
 * 
 * The FSoE slave and master stack will call these functions
 * when needed.
 *
 * The arrows in the picture below denote direct function calls:
 *
 * \verbatim
 *            ---------------
 *            |             |
 *            | Application |
 *            |             |
 *            ---------------
 *  User API:      |     ^ Application callback API:
 *  - fsoemaster.h |     | - fsoeapp.h
 *  - fsoeslave.h  |     |
 *                 |     |
 *                 v     |
 *            ---------------
 *            |             |
 *            | Master or   |
 *            | slave stack |
 *            |             |
 *            ---------------
 * \endverbatim
 *
 * A master application needs to implement these functions:
 * - fsoeapp_send()
 * - fsoeapp_recv()
 * - fsoeapp_generate_session_id()
 *
 * A slave application needs to implement these functions:
 * - fsoeapp_send()
 * - fsoeapp_recv()
 * - fsoeapp_generate_session_id()
 * - fsoeapp_verify_parameters()
 */

#ifndef FSOEAPP_H
#define FSOEAPP_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Status returned by fsoeapp_verify_parameters()
 *
 * Note that values in the range 0x80 - 0xff are also allowed,
 * indicating that some application-specific parameter is invalid.
 */
#define FSOEAPP_STATUS_OK                 0  /**< All parameters are OK */
#define FSOEAPP_STATUS_BAD_TIMOUT         9  /**< Invalid watchdog timeout */
#define FSOEAPP_STATUS_BAD_APP_PARAMETER  11 /**< Invalid application-specific
                                              *   parameter
                                              */

/**
 * \brief Send a complete FSoE PDU frame
 *
 * An FSoE PDU frame starts with the Command byte and ends with the
 * Connection ID. Its size is given by the formula 
 *   MAX (3 + 2 * data_size, 6),
 * where data_size is the number of data bytes to send and is given by
 * a field in fsoemaster_cfg_t or fsoeslave_cfg_t.
 * See ETG.5100 ch. 8.1.1 "Safety PDU structure".
 *
 * This callback function is called by the FSoE stack when it wishes
 * to send a frame. Application is required to be implement this by
 * making an attempt to send the frame in supplied buffer.
 * If application wishes to communicate an error condition to
 * the FSoE stack then it may do so by calling fsoemaster_reset_connection()
 * or fsoeslave_reset_connection().
 *
 * Example:
 * \code
 * void fsoeapp_send (void * app_ref, const void * buffer, size_t size)
 * {
 *    memcpy (&my_sent_frame, buffer, size);
 * }
 * \endcode
 *
 * \param[in,out] app_ref     Application reference. This pointer was passed to
 *                            stack in fsoemaster_create or fsoeslave_create().
 *                            Application is free to use this as it sees fit.
 * \param[in]     buffer      Buffer containing a PDU frame to be sent.
 * \param[in]     size        Size of PDU frame in bytes. Always the same, as
 *                            given by formula above.
 */
void fsoeapp_send (void * app_ref, const void * buffer, size_t size);

/**
 * \brief Try to receive a complete FSoE PDU frame
 *
 * An FSoE PDU frame starts with the Command byte and ends with the
 * Connection ID. Its size is given by the formula 
 *   MAX (3 + 2 * data_size, 6),
 * where data_size is the number of data bytes to receive and is given by
 * a field in fsoemaster_cfg_t or fsoeslave_cfg_t.
 * See ETG.5100 ch. 8.1.1 "Safety PDU structure".
 *
 * This callback function is called by the FSoE stack when it wishes
 * to receive a frame. Application is required to implement this by first
 * checking if a frame was received. If no new frame was received then 
 * the function should either
 *   a) return without waiting for any incoming frame or
 *   b) copy previously received frame to buffer and return.
 * If a frame was received then its content should be copied to buffer.
 * If application wishes to communicate an error condition to
 * the FSoE stack then it may do so by calling fsoemaster_reset_connection()
 * or fsoeslave_reset_connection().
 *
 * Example:
 * \code
 * size_t fsoeapp_recv (void * app_ref, void * buffer, size_t size)
 * {
 *    memcpy (buffer, &my_received_frame, size);
 *    return length;
 * }
 * \endcode
 * 
 * \param[in,out] app_ref     Application reference. This pointer was passed to
 *                            stack in fsoemaster_create or fsoeslave_create().
 *                            Application is free to use this as it sees fit.
 * \param[out] buffer         Buffer where recieved PDU frame will be stored.
 * \param[in]  size           Size of PDU frame in bytes. Always the same, as
 *                            given by formula above.
 * \return                    Number of bytes received. Should be equal to
 *                            \a size if a frame was received.
 *                            If no frame was received, it may be equal 0.
 *                            Alternatively, the last received frame may be put
 *                            in the buffer with \a size bytes returned.
 */
size_t fsoeapp_recv (void * app_ref, void * buffer, size_t size);

/**
 * \brief Generate a Session ID
 *
 * A Session ID is a random 16 bit number.
 * See ETG.5100 ch. 8.1.3.7 "Session ID".
 *
 * This callback function is called by the FSoE stack after power-on
 * and after each connection reset. Application is required to implement this
 * by generating a random number which is sufficiently random that a
 * (with high probability) different random number will be generated 
 * after each system restart. A normal pseudo-random algorithm with
 * fixed seed value is not sufficient.
 *
 * Example:
 * \code
 * uint16_t fsoeapp_generate_session_id (void * app_ref)
 * {
 *    return (uint16_t) rand ();
 * }
 * \endcode
 *
 * \param[in,out] app_ref     Application reference. This pointer was passed to
 *                            stack in fsoemaster_create or fsoeslave_create().
 *                            Application is free to use this as it sees fit.
 * \return                    A generated Session ID
 */
uint16_t fsoeapp_generate_session_id (void * app_ref);

/**
 * \brief Verify received parameters
 *
 * The parameters include both communication parameters (the watchdog timeout)
 * as well as application-specific parameters.
 * See ETG.5100 ch. 7.1 "FSoE Connection".
 *
 * This callback function is called by FSoE slave when all parameters
 * have been received from master.
 * Application is required to be implement this by verifying
 * that the parameters are valid, returning an error code if not. 
 * If error is returned, slave will reset the connection
 * and send the specified error code to master.
 *
 * The master stack does not call this function.
 *
 * Example:
 * \code
 * uint8_t fsoeapp_verify_parameters (
 *    void * app_ref, uint16_t timeout_ms, 
 *    const void * app_parameters, size_t app_parameters_size)
 * {
 *    if (timeout_ms < MIN_TIMOUT || timeout_ms > MAX_TIMEOUT)
 *    {
 *       return FSOEAPP_STATUS_BAD_TIMOUT;
 *    }
 *    else
 *    {
 *       uint16_t value;
 *
 *       memcpy (&value, app_parameters, sizeof (value));
 *       if (value > MAX_VALUE)
 *       {
 *          return FSOEAPP_STATUS_BAD_APP_PARAMETER;
 *       }
 *       else
 *       {
 *          return FSOEAPP_STATUS_OK;
 *       }
 *    }
 * }
 * \endcode
 *
 * \param[in,out] app_ref     Application reference. This pointer was passed to
 *                            stack in fsoeslave_create().
 *                            Application is free to use this as it sees fit.
 * \param[in] timeout_ms      Watchdog timeout in milliseconds.
 * \param[in] app_parameters  Buffer with received application-specific 
 *                            parameters.
 * \param[in] app_parameters_size
 *                            Size of application-specific parameters 
 *                            in bytes. Will always be equal to configured
 *                            application parameter size. See fsoeslave_cfg_t.
 * \return                    Error code:
 *                            - FSOEAPP_STATUS_OK if all parameters are valid,
 *                            - FSOEAPP_STATUS_BAD_TIMOUT if the watchdog
 *                              timeout is invalid,
 *                            - FSOEAPP_STATUS_BAD_APP_PARAMETER if
 *                              application-specific parameters are invalid,
 *                            - 0x80-0xFF if application-specific parameters 
 *                              are invalid and cause is given by application-
 *                              specific error code.
 */
uint8_t fsoeapp_verify_parameters (
   void * app_ref,
   uint16_t timeout_ms,
   const void * app_parameters, 
   size_t app_parameters_size);

#ifdef __cplusplus
}
#endif

#endif /* FSOEAPP_H */
