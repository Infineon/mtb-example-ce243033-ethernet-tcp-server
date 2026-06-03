/******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for TCP Server Example in ModusToolbox.
* In this example, TCP server establishes a connection with a TCP client.
* Once the connection completes successfully, the server allows the user to
* send LED ON/OFF command to the TCP client and the client responds by sending
* an acknowledgement message to the server.
*
* Related Document: See README.md
*
*
*******************************************************************************
* (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

/* Header file includes */
#include "cybsp.h"
#include "cy_retarget_io.h"

/* FreeRTOS header file */
#include <FreeRTOS.h>
#include <task.h>
#include "cyabs_rtos.h"

/* TCP server task header file. */
#include "tcp_server.h"

/*******************************************************************************
* Macros
********************************************************************************/
/* RTOS related macros for TCP server task. */
#define TCP_SERVER_TASK_STACK_SIZE                (1024u * 5u)
#define TCP_SERVER_TASK_PRIORITY                  (1u)

/*******************************************************************************
* Global Variables
********************************************************************************/
/* This enables RTOS aware debugging. */
volatile int uxTopUsedPriority;

/* TCP server task handle. */
TaskHandle_t server_task_handle;

/*******************************************************************************
 * Function Name: main
 ********************************************************************************
 * Summary:
 *  System entrance point. This function sets up user tasks and then starts
 *  the RTOS scheduler.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  int
 *
 *******************************************************************************/
int main()
{
    cy_rslt_t result;

    /* UART context */
    static cy_stc_scb_uart_context_t    UART_context;

    /* Debug UART HAL object  */
    static mtb_hal_uart_t               UART_hal_obj;

    /* This enables RTOS aware debugging in OpenOCD. */
    uxTopUsedPriority = configMAX_PRIORITIES - 1u;

    /* Enable global interrupts. */
    __enable_irq();

    /* Initialize the board support package. */
    result = cybsp_init() ;
    CY_ASSERT(result == CY_RSLT_SUCCESS);

    /* Board init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Initialize retarget-io to use the debug UART port. */
    result = (cy_rslt_t)Cy_SCB_UART_Init(UART_HW, &UART_config, &UART_context);
    Cy_SCB_UART_Enable(UART_HW);
    result |= mtb_hal_uart_setup(&UART_hal_obj, &UART_hal_config, &UART_context, NULL);
    result |= cy_retarget_io_init(&UART_hal_obj);

    /* retarget-io init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* \x1b[2J\x1b[;H - ANSI ESC sequence to clear screen. */
    printf("\x1b[2J\x1b[;H");
    printf("===============================================================\n");
    printf("                CE243033 - Ethernet:TCP Server                 \n");
    printf("===============================================================\n\n");

    /* Create the tasks. */
    xTaskCreate(tcp_server_task, "Network task", TCP_SERVER_TASK_STACK_SIZE, NULL,
                TCP_SERVER_TASK_PRIORITY, &server_task_handle);

    /* Start the FreeRTOS scheduler. */
    vTaskStartScheduler();

    /* Should never get here. */
    CY_ASSERT(0);
}

/* [] END OF FILE */
