/**
 * \file            esp_at.c
 * \brief           AT commands parsers
 */

/*
 * Copyright (c) 2017, Tilen MAJERLE
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *  * Neither the name of the author nor the
 *      names of its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * \author          Tilen MAJERLE <tilen@majerle.eu>
 */
#define ESP_INTERNAL
#include "esp.h"
#include "esp_int.h"
#include "esp_parser.h"
#include "esp_ll.h"

typedef struct {
    char data[128];
    uint8_t len;
} esp_recv_t;
static esp_recv_t recv;

#define RECV_ADD(ch)        do { recv.data[recv.len++] = ch; recv.data[recv.len] = 0; } while (0)
#define RECV_RESET()        recv.data[(recv.len = 0)] = 0;
#define RECV_LEN()          recv.len
#define RECV_IDX(index)     recv.data[index]

#define ESP_AT_PORT_SEND_STR(str)       esp.ll.send((const uint8_t *)(str), strlen(str))
#define ESP_AT_PORT_SEND_CHR(str)       esp.ll.send((const uint8_t *)(str), 1)
#define ESP_AT_PORT_SEND(d, l)          esp.ll.send((const uint8_t *)(d), l)
    
uint8_t ipd_buff[1452];

/**
 * \brief           Create 2-characters long hex from byte
 * \param[in]       num: Number to convert to string
 * \param[out]      str: Pointer to string to save result to
 */
static void
byte_to_str(uint8_t num, char* str) {
    sprintf(str, "%02X", (unsigned)num);        /* Currently use sprintf only */
}

/**
 * \brief           Create string from number
 * \param[in]       num: Number to convert to string
 * \param[out]      str: Pointer to string to save result to
 */
static void
number_to_str(uint32_t num, char* str) {
    sprintf(str, "%u", (unsigned)num);          /* Currently use sprintf only */
}

/**
 * \brief           Send IP or MAC address to AT port
 * \param[in]       d: Pointer to IP or MAC address
 * \param[in]       is_ip: Set to 1 when sending IP, or 0 when MAC
 * \param[in]       q: Set to 1 to include start and ending quotes
 */
static void
send_ip_mac(const uint8_t* d, uint8_t is_ip, uint8_t q) {
    uint8_t i, ch;
    char str[4];
    
    if (!d) {
        return;
    }
    if (q) {
        ESP_AT_PORT_SEND_STR("\"");             /* Send starting quote character */
    }
    ch = is_ip ? '.' : ':';                     /* Get delimiter character */
    for (i = 0; i < (is_ip ? 4 : 6); i++) {     /* Process byte by byte */
        if (is_ip) {                            /* In case of IP ... */
            number_to_str(d[i], str);           /* ... go to decimal format ... */
        } else {                                /* ... in case of MAC ... */
            byte_to_str(d[i], str);             /* ... go to HEX format */
        }
        ESP_AT_PORT_SEND_STR(str);              /* Send str */
        if (i < (is_ip ? 4 : 6) - 1) {          /* Check end if characters */
            ESP_AT_PORT_SEND_CHR(&ch);          /* Send character */
        }
    }
    if (q) {
        ESP_AT_PORT_SEND_STR("\"");             /* Send ending quote character */
    }
}

/**
 * \brief           Send string to AT port, either plain or escaped
 * \param[in]       str: Pointer to input string to string
 * \param[in]       e: Value to indicate string send format, escaped (1) or plain (0)
 * \param[in]       q: Value to indicate starting and ending quotes, enabled (1) or disabled (0)
 */
static void
send_string(const char* str, uint8_t e, uint8_t q) {
    char special = '\\';
    if (q) {
        ESP_AT_PORT_SEND_STR("\"");
    }
    if (e) {                                    /* Do we have to escape string? */
        while (*str) {                          /* Go through string */
            if (*str == ',' || *str == '"' || *str == '\\') {   /* Check for special character */    
                ESP_AT_PORT_SEND_CHR(&special); /* Send special character */
            }
            ESP_AT_PORT_SEND_CHR(str);          /* Send character */
            str++;
        }
    } else {
        ESP_AT_PORT_SEND_STR(str);              /* Send plain string */
    }
    if (q) {
        ESP_AT_PORT_SEND_STR("\"");
    }
}

/**
 * \brief           Check if received string includes "_CUR" or "_DEF" as current or default setup
 * \param[in]       str: Pointer to string to test
 * \return          1 if current setting, 0 if default setting
 */
static uint8_t
is_received_current_setting(const char* str) {
    return !strstr(str, "_DEF");                /* In case there is no "_DEF", we have current setting active */
}

/**
 * \brief           Process and send data from device buffer
 * \return          Member of \ref espr_t enumeration
 */
static espr_t
espi_tcpip_process_send_data(void) {
    char str[6];
    str[0] = esp.msg->msg.conn_send.conn->num + '0';
    ESP_AT_PORT_SEND_STR("AT+CIPSEND=");
    ESP_AT_PORT_SEND_CHR(str);
    ESP_AT_PORT_SEND_STR(",");
    esp.msg->msg.conn_send.sent = esp.msg->msg.conn_send.btw > 2048 ? 2048 : esp.msg->msg.conn_send.btw;
    number_to_str(esp.msg->msg.conn_send.sent, str);
    ESP_AT_PORT_SEND_STR(str);
    ESP_AT_PORT_SEND_STR("\r\n");
    return espOK;
}

/**
 * \brief           Process data sent and send remaining
 * \param[in]       sent: Status whether data were sent or not, info received from ESP with "SEND OK" or "SEND FAIL" 
 * \return          1 in case we should stop sending or 0 if we still have data to process
 */
static uint8_t
espi_tcpip_process_data_sent(uint8_t sent) {
    if (sent) {                                 /* Data were successfully sent */
        esp.msg->msg.conn_send.btw -= esp.msg->msg.conn_send.sent;
        *esp.msg->msg.conn_send.bw += esp.msg->msg.conn_send.sent;
        esp.msg->msg.conn_send.data += esp.msg->msg.conn_send.sent;
        esp.msg->msg.conn_send.tries = 0;
    } else {
        esp.msg->msg.conn_send.tries++;         /* Increase number of tries */
        if (esp.msg->msg.conn_send.tries == 3) {    /* In case we reached max number of retransmissions */
            return 1;                           /* Return 1 and indicate error */
        }
    }
    if (esp.msg->msg.conn_send.btw) {           /* Do we still have data to send? */
        espi_tcpip_process_send_data();         /* Send next packet */
        return 0;                               /* We still have data to send */
    }
    return 1;                                   /* Everything was sent, we can stop execution */
}

/**
 * \brief           Send connection callback
 * \note            Before calling function, callback structure must be prepared
 * \param[in]       conn: Pointer to connection to use as callback
 * \return          Member of \ref espr_t enumeration
 */
static espr_t
espi_send_conn_cb(esp_conn_t* conn) {
    if (conn->cb_func) {                        /* Connection custom callback? */
        conn->cb_func(&esp.cb);                 /* Process callback function */
    } else {
        esp.cb_func(&esp.cb);                   /* Process default callback function */
    }
    return espOK;
}

/**
 * \brief           Process received string from ESP 
 * \param[in]       recv: Pointer to \ref esp_rect_t structure with input string
 */
static void
espi_parse_received(esp_recv_t* rcv) {
    uint8_t is_ok = 0, is_error = 0, is_ready = 0;
    
    if (rcv->len == 2 && rcv->data[0] == '\r' && rcv->data[1] == '\n') {
        return;
    }
    //printf("Rcv! AC: %d, s: %s", (int)esp.cmd, (char *)rcv->data);
    
    /**
     * Detect most common responses from device
     */
    is_ok = !strcmp(rcv->data, "OK\r\n");       /* Check if received string is OK */
    if (!is_ok) {
        is_error = !strcmp(rcv->data, "ERROR\r\n") || !strcmp(rcv->data, "FAIL\r\n");   /* Check if received string is error */
        if (!is_error) {
            is_ready = !strcmp(rcv->data, "ready\r\n"); /* Check if received string is ready */
            if (is_ready) {
                esp.status.f.r_rdy = 1;
            }
        } else {
            esp.status.f.r_err = 1;
        }
    } else {
        esp.status.f.r_ok = 1;
    }
    
    /**
     * Read and process statements starting with '+' character
     */
    if (rcv->data[0] == '+') {
        if (!strncmp("+IPD", rcv->data, 4)) {   /* Check received network data */
            espi_parse_ipd(rcv->data + 5);      /* Parse IPD statement and start receiving network data */
        } else if ( (esp.cmd == ESP_CMD_WIFI_CIPSTAMAC_GET && !strncmp(rcv->data, "+CIPSTAMAC", 10)) || 
                    (esp.cmd == ESP_CMD_WIFI_CIPAPMAC_GET && !strncmp(rcv->data, "+CIPAPMAC", 9))) {
            const char* tmp;
            uint8_t mac[6];
            
            if (rcv->data[9] == '_') {          /* Do we have "_CUR" or "_DEF" included? */
                tmp = &rcv->data[14];
            } else if (rcv->data[10] == '_') {
                tmp = &rcv->data[15];
            } else if (rcv->data[9] == ':') {
                tmp = &rcv->data[10];
            } else if (rcv->data[10] == ':') {
                tmp = &rcv->data[11];
            }
            
            espi_parse_mac(&tmp, mac);          /* Save as current MAC address */
            if (is_received_current_setting(rcv->data)) {
                memcpy(esp.cmd == ESP_CMD_WIFI_CIPSTAMAC_GET ? esp.sta.mac : esp.ap.mac, mac, 6);   /* Copy to current setup */
            }
            if (esp.msg->msg.sta_ap_getmac.mac) {
                memcpy(esp.msg->msg.sta_ap_getmac.mac, mac, 6); /* Copy to current setup */
            }
        } else if ( (esp.cmd == ESP_CMD_WIFI_CIPSTA_GET && !strncmp(rcv->data, "+CIPSTA", 7)) ||
                    (esp.cmd == ESP_CMD_WIFI_CIPAP_GET && !strncmp(rcv->data, "+CIPAP", 6))) {
            const char* tmp = NULL;
            uint8_t *a = NULL, *b = NULL;
            uint8_t ip[4], ch;
            esp_ip_mac_t* im;
                        
            im = (esp.cmd == ESP_CMD_WIFI_CIPSTA_GET) ? &esp.sta : &esp.ap; /* Get IP and MAC structure first */
            
            /* We expect "+CIPSTA_CUR:" or "+CIPSTA_DEF:" or "+CIPAP_CUR:" or "+CIPAP_DEF:" or "+CIPSTA:" or "+CIPAP:" ... */
            if (rcv->data[6] == '_') {
                ch = rcv->data[11];
            } else if (rcv->data[7] == '_') {
                ch = rcv->data[12];
            } else if (rcv->data[6] == ':') {
                ch = rcv->data[7];
            } else if (rcv->data[7] == ':') {
                ch = rcv->data[8];
            }
            switch (ch) {
                case 'i': tmp = &rcv->data[10]; a = im->ip; b = esp.msg->msg.sta_ap_getip.ip; break;
                case 'g': tmp = &rcv->data[15]; a = im->gw; b = esp.msg->msg.sta_ap_getip.gw; break;
                case 'n': tmp = &rcv->data[15]; a = im->nm; b = esp.msg->msg.sta_ap_getip.nm; break;
                default: tmp = NULL; a = NULL; b = NULL; break;
            }
            if (tmp) {                          /* Do we have temporary string? */
                if (rcv->data[6] == '_' || rcv->data[7] == '_') {   /* Do we have "_CUR" or "_DEF" included? */
                    tmp += 4;                   /* Skip it */
                }
                if (*tmp == ':') {
                    tmp++;
                }
                espi_parse_ip(&tmp, ip);        /* Parse IP address */
                if (is_received_current_setting(rcv->data)) {
                    memcpy(a, ip, 4);           /* Copy to current setup */
                }
                if (b) {
                    memcpy(b, ip, 4);           /* Copy to user variable */
                }
            }
        } else if (esp.cmd == ESP_CMD_WIFI_CWLAP && !strncmp(rcv->data, "+CWLAP", 6)) {
            espi_parse_cwlap(rcv->data, esp.msg);   /* Parse CWLAP entry */
        }
    }
    
    /**
     * Start processing received data
     */
    if (esp.cmd == ESP_CMD_RESET && is_ok) {    /* Check for reset command */
        is_ok = 0;                              /* We must wait for "ready", not only "OK" */
    } else if (esp.cmd == ESP_CMD_TCPIP_CIPSTATUS) {
        if (!strncmp(rcv->data, "+CIPSTATUS", 10)) {
            espi_parse_cipstatus(rcv->data + 11);   /* Parse CIPSTATUS response */
        } else if (is_ok) {
            uint8_t i;
            for (i = 0; i < ESP_MAX_CONNS; i++) {   /* Set current connection statuses */
                esp.conns[i].status.f.active = !!(esp.active_conns & (1 << i));
            }
        }
    } else if (esp.cmd == ESP_CMD_TCPIP_CIPSEND) {
        if (is_ok) {                            /* Check for OK and clear as we have to check for "> " statement */
            is_ok = 0;                          /* Do not reach on OK */
        }
        if (!strncmp("SEND OK", rcv->data, 7)) {    /* Data were sent successfully */
            is_ok = espi_tcpip_process_data_sent(1);    /* Process as data were sent */
            if (is_ok) {
                esp.cb.type = ESP_CB_DATA_SENT; /* Data were fully sent */
                esp.cb.cb.conn_data_sent.conn = esp.msg->msg.conn_send.conn;
                espi_send_conn_cb(esp.msg->msg.conn_send.conn); /* Send connection callback */
            }
        } else if (is_error || !strncmp("SEND FAIL", rcv->data, 9)) {
            is_error = espi_tcpip_process_data_sent(0); /* Data were not sent due to SEND FAIL or command didn't even start */
            if (is_error) {
                esp.cb.type = ESP_CB_DATA_SEND_ERR; /* Error sending data */
                esp.cb.cb.conn_data_send_err.conn = esp.msg->msg.conn_send.conn;
                espi_send_conn_cb(esp.ipd.conn);/* Send connection callback */
            }
        }
    } else if (esp.cmd == ESP_CMD_UART) {       /* In case of UART command */
        if (is_ok) {                            /* We have valid OK result */
            esp_ll_init(&esp.ll, esp.msg->msg.uart.baudrate);   /* Set new baudrate */
        }
    }
    
    /**
     * Check if connection is just active or just closed
     */
    if (!strncmp(",CONNECT", &rcv->data[1], 8)) {
        const char* tmp = rcv->data;
        uint8_t num = espi_parse_number(&tmp);
        if (num < ESP_MAX_CONNS) {
            esp_conn_t* conn = &esp.conns[num]; /* Parse received data */
            conn->num = num;                    /* Set connection number */
            conn->status.f.active = 1;          /* Connection just active */
            if (esp.cmd == ESP_CMD_TCPIP_CIPSTART && num == esp.msg->msg.conn_start.num) {  /* Did we start connection on our own? */
                conn->status.f.client = 1;      /* Go to client mode */
                conn->cb_func = esp.msg->msg.conn_start.cb_func;    /* Set callback function */
            } else {                            /* Server connection start */
                conn->status.f.client = 0;
                conn->cb_func = esp.cb_server;  /* Set server default callback */
            }
            
            esp.cb.type = ESP_CB_CONN_ACTIVE;   /* Connection just active */
            esp.cb.cb.conn_active_closed.conn = conn;   /* Set connection */
            espi_send_conn_cb(conn);            /* Send event */
        }
    } else if (!strncmp(",CLOSED", &rcv->data[1], 7)) {
        const char* tmp = rcv->data;
        uint8_t num = espi_parse_number(&tmp);
        if (num < ESP_MAX_CONNS) {
            esp_conn_t* conn = &esp.conns[num]; /* Parse received data */
            conn->num = num;                    /* Set connection number */
            if (conn->status.f.active) {        /* Is connection actually active? */
                conn->status.f.active = 0;      /* Connection was just closed */
                
                esp.cb.type = ESP_CB_CONN_CLOSED;   /* Connection just active */
                esp.cb.cb.conn_active_closed.conn = conn;   /* Set connection */
                espi_send_conn_cb(conn);        /* Send event */
                
                /**
                 * In case we received x,CLOSED on connection we are currently sending data,
                 * terminate sending of connection with failure
                 */
                if (esp.cmd == ESP_CMD_TCPIP_CIPSEND) {
                    if (esp.msg->msg.conn_send.conn == conn) {
                        is_error = 1;           /* Set as error to stop processing or waiting for connection */
                    }
                }
            }
        }
    } else if (is_error && esp.cmd == ESP_CMD_TCPIP_CIPSTART) {
        esp_conn_t* conn = &esp.conns[esp.msg->msg.conn_start.num];
        /* TODO: Get last connection number we used to start the connection */
        esp.cb.type = ESP_CB_CONN_ERROR;        /* Connection just active */
        esp.cb.cb.conn_error.host = esp.msg->msg.conn_start.host;
        esp.cb.cb.conn_error.port = esp.msg->msg.conn_start.port;
        esp.cb.cb.conn_error.type = esp.msg->msg.conn_start.type;
        espi_send_conn_cb(conn);                /* Send event */
    }
    
    /**
     * In case of any of these events, simply release semaphore
     * and proceed with next command
     */
    if (is_ok || is_error || is_ready) {
        if (esp.msg) {                          /* Do we have active message? */
            if (is_ok || is_ready) {            /* Check ready or ok status */
                esp.msg->res = espOK;
            } else {                            /* Or error status */
                esp.msg->res = espERR;
            }
        }
        esp.cmd = ESP_CMD_IDLE;
        esp_sys_sem_release(&esp.sem_sync);     /* Release semaphore */
    }
}

/**
 * \brief           Process input data received from ESP device
 */
espr_t
espi_process(void) {
    uint8_t ch;
    size_t len;
    static uint8_t ch_prev1, ch_prev2;
    
    while (esp_buff_read(&esp.buff, &ch, 1)) {  /* Read entire set of characters from buffer */
        if (esp.ipd.read) {                     /* Do we have to read incoming IPD data? */
            esp.ipd.buff[esp.ipd.buff_ptr] = ch;/* Save data character */
            esp.ipd.buff_ptr++;
            esp.ipd.rem_len--;
            
            /**
             * Try to read more data directly from buffer
             */
            len = esp_buff_read(&esp.buff, &esp.ipd.buff[esp.ipd.buff_ptr], ESP_MIN(esp.ipd.rem_len, esp.ipd.buff_len - esp.ipd.buff_ptr));
            if (len) {                          /* Check if we did read anything more */
                esp.ipd.buff_ptr += len;
                esp.ipd.rem_len -= len;
            }
            
            /**
             * Did we reach end of buffer or no more data?
             */
            if (!esp.ipd.rem_len || esp.ipd.buff_ptr == esp.ipd.buff_len) {
                /**
                 * Call user callback function with received data
                 */
                esp.cb.type = ESP_CB_DATA_RECV; /* We have received data */
                esp.cb.cb.conn_data_recv.buff = esp.ipd.buff;
                esp.cb.cb.conn_data_recv.len = esp.ipd.buff_ptr;
                esp.cb.cb.conn_data_recv.conn = esp.ipd.conn;
                espi_send_conn_cb(esp.ipd.conn);/* Send connection callback */
                
                if (!esp.ipd.rem_len) {         /* Check if we read everything */
                    esp.ipd.read = 0;           /* Stop reading data */
                }
                esp.ipd.buff_ptr = 0;           /* Reset input buffer pointer */
            }
        } else if (ESP_ISVALIDASCII(ch)) {      /* Check for ASCII characters only */
            switch (ch) {
                case '\n':
                    RECV_ADD(ch);               /* Add character to input buffer */
                    espi_parse_received(&recv); /* Parse received string */
                    RECV_RESET();               /* Reset received string */
                    break;
                default:
                    RECV_ADD(ch);               /* Any ASCII valid character */
                    break;
            }
            
            /**
             * If we are waiting for "\n> " sequence when CIPSEND command is active
             */
            if (esp.cmd == ESP_CMD_TCPIP_CIPSEND) {
                if (ch_prev2 == '\n' && ch_prev1 == '>' && ch == ' ') {
                    RECV_RESET();               /* Reset received object */
                    
                    /**
                     * Now actually send the data prepared before
                     */
                    ESP_AT_PORT_SEND(esp.msg->msg.conn_send.data, esp.msg->msg.conn_send.sent);
                }
            }
            
            /**
             * Check if "+IPD" statement is in array and now we received colon,
             * indicating end of +IPD and start of actual data
             */
            if (ch == ':' && RECV_LEN() > 4 && RECV_IDX(0) == '+' && !strncmp(recv.data, "+IPD", 4)) {
                espi_parse_received(&recv);     /* Parse received string */
                esp.ipd.buff = ipd_buff;
                esp.ipd.buff_len = sizeof(ipd_buff);
                esp.ipd.buff_ptr = 0;
                RECV_RESET();                   /* Reset received buffer */
            }
        } else {
            RECV_RESET();                       /* Invalidate received data */
        }
        
        ch_prev2 = ch_prev1;                    /* Save previous character to previous previous */
        ch_prev1 = ch;                          /* Char current to previous */
    }
    return espOK;
}

/**
 * \brief           Send AT command to reset device
 * \param[in]       msg: Pointer to \ref esp_msg_t with data
 * \return          Member of \ref espr_t enumeration
 */
espr_t
espi_reset(esp_msg_t* msg) {
    ESP_AT_PORT_SEND_STR("AT+RST\r\n");         /* Send command to AT port */
    return espOK;
}

/**
 * \brief           Send AT command to set AT port baudrate
 * \param[in]       msg: Pointer to \ref esp_msg_t with data
 * \return          Member of \ref espr_t enumeration
 */
espr_t
espi_basic_uart(esp_msg_t* msg) {
    char str[8];
    number_to_str(msg->msg.uart.baudrate, str); /* Get string from number */
    ESP_AT_PORT_SEND_STR("AT+UART_CUR=");
    ESP_AT_PORT_SEND_STR(str);
    ESP_AT_PORT_SEND_STR(",8,1,0,0");
    ESP_AT_PORT_SEND_STR("\r\n");
    
    return espOK;
}

/**
 * \brief           Miscellaneous AT commands in TCP/IP part of commands
 * \param[in]       msg: Pointer to \ref esp_msg_t with data
 * \return          Member of \ref espr_t enumeration
 */
espr_t
espi_tcpip_misc(esp_msg_t* msg) {
    if (msg->cmd == ESP_CMD_TCPIP_CIPDINFO) {
        ESP_AT_PORT_SEND_STR("AT+CIPDINFO=");
        if (msg->msg.tcpip_dinfo.info) {
            ESP_AT_PORT_SEND_STR("1");
        } else {
            ESP_AT_PORT_SEND_STR("0");
        }
        ESP_AT_PORT_SEND_STR("\r\n");
    } else if (msg->cmd == ESP_CMD_TCPIP_CIPMUX) {
        ESP_AT_PORT_SEND_STR("AT+CIPMUX=");
        if (msg->msg.tcpip_mux.mux) {
            ESP_AT_PORT_SEND_STR("1");
        } else {
            ESP_AT_PORT_SEND_STR("0");
        }
        ESP_AT_PORT_SEND_STR("\r\n");
    }
    return espOK;
}

/**
 * \brief           Send AT command to set WiFi mode
 * \param[in]       msg: Pointer to \ref esp_msg_t with data
 * \return          Member of \ref espr_t enumeration
 */
espr_t
espi_set_wifi_mode(esp_msg_t* msg) {
    char c = (char)msg->msg.wifi_mode.mode + '0';   /* Go to ASCII */
    
    ESP_AT_PORT_SEND_STR("AT+CWMODE=");
    ESP_AT_PORT_SEND_CHR(&c);
    ESP_AT_PORT_SEND_STR("\r\n");
    return espOK;
}

/**
 * \brief           Send AT command to set server
 * \param[in]       msg: Pointer to \ref esp_msg_t with data
 * \return          Member of \ref espr_t enumeration
 */
espr_t
espi_tcpip_server(esp_msg_t* msg) {
    char str[6];
    
    ESP_AT_PORT_SEND_STR("AT+CIPSERVER=");
    if (msg->msg.tcpip_server.port) {           /* Do we have valid port? */
        number_to_str(msg->msg.tcpip_server.port, str);
        ESP_AT_PORT_SEND_STR("1,");
        ESP_AT_PORT_SEND_STR(str);
    } else {                                    /* Disable server */
        ESP_AT_PORT_SEND_STR("0");
    }
    ESP_AT_PORT_SEND_STR("\r\n");
    return espOK;
}

/**
 * \brief           Send AT command to get connections status
 * \param[in]       msg: Pointer to \ref esp_msg_t with data
 * \return          Member of \ref espr_t enumeration
 */
espr_t
espi_get_conns_status(esp_msg_t* msg) {
    esp.active_conns_last = esp.active_conns;   /* Save as last status */
    esp.active_conns = 0;                       /* Reset new status before parsing starts */
    ESP_AT_PORT_SEND_STR("AT+CIPSTATUS\r\n");   /* Send command to AT port */
    return espOK;
}

/**
 * \brief           Connects to or disconnects from access point
 * \param[in]       msg: Pointer to \ref esp_msg_t with data
 * \return          Member of \ref espr_t enumeration
 */
espr_t
espi_sta_join_quit(esp_msg_t* msg) {
    if (msg->cmd == ESP_CMD_WIFI_CWJAP) {       /* In case we want to join */
        ESP_AT_PORT_SEND_STR("AT+CWJAP_");      /* Send command to AT port */
        if (msg->def) {
            ESP_AT_PORT_SEND_STR("DEF=\"");
        } else {
            ESP_AT_PORT_SEND_STR("CUR=\"");
        }
        send_string(msg->msg.sta_join.name, 1, 0);
        ESP_AT_PORT_SEND_STR("\",\"");
        send_string(msg->msg.sta_join.pass, 1, 0);
        ESP_AT_PORT_SEND_STR("\"");
        if (msg->msg.sta_join.mac) {
            uint8_t ch = ':', i;
            char s[3];
            ESP_AT_PORT_SEND_STR(",\"");
            for (i = 0; i < 6; i++) {
                byte_to_str(msg->msg.sta_join.mac[i], s);
                ESP_AT_PORT_SEND_STR(s);
                if (i != 5) {
                    ESP_AT_PORT_SEND_CHR(&ch);
                }
            }
            ESP_AT_PORT_SEND_STR("\"");
        }
        ESP_AT_PORT_SEND_STR("\r\n");
    } else if (msg->cmd == ESP_CMD_WIFI_CWQAP) {/* In case we want to quit */
        ESP_AT_PORT_SEND_STR("AT+CWQAP\r\n");   /* Quit from access point */ 
    }

    return espOK;
}

/**
 * \brief           Deal with CIPSTA and CIPAP commands for IP and MAC addresses
 * \param[in]       msg: Pointer to \ref esp_msg_t with data
 * \return          Member of \ref espr_t enumeration
 */
espr_t
espi_cip_sta_ap_cmd(esp_msg_t* msg) {
    switch (msg->cmd) {
        case ESP_CMD_WIFI_CIPSTA_GET:           /* Get station IP address */
        case ESP_CMD_WIFI_CIPAP_GET: {          /* Get access point IP address */
            ESP_AT_PORT_SEND_STR("AT+CIP");
            if (msg->cmd == ESP_CMD_WIFI_CIPSTA_GET) {
                ESP_AT_PORT_SEND_STR("STA");
            } else {
                ESP_AT_PORT_SEND_STR("AP");
            }
            if (msg->msg.sta_ap_getip.def) {
                ESP_AT_PORT_SEND_STR("_DEF");
            } else {
                ESP_AT_PORT_SEND_STR("_CUR");
            }
            ESP_AT_PORT_SEND_STR("?\r\n");
            break;
        }
        case ESP_CMD_WIFI_CIPSTAMAC_GET:        /* Get station MAC address */
        case ESP_CMD_WIFI_CIPAPMAC_GET: {       /* Get access point MAC address */
            ESP_AT_PORT_SEND_STR("AT+CIP");
            if (msg->cmd == ESP_CMD_WIFI_CIPSTAMAC_GET) {
                ESP_AT_PORT_SEND_STR("STA");
            } else {
                ESP_AT_PORT_SEND_STR("AP");
            }
            ESP_AT_PORT_SEND_STR("MAC");
            if (msg->msg.sta_ap_getmac.def) {
                ESP_AT_PORT_SEND_STR("_DEF");
            } else {
                ESP_AT_PORT_SEND_STR("_CUR");
            }
            ESP_AT_PORT_SEND_STR("?\r\n");
            break;
        }
        case ESP_CMD_WIFI_CIPSTA_SET:           /* Set station IP address */
        case ESP_CMD_WIFI_CIPAP_SET: {          /* Set access point IP address */
            ESP_AT_PORT_SEND_STR("AT+CIP");
            if (msg->cmd == ESP_CMD_WIFI_CIPSTA_SET) {
                ESP_AT_PORT_SEND_STR("STA");
            } else {
                ESP_AT_PORT_SEND_STR("AP");
            }
            if (msg->msg.sta_ap_setip.def) {
                ESP_AT_PORT_SEND_STR("_DEF");
            } else {
                ESP_AT_PORT_SEND_STR("_CUR");
            }
            ESP_AT_PORT_SEND_STR("=");
            send_ip_mac(msg->msg.sta_ap_setip.ip, 1, 1);    /* Send IP address */
            if (msg->msg.sta_ap_setip.gw) {     /* Is gateway set? */
                ESP_AT_PORT_SEND_STR(",");
                send_ip_mac(msg->msg.sta_ap_setip.gw, 1, 1);    /* Send gateway address */
                if (msg->msg.sta_ap_setip.nm) { /* Is netmask set ? */
                    ESP_AT_PORT_SEND_STR(",");
                    send_ip_mac(msg->msg.sta_ap_setip.nm, 1, 1);    /* Send netmask address */
                }
            }
            ESP_AT_PORT_SEND_STR("\r\n");
            break;
        }
        case ESP_CMD_WIFI_CIPSTAMAC_SET:        /* Set station MAC address */
        case ESP_CMD_WIFI_CIPAPMAC_SET: {       /* Set access point MAC address */
            ESP_AT_PORT_SEND_STR("AT+CIP");
            if (msg->cmd == ESP_CMD_WIFI_CIPSTAMAC_SET) {
                ESP_AT_PORT_SEND_STR("STA");
            } else {
                ESP_AT_PORT_SEND_STR("AP");
            }
            ESP_AT_PORT_SEND_STR("MAC");
            if (msg->msg.sta_ap_setmac.def) {
                ESP_AT_PORT_SEND_STR("_DEF");
            } else {
                ESP_AT_PORT_SEND_STR("_CUR");
            }
            ESP_AT_PORT_SEND_STR("=");
            send_ip_mac(msg->msg.sta_ap_setmac.mac, 0, 1);  /* Send MAC address */
            ESP_AT_PORT_SEND_STR("\r\n");
            break;
        }
        default:
            return espERR;
    }
    return espOK;
}

/**
 * \brief           Process access point related functions
 * \param[in]       msg: Pointer to \ref esp_msg_t with data
 * \return          Member of \ref espr_t enumeration
 */
espr_t
espi_ap_cmd(esp_msg_t* msg) {
    switch (msg->cmd) {
        case ESP_CMD_WIFI_CWLAP:                /* List access points */
            ESP_AT_PORT_SEND_STR("AT+CWLAP");
            if (msg->msg.ap_list.ssid) {        /* Do we want to filter by SSID? */   
                ESP_AT_PORT_SEND_STR("=");
                send_string(msg->msg.ap_list.ssid, 1, 1);
            }
            ESP_AT_PORT_SEND_STR("\r\n");
            break;
        default:
            return espERR;
    }
    return espOK;
}

/**
 * \brief           Process everything related with connection
 * \note            Start, send data and close operations are performed here
 * \param[in]       msg: Pointer to \ref esp_msg_t with data
 * \return          Member of \ref espr_t enumeration
 */
espr_t
espi_tcpip_conn(esp_msg_t* msg) {
    int8_t i = 0;
    esp_conn_t* c = NULL;
    char str[6];
    
    if (msg->cmd == ESP_CMD_TCPIP_CIPSTART) {   /* In case user wants to start a new connection */
        msg->msg.conn_start.num = 0;            /* Reset to make sure default value is set */
        for (i = ESP_MAX_CONNS - 1; i >= 0; i--) {  /* Find available connection */
            if (!esp.conns[i].status.f.active || !(esp.active_conns & (1 << i))) {
                c = &esp.conns[i];
                c->num = i;
                msg->msg.conn_start.num = i;    /* Set connection number for message structure */
                break;
            }
        }
        if (!c) {
            return espNOFREECONN;               /* We don't have available connection */
        }
        
        if (msg->msg.conn_start.conn) {         /* Is user interested about connection info? */
            *msg->msg.conn_start.conn = c;      /* Save connection for user */
        }
        
        ESP_AT_PORT_SEND_STR("AT+CIPSTART=");
        str[0] = c->num + '0';
        ESP_AT_PORT_SEND_CHR(str);
        ESP_AT_PORT_SEND_STR(",\"");
        if (msg->msg.conn_start.type == ESP_CONN_TYPE_SSL) {
            ESP_AT_PORT_SEND_STR("SSL");
        } else if (msg->msg.conn_start.type == ESP_CONN_TYPE_TCP) {
            ESP_AT_PORT_SEND_STR("TCP");
        } else if (msg->msg.conn_start.type == ESP_CONN_TYPE_UDP) {
            ESP_AT_PORT_SEND_STR("UPD");
        }
        ESP_AT_PORT_SEND_STR("\",\"");
        ESP_AT_PORT_SEND_STR(msg->msg.conn_start.host);
        ESP_AT_PORT_SEND_STR("\",");
        number_to_str(msg->msg.conn_start.port, str);
        ESP_AT_PORT_SEND_STR(str);
        ESP_AT_PORT_SEND_STR("\r\n");
        
        esp_get_conns_status(0);                /* Get connection statuses after */
    } else if (msg->cmd == ESP_CMD_TCPIP_CIPCLOSE) {
        ESP_AT_PORT_SEND_STR("AT+CIPCLOSE=");
        str[0] = msg->msg.conn_close.conn ? msg->msg.conn_close.conn->num + '0' : '5';
        ESP_AT_PORT_SEND_CHR(str);
        ESP_AT_PORT_SEND_STR("\r\n");
    } else if (msg->cmd == ESP_CMD_TCPIP_CIPSEND) {
        espi_tcpip_process_send_data();         /* Process send data */
    }
    return espOK;
}