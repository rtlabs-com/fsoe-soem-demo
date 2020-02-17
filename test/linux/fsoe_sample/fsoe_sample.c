/** \file
* \brief Example code for Simple Open EtherCAT master running
* a FSoE Master Application.
*
* (c)Andreas Karlsson 2019
*/

#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "ethercat.h"
#include "fsoemaster.h"
#include "fsoeapp.h"

/* Running lockstep =1 , running 2 CPUs =0 */
#define FSOE_REDUNDANT_SCL_IN_HW   1

/********************** Define Standard EtherCAT Master instance *********************/
char IOmap[4096];
ec_slavet   ec_slave[EC_MAXSLAVE];
/** number of slaves found on the network */
int         ec_slavecount;
/** slave group structure */
ec_groupt   ec_groups[EC_MAXGROUP];

/** cache for EEPROM read functions */
static uint8        esibuf[EC_MAXEEPBUF];
/** bitmap for filled cache buffer bytes */
static uint32       esimap[EC_MAXEEPBITMAP];
/** current slave for EEPROM cache buffer */
static ec_eringt    ec_elist;
static ec_idxstackT ec_idxstack;
/** SyncManager Communication Type struct to store data of one slave */
static ec_SMcommtypet  ec_SMcommtype;
/** PDO assign struct to store data of one slave */
static ec_PDOassignt   ec_PDOassign;
/** PDO description struct to store data of one slave */
static ec_PDOdesct     ec_PDOdesc;
/** buffer for EEPROM SM data */
static ec_eepromSMt ec_SM;
/** buffer for EEPROM FMMU data */
static ec_eepromFMMUt ec_FMMU;
/** Global variable TRUE if error available in error stack */
static boolean    AppEcatError = FALSE;
int64         ec_DCtime;
static ecx_portt      ecx_port_fsoe;

static ecx_contextt ctx = {
   &ecx_port_fsoe,
   &ec_slave[0],
   &ec_slavecount,
   EC_MAXSLAVE,
   &ec_groups[0],
   EC_MAXGROUP,
   &esibuf[0],
   &esimap[0],
   0,
   &ec_elist,
   &ec_idxstack,
   &AppEcatError,
   0,
   0,
   &ec_DCtime,
   &ec_SMcommtype,
   &ec_PDOassign,
   &ec_PDOdesc,
   &ec_SM,
   &ec_FMMU,
   NULL,
   NULL,
   0
};

/******************************* Local FSoE Master variables ********************************/
typedef struct fsoeapp_ref
{
   ec_slavet * ecat_slave;
   fsoemaster_t fsoe_master;
   fsoemaster_syncstatus_t fsoe_status;
   uint32_t fsoe_offset_outputs;
   uint32_t fsoe_offset_inputs;
   bool in_use;
}fsoeapp_ref_t;

/* We'll have a network with 3 FSoE Slaves, one FSoE Master instance per slave */
fsoeapp_ref_t master1;
fsoeapp_ref_t master2;
fsoeapp_ref_t master3;

/********************** Define FSoE Master configurations of FSoE Slaves *********************/


/* Local Safety Data Inputs/Outputs */
typedef union float_val
{
   float float_value;
   uint8_t byte_value[sizeof(float)];
} float_val_t;

/* rt-labs sample slave 1 */
/* Inputs FSOE frame  */
PACKED_BEGIN
typedef struct PACKED
{
   uint16_t safety_status;
   float_val_t motor_postion;
   float_val_t arm_postion;
   float_val_t safe_tourque;
}safe_inputs_t;
PACKED_END
PACKED_BEGIN
typedef struct PACKED
{
   uint16_t control_command;
   uint16_t reserved;
} safety_out_t;
PACKED_END
safety_out_t safe_outputs;
safe_inputs_t safe_inputs;

uint8_t application_parameters[2] = { 0 , 1 };
/* Configuration of master instance */
fsoemaster_cfg_t cfg1 =
{
   2049,                          /* slave_address */
   0xffff,                          /* connection_id */
   0x0064,                          /* watchdog_timeout_ms */
   application_parameters,          /* application_parameters */
   sizeof(application_parameters),  /* application_parameters_size */
   sizeof(safe_outputs),            /* outputs_size */
   sizeof(safe_inputs),             /* inputs_size */
};

/****************************** EL1904 FSoE Slave ************************/
uint8_t el1904_safe_inputs = 0;
uint8_t el1904_safe_outputs = 0;
/* Taken from CTT */
const uint8_t el1904_parameters[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
fsoemaster_cfg_t cfg2 =
{
   0x0002,                          /* slave_address */
   0xBBBB,                          /* connection_id */
   0x0064,                          /* watchdog_timeout_ms */
   el1904_parameters,               /* application_parameters */
   sizeof(el1904_parameters),       /* application_parameters_size */
   sizeof(el1904_safe_outputs),     /* outputs_size */
   sizeof(el1904_safe_inputs),      /* inputs_size */
};

/****************************** EL2904 FSoE Slave ************************/
uint8_t el2904_safe_inputs = 0;
uint8_t el2904_safe_outputs = 0;
const uint8_t el2904_parameters[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
fsoemaster_cfg_t cfg3 =
{
   0x0003,                          /* slave_address */
   0xCCCC,                          /* connection_id */
   0x0064,                          /* watchdog_timeout_ms */
   el2904_parameters,               /* application_parameters */
   sizeof(el2904_parameters),       /* application_parameters_size */
   sizeof(el2904_safe_outputs),     /* outputs_size */
   sizeof(el2904_safe_inputs),      /* inputs_size */
};

uint16_t fsoeapp_generate_session_id(void * app_ref)
{
   (void)app_ref;
   return (uint16_t)(rand() % 0xffff);
}

/**************** FSoE stack send data to black channel *********************/
void fsoeapp_send(void * app_ref, const void * buffer, size_t size)
{
   /* We pass a local FSOE app ref as app_ref , set at FSoE create */
   fsoeapp_ref_t * temp_ref = (fsoeapp_ref_t *)app_ref;
   memcpy(temp_ref->ecat_slave->outputs + temp_ref->fsoe_offset_outputs, buffer, size);
}

/**************** FSoE stack send data to black channel *********************/
size_t fsoeapp_recv(void * app_ref, void * buffer, size_t size)
{
   /* We pass a local FSOE app ref as app_ref , set at FSoE create */
   fsoeapp_ref_t * temp_ref = (fsoeapp_ref_t *)app_ref;
   memcpy(buffer, temp_ref->ecat_slave->inputs + temp_ref->fsoe_offset_inputs, size);
   return size;
}

/**************** FSoE stack user API error callback *********************/
void fsoeapp_handle_user_error(
   void * app_ref, fsoeapp_usererror_t user_error)
{
   (void)app_ref;
   printf("We called an API function incorrectly: %s\n",
      fsoeapp_user_error_description(user_error));
}

/* Safety application for runing FSoE and do Safety Logic. */
void safety_app(void)
{
   if (master1.in_use)
   {
      /* Handle FSoE Slave 1 */
      /* Run stack instance for slave 1  */
      /* Dummy test, let slave 1 inputs control slave 1 & 3 outputs */
      safe_outputs.control_command = safe_inputs.safety_status;
      el2904_safe_outputs = (uint8_t)safe_inputs.safety_status;
      /* Run the FSoE Stack */
      if (fsoemaster_sync_with_slave(&master1.fsoe_master,
         &safe_outputs,
         &safe_inputs,
         &master1.fsoe_status) != FSOEMASTER_STATUS_OK)
      {
         printf("fsoemaster_sync_with_slave master %d failed\n", 1);
      }
      else
      {
         /* Enable data in parameter state */
         if (master1.fsoe_status.current_state == FSOEMASTER_STATE_PARAMETER)
         {
            if (fsoemaster_set_process_data_sending_enable_flag(&master1.fsoe_master) != FSOEMASTER_STATUS_OK)
            {
               printf("fsoemaster_set_process_data_sending_enable_flag master %d failed\n", 1);
            }
         }
      }
      /* Did a reset event occur? */
      if (master1.fsoe_status.reset_event != FSOEMASTER_RESETEVENT_NONE)
      {
         printf("Connection was reset by %s. Cause: %s\n",
            master1.fsoe_status.reset_event == FSOEMASTER_RESETEVENT_BY_MASTER ?
            "master" : "slave",
            fsoemaster_reset_reason_description(master1.fsoe_status.reset_reason));
      }
   }
   if (master2.in_use)
   {
      /* Handle FSoE Slave 2 */
      /* Run stack instance for slave 2  */
      if (fsoemaster_sync_with_slave(&master2.fsoe_master,
         &el1904_safe_outputs,
         &el1904_safe_inputs,
         &master2.fsoe_status) != FSOEMASTER_STATUS_OK)
      {
         printf("fsoemaster_sync_with_slave master %d failed\n", 2);
      }
      else
      {
         /* Enable data in parameter state */
         if (master2.fsoe_status.current_state == FSOEMASTER_STATE_PARAMETER)
         {
            if (fsoemaster_set_process_data_sending_enable_flag(&master2.fsoe_master) != FSOEMASTER_STATUS_OK)
            {
               printf("fsoemaster_set_process_data_sending_enable_flag master %d failed\n", 2);
            }
         }
         /* Did a reset event occur? */
         if (master2.fsoe_status.reset_event != FSOEMASTER_RESETEVENT_NONE)
         {
            printf("Connection was reset by %s. Cause: %s\n",
               master2.fsoe_status.reset_event == FSOEMASTER_RESETEVENT_BY_MASTER ?
               "master" : "slave",
               fsoemaster_reset_reason_description(master2.fsoe_status.reset_reason));
         }
      }
   }
   if (master3.in_use)
   {
      /* Handle FSoE Slave 3 */
      /* Run stack instance for slave 3  */
      /* Dummy test, let slave 1 inputs control slave 3 outputs */
      /* Run the FSoE Stack */
      if (fsoemaster_sync_with_slave(&master3.fsoe_master,
         &el2904_safe_outputs,
         &el2904_safe_inputs,
         &master3.fsoe_status) != FSOEMASTER_STATUS_OK)
      {
         printf("fsoemaster_sync_with_slave master %d failed\n", 3);
      }
      else
      {
         /* Enable data in parameter state */
         if (master3.fsoe_status.current_state == FSOEMASTER_STATE_PARAMETER)
         {
            if (fsoemaster_set_process_data_sending_enable_flag(&master3.fsoe_master) != FSOEMASTER_STATUS_OK)
            {
               printf("fsoemaster_set_process_data_sending_enable_flag master %d failed\n", 3);
            }
         }
         /* Did a reset event occur? */
         if (master3.fsoe_status.reset_event != FSOEMASTER_RESETEVENT_NONE)
         {
            printf("Connection was reset by %s. Cause: %s\n",
               master3.fsoe_status.reset_event == FSOEMASTER_RESETEVENT_BY_MASTER ?
               "master" : "slave",
               fsoemaster_reset_reason_description(master3.fsoe_status.reset_reason));
         }
      }
   }

}

/* Do FSoE Setup, this is application specific, FSoE cfg is decided in design time  */
void safety_setup(void)
{
   int i;
   /* Map EtherCAT slaves to expected FSoE Slaves */
   for (i = 1; i <= *ctx.slavecount; i++)
   {
      /* rt-labs sample slave */
      if (ctx.slavelist[i].eep_id == 0x1ba && ctx.slavelist[i].eep_man == 0x50c)
      {
         /* Connect rt-labs sample slave to FSoE slave 1 */
         memset(&master1, 0, sizeof(master1));
         master1.ecat_slave = &ctx.slavelist[i];
         if (fsoemaster_init(&master1.fsoe_master, &cfg1, &master1) != FSOEMASTER_STATUS_OK)
         {
            printf("fsoemaster_init master %d failed\n", 1);
         }
         master1.fsoe_offset_outputs = ctx.slavelist[i].Obytes - 11;
         master1.fsoe_offset_inputs = ctx.slavelist[i].Ibytes - 31;
         master1.in_use = TRUE;
      }
      /* EL1904 */
      else if (ctx.slavelist[i].eep_id == 0x7703052 && ctx.slavelist[i].eep_man == 0x00000002)
      {
         /* Connect EL1904 sample slave to FSoE slave 2 */
         memset(&master2, 0, sizeof(master2));
         master2.ecat_slave = &ctx.slavelist[i];
         if (fsoemaster_init(&master2.fsoe_master, &cfg2, &master2) != FSOEMASTER_STATUS_OK)
         {
            printf("fsoemaster_init master %d failed\n", 2);
         }
         master2.fsoe_offset_outputs = 0;
         master2.fsoe_offset_inputs = 0;
         master2.in_use = TRUE;
      }
      /* EL2904 */
      else if (ctx.slavelist[i].eep_id == 0xB583052 && ctx.slavelist[i].eep_man == 0x00000002)
      {
         /* Connect EL2904 sample slave to FSoE slave 3 */
         memset(&master3, 0, sizeof(master3));
         master3.ecat_slave = &ctx.slavelist[i];
         if (fsoemaster_init(&master3.fsoe_master, &cfg3, &master3) != FSOEMASTER_STATUS_OK)
         {
            printf("fsoemaster_init master %d failed\n", 3);
         }
         master3.fsoe_offset_outputs = 0;
         master3.fsoe_offset_inputs = 0;
         master3.in_use = TRUE;
      }
   }
}

void fsoemaster(char *ifname)
{
   int i, j, oloop, iloop, expectedWKC, chk;
   volatile int wkc;

   printf("Starting FSoE Master\n");

   /* initialize random seed: */
   srand((unsigned int)time(NULL));

   /* initialise SOEM, bind socket to ifname */
   if (ecx_init(&ctx, ifname))
   {
      printf("ec_init on %s succeeded.\n", ifname);
      /* find and auto-config slaves */
      if (ecx_config_init(&ctx, FALSE) > 0)
      {
         printf("%d slaves found and configured.\n", ec_slavecount);
         ecx_config_map_group(&ctx, &IOmap, 0);
         memset(IOmap, 0, sizeof(IOmap));
         /* read individual slave state and store in ec_slave[] */
         ecx_readstate(&ctx);
         /* Setup FSOE Network when EtherCAT slaves have been configured and mapped */
         safety_setup();
         printf("Slaves mapped, state to SAFE_OP.\n");
         /* wait for all slaves to reach SAFE_OP state */
         ecx_statecheck(&ctx, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE * 4);
         oloop = ctx.slavelist[0].Obytes;
         /* Setup printerinfo boundries */
         if ((oloop == 0) && (ctx.slavelist[0].Obits > 0)) oloop = 1;
         if (oloop > 8) oloop = 8;
         iloop = ctx.slavelist[0].Ibytes;
         if ((iloop == 0) && (ctx.slavelist[0].Ibits > 0)) iloop = 1;
         if (iloop > 8) iloop = 8;
         /* go to OP */
         printf("Request operational state for all slaves\n");
         expectedWKC = (ctx.grouplist[0].outputsWKC * 2) + ctx.grouplist[0].inputsWKC;
         printf("Calculated workcounter %d\n", expectedWKC);
         ctx.slavelist[0].state = EC_STATE_OPERATIONAL;
         /* send one valid process data to make outputs in slaves happy*/
         ecx_send_processdata(&ctx);
         ecx_receive_processdata(&ctx, EC_TIMEOUTRET3/*EC_TIMEOUTRET*/);
         /* request OP state for all slaves */
         ecx_writestate(&ctx, 0);
         chk = 40;
         /* wait for all slaves to reach OP state */
         do
         {
            ecx_send_processdata(&ctx);
            ecx_receive_processdata(&ctx, EC_TIMEOUTRET3/*EC_TIMEOUTRET*/);
            ecx_statecheck(&ctx, 0, EC_STATE_OPERATIONAL, 50000);
         } while (chk-- && (ec_slave[0].state != EC_STATE_OPERATIONAL));
         if (ec_slave[0].state == EC_STATE_OPERATIONAL)
         {
            printf("Operational state reached for all slaves.\n");
            /* cyclic loop */
            for (i = 1; i <= 100000; i++)
            {
               ecx_send_processdata(&ctx);
               wkc = ecx_receive_processdata(&ctx, EC_TIMEOUTRET * 10);

               /* Call the safety application */
               safety_app();

               if (wkc >= expectedWKC)
               {
                  printf("Processdata cycle %4d, WKC %d , O:", i, wkc);
                  for (j = 0; j < oloop; j++)
                  {
                     printf(" %2.2x", *(ctx.slavelist[0].outputs + j));
                  }
                  printf(" I:");
                  for (j = 0; j < iloop; j++)
                  {
                     printf(" %2.2x", *(ctx.slavelist[0].inputs + j));
                  }
                  printf("\r");
               }
               osal_usleep(2000);
            }
         }
         else
         {
            printf("Not all slaves reached operational state.\n");
            ecx_readstate(&ctx);
            for (i = 1; i <= *ctx.slavecount; i++)
            {
               if (ctx.slavelist[i].state != EC_STATE_OPERATIONAL)
               {
                  printf("Slave %d State=0x%2.2x StatusCode=0x%4.4x : %s\n",
                     i, ctx.slavelist[i].state,
                     ctx.slavelist[i].ALstatuscode,
                     ec_ALstatuscode2string(ctx.slavelist[i].ALstatuscode));
               }
            }
         }
         printf("\nRequest init state for all slaves\n");
         ctx.slavelist[0].state = EC_STATE_INIT;
         /* request INIT state for all slaves */
         ecx_writestate(&ctx, 0);
      }
      else
      {
         printf("No slaves found!\n");
      }
      printf("End simple test, close socket\n");
      /* stop SOEM, close socket */
      ecx_close(&ctx);
   }
   else
   {
      printf("No socket connection on %s\nExcecute as root\n", ifname);
   }
}

int main(int argc, char *argv[])
{
   printf("SOEM (Simple Open EtherCAT Master)\nFSoE Master Demo\n");

   if (argc > 1)
   {
      /* start cyclic part */
      fsoemaster(argv[1]);
   }
   else
   {
      printf("Usage: simple_test ifname1\nifname = eth0 for example\n");
   }

   printf("End program\n");
   return (0);
}
