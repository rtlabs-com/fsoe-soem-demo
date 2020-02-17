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
 * \brief Internal data types for master and slave
 *
 * This header files defines data types used for fields in
 * the master and slave state machine structures, fsoemaster_t and fsoeslave_t.
 * These fields are to be considered stack-internal implementation details.
 * User of the API must not directly access any field of said objects;
 * Use the public API functions in fsoemaster.h or fsoeslave.h instead.
 * The only reason these data types are exposed in a public
 * header file is to allow for static memory allocation.
 */

#ifndef FSOETYPES_H
#define FSOETYPES_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <fsoeoptions.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * \brief Number of bytes in FSoE frame containing \a data_size data bytes
 *
 * \internal
 * This macro is made public as to allow for static allocation.
 * User of the API should not use this macro.
 * Use FSOESLAVE_FRAME_SIZE() or FSOMASERT_FRAME_SIZE() instead.
 *
 * \param[in]     data_size      Number of data bytes in frame
 * \return                       Size of frame in bytes
 */
#define FSOEFRAME_SIZE(data_size) (\
   ((data_size) == 1) ? 6 : (2 * (data_size) + 3) )

/**
 * \brief An FSoE PDU frame
 *
 * \internal
 * This struct is made public as to allow for static allocation.
 * User of the API is prohibited from accessing any of the fields as the
 * layout of the structure is to be considered an implementation detail.
 * Only the stack-internal file "fsoeframe.c" may access any field directly.
 */
typedef struct fsoeframe
{
   size_t size;                              /**< Size of buffer in bytes */
   uint8_t au8Data[FSOEFRAME_SIZE (FSOE_PROCESS_DATA_MAX_SIZE) + 1]; /**< Buffer.
                                              * An extra byte at
                                              * the end is used for buffer
                                              * overflow detection.
                                              */
} fsoeframe_t;

/**
 * \brief A single 16 bit word
 *
 * Note that this is a union, not a struct.
 *
 * \internal
 * This union is made public as to allow for static allocation.
 * User of the API is prohibited from accessing any of the fields as the
 * layout of the union is to be considered an implementation detail.
 * Only the stack-internal files may access any field directly.
 */
typedef union fsoeframe_uint16
{
   uint16_t little_endian;                /**< A 16 bit word,
                                           * little endian encoded
                                           */
   uint8_t bytes[2];                      /**< A 16 bit word,
                                           * viewed as raw bytes
                                           */
} fsoeframe_uint16_t;

/**
 * \brief Little-endian encoded data transferred in Connection state
 *
 * See ETG.5100 ch. 8.2.2.4 table 15: "Safety data transferred
 * in the connection state".
 *
 * \internal
 * This struct is made public as to allow for static allocation.
 * User of the API is prohibited from accessing any of the fields as the
 * layout of the structure is to be considered an implementation detail.
 * Only the stack-internal files may access any field directly.
 */
typedef struct fsoeframe_encoded_conndata
{
   fsoeframe_uint16_t ConnId;             /**< Connection ID,
                                           * little endian encoded.
                                           */
   fsoeframe_uint16_t SlaveAddress;       /**< Slave address,
                                           * little endian encoded
                                           */
} fsoeframe_encoded_conndata_t;

/**
 * \brief Data transferred in Connection state ("ConnData")
 *
 * Note that this is a union, not a struct.
 *
 * \internal
 * This union is made public as to allow for static allocation.
 * User of the API is prohibited from accessing any of the fields as the
 * layout of the union is to be considered an implementation detail.
 * Only the stack-internal files may access any field directly.
 */
typedef union fsoeframe_conndata
{
   fsoeframe_encoded_conndata_t members;  /**< The parameters viewed as
                                           * Little-endian encoded members
                                           */
   uint8_t bytes[4];                      /**< The parameters viewed as raw
                                           * bytes.
                                           */
} fsoeframe_conndata_t;

/**
 * \brief Little-endian encoded data transferred in Parameter state
 *
 * See ETG.5100 ch. 8.2.2.5 table 18: "Safety data transferred in the
 * parameter state".
 *
 * \internal
 * This struct is made public as to allow for static allocation.
 * User of the API is prohibited from accessing any of the fields as the
 * layout of the structure is to be considered an implementation detail.
 * Only the stack-internal files may access any field directly.
 */
typedef struct fsoeframe_encoded_safepara
{
   fsoeframe_uint16_t watchdog_size;      /**< 2, little endian encoded */
   fsoeframe_uint16_t watchdog;           /**< Watchdog timeout in milliseconds,
                                           * little endian encoded.
                                           */
   fsoeframe_uint16_t app_parameters_size;/**< Size of application-
                                           * specific parameters in bytes,
                                           * little endian encoded.
                                           */
   uint8_t app_parameters[FSOE_APPLICATION_PARAMETERS_MAX_SIZE]; /**<
                                           * (Optional) application-
                                           * specific parameters.
                                           * Actual size is given by
                                           * configuration.
                                           */
} fsoeframe_encoded_safepara_t;

/**
 * \brief Data transferred in Parameter state ("SafePara")
 *
 * Note that this is a union, not a struct.
 *
 * \internal
 * This union is made public as to allow for static allocation.
 * User of the API is prohibited from accessing any of the fields as the
 * layout of the union is to be considered an implementation detail.
 * Only the stack-internal files may access any field directly.
 */
typedef union fsoeframe_safepara
{
   fsoeframe_encoded_safepara_t members; /**< The parameters viewed as
                                          * Little-endian encoded members
                                          */
   uint8_t bytes[sizeof (fsoeframe_encoded_safepara_t)]; /**<
                                          * The parameters viewed as raw bytes.
                                          */
} fsoeframe_safepara_t;

/**
 * \brief Black channel.
 *
 * \internal
 * This struct is made public as to allow for static allocation.
 * User of the API is prohibited from accessing any of the fields as the
 * layout of the structure is to be considered an implementation detail.
 * Only the stack-internal file "fsoechannel.c" may access any field directly.
 */
typedef struct fsoechannel
{
   fsoeframe_t received_frame;         /**< Received FSoE PDU frame */
   fsoeframe_t last_received_frame;    /**< Last received FSoE PDU frame */
   fsoeframe_t sent_frame;             /**< Sent FSoE PDU frame */
   void * app_ref;                     /**< Application reference. This pointer
                                        * will be passed to application callback
                                        * functions. Note that while the pointer
                                        * is never modified, application may
                                        * choose to modify the memory pointed to.
                                        */
} fsoechannel_t;

/**
 * \brief Watchdog timer
 *
 * \internal
 * This struct is made public as to allow for static allocation.
 * User of the API is prohibited from accessing any of the fields as the
 * layout of the structure is to be considered an implementation detail.
 * Only the stack-internal file "fsoewatchdog.c" may access any field directly.
 */
typedef struct fsoewatchdog
{
   uint32_t start_time_us;             /**< Time set when last frame was sent */
   uint32_t timeout_ms;                /**< Watchdog timeout in milliseconds */
   bool is_started;                    /**< true if watchdog timer is running */
} fsoewatchdog_t;

#ifdef __cplusplus
}
#endif

#endif /* FSOETYPES_H */
