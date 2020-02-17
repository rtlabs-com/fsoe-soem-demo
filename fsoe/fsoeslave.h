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
 * \brief FSoE slave state machine
 *
 * An FSoE slave state machine handles the connection with a single
 * FSoE master.
 *
 * After power-on, slave will listen for connection requests from a master.
 * Once established, slave will wait for outputs from master. When received,
 * it will respond by sending back its inputs to master.
 *
 * Inputs and outputs may contain valid process data or they
 * may contain fail-safe data (all zeros). By default, they contain
 * fail-safe data. They will only contain valid process data if sender
 * (slave for inputs, master for outputs) determines that everything is OK.
 * The sender may send valid process data while receiving fail-safe
 * data or vice versa.
 * Inputs and outputs have fixed size, but they need not be the same size.
 *
 * A user of the API will have to explicitly enable it in order for
 * valid process data to be sent.
 * Communication errors will cause the connection to be reset.
 * The slave state machine will then disable the process data inputs and
 * start listening for new connection requests from a master. If successful,
 * it restarts sending inputs as fail-safe data.
 * A user of the API may then re-enable process data inputs.
 *
 * \verbatim
 *     ----------            ----------
 *     |        |  inputs    |        |   Arrows in picture
 *     | FSoE   | ---------> | FSoE   |   denote data flow
 *     | slave  |            | master |
 *     |        | <--------- |        |
 *     ----------   outputs  ----------
 * \endverbatim
 *
 * \par Black Channel Communication
 *
 * At a lower level, the slave state machine communicates with the master
 * through a "black channel". The slave state machine does not know how the
 * black channel is implemented, it just knows how to access it - by calling
 * fsoeapp_send() and fsoeapp_recv().
 * The application implementer needs to implement these two functions.
 *
 * The arrows in the picture below denote direct function calls:
 *
 * \verbatim
 *      |  |  |  Public slave API (fsoeslave.h):
 *      |  |  |  - fsoeslave_sync_with_master()
 *      v  v  v  - fsoeslave_get_state() etc.
 *    -----------
 *    |         |
 *    | FSoE    |
 *    | slave   |
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
 * fsoeapp_generate_session_id(), fsoeapp_verify_parameters() and
 * fsoeapp_handle_user_error().
 * See the header file fsoeapp.h for more details.
 */

#ifndef FSOESLAVE_H
#define FSOESLAVE_H

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
 * Local reset (FSOESLAVE_RESETREASON_LOCAL_RESET) may be requested
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
#define FSOESLAVE_RESETREASON_LOCAL_RESET         (0)

/**
 * \brief Invalid command
 *
 * Master or slave state machine requested connection to be reset
 * after receiving a frame whose type was not valid for current state.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
*/
#define FSOESLAVE_RESETREASON_INVALID_CMD         (1)

/**
 * \brief Unknown command
 *
 * Master or slave state machine requested connection to be reset
 * after receiving a frame of unknown type.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOESLAVE_RESETREASON_UNKNOWN_CMD         (2)

/**
 * \brief Invalid Connection ID
 *
 * Master or slave state machine requested connection to be reset
 * after receiving a frame with invalid Connection ID.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOESLAVE_RESETREASON_INVALID_CONNID      (3)

/**
 * \brief Invalid CRC
 *
 * Master or slave state machine requested connection to be reset
 * after receiving a frame with invalid CRCs.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOESLAVE_RESETREASON_INVALID_CRC         (4)

/**
 * \brief Watchdog timer expired
 *
 * Master or slave state machine requested connection to be reset
 * after watchdog timer expired while waiting for a frame to be
 * received.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOESLAVE_RESETREASON_WD_EXPIRED          (5)

/**
 * \brief Invalid slave address
 *
 * Slave state machine requested connection to be reset
 * after receiving Connection frame with incorrect slave address from master.
 * Never requested by master state machine.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOESLAVE_RESETREASON_INVALID_ADDRESS     (6)

/**
 * \brief Invalid configuration data
 *
 * Master state machine requested connection to be reset
 * after receiving Connection or Parameter frame from slave containing
 * different data than what was sent to it.
 * Never requested by slave state machine.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOESLAVE_RESETREASON_INVALID_DATA        (7)

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
#define FSOESLAVE_RESETREASON_INVALID_COMPARALEN  (8)

/**
 * \brief Invalid Communication parameter data
 *
 * Slave state machine requested connection to be reset
 * after receiving Parameter frame with incompatible
 * watchdog timeout from master.
 * Never requested by master state machine.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOESLAVE_RESETREASON_INVALID_COMPARA     (9)

/**
 * \brief Invalid size of Application parameters
 *
 * Slave state machine requested connection to be reset
 * after receiving Parameter frame with incompatible
 * size for Application Parameters.
 * Never requested by master state machine.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOESLAVE_RESETREASON_INVALID_USERPARALEN (10)

/**
 * \brief Invalid Application parameter data (generic error code)
 *
 * Slave state machine requested connection to be reset
 * after receiving Parameter frame with incompatible Application Parameters.
 * Never requested by master state machine.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOESLAVE_RESETREASON_INVALID_USERPARA    (11)

/**
 * \brief Invalid Application parameter data (first device-specific error code)
 *
 * Slave state machine requested connection to be reset
 * after receiving Parameter frame with incompatible Application Parameters.
 * Never requested by master state machine.
 * The device-specific error codes are in the range 0x80 ... 0xFF.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOESLAVE_RESETREASON_INVALID_USERPARA_MIN (0x80)

/**
 * \brief Invalid Application parameter data (last device-specific error code)
 *
 * Slave state machine requested connection to be reset
 * after receiving Parameter frame with incompatible Application Parameters.
 * Never requested by master state machine.
 * The device-specific error codes are in the range 0x80 ... 0xFF.
 * See ETG.5100 ch. 8.3. table 28: "FSoE communication error codes".
 */
#define FSOESLAVE_RESETREASON_INVALID_USERPARA_MAX (0xFF)

/**
 * \brief Number of bytes in FSoE frame containing \a data_size data bytes
 *
 * \param[in]     data_size      Number of data bytes in frame.
 *                               Needs to be even or 1.
 * \return                       Size of frame in bytes.
 */
#define FSOESLAVE_FRAME_SIZE(data_size) (\
   ((data_size) == 1) ? 6 : (2 * (data_size) + 3) )

/**
 * \brief User API functions return codes
 *
 * Returned from each API function to indicate if user called the function
 * correctly as described in the function's documentation.
 *
 * \see fsoeslave_status_t
 */

/**
 * \brief User called the API correctly
 *
 */
#define FSOESLAVE_STATUS_OK         (0)

/**
 * \brief User violated the API
 *
 * User violated the function's preconditions. fsoeapp_handle_user_error()
 * callback will give detailed information about what caused the function to
 * return ERROR.
 */
#define FSOESLAVE_STATUS_ERROR      (-1)

/**
 * \brief Status returned from API function
 *
 * Returned from each API function to indicate if user called the function
 * correctly as described in the function's documentation.
 *
 * \see FSOESLAVE_STATUS_OK
 * \see FSOESLAVE_STATUS_ERROR
 */
typedef int32_t fsoeslave_status_t;

/**
 * \brief Connection state
 *
 * After power-on, the slave state machine is in Reset state. In Reset state,
 * slave is not associated with any master. Interchange of process data with a
 * master only takes place when slave is in Data state. Before Data state is
 * entered, a master first has to configure the slave by sending it configuration
 * data. This takes place in the intermediate states Session, Connection
 * and Parameter.
 *
 * With the exception of transitions to the Reset state, the slave state
 * machine does not change state on its own. Instead, it is the master which
 * orders the slave to enter a new state by means of sending a corresponding
 * frame. For example, master sending a Connection frame will cause slave to
 * enter Connection state, assuming that state transition is allowed. The slave
 * state machine will enter Reset state on its own if it detects an error.
 *
 * \verbatim
 *                     --------------
 *                     |   Reset    |<---\
 *                     --------------    | Slave detected communication
 *                           |           | error OR application requested
 *                           v           | connection to be reset.
 *                     --------------    |
 *                /--->|  Session   |--->|
 * Master         |    --------------    |
 * reported       |          |           |
 * communication  |          v           |
 * error          |    --------------    |
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
 * See ETG.5100 ch. 8.5.1.1 table 34 "States of the FSoE Slave".
 */
typedef enum fsoeslave_state
{
   FSOESLAVE_STATE_RESET,           /**< Connection is reset */
   FSOESLAVE_STATE_SESSION,         /**< The session IDs are being transferred */
   FSOESLAVE_STATE_CONNECTION,      /**< The connection ID is being transferred */
   FSOESLAVE_STATE_PARAMETER,       /**< The parameters are being transferred */
   FSOESLAVE_STATE_DATA,            /**< Process or fail-safe data is being transferred */
} fsoeslave_state_t;

/**
 * \brief Connection reset event
 *
 * A reset of connection between master and slave may be initiated by either
 * side sending a Reset frame containing a code describing why the reset was
 * initiated, such as an error detected by FSoE stack, system startup (only
 * master to slave) or application request.
 */
typedef enum fsoeslave_resetevent
{
   FSOESLAVE_RESETEVENT_NONE,       /**< No reset initiated. */
   FSOESLAVE_RESETEVENT_BY_MASTER,  /**< Reset was initiated by master
                                     * application or state machine.
                                     * A Reset frame was received from master
                                     * containing the reset code.
                                     */
   FSOESLAVE_RESETEVENT_BY_SLAVE,   /**< Reset was initiated by slave
                                     * application or state machine.
                                     * A Reset frame was sent to master
                                     * containing the reset code.
                                     */
} fsoeslave_resetevent_t;

/**
 * \brief Status after synchronisation with master
 *
 * \see fsoeslave_sync_with_master().
 */
typedef struct fsoeslave_syncstatus
{
   bool is_process_data_received;   /**< Is process data received?
                                     * true:
                                     *   Valid process data was received in
                                     *   last FSoE cycle. The process data is
                                     *   stored in \a outputs buffer.
                                     *   Note that the process data could have
                                     *   been received in a previous call
                                     *   to fsoeslave_sync_with_master(). It is
                                     *   still considered valid though as no
                                     *   communication error has occurred, such
                                     *   as timeouts or CRC errors.
                                     * false:
                                     *   No valid process data was received in
                                     *   last FSoE cycle. The \a outputs buffer
                                     *   contains only zeros.
                                     *   This will be returned if an error has
                                     *   been detected, if connection with
                                     *   master is not established or if
                                     *   fail-safe data was received.
                                     */
   fsoeslave_resetevent_t reset_event; /**< Connection reset event.
                                     * If a reset event occurred during this call
                                     * to fsoeslave_sync_with_master(), this will
                                     * indicate if it was initiated by slave or
                                     * master. Otherwise it is set to
                                     * FSOESLAVE_RESETEVENT_NONE.
                                     * Note that the slave state machine will wait
                                     * for master to reset the connection after
                                     * startup.
                                     */
   uint8_t reset_reason;            /**< Reason for connection reset.
                                     * In case a reset event occurred, this
                                     * is the code sent/received in the
                                     * Reset frame. All codes except for
                                     * FSOESLAVE_RESETREASON_LOCAL_RESET
                                     * indicates that an error was detected.
                                     * See codes defined further up. Also see
                                     * fsoeslave_reset_reason_description().
                                     */
   fsoeslave_state_t current_state; /**< Current state of the state machine */
} fsoeslave_syncstatus_t;

/**
 * \brief Configuration of FSoE slave state machine
 *
 * \see fsoeslave_init().
 */
typedef struct fsoeslave_cfg
{
   /**
    * \brief Slave Address
    *
    * An address uniquely identifying the slave;
    * No other slave within the communication system may have the same
    * Slave Address. Valid values are 0 - 65535.
    *
    * This value will be received from master when connection is established,
    * and slave will verify that the value matches this value.
    * Slave will refuse the connection if wrong Slave Address is received.
    *
    * See ETG.5100 ch. 8.2.2.4 "Connection state".
    */
   uint16_t slave_address;

   /**
    * \brief Expected size in bytes of the application parameters
    *
    * Valid values are 0 - FSOE_APPLICATION_PARAMETERS_MAX_SIZE.
    *
    * Slave will check that the size of application parameters received
    * from master match this value. If it does not match, connection
    * will be rejected.
    *
    * See ETG.5100 ch. 8.2.2.5 "Parameter state".
    */
   size_t application_parameters_size;

   /**
    * \brief Size in bytes of the inputs to be sent to master
    *
    * Only even values are allowed, except for 1, which is also allowed.
    * Maximum value is FSOE_PROCESS_DATA_MAX_SIZE.
    *
    * Slave and master need to agree on the size of the inputs.
    * Communication between slave and master will otherwise not be possible.
    * The size of PDU frames sent to master will be
    * MAX (3 + 2 * inputs_size, 6).
    *
    * See ETG.5100 ch. 4.1.2 (called "SafeOutputs").
    */
   size_t inputs_size;

   /**
    * \brief Size in bytes of the outputs to be received from master
    *
    * Only even values are allowed, except for 1, which is also allowed.
    * Maximum value is FSOE_PROCESS_DATA_MAX_SIZE.
    *
    * Slave and master need to agree on the size of the outputs.
    * Communication between slave and master will otherwise not be possible.
    * The size of PDU frames received from master will be
    * MAX (3 + 2 * outputs_size, 6).
    *
    * See ETG.5100 ch. 4.1.2 (called "SafeInputs").
    */
   size_t outputs_size;
} fsoeslave_cfg_t;

/**
 * \brief FSoE slave state machine
 *
 * An FSoE slave state machine handles the connection with a single master.
 * Multiple slave state machines are supported, where each instance have their
 * own Slave Address.
 *
 * User may allocate the instance statically or dynamically using malloc() or
 * on the stack.
 * To use an allocated instance, pass a pointer to it as the first argument to
 * any API function.
 *
 * This struct is made public as to allow for static allocation.
 * User of the API is prohibited from accessing any of the fields as the
 * layout of the structure is to be considered an implementation detail.
 * Only the stack-internal file "fsoeslave.c" may access any field directly.
 */
typedef struct fsoeslave
{
   /* Constants set when instance is initialised and then never modified */
   uint32_t magic;                  /**< Magic value checked by all API
                                     * functions to ensure that this is an
                                     * initialised slave state machine.
                                     */
   size_t inputs_size;              /**< Size in bytes of inputs to master */
   size_t outputs_size;             /**< Size in bytes of outputs from master */
   void * app_ref;                  /**< Application reference. This pointer
                                     * will be passed to application callback
                                     * functions. Note that while the pointer
                                     * is never modified, application may
                                     * choose to modify the memory pointed to.
                                     */

   /* Variables defined in standard. See ETG.5100 table 32. */
   uint16_t LastCrc;                /**< CRC_0 of last sent or received frame */
   uint16_t OldMasterCrc;           /**< CRC_0 of last received frame */
   uint16_t OldSlaveCrc;            /**< CRC_0 of last sent frame */
   uint16_t MasterSeqno;            /**< Sequence number for next received frame */
   uint16_t SlaveSeqNo;             /**< Sequence number for next sent frame */
   uint16_t InitSeqNo;              /**< Initialisation sequence number 1 */
   uint8_t DataCommand;             /**< Command sent in Data state (FailSafeData
                                     * or ProcessData)
                                     */
   size_t BytesToBeSent;            /**< Number of bytes yet to be sent before
                                     * current state is complete. Not used in
                                     * Data state.
                                     */
   uint16_t ConnectionId;           /**< Connection ID. Received from master
                                     * in Connection state.
                                     */
   fsoeframe_conndata_t ConnectionData; /**< Addressing information received from
                                     * master in Connection state.
                                     * Little-endian encoded.
                                     */
   uint16_t SlaveAddress;           /**< Slave address. Configured when slave is
                                     * instantiated and then never changed.
                                     */
   fsoeframe_safepara_t SafePara;   /**< Parameters data: The watchdog timeout
                                     * and (optional) application-specific
                                     * parameters.
                                     * Received from master in Parameter state.
                                     * Little-endian encoded.
                                     */
   size_t ExpectedSafeParaSize;     /**< Expected size in bytes of the parameter
                                     * data to be received from master
                                     */
   uint8_t SafeOutputs[FSOE_PROCESS_DATA_MAX_SIZE]; /**< Outputs received in Data state.
                                     * All zeros by default (fail-safe state),
                                     * unless we are in Data state and valid
                                     * ProcessData is received from master.
                                     */
   uint8_t CommFaultReason;         /**< Error code in case of communication error */
   fsoeframe_uint16_t SessionId;    /**< Slave Session ID. A random number
                                     * encoded in little endian format.
                                     * Sent to master in Session state.
                                     * Note that all subsequent frames will
                                     * "inherit" from this random number due
                                     * the inclusion of received CRC_0 in
                                     * sent frames. See ETG.5100 ch. 8.1.3.7.
                                     * This variable is not listed in table 32.
                                     */

   /* Other variables */
   bool is_reset_requested;         /**< Set by application */
   fsoeframe_uint16_t master_session_id; /**< Master Session ID.
                                     * Received from master in Session state.
                                     * Encoded in little endian format.
                                     */
   fsoeslave_syncstatus_t sync_status; /**< Status from fsoeslave_sync_with_master() */
   fsoewatchdog_t watchdog;         /**< Watchdog timer */
   fsoechannel_t channel;           /**< Black channel for frame transfer */
} fsoeslave_t;

/**
 * \brief Return description of reset reason as a string literal
 *
 * Example:
 * \code
 * #include <fsoeslave.h>
 * void handle_connection_reset_by_master (uint8_t reset_reason)
 * {
 *    printf ("Master initiated connection reset due to %s (%u)\n",
 *          fsoeslave_reset_reason_description (reset_reason),
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
 * \see fsoeslave_syncstatus_t.
 */
FSOE_EXPORT const char * fsoeslave_reset_reason_description (
   uint8_t reset_reason);

/**
 * \brief Return description of state machine state as a string literal
 *
 * Example:
 * \code
 * #include <fsoeslave.h>
 * fsoeslave_state_t state;
 *
 * status = fsoeslave_get_state (slave, &state);
 * if (status == FSOESLAVE_STATUS_OK)
 * {
 *    printf ("Current state is %s\n",
 *       fsoeslave_state_description (state));
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
FSOE_EXPORT const char * fsoeslave_state_description (
   fsoeslave_state_t state);

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
 * If used, the SRA CRC will be received from master as Application parameter,
 * where it is placed first (encoded in little endian byte order).
 * See ETG.5120 "Safety over EtherCAT Protocol Enhancements",
 * ch. 6.3 "SRA CRC Calculation".
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOESLAVE_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoeslave.h>
 * fsoeslave_status_t status1;
 * fsoeslave_status_t status2;
 * uint32_t crc;
 *
 * crc = 0;
 * status1 = fsoeslave_update_sra_crc (&crc, data1, sizeof (data1));
 * status2 = fsoeslave_update_sra_crc (&crc, data2, sizeof (data2));
 * if (status1 == FSOESLAVE_STATUS_OK &&
 *     status2 == FSOESLAVE_STATUS_OK)
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
 *                            first call to fsoeslave_update_sra_crc() then its
 *                            value should first be set to zero.
 * \param[in]     data        Buffer with data.
 * \param[in]     size        Size of buffer in bytes. If zero, \a crc
 *                            will be left unmodified.
 * \return                    Status:
 *                            - FSOESLAVE_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOESLAVE_STATUS_ERROR if user
 *                              violated a precondition.
 */
FSOE_EXPORT fsoeslave_status_t fsoeslave_update_sra_crc (
   uint32_t * crc,
   const void * data,
   size_t size);

/**
 * \brief Get current state of the FSoE slave state machine
 *
 * See ETG.5100 ch. 8.5.1.1 table 34: "States of the FSoE Slave".
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOESLAVE_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoeslave.h>
 * fsoeslave_status_t status;
 * fsoeslave_state_t state;
 *
 * status = fsoeslave_get_state (slave, &state);
 * if (status == FSOESLAVE_STATUS_OK)
 * {
 *    printf ("Current state is %s\n",
 *       fsoeslave_state_description (state));
 * }
 * else
 * {
 *    printf ("We called function incorrectly\n");
 * }
 * \endcode
 *
 * \pre The pointers \a slave and \a state are non-null.
 * \pre fsoeslave_init() has been called for instance \a slave.
 *
 * \param[in]     slave       FSoE slave state machine
 * \param[out]    state       Current state
 * \return                    Status:
 *                            - FSOESLAVE_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOESLAVE_STATUS_ERROR if user
 *                              violated a precondition.
 *
 * \see fsoeslave_state_description().
 */
FSOE_EXPORT fsoeslave_status_t fsoeslave_get_state (
   const fsoeslave_t * slave,
   fsoeslave_state_t * state);

/**
 * \brief Get generated Slave Session ID
 *
 * The Slave Session ID was generated by the slave state machine when
 * entering Session state.
 *
 * Calling this function while slave state machine is in Reset state
 * is not allowed as no Slave Session ID has yet been generated.
 *
 * See ETG.5100 ch. 8.2.2.3: "Session state".
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOESLAVE_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoeslave.h>
 * fsoeslave_status_t status;
 * uint16_t session_id;
 *
 * status = fsoeslave_get_slave_session_id (slave, &session_id);
 * if (status == FSOESLAVE_STATUS_OK)
 * {
 *    printf ("Generated Slave Session ID: %u\n", session_id);
 * }
 * else
 * {
 *    printf ("We called function incorrectly\n");
 * }
 * \endcode
 *
 * \pre The pointers \a slave and \a session_id are non-null.
 * \pre fsoeslave_init() has been called for instance \a slave.
 * \pre Slave state machine is at least in Session state.
 *
 * \param[in]     slave       FSoE slave state machine
 * \param[out]    session_id  Current Slave Session ID
 * \return                    Status:
 *                            - FSOESLAVE_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOESLAVE_STATUS_ERROR if user
 *                              violated a precondition.
 */
FSOE_EXPORT fsoeslave_status_t fsoeslave_get_slave_session_id (
   const fsoeslave_t * slave,
   uint16_t * session_id);

/**
 * \brief Get received Master Session ID
 *
 * The Master Session ID was generated by master and received by the slave
 * state machine while in Session state.
 *
 * Calling this function while slave state machine is in Reset or Session
 * state is not allowed as no Slave Session ID has yet been received.
 *
 * See ETG.5100 ch. 8.2.2.3: "Session state".
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOESLAVE_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoeslave.h>
 * fsoeslave_status_t status;
 * uint16_t session_id;
 *
 * status = fsoeslave_get_master_session_id (slave, &session_id);
 * if (status == FSOESLAVE_STATUS_OK)
 * {
 *    printf ("Received Master Session ID: %u\n", session_id);
 * }
 * else
 * {
 *    printf ("We called function incorrectly\n");
 * }
 * \endcode
 *
 * \pre The pointers \a slave and \a session_id are non-null.
 * \pre fsoeslave_init() has been called for instance \a slave.
 * \pre Slave state machine is at least in Connection state.
 *
 * \param[in]     slave       FSoE slave state machine
 * \param[out]    session_id  Current Master Session ID
 * \return                    Status:
 *                            - FSOESLAVE_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOESLAVE_STATUS_ERROR if user
 *                              violated a precondition.
 */
FSOE_EXPORT fsoeslave_status_t fsoeslave_get_master_session_id (
   const fsoeslave_t * slave,
   uint16_t * session_id);

/**
 * \brief Get flag indicating if sending process data to master is enabled
 *
 * This will only check a flag indicating that everything is OK from the
 * perspective of the application. Slave state machine will not send normal
 * process data if connection with master is not fully established
 * (Data state), even if application allows it.
 *
 * See ETG.5100 ch. 8.5.1.2 "Set Data Command event".
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOESLAVE_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoeslave.h>
 * fsoeslave_status_t status;
 * bool is_enabled;
 *
 * status = fsoeslave_get_process_data_sending_enable_flag (slave, &is_enabled);
 * if (status == FSOESLAVE_STATUS_OK)
 * {
 *    if (is_enabled)
 *    {
 *       printf ("Slave is allowed to send process data to master\n");
 *    }
 *    else
 *    {
 *       printf ("Slave is not allowed to send process data to master\n");
 *    }
 * }
 * else
 * {
 *    printf ("We called function incorrectly\n");
 * }
 * \endcode
 *
 * \pre The pointers \a slave and \a is_enabled are non-null.
 * \pre fsoeslave_init() has been called for instance \a slave.
 *
 * \param[in]     slave       FSoE slave state machine
 * \param[out]    is_enabled  Current process data send status:
 *                            - true if slave is allowed to send process data,
 *                            - false if not.
 * \return                    Status:
 *                            - FSOESLAVE_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOESLAVE_STATUS_ERROR if user
 *                              violated a precondition.
 *
 * \see fsoeslave_set_process_data_sending_enable_flag().
 * \see fsoeslave_clear_process_data_sending_enable_flag().
 */
FSOE_EXPORT fsoeslave_status_t fsoeslave_get_process_data_sending_enable_flag (
   const fsoeslave_t * slave,
   bool * is_enabled);

/**
 * \brief Clear flag indicating that sending process data to master is enabled
 *
 * This will clear a flag indicating that everything is OK from the
 * perspective of the application.
 * Slave will only send fail-safe data (zeros) to master.
 * This is the default setting after power-on and after detection
 * of any errors.
 *
 * See ETG.5100 ch. 8.5.1.2 "Set Data Command event".
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOESLAVE_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoeslave.h>
 * fsoeslave_status_t status;
 *
 * status = fsoeslave_clear_process_data_sending_enable_flag (slave);
 * if (status == FSOESLAVE_STATUS_OK)
 * {
 *    printf ("Sending process data to master is no longer allowed\n");
 * }
 * else
 * {
 *    printf ("We called function incorrectly\n";
 * }
 * \endcode
 *
 * \pre The pointer \a slave is non-null.
 * \pre fsoeslave_init() has been called for instance \a slave.
 *
 * \param[in,out] slave       FSoE slave state machine
 * \return                    Status:
 *                            - FSOESLAVE_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOESLAVE_STATUS_ERROR if user
 *                              violated a precondition.
 *
 * \see fsoeslave_get_process_data_sending_enable_flag().
 * \see fsoeslave_set_process_data_sending_enable_flag().
 */
FSOE_EXPORT fsoeslave_status_t fsoeslave_clear_process_data_sending_enable_flag (
   fsoeslave_t * slave);

/**
 * \brief Set flag indicating that sending process data to master is enabled
 *
 * This will set a flag indicating that everything is OK from the
 * perspective of the application.
 * Setting the flag will cause slave to send inputs containing valid process
 * data once connection is established, assuming no errors are detected.
 * If any errors are detected, this flag will revert to its disabled state
 * and only fail-safe inputs will be sent.
 *
 * See ETG.5100 ch. 8.5.1.2 "Set Data Command event".
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOESLAVE_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoeslave.h>
 * fsoeslave_status_t status;
 *
 * status = fsoeslave_set_process_data_sending_enable_flag (slave);
 * if (status == FSOESLAVE_STATUS_OK)
 * {
 *    printf ("Sending process data to master is now allowed\n");
 * }
 * else
 * {
 *    printf ("We called function incorrectly\n");
 * }
 * \endcode
 *
 * \pre The pointer \a slave is non-null.
 * \pre fsoeslave_init() has been called for instance \a slave.
 *
 * \param[in,out] slave       FSoE slave state machine
 * \return                    Status:
 *                            - FSOESLAVE_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOESLAVE_STATUS_ERROR if user
 *                              violated a precondition.
 *
 * \see fsoeslave_get_process_data_sending_enable_flag().
 * \see fsoeslave_clear_process_data_sending_enable_flag().
 */
FSOE_EXPORT fsoeslave_status_t fsoeslave_set_process_data_sending_enable_flag (
   fsoeslave_t * slave);

/**
 * \brief Set reset request flag
 *
 * This will set a flag, which in next call to fsoeslave_sync_with_master()
 * will cause the slave state machine to send a Reset frame to master
 * and then wait for master to re-establish connection.
 * Fail-safe mode will then be entered, where normal process data inputs will
 * not be sent even after connection has been re-established.
 * Application needs to explicitly re-enable process data inputs in
 * order to leave fail-safe mode.
 * See fsoeslave_set_process_data_sending_enable_flag().
 *
 * See ETG.5100 ch. 8.5.1.2 "Reset Connection event".
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOESLAVE_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoeslave.h>
 * fsoeslave_status_t status;
 *
 * status = fsoeslave_set_reset_request_flag (slave);
 * if (status == FSOESLAVE_STATUS_OK)
 * {
 *    printf ("Slave state machine reset was requested\n");
 * }
 * else
 * {
 *    printf ("We called function incorrectly\n"));
 * }
 * \endcode
 *
 * \pre The pointer \a slave is non-null.
 * \pre fsoeslave_init() has been called for instance \a slave.
 *
 * \param[in,out] slave       FSoE slave state machine
 * \return                    Status:
 *                            - FSOESLAVE_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOESLAVE_STATUS_ERROR if user
 *                              violated a precondition.
 */
FSOE_EXPORT fsoeslave_status_t fsoeslave_set_reset_request_flag (
   fsoeslave_t * slave);

/**
 * \brief Synchronise with master
 *
 * Needs to be called periodically in order to avoid watchdog timeout.
 * It is recommended that delay between calls to the function is no more
 * than half the watchdog timeout.
 *
 * Depending on current state, the slave state machine may try to send a
 * single frame or read a single frame by calling fsoeapp_send() and/or
 * fsoeapp_recv(), which are non-blocking functions.
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOESLAVE_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoeslave.h>
 * fsoeslave_syncstatus_t sync_status;
 * fsoeslave_status_t status;
 * uint8_t outputs[2];
 *
 * inputs[0] = 0x56;
 * inputs[1] = 0x78;
 * status = fsoeslave_sync_with_master (slave, inputs, outputs, &sync_status);
 * if (status == FSOESLAVE_STATUS_OK)
 * {
 *    if (sync_status.reset_event != FSOESLAVE_RESETEVENT_NONE)
 *    {
 *       printf ("Connection was reset by %s. Cause: %s\n",
 *          sync_status.reset_event == FSOESLAVE_RESETEVENT_BY_MASTER ?
 *          "master" : "slave",
 *          fsoeslave_reset_reason_description (sync_status.reset_reason));
 *    }
 *    if (sync_status.is_process_data_received)
 *    {
 *       handle_received_data (outputs);
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
 * \pre The pointers \a slave, \a inputs, \a outputs and \a sync_status are non-null.
 * \pre fsoeslave_init() has been called for instance \a slave.
 *
 * \param[in,out] slave       FSoE slave state machine
 * \param[in]     inputs      Buffer containing inputs to be sent to master.
 *                            Its size is given in configuration.
 * \param[out]    outputs     Buffer to store outputs received from master.
 *                            Its size is given in configuration.
 *                            Whether outputs are valid or not is given
 *                            by \a sync_status.
 * \param[out]    sync_status Status of FSoE connection.
 * \return                    Status:
 *                            - FSOESLAVE_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOESLAVE_STATUS_ERROR if user
 *                              violated a precondition.
 */
FSOE_EXPORT fsoeslave_status_t fsoeslave_sync_with_master (
      fsoeslave_t * slave,
      const void * inputs,
      void * outputs,
      fsoeslave_syncstatus_t * sync_status);

/**
 * \brief Initialise FSoE slave state machine
 *
 * This will configure the instance according to supplied configuration.
 *
 * Before taking any action, function will first validate that its
 * preconditions (see below) were respected. If this was not the case, the
 * function fsoeapp_handle_user_error() will first be called after which
 * function will exit with status FSOESLAVE_STATUS_ERROR.
 *
 * Example:
 * \code
 * #include <fsoeslave.h>
 * const fsoeslave_cfg_t cfg =
 * {
 *    .slave_address               = 0x0304,
 *    .application_parameters_size = 0,
 *    .inputs_size                 = 2,
 *    .outputs_size                = 2,
 * };
 * fsoeslave_t slave;
 * fsoeslave_status_t status;
 *
 * status = fsoeslave_init (&slave, &cfg, NULL);
 * if (status == FSOESLAVE_STATUS_OK)
 * {
 *    printf ("Slave state machine was initialised\n");
 * }
 * else
 * {
 *    printf ("We called function incorrectly\n");
 * }
 * \endcode
 *
 * \pre The pointers \a slave and \a cfg are non-null.
 * \pre The fields in \a cfg are valid.
 *
 * \param[out]    slave       FSoE slave state machine
 * \param[in]     cfg         Configuration
 * \param[in]     app_ref     Application reference. This will be passed
 *                            as first argument to callback functions
 *                            implemented by application. The stack
 *                            does not interpret this value in any way.
 * \return                    Status:
 *                            - FSOESLAVE_STATUS_OK if function was
 *                              called correctly.
 *                            - FSOESLAVE_STATUS_ERROR if user
 *                              violated a precondition.
 */
FSOE_EXPORT fsoeslave_status_t fsoeslave_init (
   fsoeslave_t * slave,
   const fsoeslave_cfg_t * cfg,
   void * app_ref);

#ifdef __cplusplus
}
#endif

#endif /* FSOESLAVE_H */
