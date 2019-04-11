/*==============================================================================
Copyright 2016 Microchip Technology Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

//DOM-IGNORE-BEGIN

#ifndef __WF_SOCKET_HOST_IF
#define __WF_SOCKET_HOST_IF


#ifdef  __cplusplus
extern "C" {
#endif
    
//==============================================================================
// CONSTANTS
//==============================================================================
#define SSL_MAX_OPT_LEN                 M2M_HOSTNAME_MAX_SIZE
#define SOCKET_CMD_INVALID              0x00                    // Invalid Socket command 
#define SOCKET_CMD_BIND                 0x41                    // Socket Bind command 
#define SOCKET_CMD_LISTEN               0x42                    // Socket Listen command 
#define SOCKET_CMD_ACCEPT               0x43                    // Socket Accept command 
#define SOCKET_CMD_CONNECT              0x44                    // Socket Connect command 
#define SOCKET_CMD_SEND                 0x45                    // Socket Send command 
#define SOCKET_CMD_RECV                 0x46                    // Socket Receive command 
#define SOCKET_CMD_SENDTO               0x47                    // Socket SendTo command 
#define SOCKET_CMD_RECVFROM             0x48                    // Socket RecvFrom command 
#define SOCKET_CMD_CLOSE                0x49                    // Socket Close command 
#define SOCKET_CMD_DNS_RESOLVE          0x4A                    // Socket DNS Resolve command 
#define SOCKET_CMD_SSL_CONNECT          0x4B                    // SSL-Socket Connect command 
#define SOCKET_CMD_SSL_SEND             0x4C                    // SSL-Socket Send command 
#define SOCKET_CMD_SSL_RECV             0x4D                    // SSL-Socket Recieve command 
#define SOCKET_CMD_SSL_CLOSE            0x4E                    // SSL-Socket Close command 
#define SOCKET_CMD_SET_SOCKET_OPTION    0x4F                    // Socket Set Option command 
#define SOCKET_CMD_SSL_CREATE           0x50                    // SSL-Socket Create command
#define SOCKET_CMD_SSL_SET_SOCK_OPT     0x51                    // SSL-Socket Set Option command
#define SOCKET_CMD_PING                 0x52                    // Socket Ping command
    
#define SOCKET_CMD_SSL_SET_CS_LIST      0x53                    // Cipher Suite command 
                                                                // Recommend instead using M2M_SSL_REQ_SET_CS_LIST and
                                                                // associated response M2M_SSL_RESP_SET_CS_LIST
#define SOCKET_CMD_SSL_BIND             0x54    
#define SOCKET_CMD_SSL_EXP_CHECK        0x55                    

//==============================================================================
// DATA TYPES
//==============================================================================
typedef struct
{
    uint8_t     *p_userBuf;
    uint16_t    userBufSize;
    uint16_t    sessionId;
    uint16_t    dataOffset;
    uint8_t     isUsed;
    uint8_t     sslFlags;
    uint8_t     isRecvPending;
} t_socket;
    
typedef struct
{    
    uint16_t    family;
    uint16_t    port;
    uint32_t    ipAddr;
} t_sockAddr;

typedef struct
{
    t_sockAddr  addr;
    SOCKET      sock;
    uint8_t     notUsed;
    uint16_t    sessionId;
} t_bindCmd;

typedef struct
{
    SOCKET      sock;       // socket ID
    int8_t      status;     // bind status (SOCK_ERR_NO_ERROR for success)
    uint16_t    sessionId;
} t_bindReply;

typedef struct
{
    SOCKET      sock;
    uint8_t     backlog;
    uint16_t    sessionId;
} t_listenCmd;

typedef struct
{
    SOCKET      sock;
    int8_t      status;
    uint16_t    sessionId;
} t_listenReply;

typedef struct
{
    t_sockAddr  addr;
    SOCKET      listenSock;
    SOCKET      connectSock;
    uint16_t    appDataOffset; // In further packet send requests the host interface should put the user application
                               //  data at this offset in the allocated shared data packet.
} t_acceptReply;


typedef struct
{
    t_sockAddr  addr;
    SOCKET      sock;
    uint8_t     sslFlags;
    uint16_t    sessionId;
} t_connectCmd;

typedef struct
{
    SOCKET      sock;
    uint8_t     notUsed;
    uint16_t    sessionId;
} t_closeCmd;

typedef struct
{
    SOCKET      sock;
    int8_t      error;
    uint16_t    appDataOffset;  // In further packet send requests the host interface 
                                //  should put the user application
} t_socketConnectReply;

typedef struct
{
    SOCKET      sock;
    uint8_t     notUsed1;
    uint16_t    dataSize;
    t_sockAddr  addr;
    uint16_t    sessionId;
    uint16_t    notUsed2;
} t_sendCmd;

typedef struct
{
    SOCKET      sock;
    uint8_t     notUsed1;
    int16_t     sentBytes;
    uint16_t    sessionId;
    uint16_t    notUsed2;
} t_sendReply;

typedef struct
{
    uint32_t    timeout;
    SOCKET      sock;
    uint8_t     notUsed1;
    uint16_t    sessionId;
} t_recvCmd;

typedef struct
{
    t_sockAddr  addr;
    int16_t     recvStatus;
    uint16_t    dataOffset;
    SOCKET      sock;
    uint8_t     notUsed;
    uint16_t    sessionId;
} t_recvReply;

typedef struct
{
    uint32_t    optionValue;
    SOCKET      sock;
    uint8_t     option;
    uint16_t    sessionId;
} t_setSockOptCmd;


typedef struct
{
    SOCKET      sslSock;
    uint8_t     padding[3];
} t_sslSocketCreateCmd;

typedef struct
{
    SOCKET      sock;
    uint8_t     option;
    uint16_t    sessionId;
    uint32_t    optLength;
    uint8_t     optValue[SSL_MAX_OPT_LEN];
} t_sslSetSockOptCmd;

typedef struct
{
    uint32_t    destIpAddress;
    uint32_t    reserved;
    uint16_t    pingCount;
    uint8_t     ttl;
    uint8_t     padding;
} tstrPingCmd;

typedef struct
{
    uint32_t    cipherSuiteMask;
} t_sslActiveCipherSuites;

typedef struct
{
    uint32_t    enable;
} t_sslCertExpSettings;


//==============================================================================
// Function Prototypes
//==============================================================================
void ReadSocketData(SOCKET       sock, 
                    t_socketRecv *p_socketRecv, 
                    uint8_t      socketMsgId,
                    uint32_t     startAddress, 
                    uint16_t     readCount);


#ifdef  __cplusplus
}
#endif /* __cplusplus */

#endif // __WF_SOCKET_HOST_IF

//DOM-IGNORE-END