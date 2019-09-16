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
 * \brief FSoE slave
 *
 * An FSoE slave handles the connection with a single FSoE master.
 * 
 * After power-on, slave will listen for connection requests from a master.
 * Once established, slave will wait for outputs from master. When received,
 * it will respond by sending back its inputs to master.
 *
 * Inputs and outputs may contain valid process data or they
 * may contain fail-safe data (all zeroes). By default, they contain 
 * fail-safe data. They will only contain valid process data if sender
 * (slave for inputs, master for outputs) determines that everything is OK.
 * The sender may send valid process data while receiving fail-safe 
 * data or vice versa.
 * Inputs and outputs have fixed size, but they need not be the same size.
 *
 * A user of the slave API will have to explicitly enable it in order for
 * valid process data to be sent.
 * Communication errors will cause the connection to be reset. 
 * Slave will then disable the process data inputs and start listening for
 * new connection requests from a master. If successful, it restarts sending 
 * inputs as fail-safe data.
 * A user of the slave API may then re-enable process data inputs.
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
 * At a lower level, the slave communicates with the master through a 
 * "black channel". Slave does not know how the black channel is implemented,
 * it just knows how to access it - by calling fsoeapp_send() and fsoeapp_recv().
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
 */

#ifndef FSOESLAVE_H
#define FSOESLAVE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * \brief Status after synchronisation with master
 *
 * See fsoeslave_sync_with_master()
 */
typedef struct fsoeslave_status
{
   bool is_process_data_received;/**< true: 
                                  *      Valid process data was received in
                                  *      last FSoE cycle. The process data is
                                  *      stored in \a outputs buffer.
                                  *   false:
                                  *      No process data was received in
                                  *      last FSoE cycle. The \a outputs buffer
                                  *      contains only zeroes.
                                  *      This will be returned if slave has
                                  *      detected an error, if connection with
                                  *      master is not established or if
                                  *      fail-safe data was received.
                                  */
} fsoeslave_status_t;

/**
 * \brief FSoE connection state
 *
 * After power-on, slave is in Reset state. In Reset state, slave is not
 * associated with any master. Interchange of process data with a master only
 * takes place when slave is in Data state. Before Data state is entered,
 * a master first has to configure the slave by sending it configuration
 * data. This takes place in the intermediate states Session, Connection
 * and Parameter.
 *
 * With the exception of transitions to the Reset state, slave does not change
 * state on its own. Instead, it is the master which orders the slave to enter 
 * a new state by means of sending a corresponding frame. For example, master
 * sending a Connection frame will cause slave to enter Connection state,
 * assuming that state transition is allowed. Slave will enter Reset state
 * on its own if it detects an error.
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
   FSOESLAVE_STATE_RESET,       /**< Connection is reset */
   FSOESLAVE_STATE_SESSION,     /**< The session ID is being transferred */
   FSOESLAVE_STATE_CONNECTION,  /**< The connection ID is being transferred */
   FSOESLAVE_STATE_PARAMETER,   /**< The parameters are being transferred */
   FSOESLAVE_STATE_DATA,        /**< Process or fail-safe data is being transferred */
} fsoeslave_state_t;

/**
 * \brief Configuration of FSoE slave
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
    * This value will be received by master when connection is established,
    * and slave will verify that the value matches this value.
    * Slave will refuse the connection if wrong Slave Address is received.
    *
    * See ETG.5100 ch. 8.2.2.4 "Connection state".
    */
   uint16_t slave_address;

   /**
    * \brief Expected size in bytes of the application parameters
    *
    * Valid values are 0 - 65535.
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
    * Maximum value is 126.
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
    * Maximum value is 126.
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
 * \brief FSoE slave instance
 *
 * An FSoE slave instance handles the connection with a single master.
 * Multiple slaves are supported, where each one have their own Slave Address.
 */
typedef struct fsoeslave fsoeslave_t;

/**
 * \brief Get current state of the FSoE slave state machine
 *
 * See ETG.5100 ch. 8.5.1.1 table 34: "States of the FSoE Slave".
 *
 * \param[in]     slave       FSoE slave instance
 * \return                    Current state of the FSoE slave
 */
fsoeslave_state_t fsoeslave_get_state (const fsoeslave_t * slave);

/**
 * \brief Check if FSoE slave is allowed to send normal process data to master
 *
 * This will only check a flag indicating that everything is OK from the
 * perspective of the application. Slave will not send normal process
 * data if connection with master is not fully established (Data state), 
 * even if application allows it.
 *
 * See ETG.5100 ch. 8.5.1.2 "Set Data Command event".
 *
 * \param[in,out] slave       FSoE slave instance
 */
bool fsoeslave_is_sending_process_data_enabled (const fsoeslave_t * slave);

/**
 * \brief Enable FSoE slave to send valid process data to master
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
 * \param[in,out] slave       FSoE slave instance
 */
void fsoeslave_enable_sending_process_data (fsoeslave_t * slave);

/**
 * \brief Disallow FSoE slave from sending normal process data to master
 *
 * This will clear a flag indicating that everything is OK from the
 * perspective of the application.
 * Slave will only send fail safe data (zeroes) to master.
 * This is the default setting after power-on and after detection
 * of any errors.
 *
 * See ETG.5100 ch. 8.5.1.2 "Set Data Command event".
 *
 * \param[in,out] slave       FSoE slave instance
 */
void fsoeslave_disable_sending_process_data (fsoeslave_t * slave);

/**
 * \brief Reset connection with master
 *
 * Slave will send a Reset frame to master and then wait for master
 * to re-establish connection.
 * Fail-safe mode will be entered, where normal process data inputs will
 * not be sent even after connection has been re-established.
 * Application needs to explicitly re-enable process data inputs in
 * order to leave fail-safe mode.
 *
 * See ETG.5100 ch. 8.5.1.2 "Reset Connection event".
 *
 * \param[in,out] slave       FSoE slave instance
 */
void fsoeslave_reset_connection (fsoeslave_t * slave);

/**
 * \brief Run FSoE slave state machine
 *
 * Needs to be called periodically in order to avoid watchdog timeout.
 * It is recommended that delay between calls to the function is no more
 * than half the watchdog timeout.
 *
 * Depending on current state, slave may try to send a single frame or 
 * read a single frame by calling fsoeapp_send() and/or fsoeapp_recv(), 
 * which are non-blocking functions.
 *
 * \param[in,out] slave       FSoE slave instance
 * \param[in]     inputs      Buffer containing inputs to be sent to master.
 *                            Its size is given in configuration.
 * \param[out]    outputs     Buffer to store outputs received from master.
 *                            Its size is given in configuration.
 *                            Whether outputs are valid or not is given
 *                            by \a status.
 * \param[out]    status      Status of FSoE connection.
 */
void fsoeslave_sync_with_master (
      fsoeslave_t * slave,
      const void * inputs,
      void * outputs,
      fsoeslave_status_t * status);

/**
 * \brief Destroy FSoE slave instance
 *
 * All allocated memory will be freed.
 * The instance may no longer be used after calling this function.
 *
 * \param[in,out] slave       FSoE slave instance
 */
void fsoeslave_destroy (fsoeslave_t * slave);

/**
 * \brief Create FSoE slave instance
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
 * \return                    FSoE slave instance. Can't be NULL
 */
fsoeslave_t * fsoeslave_create (
   const fsoeslave_cfg_t * cfg,
   void * app_ref);

#ifdef __cplusplus
}
#endif

#endif /* FSOESLAVE_H */
