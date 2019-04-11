/*==============================================================================
Copyright 2016 Microchip Technology Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUreT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*
INCLUDES
*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
#include "winc1500_api.h"
#include "wf_socket_internal.h"
#include "wf_hif.h"
#include "wf_types.h"
#include "wf_common.h"
#include "wf_drv.h"

//==============================================================================
// CONSTANTS
//==============================================================================
#define TLS_RECORD_HEADER_LENGTH            (5)
#define ETHERNET_HEADER_OFFSET              (34)
#define ETHERNET_HEADER_LENGTH              (14)
#define TCP_IP_HEADER_LENGTH                (40)
#define UDP_IP_HEADER_LENGTH                (28)

#define IP_PACKET_OFFSET                    (ETHERNET_HEADER_LENGTH + ETHERNET_HEADER_OFFSET - M2M_HIF_HDR_OFFSET)

#define TCP_TX_PACKET_OFFSET                (IP_PACKET_OFFSET + TCP_IP_HEADER_LENGTH)
#define UDP_TX_PACKET_OFFSET                (IP_PACKET_OFFSET + UDP_IP_HEADER_LENGTH)
#define SSL_TX_PACKET_OFFSET                (TCP_TX_PACKET_OFFSET + TLS_RECORD_HEADER_LENGTH)

#define SSL_FLAGS_ACTIVE                    NBIT0
#define SSL_FLAGS_BYPASS_X509               NBIT1
#define SSL_FLAGS_2_RESERVD                 NBIT2
#define SSL_FLAGS_3_RESERVD                 NBIT3
#define SSL_FLAGS_CACHE_SESSION             NBIT4
#define SSL_FLAGS_NO_TX_COPY                NBIT5

//==============================================================================
// MACROS
//==============================================================================
#define SOCKET_REQUEST(reqID, reqArgs, reqSize, reqPayload, reqPayloadSize, reqPayloadOffset)        \
    hif_send(REQ_GROUP_IP, reqID, reqArgs, reqSize, reqPayload, reqPayloadSize, reqPayloadOffset)

//==============================================================================
// DATA TYPES
//==============================================================================
typedef struct
{
    uint32_t    u32StaticIp;
    uint32_t    cmdPrivate;
    uint32_t    rtt;
    uint16_t    success;        // 1 if successful
    uint16_t    fail;           
    uint8_t     errorCode;      // see t_m2mPingErrorCode
    uint8_t     padding[3];
} t_internalPingReply;

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*
GLOBALS
*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/

volatile t_socket   g_sockets[MAX_SOCKET];
volatile uint16_t   g_sessionId  = 0;    
volatile uint8_t    g_socketInit = 0;
t_socketEventData  g_socketEventData;

static int8_t sslSetSockOpt(SOCKET sock, uint8_t  option, const void *p_optionValue, uint16_t optionLength);


t_socketEventData * m2m_wifi_get_socket_event_data(void)
{
    return &g_socketEventData;
}


void ReadSocketData(SOCKET       sock, 
                    t_socketRecv *p_socketRecv,
                    uint8_t      socketMsgId,
                    uint32_t     startAddress,
                    uint16_t     readCount)
{
    if((readCount > 0) && (g_sockets[sock].p_userBuf != NULL) && (g_sockets[sock].userBufSize > 0) && (g_sockets[sock].isUsed == 1))
    {
        uint32_t address = startAddress;
        uint16_t read;
        int16_t  diff;
        uint8_t  setRxDone;

        p_socketRecv->remainingSize = readCount;
        do
        {
            setRxDone = 1;
            read = readCount;
            diff = read - g_sockets[sock].userBufSize;
            if(diff > 0)
            {
                setRxDone = 0;
                read = g_sockets[sock].userBufSize;
            }
            
            
            hif_receive(address, g_sockets[sock].p_userBuf, read, setRxDone);
            p_socketRecv->p_rxBuf       = g_sockets[sock].p_userBuf;
            p_socketRecv->bufSize       = read;
            p_socketRecv->remainingSize -= read;
            memcpy(&g_socketEventData.recvMsg, p_socketRecv, sizeof(t_socketRecv));
            m2m_socket_handle_events(sock, socketMsgId, &g_socketEventData);
            readCount -= read;
            address += read;
            
            if ((!g_sockets[sock].isUsed) && (readCount))
            {
                // application closed socket while rx not complete
                hif_receive(0, NULL, 0, 1);
            }
            
        } while (readCount != 0);
    }
}
void SocketInternalEventHandler(uint8_t opCode, uint16_t bufferSize,uint32_t address)
{
    switch (opCode)
    {
        case SOCKET_CMD_BIND:
            {
                t_bindReply bindReply;
                hif_receive(address, (uint8_t*)&bindReply, sizeof(t_bindReply), 0);
                bindReply.sessionId = FIX_ENDIAN_16(bindReply.sessionId);
                g_socketEventData.bindStatus = bindReply.status;
                m2m_socket_handle_events(bindReply.sock, M2M_SOCKET_BIND_EVENT, &g_socketEventData);
            }
            break;
            
        case SOCKET_CMD_LISTEN:
            {
                t_listenReply   listenReply;
                hif_receive(address, (uint8_t*)&listenReply, sizeof(t_listenReply), 0);
                listenReply.sessionId = FIX_ENDIAN_16(listenReply.sessionId);
                g_socketEventData.listenStatus = listenReply.status;
                m2m_socket_handle_events(listenReply.sock, M2M_SOCKET_LISTEN_EVENT, &g_socketEventData);
            }
            break;
            
        case SOCKET_CMD_ACCEPT:
            {
                t_acceptReply     acceptReply;
                hif_receive(address, (uint8_t*)&acceptReply, sizeof(t_acceptReply), 0);
                acceptReply.appDataOffset = FIX_ENDIAN_16(acceptReply.appDataOffset);
                acceptReply.addr.port = _htons(acceptReply.addr.port);

                if (acceptReply.connectSock >= 0)
                {
                    g_sockets[acceptReply.connectSock].sslFlags = 0;
                    g_sockets[acceptReply.connectSock].isUsed   = 1;

                    // The session ID is used to distinguish different socket connections
                    // by comparing the assigned session ID to the one reported by the firmware
                    ++g_sessionId;
                    if(g_sessionId == 0)
                    {
                        ++g_sessionId;
                    }

                    g_sockets[acceptReply.connectSock].sessionId = g_sessionId;
                    //dprintf("Socket %d session ID = %d\n",strAcceptReply.connectSock , g_sessionId );        
                }
                g_socketEventData.acceptResponse.sock = acceptReply.connectSock;
                g_socketEventData.acceptResponse.strAddr.sin_family      = AF_INET;
                g_socketEventData.acceptResponse.strAddr.sin_port        = acceptReply.addr.port;
                g_socketEventData.acceptResponse.strAddr.sin_addr.s_addr = acceptReply.addr.ipAddr;
                m2m_socket_handle_events(acceptReply.listenSock, M2M_SOCKET_ACCEPT_EVENT, &g_socketEventData);
            }
        
        case SOCKET_CMD_CONNECT:
        case SOCKET_CMD_SSL_CONNECT:
            {
                t_socketConnectReply  strConnectReply;
                hif_receive(address, (uint8_t*)&strConnectReply, sizeof(t_socketConnectReply), 0);
                strConnectReply.appDataOffset = FIX_ENDIAN_16(strConnectReply.appDataOffset);
                g_socketEventData.connectResponse.sock    = strConnectReply.sock;
                g_socketEventData.connectResponse.error = strConnectReply.error;
                if(strConnectReply.error == SOCK_ERR_NO_ERROR)
                {
                    g_sockets[strConnectReply.sock].dataOffset = strConnectReply.appDataOffset - M2M_HIF_HDR_OFFSET;
                }
                m2m_socket_handle_events(strConnectReply.sock, M2M_SOCKET_CONNECT_EVENT, &g_socketEventData);
            }
            break;
            
        case SOCKET_CMD_DNS_RESOLVE:
            hif_receive(address, (uint8_t*)&g_socketEventData, sizeof(t_dnsReply), 0);
            m2m_socket_handle_events(0, M2M_SOCKET_DNS_RESOLVE_EVENT, &g_socketEventData);
            break;
            
        case SOCKET_CMD_RECV:
        case SOCKET_CMD_RECVFROM:
        case SOCKET_CMD_SSL_RECV:
            {
                SOCKET      sock;
                int16_t     recvStatus;
                t_recvReply recvReply;
                uint16_t    readSize;
                uint8_t     msgId = M2M_SOCKET_RECV_EVENT;
                uint16_t    dataOffset;
                uint16_t    sessionId;
                
                if(opCode == SOCKET_CMD_RECVFROM)
                {
                    msgId = M2M_SOCKET_RECVFROM_EVENT;
                }

                /* Read RECV REPLY data structure.
                */
                readSize = sizeof(t_recvReply);
                hif_receive(address, (uint8_t*)&recvReply, readSize, 0);
                sock       = recvReply.sock;
                sessionId  = FIX_ENDIAN_16(recvReply.sessionId);
                recvStatus = FIX_ENDIAN_16(recvReply.recvStatus);
                dataOffset = FIX_ENDIAN_16(recvReply.dataOffset);
                recvReply.addr.port = _htons(recvReply.addr.port);

                // Reset the Socket RX pending flag
                g_sockets[sock].isRecvPending = 0;

                g_socketEventData.recvMsg.ai_addr.sin_port        = recvReply.addr.port;
                g_socketEventData.recvMsg.ai_addr.sin_addr.s_addr = recvReply.addr.ipAddr;

                if(sessionId == g_sockets[sock].sessionId)
                {
                    if((recvStatus > 0) && (recvStatus < bufferSize))
                    {
                        /* Skip incoming bytes until reaching the Start of Application Data. 
                        */
                        address += dataOffset;

                        /* Read the Application data and deliver it to the application callback in
                        the given application buffer. If the buffer is smaller than the received data,
                        the data is passed to the application in chunks according to its buffer size.
                        */
                        readSize = (uint16_t)recvStatus;
                        ReadSocketData(sock, &g_socketEventData.recvMsg, msgId, address, readSize);
                    }
                    else
                    {
                        g_socketEventData.recvMsg.bufSize    = recvStatus;
                        g_socketEventData.recvMsg.p_rxBuf    = NULL;
                        m2m_socket_handle_events(sock, msgId, &g_socketEventData);
                    }
                }
                else
                {
                    dprintf("Discard recv callback %d %d \r\n", sessionId , g_sockets[sock].sessionId);
                    if(readSize < bufferSize)
                    {
                        hif_receive(0, NULL, 0, 1);
                    }
                }
            }
            break;
            
        case SOCKET_CMD_SEND:
        case SOCKET_CMD_SENDTO:
        case SOCKET_CMD_SSL_SEND:
            {
                SOCKET      sock;
                t_sendReply reply;
                uint8_t     msgId = M2M_SOCKET_SEND_EVENT;
                uint16_t    sessionId;

                if(opCode == SOCKET_CMD_SENDTO)
                {
                    msgId = M2M_SOCKET_SENDTO_EVENT;
                }

                hif_receive(address, (uint8_t*)&reply, sizeof(t_sendReply), 0);
                sessionId = FIX_ENDIAN_16(reply.sessionId);

                sock = reply.sock;
                g_socketEventData.numSendBytes = FIX_ENDIAN_16(reply.sentBytes);
                if(sessionId == g_sockets[sock].sessionId)
                {
                    m2m_socket_handle_events(sock, msgId, &g_socketEventData);
                }
                else
                {
                    GenerateErrorEvent(M2M_WIFI_MISMATCH_SESSION_ID_ERROR);
                    return;
                }
            }
            break;
            
        case SOCKET_CMD_PING:
        {
            t_internalPingReply internalPingReply;
            hif_receive(address, (uint8_t*)&internalPingReply, sizeof(t_internalPingReply), 1);
            g_socketEventData.pingReply.errorCode = internalPingReply.errorCode;
            g_socketEventData.pingReply.u32StaticIp = internalPingReply.u32StaticIp;
            g_socketEventData.pingReply.rtt       = FIX_ENDIAN_32(internalPingReply.rtt);
            m2m_socket_handle_events(0, M2M_SOCKET_PING_RESPONSE_EVENT, &g_socketEventData);
        }
            break;
            
        default:
            dprintf("Invalid socket op code\r\n");
            break;
            
    } // end switch
}

void SocketInit(void)
{
    if(g_socketInit == 0)
    {
        memset((uint8_t*)g_sockets, 0, MAX_SOCKET * sizeof(t_socket));
        g_socketInit = 1;
        g_sessionId = 0;
    }
}

#if 0
void socketDeinit(void)
{    
    memset((uint8_t*)g_sockets, 0, MAX_SOCKET * sizeof(t_socket));
    hif_register_cb(REQ_GROUP_IP, NULL);
    g_socketInit = 0;
}
#endif

SOCKET socket(uint16_t u16Domain, uint8_t u8Type, uint8_t u8Flags)
{
    SOCKET             sock = -1;
    uint8_t            count, socketCount = MAX_SOCKET;
    volatile t_socket  *p_sock;
    
    /* The only supported family is the AF_INET for UDP and TCP transport layer protocols. */
    if(u16Domain == AF_INET)
    {
        if(u8Type == SOCK_STREAM)
        {
            socketCount = TCP_SOCK_MAX;
            count = 0;
        }
        else if(u8Type == SOCK_DGRAM)
        {
            /*--- UDP SOCKET ---*/
            socketCount = MAX_SOCKET;
            count = TCP_SOCK_MAX;
        }
        else
            return sock;

        for(;count < socketCount; count ++)
        {
            p_sock = &g_sockets[count];
            if(p_sock->isUsed == 0)
            {
                memset((uint8_t*)p_sock, 0, sizeof(t_socket));

                p_sock->isUsed = 1;

                /* The session ID is used to distinguish different socket connections
                    by comparing the assigned session ID to the one reported by the firmware*/
                ++g_sessionId;
                if(g_sessionId == 0)
                {
                    ++g_sessionId;
                }
                
                p_sock->sessionId = g_sessionId;
                sock = (SOCKET)count;
                if(u8Flags & SOCKET_FLAGS_SSL)
                {
                    t_sslSocketCreateCmd    sslCreateConfig;
                    sslCreateConfig.sslSock = sock;
                    p_sock->sslFlags = SSL_FLAGS_ACTIVE | SSL_FLAGS_NO_TX_COPY;
                    SOCKET_REQUEST(SOCKET_CMD_SSL_CREATE, (uint8_t*)&sslCreateConfig, sizeof(t_sslSocketCreateCmd), 0, 0, 0);
                }
                break;
            }
        }
    }
    return sock;
}

int8_t bind(SOCKET sock, struct sockaddr *pstrAddr, uint8_t u8AddrLen)
{
    int8_t    retVal = SOCK_ERR_INVALID_ARG;
    
    if((pstrAddr != NULL) && (sock >= 0) && (g_sockets[sock].isUsed == 1) && (u8AddrLen != 0))
    {
        t_bindCmd bindConfig;

        /* Build the bind request. */
        bindConfig.sock = sock;
        memcpy((uint8_t *)&bindConfig.addr, (uint8_t *)pstrAddr, sizeof(t_sockAddr));

        bindConfig.addr.port = bindConfig.addr.port;
        bindConfig.sessionId = FIX_ENDIAN_16(g_sockets[sock].sessionId);
        
        /* Send the request. */
        retVal = SOCKET_REQUEST(SOCKET_CMD_BIND, (uint8_t*)&bindConfig,sizeof(t_bindCmd) , NULL , 0, 0);
        if(retVal != SOCK_ERR_NO_ERROR)
        {
            retVal = SOCK_ERR_INVALID;
        }
    }
    return retVal;
}

int8_t listen(SOCKET sock, uint8_t backlog)
{
    int8_t    retVal = SOCK_ERR_INVALID_ARG;
    
    if(sock >= 0 && (g_sockets[sock].isUsed == 1))
    {
        t_listenCmd listenConfig;

        listenConfig.sock      = sock;
        listenConfig.backlog   = backlog;
        listenConfig.sessionId = FIX_ENDIAN_16(g_sockets[sock].sessionId);

        retVal = SOCKET_REQUEST(SOCKET_CMD_LISTEN, (uint8_t*)&listenConfig, sizeof(t_listenCmd), NULL, 0, 0);
        if(retVal != SOCK_ERR_NO_ERROR)
        {
            retVal = SOCK_ERR_INVALID;
        }
    }
    return retVal;
}

int8_t accept(SOCKET sock, struct sockaddr *addr, uint8_t *addrlen)
{
    int8_t    retVal = SOCK_ERR_INVALID_ARG;
    
    if(sock >= 0 && (g_sockets[sock].isUsed == 1) )
    {
        retVal = SOCK_ERR_NO_ERROR;
    }
    return retVal;
}

int8_t connect(SOCKET sock, struct sockaddr *my_addr, uint8_t addrlen)
{
    int8_t    retVal = SOCK_ERR_INVALID_ARG;
    
    if((sock >= 0) && (my_addr != NULL) && (g_sockets[sock].isUsed == 1) && (addrlen != 0))
    {
        t_connectCmd  connectConfig;
        uint8_t cmd = SOCKET_CMD_CONNECT;
        
        if((g_sockets[sock].sslFlags) & SSL_FLAGS_ACTIVE)
        {
            cmd = SOCKET_CMD_SSL_CONNECT;
            connectConfig.sslFlags = g_sockets[sock].sslFlags;
        }
        
        connectConfig.sock = sock;
        memcpy((uint8_t *)&connectConfig.addr, (uint8_t *)my_addr, sizeof(t_sockAddr));
        connectConfig.addr.port = connectConfig.addr.port;
        
        connectConfig.sessionId = FIX_ENDIAN_16(g_sockets[sock].sessionId);
        retVal = SOCKET_REQUEST(cmd, (uint8_t*)&connectConfig,sizeof(t_connectCmd), NULL, 0, 0);
        if(retVal != SOCK_ERR_NO_ERROR)
        {
            retVal = SOCK_ERR_INVALID;
        }
    }
    return retVal;
}

int8_t send(SOCKET sock, void *buf, uint16_t len, uint16_t flags)
{
    int16_t    retVal = SOCK_ERR_INVALID_ARG;
    
    if((sock >= 0) && (buf != NULL) && (len <= SOCKET_BUFFER_MAX_LENGTH) && (g_sockets[sock].isUsed == 1))
    {
        uint16_t  dataOffset;
        t_sendCmd sendConfig;
        uint8_t   cmd;

        cmd        = SOCKET_CMD_SEND;
        dataOffset = TCP_TX_PACKET_OFFSET;

        sendConfig.sock      = sock;
        sendConfig.dataSize  = FIX_ENDIAN_16(len);
        sendConfig.sessionId = FIX_ENDIAN_16(g_sockets[sock].sessionId);

        if(sock >= TCP_SOCK_MAX)
        {
            dataOffset = UDP_TX_PACKET_OFFSET;
        }
        
        if(g_sockets[sock].sslFlags & SSL_FLAGS_ACTIVE)
        {
            cmd        = SOCKET_CMD_SSL_SEND;
            dataOffset = g_sockets[sock].dataOffset;
        }

        retVal =  SOCKET_REQUEST(cmd|REQ_DATA_PKT, (uint8_t*)&sendConfig, sizeof(t_sendCmd), buf, len, dataOffset);
        if(retVal != SOCK_ERR_NO_ERROR)
        {
            retVal = SOCK_ERR_BUFFER_FULL;
        }
    }
    return retVal;
}

int8_t sendto(SOCKET sock, void *buf, uint16_t len, uint16_t flags, struct sockaddr *to, uint8_t tolen)
{
    int8_t retVal = SOCK_ERR_INVALID_ARG;
    
    (void)tolen; // avoid warning on unused parameter
    
    if((sock >= 0) && (buf != NULL) && (len <= SOCKET_BUFFER_MAX_LENGTH) && (g_sockets[sock].isUsed == 1))
    {
        if(g_sockets[sock].isUsed)
        {
            t_sendCmd sendToConfig;

            memset((uint8_t*)&sendToConfig, 0, sizeof(t_sendCmd));
            sendToConfig.sock         = sock;
            sendToConfig.dataSize  = FIX_ENDIAN_16(len);
            sendToConfig.sessionId = FIX_ENDIAN_16(g_sockets[sock].sessionId);
            
            if(to != NULL)
            {
                struct sockaddr_in *my_addr;
                my_addr = (void*)to;

                sendToConfig.addr.family  = my_addr->sin_family;
                sendToConfig.addr.port    = my_addr->sin_port;
                sendToConfig.addr.ipAddr  = my_addr->sin_addr.s_addr;
            }
            retVal = SOCKET_REQUEST(SOCKET_CMD_SENDTO|REQ_DATA_PKT, (uint8_t*)&sendToConfig,  sizeof(t_sendCmd),
                buf, len, UDP_TX_PACKET_OFFSET);

            if(retVal != SOCK_ERR_NO_ERROR)
            {
                retVal = SOCK_ERR_BUFFER_FULL;
            }
        }
    }
    return retVal;
}

int8_t recv(SOCKET sock, void *buf, uint16_t len, uint32_t timeout)
{
    int8_t retVal = SOCK_ERR_INVALID_ARG;
    
    if((sock >= 0) && (buf != NULL) && (len != 0) && (g_sockets[sock].isUsed == 1))
    {
        retVal = SOCK_ERR_NO_ERROR;
        g_sockets[sock].p_userBuf   = (uint8_t*)buf;
        g_sockets[sock].userBufSize = len;

        if(!g_sockets[sock].isRecvPending)
        {
            t_recvCmd recvConfig;
            uint8_t   cmd = SOCKET_CMD_RECV;

            g_sockets[sock].isRecvPending = 1;
            if(g_sockets[sock].sslFlags & SSL_FLAGS_ACTIVE)
            {
                cmd = SOCKET_CMD_SSL_RECV;
            }

            /* Check the timeout value. */
            if(timeout == 0)
            {
                recvConfig.timeout = 0xFFFFFFFF;
            }
            else
            {
                recvConfig.timeout = FIX_ENDIAN_32(timeout);
            }
            recvConfig.sock = sock;
            recvConfig.sessionId = FIX_ENDIAN_16(g_sockets[sock].sessionId);
        
            retVal = SOCKET_REQUEST(cmd, (uint8_t*)&recvConfig, sizeof(t_recvCmd), NULL , 0, 0);
            if(retVal != SOCK_ERR_NO_ERROR)
            {
                retVal = SOCK_ERR_BUFFER_FULL;
            }
        }
    }
    return retVal;
}

int8_t close(SOCKET sock)
{
    int8_t    retVal = SOCK_ERR_INVALID_ARG;
    if(sock >= 0 && (g_sockets[sock].isUsed == 1))
    {
        uint8_t    cmd = SOCKET_CMD_CLOSE;
        t_closeCmd strclose;
        strclose.sock = sock; 
        strclose.sessionId = FIX_ENDIAN_16(g_sockets[sock].sessionId);
        
        g_sockets[sock].isUsed = 0;
        g_sockets[sock].sessionId =0;
        
        if(g_sockets[sock].sslFlags & SSL_FLAGS_ACTIVE)
        {
            cmd = SOCKET_CMD_SSL_CLOSE;
        }
        retVal = SOCKET_REQUEST(cmd, (uint8_t*)&strclose, sizeof(t_closeCmd), NULL,0, 0);
        if(retVal != SOCK_ERR_NO_ERROR)
        {
            retVal = SOCK_ERR_INVALID;
        }
        memset((uint8_t*)&g_sockets[sock], 0, sizeof(t_socket));
    }
    return retVal;
}

int8_t recvfrom(SOCKET sock, void *buf, uint16_t len, uint32_t timeout)
{
    int8_t    retVal = SOCK_ERR_NO_ERROR;
    
    if((sock >= 0) && (buf != NULL) && (len != 0) && (g_sockets[sock].isUsed == 1))
    {
        if(g_sockets[sock].isUsed)
        {
            retVal = SOCK_ERR_NO_ERROR;
            g_sockets[sock].p_userBuf = (uint8_t*)buf;
            g_sockets[sock].userBufSize = len;

            if(!g_sockets[sock].isRecvPending)
            {
                t_recvCmd recvConfig;

                g_sockets[sock].isRecvPending = 1;

                /* Check the timeout value. */
                if(timeout == 0)
                {
                    recvConfig.timeout = 0xFFFFFFFF;
                }
                else
                {
                    recvConfig.timeout = FIX_ENDIAN_32(timeout);
                }
                recvConfig.sock      = sock;
                recvConfig.sessionId = FIX_ENDIAN_16(g_sockets[sock].sessionId);
                
                retVal = SOCKET_REQUEST(SOCKET_CMD_RECVFROM, (uint8_t*)&recvConfig, sizeof(t_recvCmd), NULL , 0, 0);
                if(retVal != SOCK_ERR_NO_ERROR)
                {
                    retVal = SOCK_ERR_BUFFER_FULL;
                }
            }
        }
    }
    else
    {
        retVal = SOCK_ERR_INVALID_ARG;
    }
    return retVal;
}

int8_t gethostbyname(const char * name)
{
    int8_t   retVal = SOCK_ERR_INVALID_ARG;
    uint8_t  hostNameSize = (uint8_t)strlen((char *)name);
    
    if(hostNameSize <= M2M_HOSTNAME_MAX_SIZE)
    {
        retVal = SOCKET_REQUEST(SOCKET_CMD_DNS_RESOLVE|REQ_DATA_PKT, (uint8_t*)name, hostNameSize + 1, NULL,0, 0);
        if(retVal != SOCK_ERR_NO_ERROR)
        {
            retVal = SOCK_ERR_INVALID;
        }
    }
    return retVal;
}

/*********************************************************************
Function
        setsockopt

Description

Return
        None.

Author
        Abdelrahman Diab

Version
        1.0

Date
        9 September 2014
*********************************************************************/
int8_t setsockopt(SOCKET sock, 
                  uint8_t level, 
                  uint8_t optname,
                  const void *optval, 
                  uint16_t optlen)
{
    int8_t    retVal = SOCK_ERR_INVALID_ARG;
    
    if((sock >= 0)  && (optval != NULL)  && (g_sockets[sock].isUsed == 1))
    {
        if(level == SOL_SSL_SOCKET)
        {
            retVal = sslSetSockOpt(sock, optname, optval, optlen);
        }
        else
        {
            uint8_t    cmd = SOCKET_CMD_SET_SOCKET_OPTION;
            t_setSockOptCmd setOptConfig;
            setOptConfig.option      = optname;
            setOptConfig.sock        = sock; 
            setOptConfig.optionValue = FIX_ENDIAN_32(*(uint32_t*)optval);
            setOptConfig.sessionId   = FIX_ENDIAN_16(g_sockets[sock].sessionId);

            retVal = SOCKET_REQUEST(cmd, (uint8_t*)&setOptConfig, sizeof(t_setSockOptCmd), NULL,0, 0);
            if(retVal != SOCK_ERR_NO_ERROR)
            {
                retVal = SOCK_ERR_INVALID;
            }
        }
    }
    return retVal;    
}

int8_t getsockopt(SOCKET sock, uint8_t level, uint8_t optname, const void *optval, uint8_t* optlen)
{
    /* TBD */
    return SOCK_ERR_NO_ERROR;
}

void m2m_ping_req(uint32_t destIpAddress, uint8_t ttl)
{
    if (destIpAddress != 0)
    {
        tstrPingCmd    pingCmd;

        pingCmd.pingCount     = FIX_ENDIAN_16(1);
        pingCmd.destIpAddress = destIpAddress;
        pingCmd.reserved      = (uint32_t)0xa0000000; // any non-NULL value OK, not being used anymore
        pingCmd.ttl           = ttl;
        SOCKET_REQUEST(SOCKET_CMD_PING, (uint8_t*)&pingCmd, sizeof(tstrPingCmd), NULL, 0, 0);
    }
    else
    {
        dprintf("ERROR: WFPing, invalid address");
    }
}

static int8_t sslSetSockOpt(SOCKET sock, uint8_t  option, const void *p_optionValue, uint16_t optionLength)
{
    int8_t    retVal = SOCK_ERR_INVALID_ARG;
    
    if(sock < TCP_SOCK_MAX)
    {
        if(g_sockets[sock].sslFlags & SSL_FLAGS_ACTIVE)
        {
            if(option == SO_SSL_BYPASS_X509_VERIF)
            {
                int    optVal = *((int*)p_optionValue);
                if(optVal)
                {
                    g_sockets[sock].sslFlags |= SSL_FLAGS_BYPASS_X509;
                }
                else
                {
                    g_sockets[sock].sslFlags &= ~SSL_FLAGS_BYPASS_X509;
                }
                retVal = SOCK_ERR_NO_ERROR;
            }
            else if(option == SO_SSL_ENABLE_SESSION_CACHING)
            {
                int    optVal = *((int*)p_optionValue);
                if(optVal)
                {
                    g_sockets[sock].sslFlags |= SSL_FLAGS_CACHE_SESSION;
                }
                else
                {
                    g_sockets[sock].sslFlags &= ~SSL_FLAGS_CACHE_SESSION;
                }
                retVal = SOCK_ERR_NO_ERROR;
            }
            else if(option == SO_SSL_SNI)
            {
                if(optionLength < M2M_HOSTNAME_MAX_SIZE)
                {
                    uint8_t             *p_sni = (uint8_t*)p_optionValue;
                    t_sslSetSockOptCmd  cmd;

                    cmd.sock         = sock;
                    cmd.sessionId    = FIX_ENDIAN_16(g_sockets[sock].sessionId);
                    cmd.option       = option;
                    cmd.optLength    = FIX_ENDIAN_32(optionLength);
                    memcpy(cmd.optValue, p_sni, M2M_HOSTNAME_MAX_SIZE);
                    
                    if (SOCKET_REQUEST(SOCKET_CMD_SSL_SET_SOCK_OPT, 
                                       (uint8_t*)&cmd, 
                                       sizeof(t_sslSetSockOptCmd),
                                       0, 0, 0) == M2M_ERR_MEM_ALLOC)
                    {
                        retVal = SOCKET_REQUEST(SOCKET_CMD_SSL_SET_SOCK_OPT | REQ_DATA_PKT, 
                                                (uint8_t*)&cmd, 
                                                sizeof(t_sslSetSockOptCmd), 
                                                0, 0, 0);
                    }
                    retVal = SOCK_ERR_NO_ERROR;
                }
                else
                {
                    dprintf("SNI Exceeds Max Length\n");
                }
            }
            else
            {
                dprintf("Unknown SSL Socket Option %d\n",option);
            }
        }
        else
        {
            dprintf("Not SSL Socket\n");
        }
    }
    return retVal;
}

int8_t sslEnableCertExpirationCheck(uint8_t enable)
{
    t_sslCertExpSettings settings;
    
    settings.enable = enable;
    return SOCKET_REQUEST(SOCKET_CMD_SSL_EXP_CHECK, (uint8_t*)&settings, sizeof(t_sslCertExpSettings), NULL, 0, 0);
}

#if defined(__XC8)
extern void m2m_socket_handle_events_Pic18WaiteHttpSend(SOCKET sock, t_m2mSocketEventType eventCode, t_socketEventData *p_eventData);
void SocketInternalEventHandler_Pic18WaiteHttpSend(uint8_t opCode, uint16_t bufferSize,uint32_t address)
{
    switch (opCode)
    {             
        case SOCKET_CMD_SEND:
        case SOCKET_CMD_SENDTO:
        case SOCKET_CMD_SSL_SEND:
            {
                SOCKET      sock;
                t_sendReply reply;
                uint8_t     msgId = M2M_SOCKET_SEND_EVENT;
                uint16_t    sessionId;

                if(opCode == SOCKET_CMD_SENDTO)
                {
                    msgId = M2M_SOCKET_SENDTO_EVENT;
                }

                hif_receive(address, (uint8_t*)&reply, sizeof(t_sendReply), 0);
                sessionId = FIX_ENDIAN_16(reply.sessionId);

                sock = reply.sock;
                g_socketEventData.numSendBytes = FIX_ENDIAN_16(reply.sentBytes);
                if(sessionId == g_sockets[sock].sessionId)
                {
                    m2m_socket_handle_events_Pic18WaiteHttpSend(sock, msgId, &g_socketEventData);
                }
                else
                {
                    GenerateErrorEvent(M2M_WIFI_MISMATCH_SESSION_ID_ERROR);
                    return;
                }
            }
            break;

        default:

            break;
            
    } // end switch
}
#endif