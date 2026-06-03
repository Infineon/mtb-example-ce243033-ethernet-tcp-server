/******************************************************************************
* File Name:   tcp_server.c
*
* Description: This file contains declaration of task and functions related to
* TCP server operation.
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

/* TCP server task header file. */
#include "tcp_server.h"

/* Header file includes */
#include "cybsp.h"
#include "cy_retarget_io.h"

/* Ethernet connection manager header files */
#include "cy_ecm.h"
#include "cy_ecm_error.h"

/* Cypress secure socket header file */
#include "cy_secure_sockets.h"

/* Network connectivity utility header file. */
#include "cy_nw_helper.h"

/* FreeRTOS header file */
#include <FreeRTOS.h>
#include <task.h>

/* Standard C header files */
#include <string.h>
#include <inttypes.h>

#include "cy_eth_phy_driver.h"

/*******************************************************************************
* Macros
********************************************************************************/
/* Maximum number of connection retries to the ethernet network */
#define MAX_ETH_RETRY_COUNT                       (3u)

/* TCP keep alive related macros. */
#define TCP_KEEP_ALIVE_IDLE_TIME_MS               (10000u)
#define TCP_KEEP_ALIVE_INTERVAL_MS                (1000u)
#define TCP_KEEP_ALIVE_RETRY_COUNT                (2u)

/* Length of the LED ON/OFF command issued from the TCP server. */
#define TCP_LED_CMD_LEN                           (1)

/* LED ON and LED OFF commands. */
#define LED_ON_CMD                                '1'
#define LED_OFF_CMD                               '0'

/* Interrupt priority of the user button. */
#define USER_BTN_INTR_PRIORITY                    (5)

/* Debounce delay for user button. */
#define DEBOUNCE_DELAY_MS                         (50)

/* Length of Buffer to store IP address */
#define IP_ADDR_BUFFER_SIZE                       (50)

/* Ethernet interface ID */
#define INTERFACE_ID                        CY_ECM_INTERFACE_ETH0

/*******************************************************************************
* Function Prototypes
********************************************************************************/
static cy_rslt_t create_tcp_server_socket(void);
static cy_rslt_t tcp_connection_handler(cy_socket_t socket_handle, void *arg);
static cy_rslt_t tcp_receive_msg_handler(cy_socket_t socket_handle, void *arg);
static cy_rslt_t tcp_disconnection_handler(cy_socket_t socket_handle, void *arg);
static void isr_button_press(void);

/* Establish ethernet connection to the network */
static cy_rslt_t connect_to_ethernet(void);

/*******************************************************************************
* Global Variables
********************************************************************************/
/* Secure socket variables. */
cy_socket_sockaddr_t tcp_server_addr, peer_addr;
cy_socket_t server_handle, client_handle;

/* Size of the peer socket address. */
uint32_t peer_addr_len;

/* Flags to track the LED state. */
bool led_state = CYBSP_LED_STATE_OFF;

/* TCP server task handle. */
extern TaskHandle_t server_task_handle;

/* Ethernet connection manager handle. */
cy_ecm_t ecm_handle = NULL;

/* Flag variable to check if TCP client is connected. */
bool client_connected;

/* User button interrupt configuration structure. */
cy_stc_sysint_t intrCfg =
{
    .intrSrc = ((NvicMux3_IRQn << CY_SYSINT_INTRSRC_MUXIRQ_SHIFT) | CYBSP_USER_BTN_IRQ),
    .intrPriority = 7u
};

/* Ethernet PHY callback structure. */
cy_ecm_phy_callbacks_t phy_callbacks =
{
        .phy_init = cy_eth_phy_init,
        .phy_configure = cy_eth_phy_configure,
        .phy_enable_ext_reg = cy_eth_phy_enable_ext_reg,
        .phy_discover = cy_eth_phy_discover,
        .phy_get_auto_neg_status = cy_eth_phy_get_auto_neg_status,
        .phy_get_link_partner_cap = cy_eth_phy_get_link_partner_cap,
        .phy_get_linkspeed = cy_eth_phy_get_linkspeed,
        .phy_get_linkstatus = cy_eth_phy_get_linkstatus,
        .phy_reset = cy_eth_phy_reset
};

/*******************************************************************************
 * Function Name: tcp_server_task
 *******************************************************************************
 * Summary:
 *  Task used to establish a connection to a TCP client.
 *
 * Parameters:
 *  void *args : Task parameter defined during task creation (unused)
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void tcp_server_task(void *arg)
{
    cy_rslt_t result;

    /* Variable to store number of bytes sent over TCP socket. */
    uint32_t bytes_sent = 0;

    /* Variable to receive LED ON/OFF command from the user button ISR. */
    uint32_t led_state_cmd = LED_OFF_CMD;

    /* Initialize the user button (CYBSP_USER_BTN) and register interrupt on falling edge. */
    Cy_SysInt_Init(&intrCfg, isr_button_press);
    NVIC_ClearPendingIRQ((IRQn_Type)intrCfg.intrSrc);
    NVIC_EnableIRQ((IRQn_Type)intrCfg.intrSrc);

    /* Connect to ethernet network. */
    result = connect_to_ethernet();
    if(result != CY_RSLT_SUCCESS)
    {
        printf("\nFailed to connect to the ethernet network! Error code: 0x%08"PRIx32"\n", (uint32_t)result);
        CY_ASSERT(0);
    }

    /* Initialize secure socket library. */
    result = cy_socket_init();
    if (result != CY_RSLT_SUCCESS)
    {
        printf("Secure Socket initialization failed! Error code: 0x%08"PRIx32"\n", (uint32_t)result);
        CY_ASSERT(0);
    }
    printf("Secure Socket initialized.\n");

    /* Create TCP server socket. */
    result = create_tcp_server_socket();
    if (result != CY_RSLT_SUCCESS)
    {
        printf("Failed to create socket! Error code: 0x%08"PRIx32"\n", (uint32_t)result);
        CY_ASSERT(0);
    }

    /* Start listening on the TCP server socket. */
    result = cy_socket_listen(server_handle, TCP_SERVER_MAX_PENDING_CONNECTIONS);
    if (result != CY_RSLT_SUCCESS)
    {
        cy_socket_delete(server_handle);
        printf("cy_socket_listen returned error. Error code: 0x%08"PRIx32"\n", (uint32_t)result);
        CY_ASSERT(0);
    }
    else
    {
        printf("===============================================================\n");
        printf("Listening for incoming TCP client connection on Port: %d\n",
                tcp_server_addr.port);
    }

    for(;;)
    {
        /* Wait till user button is pressed to send LED ON/OFF command to TCP client. */
        xTaskNotifyWait(0, 0, &led_state_cmd, portMAX_DELAY);

        /* Wait till the debounce period of the user button. */
        vTaskDelay(DEBOUNCE_DELAY_MS/portTICK_PERIOD_MS);

        /* Send LED ON/OFF command to TCP client if there is an active
         * TCP client connection.
         */
        if(client_connected)
        {
            /* Send the command to TCP client. */
            result = cy_socket_send(client_handle, &led_state_cmd, TCP_LED_CMD_LEN,
                                    CY_SOCKET_FLAGS_NONE, &bytes_sent);
            if(result == CY_RSLT_SUCCESS)
            {
                if(led_state_cmd == LED_ON_CMD)
                {
                    printf("LED ON command sent to TCP client\n");
                }
                else
                {
                    printf("LED OFF command sent to TCP client\n");
                }
            }
            else
            {
                printf("Failed to send command to client. Error code: 0x%08"PRIx32"\n", (uint32_t)result);
                if(result == CY_RSLT_MODULE_SECURE_SOCKETS_CLOSED)
                {
                    /* Disconnect the socket. */
                    cy_socket_disconnect(client_handle, 0);
                    /* Delete the socket. */
                    cy_socket_delete(client_handle);
                }
            }
        }
    }
}

/*******************************************************************************
 * Function Name: connect_to_ethernet
 *******************************************************************************
 * Summary:
 *  Connects to ethernet, retries up to a
 *  configured number of times until the connection succeeds.
 *
 * Parameters:
 *  none
 *
 * Return:
 *  cy_rslt_t : Result of the ethernet connection attempt
 *
 *******************************************************************************/
cy_rslt_t connect_to_ethernet(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    uint8_t retry_count = 0;

    /* Variables used by ethernet connection manager.*/
    cy_ecm_ip_address_t ip_addr;

    /* Initialize ethernet connection manager. */
    result = cy_ecm_init();
    if (result != CY_RSLT_SUCCESS)
    {
        printf("Ethernet connection manager initialization failed! Error code: 0x%08"PRIx32"\n", (uint32_t)result);
        CY_ASSERT(0);
    }
    else
    {
        printf("Ethernet connection manager initialized.\n");
    }

    /* Initialize the Ethernet Interface and PHY driver */
    result =  cy_ecm_ethif_init(INTERFACE_ID, &phy_callbacks, &ecm_handle);
    if (result != CY_RSLT_SUCCESS)
    {
        printf("Ethernet interface initialization failed! Error code: 0x%08"PRIx32"\n", (uint32_t)result);
        CY_ASSERT(0);
    }

    /* Establish a connection to the ethernet network */
    for(;;)
    {
        /* Connect to the ethernet network with the dynamically allocated IP address by DHCP */
        result = cy_ecm_connect(ecm_handle, NULL, &ip_addr);
        if(result != CY_RSLT_SUCCESS)
        {
            retry_count++;
            if (retry_count >= MAX_ETH_RETRY_COUNT)
            {
                printf("Exceeded max ethernet connection attempts\n");
                return result;
            }
            printf("Connection to ethernet network failed. Retrying...\n");
            continue;
        }
        else
        {
            printf("Successfully connected to ethernet network.\n");
            printf("IP Address Assigned: %d.%d.%d.%d\n", (uint8)ip_addr.ip.v4,(uint8)(ip_addr.ip.v4 >> 8), (uint8)(ip_addr.ip.v4 >> 16),
                    (uint8)(ip_addr.ip.v4 >> 24));

            /* IP address and TCP port number of the TCP server */
            tcp_server_addr.ip_address.ip.v4 = ip_addr.ip.v4;
            tcp_server_addr.ip_address.version = CY_SOCKET_IP_VER_V4;
            tcp_server_addr.port = TCP_SERVER_PORT;

            break;
        }
    }
    return result;
}

/*******************************************************************************
 * Function Name: create_tcp_server_socket
 *******************************************************************************
 * Summary:
 *  Function to create a socket and set the socket options
 *
 * Parameters:
 *  none
 *
 * Return:
 *  cy_rslt_t : Result of the socket creation and configuration
 *
 *******************************************************************************/
static cy_rslt_t create_tcp_server_socket(void)
{
    cy_rslt_t result;
    /* TCP socket receive timeout period. */
    uint32_t tcp_recv_timeout = TCP_SERVER_RECV_TIMEOUT_MS;

    /* Variables used to set socket options. */
    cy_socket_opt_callback_t tcp_receive_option;
    cy_socket_opt_callback_t tcp_connection_option;
    cy_socket_opt_callback_t tcp_disconnection_option;

    /* Create a TCP socket */
    result = cy_socket_create(CY_SOCKET_DOMAIN_AF_INET, CY_SOCKET_TYPE_STREAM,
                              CY_SOCKET_IPPROTO_TCP, &server_handle);
    if(result != CY_RSLT_SUCCESS)
    {
        printf("Failed to create socket! Error code: 0x%08"PRIx32"\n", (uint32_t)result);
        return result;
    }

    /* Set the TCP socket receive timeout period. */
    result = cy_socket_setsockopt(server_handle, CY_SOCKET_SOL_SOCKET,
                                 CY_SOCKET_SO_RCVTIMEO, &tcp_recv_timeout,
                                 sizeof(tcp_recv_timeout));
    if(result != CY_RSLT_SUCCESS)
    {
        printf("Set socket option: CY_SOCKET_SO_RCVTIMEO failed\n");
        return result;
    }

    /* Register the callback function to handle connection request from a TCP client. */
    tcp_connection_option.callback = tcp_connection_handler;
    tcp_connection_option.arg = NULL;

    result = cy_socket_setsockopt(server_handle, CY_SOCKET_SOL_SOCKET,
                                  CY_SOCKET_SO_CONNECT_REQUEST_CALLBACK,
                                  &tcp_connection_option, sizeof(cy_socket_opt_callback_t));
    if(result != CY_RSLT_SUCCESS)
    {
        printf("Set socket option: CY_SOCKET_SO_CONNECT_REQUEST_CALLBACK failed\n");
        return result;
    }

    /* Register the callback function to handle messages received from a TCP client. */
    tcp_receive_option.callback = tcp_receive_msg_handler;
    tcp_receive_option.arg = NULL;

    result = cy_socket_setsockopt(server_handle, CY_SOCKET_SOL_SOCKET,
                                  CY_SOCKET_SO_RECEIVE_CALLBACK,
                                  &tcp_receive_option, sizeof(cy_socket_opt_callback_t));
    if(result != CY_RSLT_SUCCESS)
    {
        printf("Set socket option: CY_SOCKET_SO_RECEIVE_CALLBACK failed\n");
        return result;
    }

    /* Register the callback function to handle disconnection. */
    tcp_disconnection_option.callback = tcp_disconnection_handler;
    tcp_disconnection_option.arg = NULL;

    result = cy_socket_setsockopt(server_handle, CY_SOCKET_SOL_SOCKET,
                                  CY_SOCKET_SO_DISCONNECT_CALLBACK,
                                  &tcp_disconnection_option, sizeof(cy_socket_opt_callback_t));
    if(result != CY_RSLT_SUCCESS)
    {
        printf("Set socket option: CY_SOCKET_SO_DISCONNECT_CALLBACK failed\n");
        return result;
    }

    /* Bind the TCP socket created to Server IP address and to TCP port. */
    result = cy_socket_bind(server_handle, &tcp_server_addr, sizeof(tcp_server_addr));
    if(result != CY_RSLT_SUCCESS)
    {
        printf("Failed to bind to socket! Error code: 0x%08"PRIx32"\n", (uint32_t)result);
    }

    return result;
}

/*******************************************************************************
 * Function Name: tcp_connection_handler
 *******************************************************************************
 * Summary:
 *  Callback function to handle incoming TCP client connection.
 *
 * Parameters:
 * cy_socket_t socket_handle: Connection handle for the TCP server socket
 *  void *args : Parameter passed on to the function (unused)
 *
 * Return:
 *  cy_result result: Result of the operation
 *
 *******************************************************************************/
static cy_rslt_t tcp_connection_handler(cy_socket_t socket_handle, void *arg)
{
    cy_rslt_t result;
    char ip_addr_ptr[IP_ADDR_BUFFER_SIZE];

    /* TCP keep alive parameters. */
    int keep_alive = 1;
    uint32_t keep_alive_interval = TCP_KEEP_ALIVE_INTERVAL_MS;
    uint32_t keep_alive_count    = TCP_KEEP_ALIVE_RETRY_COUNT;
    uint32_t keep_alive_idle_time = TCP_KEEP_ALIVE_IDLE_TIME_MS;

    /* Accept new incoming connection from a TCP client.*/
    result = cy_socket_accept(socket_handle, &peer_addr, &peer_addr_len,
                              &client_handle);
    if(result == CY_RSLT_SUCCESS)
    {
        printf("Incoming TCP connection accepted: ");

        cy_nw_ntoa((cy_nw_ip_address_t *)&peer_addr.ip_address, ip_addr_ptr);
        printf("IP Address : %s\n\n", ip_addr_ptr);
        printf("Press the user button to send LED ON/OFF command to the TCP client\n");

        /* Set the TCP keep alive interval. */
        result = cy_socket_setsockopt(client_handle, CY_SOCKET_SOL_TCP,
                                      CY_SOCKET_SO_TCP_KEEPALIVE_INTERVAL,
                                      &keep_alive_interval, sizeof(keep_alive_interval));
        if(result != CY_RSLT_SUCCESS)
        {
            printf("Set socket option: CY_SOCKET_SO_TCP_KEEPALIVE_INTERVAL failed\n");
            return result;
        }

        /* Set the retry count for TCP keep alive packet. */
        result = cy_socket_setsockopt(client_handle, CY_SOCKET_SOL_TCP,
                                      CY_SOCKET_SO_TCP_KEEPALIVE_COUNT,
                                      &keep_alive_count, sizeof(keep_alive_count));
        if(result != CY_RSLT_SUCCESS)
        {
            printf("Set socket option: CY_SOCKET_SO_TCP_KEEPALIVE_COUNT failed\n");
            return result;
        }

        /* Set the network idle time before sending the TCP keep alive packet. */
        result = cy_socket_setsockopt(client_handle, CY_SOCKET_SOL_TCP,
                                      CY_SOCKET_SO_TCP_KEEPALIVE_IDLE_TIME,
                                      &keep_alive_idle_time, sizeof(keep_alive_idle_time));
        if(result != CY_RSLT_SUCCESS)
        {
            printf("Set socket option: CY_SOCKET_SO_TCP_KEEPALIVE_IDLE_TIME failed\n");
            return result;
        }

        /* Enable TCP keep alive. */
        result = cy_socket_setsockopt(client_handle, CY_SOCKET_SOL_SOCKET,
                                      CY_SOCKET_SO_TCP_KEEPALIVE_ENABLE,
                                      &keep_alive, sizeof(keep_alive));
        if(result != CY_RSLT_SUCCESS)
        {
            printf("Set socket option: CY_SOCKET_SO_TCP_KEEPALIVE_ENABLE failed\n");
            return result;
        }

        /* Set the client connection flag as true. */
        client_connected = true;
    }
    else
    {
        printf("Failed to accept incoming client connection. Error code: 0x%08"PRIx32"\n", (uint32_t)result);
        printf("===============================================================\n");
        printf("Listening for incoming TCP client connection on Port: %d\n",
                tcp_server_addr.port);
    }

    return result;
}

/*******************************************************************************
 * Function Name: tcp_receive_msg_handler
 *******************************************************************************
 * Summary:
 *  Callback function to handle incoming TCP client messages.
 *
 * Parameters:
 *  cy_socket_t socket_handle: Connection handle for the TCP client socket
 *  void *args : Parameter passed on to the function (unused)
 *
 * Return:
 *  cy_rslt_t result: Result of the operation
 *
 *******************************************************************************/
static cy_rslt_t tcp_receive_msg_handler(cy_socket_t socket_handle, void *arg)
{
    char message_buffer[MAX_TCP_RECV_BUFFER_SIZE];
    cy_rslt_t result;

    /* Variable to store number of bytes received from TCP client. */
    uint32_t bytes_received = 0;
    result = cy_socket_recv(socket_handle, message_buffer, MAX_TCP_RECV_BUFFER_SIZE,
                            CY_SOCKET_FLAGS_NONE, &bytes_received);

    if(result == CY_RSLT_SUCCESS)
    {
        /* Terminate the received string with '\0'. */
        message_buffer[bytes_received] = '\0';
        printf("\r\nAcknowledgement from TCP Client: %s\n", message_buffer);

        /* Set the LED state based on the acknowledgement received from the TCP client. */
        if(strcmp(message_buffer, "LED ON ACK") == 0)
        {
            led_state = CYBSP_LED_STATE_ON;
        }
        else
        {
            led_state = CYBSP_LED_STATE_OFF;
        }
    }
    else
    {
        printf("Failed to receive acknowledgement from the TCP client. Error: 0x%08"PRIx32"\n",
              (uint32_t)result);
        if(result == CY_RSLT_MODULE_SECURE_SOCKETS_CLOSED)
        {
            /* Disconnect the socket. */
            cy_socket_disconnect(socket_handle, 0);
            /* Delete the socket. */
            cy_socket_delete(socket_handle);
        }
    }

    printf("===============================================================\n");
    printf("Press the user button to send LED ON/OFF command to the TCP client\n");

    return result;
}

/*******************************************************************************
 * Function Name: tcp_disconnection_handler
 *******************************************************************************
 * Summary:
 *  Callback function to handle TCP client disconnection event.
 *
 * Parameters:
 * cy_socket_t socket_handle: Connection handle for the TCP client socket
 *  void *args : Parameter passed on to the function (unused)
 *
 * Return:
 *  cy_result result: Result of the operation
 *
 *******************************************************************************/
static cy_rslt_t tcp_disconnection_handler(cy_socket_t socket_handle, void *arg)
{
    cy_rslt_t result;

    /* Disconnect the TCP client. */
    result = cy_socket_disconnect(socket_handle, 0);
    /* Delete the socket. */
    cy_socket_delete(socket_handle);

    /* Set the client connection flag as false. */
    client_connected = false;
    printf("TCP Client disconnected! Please reconnect the TCP Client\n");
    printf("===============================================================\n");
    printf("Listening for incoming TCP client connection on Port: %d\n",
            tcp_server_addr.port);

    /* Set the LED state to OFF when the TCP client disconnects. */
    led_state = CYBSP_LED_STATE_OFF;

    return result;
}

/*******************************************************************************
 * Function Name: isr_button_press
 *******************************************************************************
 *
 * Summary:
 *  GPIO interrupt service routine. This function detects button presses and
 *  sets the command to be sent to TCP client.
 *
 * Parameters:
 *  void *callback_arg : pointer to the variable passed to the ISR
 *  cyhal_gpio_event_t event : GPIO event type
 *
 * Return:
 *  None
 *
 *******************************************************************************/
static void isr_button_press(void)
{
    /* Clears the triggered pin interrupt */
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN_PORT, CYBSP_USER_BTN_NUM);

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* Variable to hold the LED ON/OFF command to be sent to the TCP client. */
    uint32_t led_state_cmd;

    /* Set the command to be sent to TCP client. */
    if(led_state == CYBSP_LED_STATE_ON)
    {
        led_state_cmd = LED_OFF_CMD;
    }
    else
    {
        led_state_cmd = LED_ON_CMD;
    }

    /* Set the flag to send command to TCP client. */
    xTaskNotifyFromISR(server_task_handle, led_state_cmd,
                       eSetValueWithoutOverwrite, &xHigherPriorityTaskWoken);

    /* Force a context switch if xHigherPriorityTaskWoken is now set to pdTRUE. */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* [] END OF FILE */
