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
 * \brief FSoE master state machine
 *
 * An FSoE master state machine handles the connection with a single
 * FSoE slave.
 *
 * After power-on, master will try to establish a connection with its slave
 * Once established, it will periodically send outputs to the slave.
 * The slave will respond by sending back its inputs.
 *
 * Inputs and outputs may contain valid process data or they
 * may contain fail-safe data (all zeros). By default, they contain
 * fail-safe data. They will only contain valid process data if sender
 * (master for outputs, slave for inputs) determines that everything is OK.
 * The sender may send valid process data while receiving fail-safe
 * data or vice versa.
 * Inputs and outputs have fixed size, but they need not be the same size.
 *
 * A user of the API will have to explicitly enable it in order for
 * valid process data to be sent.
 * Communication errors will cause the connection to be reset.
 * The master state machine will then disable the process data outputs and
 * try to re-establish connection with its slave. If successful, it restarts
 * sending outputs as fail-safe data.
 * A user of the API may then re-enable process data outputs.
 *
 * \verbatim
 *     ----------            ---------
 *     |        |  outputs   |       |   Arrows in picture
 *     | FSoE   | ---------> | FSoE  |   denote data flow
 *     | master |            | slave |
 *     |        | <--------- |       |
 *     ----------   inputs   ---------
 * \endverbatim
 *
 * \par Black Channel Communication
 *
 * At a lower level, the master state machine communicates with the slave
 * through a "black channel". The master state machine does not know how the
 * black channel is implemented,it just knows how to access it - by calling
 * fsoeapp_send() and fsoeapp_recv().
 * The application implementer needs to implement these two functions.
 *
 * The arrows in the picture below denote direct function calls:
 *
 * \verbatim
 *      |  |  |  Public master API (fsoemaster.h):
 *      |  |  |  - fsoemaster_sync_with_slave()
 *      v  v  v  - fsoemaster_get_state() etc.
 *    -----------
 *    |         |
 *    | FSoE    |
 *    | master  |
 *    |         |
 *    -----------
 *      |     | Black channel API (fsoeapp.h):
 *      |     | - fsoeapp_send()
 *      |     | - fsoeapp_recv()
 *      v     v
 *    -----------
 *    |         |
 *    | Black   |
 *    | channel |
 *    |         |
 *    -----------
 * \endverbatim
 *
 * In addition to fsoeapp_send() and fsoeapp_recv(), the application
 * implementer also needs to implement the functions
 * fsoeapp_generate_session_id() and fsoeapp_handle_user_error().
 * See the header file fsoeapp.h for more details.
 */

#ifndef FSOEMASTER_H
#define FSOEMASTER_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <fsoetypes.h>
#include <fsoeexport.h>

/**
 * \brief Reset reasons
 *
 * These codes are sent between master and slave when either side
 * requests connection to be reset. They are sent in Reset frames.
 * Local reset (FSOEMASTER_RESETREASON_LOCAL_RESET) may be requested
 * by any master or slave application. Local reset is also
 * the reset reason sent by master to slave at startup.
 * All other reset reasons are error conditions detected by an
 * FSoE state machine.
 *
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */

/**
 * \brief Local reset
 *
 * Master or slave application requested connection to be reset.
 * Also sent by master state machine at startup.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOEMASTER_RESETREASON_LOCAL_RESET         (0)

/**
 * \brief Invalid command
 *
 * Master or slave state machine requested connection to be reset
 * after receiving a frame whose type was not valid for current state.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
*/
#define FSOEMASTER_RESETREASON_INVALID_CMD         (1)

/**
 * \brief Unknown command
 *
 * Master or slave state machine requested connection to be reset
 * after receiving a frame of unknown type.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOEMASTER_RESETREASON_UNKNOWN_CMD         (2)

/**
 * \brief Invalid Connection ID
 *
 * Master or slave state machine requested connection to be reset
 * after receiving a frame with invalid Connection ID.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOEMASTER_RESETREASON_INVALID_CONNID      (3)

/**
 * \brief Invalid CRC
 *
 * Master or slave state machine requested connection to be reset
 * after receiving a frame with invalid CRCs.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOEMASTER_RESETREASON_INVALID_CRC         (4)

/**
 * \brief Watchdog timer expired
 *
 * Master or slave state machine requested connection to be reset
 * after watchdog timer expired while waiting for a frame to be
 * received.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOEMASTER_RESETREASON_WD_EXPIRED          (5)

/**
 * \brief Invalid slave address
 *
 * Slave state machine requested connection to be reset
 * after receiving Connection frame with incorrect slave address from master.
 * Never requested by master state machine.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOEMASTER_RESETREASON_INVALID_ADDRESS     (6)

/**
 * \brief Invalid configuration data
 *
 * Master state machine requested connection to be reset
 * after receiving Connection or Parameter frame from slave containing
 * different data than what was sent to it.
 * Never requested by slave state machine.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOEMASTER_RESETREASON_INVALID_DATA        (7)

/**
 * \brief Invalid size of Communication parameters
 *
 * Slave state machine requested connection to be reset
 * after receiving Parameter frame with incorrect size of
 * Communication Parameters from master.
 * Never requested by master state machine.
 * The only communication parameter is the watchdog timeout, whose size
 * is always two bytes.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOEMASTER_RESETREASON_INVALID_COMPARALEN  (8)

/**
 * \brief Invalid Communication parameter data
 *
 * Slave state machine requested connection to be reset
 * after receiving Parameter frame with incompatible
 * watchdog timeout from master.
 * Never requested by master state machine.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOEMASTER_RESETREASON_INVALID_COMPARA     (9)

/**
 * \brief Invalid size of Application parameters
 *
 * Slave state machine requested connection to be reset
 * after receiving Parameter frame with incompatible
 * size for Application Parameters.
 * Never requested by master state machine.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOEMASTER_RESETREASON_INVALID_USERPARALEN (10)

/**
 * \brief Invalid Application parameter data (generic error code)
 *
 * Slave state machine requested connection to be reset
 * after receiving Parameter frame with incompatible Application Parameters.
 * Never requested by master state machine.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOEMASTER_RESETREASON_INVALID_USERPARA    (11)

/**
 * \brief Invalid Application parameter data (first device-specific error code)
 *
 * Slave state machine requested connection to be reset
 * after receiving Parameter frame with incompatible Application Parameters.
 * Never requested by master state machine.
 * The device-specific error codes are in the range 0x80 ... 0xFF.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOEMASTER_RESETREASON_INVALID_USERPARA_MIN (0x80)

/**
 * \brief Invalid Application parameter data (last device-specific error code)
 *
 * Slave state machine requested connection to be reset
 * after receiving Parameter frame with incompatible Application Parameters.
 * Never requested by master state machine.
 * The device-specific error codes are in the range 0x80 ... 0xFF.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOEMASTER_RESETREASON_INVALID_USERPARA_MAX (0xFF)

/**
 * \brief Number of bytes in FSoE frame containing \a data_size data bytes
 *
 * \param[in]     data_size      Number of data bytes in frame.
 *                               Needs to be even or 1.
 * \return                       Size of frame in bytes.
 */
#define FSOEMASTER_FRAME_SIZE(data_size) (\
   ((data_size) == 1) ? 6 : (2 * (data_size) + 3) )

/**
 * \brief User API functions return codes
 *
 * Returned from each API function to indicate if user called the function
 * correctly as described in the function's documentation.
 *
 * \see fsoemaster_status_t
 */

/**
 * \brief User called the API correctly
 *
 */
#define FSOEMASTER_STATUS_OK         (0)

/**
 * \brief User violated the API
 *
 * User violated the function's preconditions. fsoeapp_handle_user_error()
 * callback will give detailed information about what caused the function to
 * return ERROR.
 */
#define FSOEMASTER_STATUS_ERROR      (-1)

/**
 * \brief Status returned from API function
 *
 * Returned from each API function to indicate if user called the function
 * correctly as described in the function's documentation.
 *
 * \see FSOEMASTER_STATUS_OK
 * \see FSOEMASTER_STATUS_ERROR
 */
typedef int32_t fsoemaster_status_t;

/**
 * \brief Connection state
 *
 * After power-on, the master state machine is in Reset state. In Reset state,
 * master is not connected with any slave. Interchange of process data with a
 * slave only takes place when master is in Data state. Before Data state is
 * entered, master first has to configure the slave by sending it configuration
 * data. This takes place in the intermediate states Session, Connection
 * and Parameter.
 *
 * The master state machine transitions to new states once configuration data
 * sent to slave has been ACKed by slave. It also sets the slave's state by
 * means of sending a corresponding frame. For example, master sending
 * a Connection frame will cause slave to enter Connection state,
 * assuming that state transition is allowed.
 *
 * \verbatim
 *                     --------------
 *                     |   Reset    |<---\
 *                     --------------    | Master detected communication
 *                           |           | error OR application requested
 *                           v           | connection to be reset.
 *                     --------------    |
 *                /--->|  Session   |--->|
 * Slave reported |    --------------    |
 * communication  |          |           |
 * error          |          v           |
 *                |    --------------    |
 *                |<---| Connection |--->|
 *                |    --------------    |
 *                |          |           |
 *                |          v           |
 *                |    --------------    |
 *                |<---| Parameter  |--->|
 *                |    --------------    |
 *                |          |           |
 *                |          v           |
 *                |    --------------    |
 *                \<---|    Data    |--->/
 *                     --------------
 * \endverbatim
 *
 * See ETG.5100 ch. 8.4.1.1 table 29 "States of the FSoE Master".
 */
typedef enum fsoemaster_state
{
   FSOEMASTER_STATE_RESET,       /**< Connection is reset */
   FSOEMASTER_STATE_SESSION,     /**< The session IDs are being transferred */
   FSOEMASTER_STATE_CONNECTION,  /**< The connection ID is being transferred */
   FSOEMASTER_STATE_PARAMETER,   /**< The parameters are being transferred */
   FSOEMASTER_STATE_DATA,        /**< Process or fail-safe data is being transferred */
} fsoemaster_state_t;

/**
 * \brief Connection reset event
 *
 * A reset of connection between master and slave may be initiated by either
 * side sending a Reset frame containing a code describing why the reset was
 * initiated, such as an error detected by FSoE stack, system startup (only
 * master to slave) or application request.
 */
typedef enum fsoemaster_resetevent
{
   FSOEMASTER_RESETEVENT_NONE,      /**< No reset initiated. */
   FSOEMASTER_RESETEVENT_BY_MASTER, /**< Reset was initiated by master
                                     * application or state machine.
                                     * A Reset frame was sent to slave
                                     * containing the reset code.
                                     */
   FSOEMASTER_RESETEVENT_BY_SLAVE,  /**< Reset was initiated by slave
                                     * application or state machine.
                                     * A Reset frame was received from slave
                                     * containing the reset code.
                                     */
} fsoemaster_resetevent_t;

/**
 * \brief Status after synchronisation with slave
 *
 * \see fsoemaster_sync_with_slave().
 */
typedef struct fsoemaster_syncstatus
{
   bool is_process_data_received;   /**< Is process data received?
                                     * true:
                                     *   Valid process data was received in
                                     *   last FSoE cycle. The process data is
                                     *   stored in \a inputs buffer.
                                     *   Note that the process data could have
                                     *   been received in a previous call
                                     *   to fsoemaster_sync_with_slave(). It is
                                     *   still considered valid though as no
                                     *   communication error has occurred, such
                                     *   as timeouts or CRC errors.
                                     * false:
                                     *   No valid process data was received in
                                     *   last FSoE cycle. The \a inputs buffer
                                     *   contains only zeros.
                                     *   This will be returned if an error has
                                     *   been detected, if connection with
                                     *   slave is not established or if
                                     *   fail-safe data was received.
                                     */
   fsoemaster_resetevent_t reset_event; /**< Connection reset event.
                                     * If a reset event occurred during this call
                                     * to fsoemaster_sync_with_slave(), this will
                                     * indicate if it was initiated by master or
                                     * slave. Otherwise it is set to
                                     * FSOEMASTER_RESETEVENT_NONE.
                                     * Note that the master state machine
                                     * will reset the connection at startup.
                                     */
   uint8_t reset_reason;            /**< Reason for connection reset.
                                     * In case a reset event occurred, this
                                     * is the code sent/received in the
                                     * Reset frame. All codes except for
                                     * FSOEMASTER_RESETREASON_LOCAL_RESET
                                     * indicates that an error was detected.
                                     * See codes defined further up. Also see
                                     * fsoemaster_reset_reason_description().
                                     */
   fsoemaster_state_t current_state; /**< Current state of the state machine */
} fsoemaster_syncstatus_t;

/**
 * \brief Configuration of FSoE master state machine
 *
 * \see fsoemaster_init().
 */
typedef struct fsoemaster_cfg
{
   /**
    * \brief Slave Address
    *
    * An address uniquely identifying the slave;
    * No other slave within the communication system may have the same
    * Slave Address. Valid values are 0 - 65535.
    *
    * This value will be sent to slave when connection is established, which
    * will verify that the value matches its own Slave Address.
    * Slave will refuse the connection if wrong Slave Address is sent to it.
    *
    * See ETG.5100 ch. 8.2.2.4 "Connection state".
    */
   uint16_t slave_address;

   /**
    * \brief Connection ID
    *
    * A non-zero address uniquely identifying the master;
    * No other master within the communication system may have the same
    * Connection ID.
    *
    * This value will be sent to slave when connection is established.
    *
    * See ETG.5100 ch. 8.2.2.4 "Connection state".
    */
   uint16_t connection_id;

   /**
    * \brief Timeout value in milliseconds for the watchdog timer
    *
    * This value will be sent to slave when connection is established.
    * Valid values are 1 - 65535.
    * Slave will refuse the connection if value is outside the slave's
    * supported range.
    *
    * See ETG.5100 ch.8.2.2.5 "Parameter state".
    */
   uint16_t watchdog_timeout_ms;

   /**
    * \brief Application parameters (optional)
    *
    * The application parameters are device-specific and will be sent to
    * slave when connection is established.
    * May be set to NULL if no application parameters are needed.
    * Slave will refuse the connection if it determines that a parameter
    * has the wrong value.
    *
    * See ETG.5100 ch. 8.2.2.5 "Parameter state".
    */
   const void * application_parameters;

   /**
    * \brief Size in bytes of the application parameters
    *
    * Valid values are 0 - FSOE_APPLICATION_PARAMETERS_MAX_SIZE.
    *
    * This value will be sent to slave when connection is established.
    * Slave will refuse the connection if it expected a different size.
    *
    * See ETG.5100 ch. 8.2.2.5 "Parameter state".
    */
   size_t application_parameters_size;

   /**
    * \brief Size in bytes of the outputs to be sent to slave
    *
    * Only even values are allowed, except for 1, which is also allowed.
    * Maximum value is FSOE_PROCESS_DATA_MAX_SIZE.
    *
    * Master and slave need to agree on the size of the outputs.
    * Communication between master and slave will otherwise not be possible.
    * The size of PDU frames received from slave will be
    * MAX (3 + 2 * outputs_size, 6).
    *
    * See ETG.5100 ch. 4.1.2 (called "SafeOutputs").
    */
   size_t outputs_size;

   /**
    * \brief Size in bytes of the inputs to be received from slave
    *
    * Only even values are allowed, except for 1, which is also allowed.
    * Maximum value is FSOE_PROCESS_DATA_MAX_SIZE.
    *
    * Master and slave need to agree on the size of the inputs.
    * Communication between master and slave will otherwise not be possible.
    * The size of PDU frames received from slave will be
    * MAX (3 + 2 * inputs_size, 6).
    *
    * See ETG.5100 ch. 4.1.2 (called "SafeInputs").
    */
   size_t inputs_size;
} fsoemaster_cfg_t;

/**
 * \brief FSoE master state machine
 *
 * An FSoE master state machine handles the connection with a single slave.
 * Multiple master state machines are supported, where each instance have their
 * own Connection ID and associated slave.
 *
 * User may allocate the instance statically or dynamically using malloc() or
 * on the stack.
 * To use an allocated instance, pass a pointer to it as the first argument to
 * any API function.
 *
 * This struct is made public as to allow for static allocation.
 * User of the API is prohibited from accessing any of the fields as the
 * layout of the structure is to be considered an implementation detail.
 * Only the stack-internal file "fsoemaster.c" may access any field directly.
 */
typedef struct fsoemaster
{
   /* Constants set when instance is initialised and then never modified */
   uint32_t magic;                  /**< Magic value checked by all API
                                     * functions to ensure that this is an
                                     * initialised master state machine.
                                     */
   uint16_t connection_id;          /**< Connection ID */
   uint16_t slave_address;          /**< Slave address */
   size_t outputs_size;             /**< Size in bytes of outputs to slave */
   size_t inputs_size;              /**< Size in bytes of inputs from slave */
   void * app_ref;                  /**< Application reference. This pointer
                                     * will be passed to application callback
                                     * functions. Note that while the pointer
                                     * is never modified, application may
                                     * choose to modify the memory pointed to.
                                     */

   /* Variables defined in standard. See ETG.5100 table 32. */
   uint16_t LastCrc;                /**< CRC_0 of last sent or received frame */
   uint16_t OldMasterCrc;           /**< CRC_0 of last sent frame */
   uint16_t OldSlaveCrc;            /**< CRC_0 of last received frame */
   uint16_t MasterSeqNo;            /**< Sequence number for next sent frame */
   uint16_t SlaveSeqno;             /**< Sequence number for next received frame */
   fsoeframe_uint16_t SessionId;    /**< Master Session ID. A random number
                                     * encoded in little endian format.
                                     * Sent to slave in Session state.
                                     * Note that all subsequent frames will
                                     * "inherit" from this random number due
                                     * the inclusion of received CRC_0 in
                                     * sent frames. See ETG.5100 ch. 8.1.3.7.
                                     */
   uint8_t DataCommand;              /**< Command sent in Data state (FailSafeData
                                     * or ProcessData)
                                     */
   size_t BytesToBeSent;            /**< Number of bytes yet to be sent before
                                     * current state is complete. Not used in
                                     * Data state.
                                     */
   fsoeframe_conndata_t ConnData;   /**< Connection data: The Connection ID (i.e.
                                     * master address) and the slave address.
                                     * Initialised (encoded in little endian
                                     * format) when instance is created.
                                     * Sent to slave in Connection state.
                                     */
   fsoeframe_safepara_t SafePara;   /**< Parameters data: The watchdog timeout
                                     * and (optional) application-specific
                                     * parameters.
                                     * Initialised (encoded in little endian
                                     * format) when instance is created.
                                     * Sent to slave in Parameter state.
                                     */
   size_t SafeParaSize;             /**< Size in bytes of the parameter data */
   uint8_t SafeInputs[FSOE_PROCESS_DATA_MAX_SIZE]; /**< Inputs received in Data state.
                                     * All zeros by default (fail-safe state),
                                     * unless we are in Data state and valid
                                     * ProcessData is received from slave.
                                     */
   uint8_t CommFaultReason;         /**< Error code in case of communication error */
   bool SecondSessionFrameSent;     /**< True if second Session frame has been
                                     * sent in Session state. Only used if
                                     * size of inputs or outputs is 1.
                                     */

   /* Other variables */
   bool is_reset_requested;         /**< Set by application */
   fsoeframe_uint16_t slave_session_id;  /**< Slave Session ID.
                                     * Received from slave in Session state.
                                     * Encoded in little endian format.
                                     */
   fsoemaster_syncstatus_t sync_status; /**< Status from fsoemaster_sync_with_slave() */
   fsoewatchdog_t watchdog;         /**< Watchdog timer */
   fsoechannel_t channel;           /**< Black channel for frame transfer */
} fsoemaster_t;

/**
 * \brief Return description of reset reason as a string literal
 *
 * Example:
 * \code
 * #include <fsoemaster.h>
 * void handle_connection_reset_by_slave (uint8_t reset_reason)
 * {
 *    printf ("Slave initiated connection reset due to %s (%u)\n",
 *          fsoemaster_reset_reason_description (reset_reason),
 *          reset_reason);
 * }
 * \endcode
 *
 * \param[in]     reset_reason Reset reason
 * \return                    String describing the reset reason, e.g.
 *                            "local reset" or "INVALID_CRC", unless
 *                            \a reset_reason is not a valid reset reason,
 *                            in which case "invalid error code" is returned.
 *                            In either case, the returned string is null-
 *                            terminated and statically allocated as a string
 *                            literal.
 *
 * \see fsoemaster_status_t.
 */
FSOE_EXPORT const char * fsoemaster_reset_reason_description (
   uint8_t reset_reason);

/**
 * \brief Return description of state machine state as a string literal
 *
 * Example:
 * \code
 * #include <fsoemaster.h>
 * fsoemaster_state_t state;
 *
 * status = fsoemaster_get_state (master, &state);
 * if (status == FSOEMASTER_STATUS_OK)
 * {
 *    printf ("Current state is %s\n",
 *       fsoemaster_state_description (state));
 * }
 * \endcode
 *
 * \param[in]     state       State
 * \return                    String describing the state, unless
 *                            \a state is not a valid state, in which
 *                            case "invalid" is returned.
 *                            In either case, the returned string is null-
 *                            terminated and statically allocated as a string
 *                            literal.
 */
FSOE_EXPORT const char * fsoemaster_state_description (
   fsoemaster_state_t state);

/**
 * \brief Update SRA CRC value
 *
 * This function will calculate the SRA CRC for data in \a data.
 * If this is the first time the function is called, then user should first
 * set \a crc to zero before calling the function.
 * If this is a subsequent call then the previously calculated CRC value
 * will be is used as input to the CRC calculation.
 * The CRC \a crc will be updated in-place.
 *
 * SRA CRC is an optional feature whose use is not mandated nor specified
 * by the FSoE ETG.5100 specification.
 * If used, the SRA CRC should be sent to slave as Application parameter,
 * where it should be placed first (encoded in little endian byte order).
 * See ETG.5120 "Safety over EtherCAT Protocol Enhancements",
 * ch. 6.3 "SRA CRC Calculation".
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOEMASTER_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoemaster.h>
 * fsoemaster_status_t status1;
 * fsoemaster_status_t status2;
 * uint32_t crc;
 *
 * crc = 0;
 * status1 = fsoemaster_update_sra_crc (&crc, data1, sizeof (data1));
 * status2 = fsoemaster_update_sra_crc (&crc, data2, sizeof (data2));
 * if (status1 == FSOEMASTER_STATUS_OK &&
 *     status2 == FSOEMASTER_STATUS_OK)
 * {
 *    printf ("Calculated SRA CRC: 0x%x\n", crc);
 * }
 * else
 * {
 *    printf ("We called function incorrectly (with null-pointers)\n");
 * }
 * \endcode
 *
 * \pre The pointers \a crc and \a data are non-null.
 *
 * \param[in,out] crc         SRA CRC value to update in-place. If this is the
 *                            first call to fsoemaster_update_sra_crc() then its
 *                            value should first be set to zero.
 * \param[in]     data        Buffer with data.
 * \param[in]     size        Size of buffer in bytes. If zero, \a crc
 *                            will be left unmodified.
 * \return                    Status:
 *                            - FSOEMASTER_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOEMASTER_STATUS_ERROR if user
 *                              violated a precondition.
 */
FSOE_EXPORT fsoemaster_status_t fsoemaster_update_sra_crc (
   uint32_t * crc,
   const void * data,
   size_t size);

/**
 * \brief Get current state of the FSoE master state machine
 *
 * See ETG.5100 ch. 8.4.1.1 table 29: "States of the FSoE Master".
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOEMASTER_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoemaster.h>
 * fsoemaster_status_t status;
 * fsoemaster_state_t state;
 *
 * status = fsoemaster_get_state (master, &state);
 * if (status == FSOEMASTER_STATUS_OK)
 * {
 *    printf ("Current state is %s\n",
 *       fsoemaster_state_description (state));
 * }
 * else
 * {
 *    printf ("We called function incorrectly\n");
 * }
 * \endcode
 *
 * \pre The pointers \a master and \a state are non-null.
 * \pre fsoemaster_init() has been called for instance \a master.
 *
 * \param[in]     master      FSoE master state machine
 * \param[out]    state       Current state
 * \return                    Status:
 *                            - FSOEMASTER_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOEMASTER_STATUS_ERROR if user
 *                              violated a precondition.
 *
 * \see fsoemaster_state_description().
 */
FSOE_EXPORT fsoemaster_status_t fsoemaster_get_state (
   const fsoemaster_t * master,
   fsoemaster_state_t * state);

/**
 * \brief Get generated Master Session ID
 *
 * The Master Session ID was generated by the master state machine when
 * entering Session state.
 *
 * Calling this function while master state machine is in Reset state
 * is not allowed as no Master Session ID has yet been generated.
 *
 * See ETG.5100 ch. 8.2.2.3: "Session state".
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOEMASTER_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoemaster.h>
 * fsoemaster_status_t status;
 * uint16_t session_id;
 *
 * status = fsoemaster_get_master_session_id (master, &session_id);
 * if (status == FSOEMASTER_STATUS_OK)
 * {
 *    printf ("Generated Master Session ID: %u\n", session_id);
 * }
 * else
 * {
 *    printf ("We called function incorrectly\n");
 * }
 * \endcode
 *
 * \pre The pointers \a master and \a session_id are non-null.
 * \pre fsoemaster_init() has been called for instance \a master.
 * \pre Master state machine is at least in Session state.
 *
 * \param[in]     master      FSoE master state machine
 * \param[out]    session_id  Current Master Session ID
 * \return                    Status:
 *                            - FSOEMASTER_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOEMASTER_STATUS_ERROR if user
 *                              violated a precondition.
 */
FSOE_EXPORT fsoemaster_status_t fsoemaster_get_master_session_id (
   const fsoemaster_t * master,
   uint16_t * session_id);

/**
 * \brief Get received Slave Session ID
 *
 * The Slave Session ID was generated by slave and then received by the master
 * state machine when entering Connection state.
 *
 * Calling this function while master state machine is in Reset or Session
 * state is not allowed as no Slave Session ID has yet been received.
 *
 * See ETG.5100 ch. 8.2.2.3: "Session state".
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOEMASTER_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoemaster.h>
 * fsoemaster_status_t status;
 * uint16_t session_id;
 *
 * status = fsoemaster_get_slave_session_id (master, &session_id);
 * if (status == FSOEMASTER_STATUS_OK)
 * {
 *    printf ("Received Slave Session ID: %u\n", session_id);
 * }
 * else
 * {
 *    printf ("We called function incorrectly\n");
 * }
 * \endcode
 *
 * \pre The pointers \a master and \a session_id are non-null.
 * \pre fsoemaster_init() has been called for instance \a master.
 * \pre Master state machine is at least in Connection state.
 *
 * \param[in]     master      FSoE master state machine
 * \param[out]    session_id  Current Slave Session ID
 * \return                    Status:
 *                            - FSOEMASTER_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOEMASTER_STATUS_ERROR if user
 *                              violated a precondition.
 */
FSOE_EXPORT fsoemaster_status_t fsoemaster_get_slave_session_id (
   const fsoemaster_t * master,
   uint16_t * session_id);

/**
 * \brief Get time remaining until watchdog timer timeouts, in milliseconds
 *
 * This function is mainly used for unit-testing purposes.
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOEMASTER_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoemaster.h>
 * fsoemaster_status_t status;
 * uint32_t time_ms;
 *
 * status = fsoemaster_get_time_until_timeout_ms (master, &time_ms);
 * if (status == FSOEMASTER_STATUS_OK)
 * {
 *    printf ("Time remaining until watchdog timeout: %u ms\n", time_ms);
 * }
 * else
 * {
 *    printf ("We called function incorrectly\n");
 * }
 * \endcode
 *
 * \pre The pointers \a master and \a time_ms are non-null.
 * \pre fsoemaster_init() has been called for instance \a master.
 *
 * \param[in]     master      FSoE master state machine
 * \param[out]    time_ms     Time remaining in milliseconds. If watchdog
 *                            timer is not started, UINT32_MAX is returned.
 * \return                    Status:
 *                            - FSOEMASTER_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOEMASTER_STATUS_ERROR if user
 *                              violated a precondition.
 */
FSOE_EXPORT fsoemaster_status_t fsoemaster_get_time_until_timeout_ms (
   const fsoemaster_t * master,
   uint32_t * time_ms);

/**
 * \brief Get flag indicating if sending process data to slave is enabled
 *
 * This will only check a flag indicating that everything is OK from the
 * perspective of the application. Master state machine will not send normal
 * process data if connection with slave is not fully established (Data state),
 * even if application allows it.
 *
 * See ETG.5100 ch. 8.4.1.2 "Set Data Command event".
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOEMASTER_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoemaster.h>
 * fsoemaster_status_t status;
 * bool is_enabled;
 *
 * status = fsoemaster_get_process_data_sending_enable_flag (master, &is_enabled);
 * if (status == FSOEMASTER_STATUS_OK)
 * {
 *    if (is_enabled)
 *    {
 *       printf ("Master is allowed to send process data to slave\n");
 *    }
 *    else
 *    {
 *       printf ("Master is not allowed to send process data to slave\n");
 *    }
 * }
 * else
 * {
 *    printf ("We called function incorrectly\n");
 * }
 * \endcode
 *
 * \pre The pointers \a master and \a is_enabled are non-null.
 * \pre fsoemaster_init() has been called for instance \a master.
 *
 * \param[in]     master      FSoE master state machine
 * \param[out]    is_enabled  Current process data send status:
 *                            - true if master is allowed to send process data,
 *                            - false if not.
 * \return                    Status:
 *                            - FSOEMASTER_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOEMASTER_STATUS_ERROR if user
 *                              violated a precondition.
 *
 * \see fsoemaster_set_process_data_sending_enable_flag().
 * \see fsoemaster_clear_process_data_sending_enable_flag().
 */
FSOE_EXPORT fsoemaster_status_t fsoemaster_get_process_data_sending_enable_flag (
   const fsoemaster_t * master,
   bool * is_enabled);

/**
 * \brief Clear flag indicating that sending process data to slave is enabled
 *
 * This will clear a flag indicating that everything is OK from the
 * perspective of the application.
 * Master will only send fail-safe data (zeros) to slave.
 * This is the default setting after power-on and after detection
 * of any errors.
 *
 * See ETG.5100 ch. 8.4.1.2 "Set Data Command event".
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOEMASTER_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoemaster.h>
 * fsoemaster_status_t status;
 *
 * status = fsoemaster_clear_process_data_sending_enable_flag (master);
 * if (status == FSOEMASTER_STATUS_OK)
 * {
 *    printf ("Sending process data to slave is no longer allowed\n");
 * }
 * else
 * {
 *    printf ("We called function incorrectly\n");
 * }
 * \endcode
 *
 * \pre The pointer \a master is non-null.
 * \pre fsoemaster_init() has been called for instance \a master.
 *
 * \param[in,out] master      FSoE master state machine
 * \return                    Status:
 *                            - FSOEMASTER_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOEMASTER_STATUS_ERROR if user
 *                              violated a precondition.
 *
 * \see fsoemaster_get_process_data_sending_enable_flag().
 * \see fsoemaster_set_process_data_sending_enable_flag().
 */
FSOE_EXPORT fsoemaster_status_t fsoemaster_clear_process_data_sending_enable_flag (
   fsoemaster_t * master);

/**
 * \brief Set flag indicating that sending process data to slave is enabled
 *
 * This will set a flag indicating that everything is OK from the
 * perspective of the application.
 * Setting the flag will cause master to send outputs containing valid process
 * data once connection is established, assuming no errors are detected.
 * If any errors are detected, this flag will revert to its disabled state
 * and only fail-safe outputs will be sent.
 *
 * See ETG.5100 ch. 8.4.1.2 "Set Data Command event".
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOEMASTER_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoemaster.h>
 * fsoemaster_status_t status;
 *
 * status = fsoemaster_set_process_data_sending_enable_flag (master);
 * if (status == FSOEMASTER_STATUS_OK)
 * {
 *    printf ("Sending process data to slave is now allowed\n");
 * }
 * else
 * {
 *    printf ("We called function incorrectly\n");
 * }
 * \endcode
 *
 * \pre The pointer \a master is non-null.
 * \pre fsoemaster_init() has been called for instance \a master.
 *
 * \param[in,out] master      FSoE master state machine
 * \return                    Status:
 *                            - FSOEMASTER_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOEMASTER_STATUS_ERROR if user
 *                              violated a precondition.
 *
 * \see fsoemaster_get_process_data_sending_enable_flag().
 * \see fsoemaster_clear_process_data_sending_enable_flag().
 */
FSOE_EXPORT fsoemaster_status_t fsoemaster_set_process_data_sending_enable_flag (
   fsoemaster_t * master);

/**
 * \brief Set reset request flag
 *
 * This will set a flag, which in next call to fsoemaster_sync_with_slave()
 * will cause the master state machine to send the Reset frame to slave
 * and then enter the Reset state.
 * Fail-safe mode will then be entered, where normal process data outputs will
 * not be sent even after connection has been re-established.
 * Application needs to explicitly re-enable process data outputs in
 * order to leave fail-safe mode.
 * See fsoemaster_set_process_data_sending_enable_flag().
 *
 * See ETG.5100 ch. 8.4.1.2 "Reset Connection event".
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOEMASTER_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoemaster.h>
 * fsoemaster_status_t status;
 *
 * status = fsoemaster_set_reset_request_flag (master);
 * if (status == FSOEMASTER_STATUS_OK)
 * {
 *    printf ("Master state machine reset was requested\n");
 * }
 * else
 * {
 *    printf ("We called function incorrectly\n");
 * }
 * \endcode
 *
 * \pre The pointer \a master is non-null.
 * \pre fsoemaster_init() has been called for instance \a master.
 *
 * \param[in,out] master      FSoE master state machine
 * \return                    Status:
 *                            - FSOEMASTER_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOEMASTER_STATUS_ERROR if user
 *                              violated a precondition.
 */
FSOE_EXPORT fsoemaster_status_t fsoemaster_set_reset_request_flag (
   fsoemaster_t * master);

/**
 * \brief Synchronise with slave
 *
 * Needs to be called periodically in order to avoid watchdog timeout.
 * It is recommended that delay between calls to the function is no more
 * than half the watchdog timeout.
 *
 * Depending on current state, the master state machine may try to send a
 * single frame or read a single frame by calling fsoeapp_send() and/or
 * fsoeapp_recv(), which are non-blocking functions.
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOEMASTER_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoemaster.h>
 * fsoemaster_syncstatus_t sync_status;
 * fsoemaster_status_t status;
 * uint8_t inputs[2];
 *
 * outputs[0] = 0x12;
 * outputs[1] = 0x34;
 * status = fsoemaster_sync_with_slave (master, outputs, inputs, &sync_status);
 * if (status == FSOEMASTER_STATUS_OK)
 * {
 *    if (sync_status.reset_event != FSOEMASTER_RESETEVENT_NONE)
 *    {
 *       printf ("Connection was reset by %s. Cause: %s\n",
 *          sync_status.reset_event == FSOEMASTER_RESETEVENT_BY_MASTER ?
 *          "master" : "slave",
 *          fsoemaster_reset_reason_description (sync_status.reset_reason));
 *    }
 *    if (sync_status.is_process_data_received)
 *    {
 *       handle_received_data (inputs);
 *    }
 *    else
 *    {
 *       printf ("No valid process data was received\n");
 *    }
 * }
 * else
 * {
 *    printf ("We called function incorrectly\n");
 * }
 * \endcode
 *
 * \pre The pointers \a master, \a outputs, \a inputs and \a sync_status are non-null.
 * \pre fsoemaster_init() has been called for instance \a master.
 *
 * \param[in,out] master      FSoE master state machine
 * \param[in]     outputs     Buffer containing outputs to be sent to slave.
 *                            Its size is given in configuration.
 * \param[out]    inputs      Buffer to store inputs received from slave.
 *                            Its size is given in configuration.
 *                            Whether inputs are valid or not is given
 *                            by \a sync_status.
 * \param[out]    sync_status Status of FSoE connection.
 * \return                    Status:
 *                            - FSOEMASTER_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOEMASTER_STATUS_ERROR if user
 *                              violated a precondition.
 */
FSOE_EXPORT fsoemaster_status_t fsoemaster_sync_with_slave (
      fsoemaster_t * master,
      const void * outputs,
      void * inputs,
      fsoemaster_syncstatus_t * sync_status);

/**
 * \brief Initialise FSoE master state machine
 *
 * This will configure the instance according to supplied configuration.
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOEMASTER_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoemaster.h>
 * const fsoemaster_cfg_t cfg =
 * {
 *    .slave_address               = 0x0304,
 *    .connection_id               = 8,
 *    .watchdog_timeout_ms         = 100,
 *    .application_parameters      = NULL,
 *    .application_parameters_size = 0,
 *    .outputs_size                = 2,
 *    .inputs_size                 = 2,
 * };
 * fsoemaster_t master;
 * fsoemaster_status_t status;
 *
 * status = fsoemaster_init (&master, &cfg, NULL);
 * if (status == FSOEMASTER_STATUS_OK)
 * {
 *    printf ("Master state machine was initialised\n");
 * }
 * else
 * {
 *    printf ("We called function incorrectly\n");
 * }
 * \endcode
 *
 * \pre The pointers \a master and \a cfg are non-null.
 * \pre The fields in \a cfg are valid.
 *
 * \param[out]    master      FSoE master state machine
 * \param[in]     cfg         Configuration
 * \param[in]     app_ref     Application reference. This will be passed
 *                            as first argument to callback functions
 *                            implemented by application. The stack
 *                            does not interpret this value in any way.
 * \return                    Status:
 *                            - FSOEMASTER_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOEMASTER_STATUS_ERROR if user
 *                              violated a precondition.
 */
FSOE_EXPORT fsoemaster_status_t fsoemaster_init (
   fsoemaster_t * master,
   const fsoemaster_cfg_t * cfg,
   void * app_ref);

#ifdef __cplusplus
}
#endif

#endif /* FSOEMASTER_H */
