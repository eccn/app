////////////////////////////////////////////////////////////////////////////////
// File Name          : Iperf_Task.c
// Author             : Claudius M Zingerli, czingerl@zeuz.ch
// Version            : 0.0.1 - Initial release
// Date               : 2013/06/16
// Subversion Info    : $Id: Iperf_Task.c 1424 2014-03-09 14:09:55Z czingerl $
//                      $HeadURL$
// Description        : Iperf server thread
//                      - This is a trivial implementation of Iperf, that
//                        silently discards any data received via TCP on
//                        port 5001. Single threaded only
// License            : GPL v3
// Copyright          : Claudius M Zingerli
////////////////////////////////////////////////////////////////////////////////

// Includes --------------------------------------------------------------------
//#include <stdint.h> //uint8_t and friends
#include "platform_config.h"
#include "stm32_periphlib.h"

#include <freertos/FreeRTOS.h>

#include <lwip/api.h> //netconn API

#ifndef mini_printf_DEFINED
#if USE_UART_TERMINAL
	int mini_printf(const char *p_format, ...);
	int mini_snprintf(char *p_out_str, size_t p_out_str_size, const char *format, ...);
#else
	#define mini_printf(...)
	#define mini_snprintf(...)
#endif
#define mini_printf_DEFINED
#endif
// Private typedef -------------------------------------------------------------

//First data sent to server
struct Iperf_client_header
{
	int32_t f_flags;
	int32_t f_numThreads;
	int32_t f_Port;
	int32_t f_bufferlen;
	int32_t f_WinBand;
	int32_t f_Amount;
};

//Response from the server to the client header
struct Iperf_server_header
{
	int32_t f_flags;
	int32_t f_total_len1;
	int32_t f_total_len2;
	int32_t f_stop_sec;
	int32_t f_stop_usec;
	int32_t f_error_cnt;
	int32_t f_outorder_cnt;
	int32_t f_datagrams;
	int32_t f_jitter1;
	int32_t f_jitter2;
};

// Private macro ---------------------------------------------------------------
#define IPERF_PORT     5001 //Port to listen
#define IPERF_USE_PBUF    1 //Set to nonzero to use pbuf (faster) instead of netbuf (safer)
#define iperfSHORT_DELAY 50

// Private variables -----------------------------------------------------------
// Private function prototypes -------------------------------------------------
static void vIperf_Process(struct netconn *p_conn);

// Private functions -----------------------------------------------------------

void vIperfTask(void * pvParameters)
{
	struct netconn *conn_listen, *newconn;
	err_t err;
	(void)pvParameters;

	// Create a new TCP connection handle
	if ((conn_listen = netconn_new(NETCONN_TCP)) == NULL)
	{
		vTaskDelete(NULL);
	}

	do
	{
		// Bind to port 5001 and any IP address
		if ((err = netconn_bind(conn_listen, NULL, IPERF_PORT)) != ERR_OK)
		{
			break;
		}

		// Put the connection into LISTEN state
		if ((err = netconn_listen(conn_listen)) != ERR_OK)
		{
			break;
		}

		do
		{
			// Block and wait for an incoming connection
			if ((err = netconn_accept(conn_listen, &newconn)) == ERR_OK)
			{
				mini_printf("Iperf: started\n");
				// serve connection
				vIperf_Process(newconn); //Handles request and closes connection
			}
		} while (err == ERR_OK);
	} while (0); //Loop just once
	//netconn_close(conn_listen); //TODO: Find out if this call is really needed
	netconn_delete(conn_listen); //Also closes connection
	vTaskDelete(NULL);
}


static void vIperf_Process(struct netconn *p_conn)
{
	err_t err;
	do
	{
#if IPERF_USE_PBUF
		struct pbuf *inbuf=NULL;

		//TODO: receive client header and do something useful with it

		while ((err = netconn_recv_tcp_pbuf(p_conn,&inbuf)) == ERR_OK)
		{
//			vTaskDelay(iperfSHORT_DELAY);
			if (inbuf != NULL)
			{
				pbuf_free(inbuf);
				inbuf=NULL;
			}
//			vTaskDelay(iperfSHORT_DELAY);
		}
		if (inbuf != NULL)
		{
			pbuf_free(inbuf);
		}
#else
		struct netbuf *inbuf;
		void* buf;
		u16_t buflen;

		//Receive header
//		if ((err = netconn_recv(p_conn,&inbuf)) != ERR_OK)
//		{
//			netbuf_delete(inbuf);
//			break;
//		}
//		netbuf_data(inbuf, (void**) &buf, &buflen);
		//TODO: Version check and return server header

		//Dump the rest
		while ((err = netconn_recv(p_conn,&inbuf)) == ERR_OK)
		{
			mini_printf(".");
			do
			{
				netbuf_data(inbuf, &buf, &buflen);
				if ((err = netconn_write(p_conn, buf, buflen, NETCONN_COPY)) != ERR_OK)
				{
					mini_printf("netconn_write=%d\n",err);
				}
			} while (netbuf_next(inbuf) >= 0);
			netbuf_delete(inbuf); //Does the NULL check itself
		}
#endif
	} while (0);
	switch (err)
	{
	default:
		mini_printf("ERROR: netconn_recv=%d\n",err);
		break;
	case ERR_CLSD:
		mini_printf("Iperf: finished\n");
		break;
	}
	//err=ERR_CLSD -> Remote close
	//err=ERR_MEM  -> Allocation failed
	//err=ERR_ARG  -> Invalid arguments
	//err=ERR_CONN -> Invalid connection (state)
	//err=ERR_TIMEOUT -> No data received withing recv_timeout

	//Send FIN
	//netconn_close_shutdown(p_conn,NETCONN_SHUT_WR);
	netconn_close(p_conn);

	//Loop until client ACKs our FIN
	while (netconn_delete(p_conn) != ERR_OK)
	{
		vTaskDelay(iperfSHORT_DELAY);
	}
}