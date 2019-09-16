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
 * \brief FSoE master
 *
 * An FSoE master handles the connection with a single FSoE slave.
 * 
 * After power-on, master will try to establish a connection with its slave
 * Once established, it will periodically send outputs to the slave.
 * The slave will respond by sending back its inputs.
 *
 * Inputs and outputs may contain valid process data or they
 * may contain fail-safe data (all zeroes). By default, they contain 
 * fail-safe data. They will only contain valid process data if sender
 * (master for outputs, slave for inputs) determines that everything is OK.
 * The sender may send valid process data while receiving fail-safe 
 * data or vice versa.
 * Inputs and outputs have fixed size, but they need not be the same size.
 *
 * A user of the master API will have to explicitly enable it in order for
 * valid process data to be sent.
 * Communication errors will cause the connection to be reset. 
 * Master will then disable the process data outputs and try to re-establish
 * connection with its slave. If successful, it restarts sending 
 * outputs as fail-safe data.
 * A user of the master API may then re-enable process data outputs.
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
 * At a lower level, the master communicates with the slave through a 
 * "black channel". Master does not know how the black channel is implemented,
 * it just knows how to access it - by calling fsoeapp_send() and fsoeapp_recv().
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
 */

#ifndef FSOEMASTER_H
#define FSOEMASTER_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * \brief Status after synchronisation with slave
 *
 * See fsoemaster_sync_with_slave()
 */
typedef struct fsoemaster_status
{
   bool is_process_data_received;/**< true: 
                                  *      Valid process data was received in
                                  *      last FSoE cycle. The process data is
                                  *      stored in \a inputs buffer.
                                  *   false:
                                  *      No process data was received in
                                  *      last FSoE cycle. The \a inputs buffer
                                  *      contains only zeroes.
                                  *      This will be returned if master has
                                  *      detected an error, if connection with
                                  *      slave is not established or if fail-safe
                                  *      data was received.
                                  */
} fsoemaster_status_t;

/**
 * \brief FSoE connection state
 *
 * After power-on, master is in Reset state. In Reset state, master is not
 * connected with any slave. Interchange of process data with a slave only
 * takes place when master is in Data state. Before Data state is entered,
 * master first has to configure the slave by sending it configuration
 * data. This takes place in the intermediate states Session, Connection
 * and Parameter.
 *
 * Master transitions to new states once configuration data sent to slave
 * has been ACKed by slave. It also sets the slave's state by means of
 * sending a corresponding frame. For example, master sending
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
   FSOEMASTER_STATE_SESSION,     /**< The session ID is being transferred */
   FSOEMASTER_STATE_CONNECTION,  /**< The connection ID is being transferred */
   FSOEMASTER_STATE_PARAMETER,   /**< The parameters are being transferred */
   FSOEMASTER_STATE_DATA,        /**< Process or fail-safe data is being transferred */
} fsoemaster_state_t;

/**
 * \brief Configuration of FSoE master
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
    * This value will be sent to slave when connection is established.
    * May be zero if no application parameters are needed.
    * Slave will refuse the connection if it expected a different size.
    *
    * See ETG.5100 ch. 8.2.2.5 "Parameter state".
    */
   size_t application_parameters_size;

   /**
    * \brief Size in bytes of the outputs to be sent to slave
    *
    * Only even values are allowed, except for 1, which is also allowed.
    * Maximum value is 126.
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
    * Maximum value is 126.
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
 * \brief FSoE master instance
 *
 * An FSoE master instance handles the connection with a single slave.
 * Multiple masters are supported, where each one have their own Connection ID.
 */
typedef struct fsoemaster fsoemaster_t;

/**
 * \brief Get current state of the FSoE master state machine
 *
 * See ETG.5100 ch. 8.4.1.1 table 29: "States of the FSoE Master".
 *
 * \param[in]     master      FSoE master instance
 * \return                    Current state of the FSoE master
 */
fsoemaster_state_t fsoemaster_get_state (const fsoemaster_t * master);

/**
 * \brief Get time remaining until watchdog timer timeouts, in milliseconds
 *
 * This functions is mainly used for unit-testing purposes.
 *
 * \param[in]     master      FSoE master instance
 * \return                    Time remaining in milliseconds. If watchdog
 *                            timer is not started, UINT32_MAX is returned.
 */
uint32_t fsoemaster_time_until_timeout_ms (const fsoemaster_t * master);

/**
 * \brief Check if FSoE master is allowed to send normal process data to slave
 *
 * This will only check a flag indicating that everything is OK from the
 * perspective of the application. Master will not send normal process
 * data if connection with slave is not fully established (Data state), 
 * even if application allows it.
 *
 * See ETG.5100 ch. 8.4.1.2 "Set Data Command event".
 *
 * \param[in,out] master       FSoE master instance
 */
bool fsoemaster_is_sending_process_data_enabled (const fsoemaster_t * master);

/**
 * \brief Enable master to send valid process data to slave
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
 * \param[in,out] master      FSoE master instance
 */
void fsoemaster_enable_sending_process_data (fsoemaster_t * master);

/**
 * \brief Disallow master from sending normal process data to slave
 *
 * This will clear a flag indicating that everything is OK from the
 * perspective of the application.
 * Master will only send fail safe data (zeroes) to slave.
 * This is the default setting after power-on and after detection
 * of any errors.
 *
 * See ETG.5100 ch. 8.4.1.2 "Set Data Command event".
 *
 * \param[in,out] master      FSoE master instance
 */
void fsoemaster_disable_sending_process_data (fsoemaster_t * master);

/**
 * \brief Reset connection with slave
 *
 * Master will send the Reset command to slave and then enter the Reset state.
 *
 * See ETG.5100 ch. 8.4.1.2 "Reset Connection event".
 *
 * \param[in,out] master      FSoE master instance
 */
void fsoemaster_reset_connection (fsoemaster_t * master);

/**
 * \brief Run FSoE master state machine
 *
 * Needs to be called periodically in order to avoid watchdog timeout.
 * It is recommended that delay between calls to the function is no more
 * than half the watchdog timeout.
 *
 * Depending on current state, master may try to send a single frame or 
 * read a single frame by calling fsoeapp_send() and/or fsoeapp_recv(), 
 * which are non-blocking functions.
 *
 * \param[in,out] master      FSoE master instance
 * \param[in]     outputs     Buffer containing outputs to be sent to slave.
 *                            Its size is given in configuration.
 * \param[out]    inputs      Buffer to store inputs received from slave.
 *                            Its size is given in configuration.
 *                            Whether inputs are valid or not is given
 *                            by \a status.
 * \param[out]    status      Status of FSoE connection.
 */
void fsoemaster_sync_with_slave (
      fsoemaster_t * master,
      const void * outputs,
      void * inputs,
      fsoemaster_status_t * status);

/**
 * \brief Destroy FSoE master instance
 *
 * All allocated memory will be freed.
 * The instance may no longer be used after calling this function.
 *
 * \param[in,out] master      FSoE master instance
 */
void fsoemaster_destroy (fsoemaster_t * master);

/**
 * \brief Create FSoE master instance
 *
 * This will allocate memory used by the instance, including itself.
 * The instance will then be configured according to supplied configuration.
 * If memory allocation fails or fields in the configuration are invalid,
 * an assertion error will be triggered and program execution will halt.
 *
 * \param[in]     cfg         Configuration
 * \param[in]     app_ref     Application reference. This will be passed
 *                            as first argument to callback functions
 *                            implemented by application. The stack
 *                            does not interpret this value in any way.
 * \return                    FSoE master instance. Can't be NULL
 */
fsoemaster_t * fsoemaster_create (
   const fsoemaster_cfg_t * cfg,
   void * app_ref);

#ifdef __cplusplus
}
#endif

#endif /* FSOEMASTER_H */
