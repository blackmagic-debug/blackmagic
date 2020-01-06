/*******************************************************************************
   File Name:
    wf_socket.h

  Summary:
    BSD-like socket API

  Description:
    Provides a BSD-like socket API.  These functions are used by the Host MCU
    to communicate with with other network hosts.
*******************************************************************************/
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

#ifndef __WF_SOCKET_H
#define __WF_SOCKET_H

#ifdef  __cplusplus
extern "C" {
#endif

//==============================================================================
// CONSTANTS
//==============================================================================    
#define  AF_INET            2    // The AF_INET is the address family used for IPv4 and 
                                 //  is the only supported type in the WINC1500 driver.
#define  SOCK_STREAM        1    // The IPv4 supported socket type for reliable connection-oriented 
                                 //  stream connection (TCP)
#define  SOCK_DGRAM         2    // The IPv4 supported socket type for an unreliable 
                                 //  connectionless datagram connection (UDP)
#define SOCKET_FLAGS_SSL    0x01 // Bit mask for the flags parameter when creating an SSL socket
#define TCP_SOCK_MAX        7    // Maximum number of simultaneous TCP sockets    
#define UDP_SOCK_MAX        4    // Maximum number of simultaneous UDP sockets    
#define MAX_SOCKET         (TCP_SOCK_MAX + UDP_SOCK_MAX) // Maximum number of all socket types

#define SOL_SOCKET          1    // parameter for 'level' field in setsockopt and getsockopt
#define SOL_SSL_SOCKET      2    // SSL socket option    

#define SOCKET_BUFFER_MAX_LENGTH  1400  // Maximum allowed size for a socket data buffer.  
                                        //  Used with sendsock() to ensure that the buffer 
                                        //  sent is within the allowed range. 
 
#define M2M_HOSTNAME_MAX_SIZE        64      // Maximum allowed size for a host domain name 
#define M2M_INET4_ADDRSTRLEN         (sizeof("255.255.255.255"))  // Max size of ipV4 string       

//-------------------------------
// General Purpose Socket Options
//-------------------------------    
#define SO_SET_UDP_SEND_CALLBACK    0   // used to enable/disable the UDP send/sendto event
#define IP_ADD_MEMBERSHIP           1   // used for enabling the sending or receiving multicast IP address    
#define IP_DROP_MEMBERSHIP          2   // used for disabling the sending or receiving multicast IP address   

//-------------------
// TLS Socket Options
//-------------------
// Allow an opened SSL socket to bypass the X509 certificate verification process.
// It is highly required NOT to use this socket option in production software applications. 
// It is supported for debugging and testing purposes.  The option value should be cast
// to int type and it is handled as a boolean flag.
#define SO_SSL_BYPASS_X509_VERIF                            0x01

// Set the Server Name Indicator (SNI) for an SSL socket. The SNI is a NULL-terminated 
// string containing the server name assocated with the connection. It must not exceed 
// the size of M2M_HOSTNAME_MAX_SIZE.
#define SO_SSL_SNI                                          0x02

// This option allow the TLS to cache the session information for fast
// TLS session establishment in future connections using the TLS Protocol session 
// resume features.
#define SO_SSL_ENABLE_SESSION_CACHING                       0x03

// Enable SNI validation against the server's certificate subject
// common name. If there is no SNI provided (via the SO_SSL_SNI 
// option), setting this option does nothing.
#define SO_SSL_ENABLE_SNI_VALIDATION                        0x04

    
// SSLCipherSuiteID TLS Cipher Suite IDs
// The following list of macros defined the list of supported TLS Cipher suites.
// Each MACRO defines a single Cipher suite.    
#define SSL_CIPHER_RSA_WITH_AES_128_CBC_SHA                 NBIT0
#define SSL_CIPHER_RSA_WITH_AES_128_CBC_SHA256              NBIT1
#define SSL_CIPHER_DHE_RSA_WITH_AES_128_CBC_SHA             NBIT2
#define SSL_CIPHER_DHE_RSA_WITH_AES_128_CBC_SHA256          NBIT3
#define SSL_CIPHER_RSA_WITH_AES_128_GCM_SHA256              NBIT4
#define SSL_CIPHER_DHE_RSA_WITH_AES_128_GCM_SHA256          NBIT5
#define SSL_CIPHER_RSA_WITH_AES_256_CBC_SHA                 NBIT6
#define SSL_CIPHER_RSA_WITH_AES_256_CBC_SHA256              NBIT7
#define SSL_CIPHER_DHE_RSA_WITH_AES_256_128_CBC_SHA         NBIT8
#define SSL_CIPHER_DHE_RSA_WITH_AES_256_CBC_SHA256          NBIT9
#define SSL_CIPHER_ECDHE_RSA_WITH_AES_128_CBC_SHA           NBIT10
#define SSL_CIPHER_ECDHE_RSA_WITH_AES_256_CBC_SHA           NBIT11
#define SSL_CIPHER_ECDHE_RSA_WITH_AES_128_CBC_SHA256        NBIT12
#define SSL_CIPHER_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256      NBIT13
#define SSL_CIPHER_ECDHE_RSA_WITH_AES_128_GCM_SHA256        NBIT14
#define SSL_CIPHER_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256      NBIT15

/* All ciphers that use ECC crypto only. This excludes ciphers that use RSA. They use ECDSA instead. 
   These ciphers are turned off by default at startup.
   The application may enable them if it has an ECC math engine (like ATECC508). */
#define SSL_ECC_ONLY_CIPHERS                            \
(                                                       \
	SSL_CIPHER_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256  |   \
	SSL_CIPHER_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256      \
)

/* All supported ECC Ciphers including those ciphers that depend on RSA and ECC. 
   These ciphers are turned off by default at startup.
   The application may enable them if it has an ECC math engine (like ATECC508). */
#define SSL_ECC_CIPHERS_ALL_128                         \
(                                                       \
    SSL_CIPHER_ECDHE_RSA_WITH_AES_128_CBC_SHA       |   \
    SSL_CIPHER_ECDHE_RSA_WITH_AES_128_CBC_SHA256    |   \
    SSL_CIPHER_ECDHE_RSA_WITH_AES_128_GCM_SHA256    |   \
    SSL_CIPHER_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256  |   \
    SSL_CIPHER_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256      \
)

#define SSL_NON_ECC_CIPHERS_AES_128                     \
(                                                       \
    SSL_CIPHER_RSA_WITH_AES_128_CBC_SHA           |     \
    SSL_CIPHER_RSA_WITH_AES_128_CBC_SHA256        |     \
    SSL_CIPHER_DHE_RSA_WITH_AES_128_CBC_SHA       |     \
    SSL_CIPHER_DHE_RSA_WITH_AES_128_CBC_SHA256    |     \
    SSL_CIPHER_RSA_WITH_AES_128_GCM_SHA256        |     \
    SSL_CIPHER_DHE_RSA_WITH_AES_128_GCM_SHA256          \
)
/*!<
    All supported AES-128 Ciphers (ECC ciphers are not counted). This is the default active group after startup.
*/


#define SSL_ECC_CIPHERS_AES_256                 \
(                                               \
    SSL_CIPHER_ECDHE_RSA_WITH_AES_256_CBC_SHA   \
)
/*!<
    ECC AES-256 supported ciphers.
*/


#define SSL_NON_ECC_CIPHERS_AES_256                     \
(                                                       \
    SSL_CIPHER_RSA_WITH_AES_256_CBC_SHA         |       \
    SSL_CIPHER_RSA_WITH_AES_256_CBC_SHA256      |       \
    SSL_CIPHER_DHE_RSA_WITH_AES_256_128_CBC_SHA |       \
    SSL_CIPHER_DHE_RSA_WITH_AES_256_CBC_SHA256          \
)
/*!<
    AES-256 Ciphers.
    This group is disabled by default at startup because the WINC1500 HW Accelerator 
    supports only AES-128. If the application needs to force AES-256 cipher support, 
    it could enable them (or any of them) explicitly by calling sslSetActiveCipherSuites.
*/


#define SSL_CIPHER_ALL                                  \
(                                                       \
    SSL_CIPHER_RSA_WITH_AES_128_CBC_SHA             |   \
    SSL_CIPHER_RSA_WITH_AES_128_CBC_SHA256          |   \
    SSL_CIPHER_DHE_RSA_WITH_AES_128_CBC_SHA         |   \
    SSL_CIPHER_DHE_RSA_WITH_AES_128_CBC_SHA256      |   \
    SSL_CIPHER_RSA_WITH_AES_128_GCM_SHA256          |   \
    SSL_CIPHER_DHE_RSA_WITH_AES_128_GCM_SHA256      |   \
    SSL_CIPHER_RSA_WITH_AES_256_CBC_SHA             |   \
    SSL_CIPHER_RSA_WITH_AES_256_CBC_SHA256          |   \
    SSL_CIPHER_DHE_RSA_WITH_AES_256_128_CBC_SHA     |   \
    SSL_CIPHER_DHE_RSA_WITH_AES_256_CBC_SHA256      |   \
    SSL_CIPHER_ECDHE_RSA_WITH_AES_128_CBC_SHA       |   \
    SSL_CIPHER_ECDHE_RSA_WITH_AES_128_CBC_SHA256    |   \
    SSL_CIPHER_ECDHE_RSA_WITH_AES_128_GCM_SHA256    |   \
    SSL_CIPHER_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256  |   \
    SSL_CIPHER_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256  |   \
    SSL_CIPHER_ECDHE_RSA_WITH_AES_256_CBC_SHA           \
)

// SocketEvents. See m2m_socket_handle_events().
typedef enum
{
    M2M_SOCKET_BIND_EVENT          = 1,     // Bind socket event
    M2M_SOCKET_LISTEN_EVENT        = 2,     // Listen socket event
    M2M_SOCKET_DNS_RESOLVE_EVENT   = 3,     // DNS Resolution event
    M2M_SOCKET_ACCEPT_EVENT        = 4,     // Socket accept event
    M2M_SOCKET_CONNECT_EVENT       = 5,     // Socket connect event
    M2M_SOCKET_RECV_EVENT          = 6,     // Socket recv event
    M2M_SOCKET_SEND_EVENT          = 7,     // Socket send event
    M2M_SOCKET_SENDTO_EVENT        = 8,     // Socket sendto event
    M2M_SOCKET_RECVFROM_EVENT      = 9,     // Socket recvfrom event
    M2M_SOCKET_PING_RESPONSE_EVENT = 10     // Ping response event
} t_m2mSocketEventType;

typedef enum
{
    M2M_SSL_REQ_CERT_VERIF,
    M2M_SSL_REQ_ECC,
    M2M_SSL_RESP_ECC, 
    M2M_SSL_IND_CRL,
    M2M_SSL_IND_CERTS_ECC,
	M2M_SSL_REQ_SET_CS_LIST,
	M2M_SSL_RESP_SET_CS_LIST
} t_m2mSslCmd;

typedef int8_t  SOCKET;  
 

// SSL Certificate Expiry Validation Options
typedef enum
{

    SSL_CERT_EXP_CHECK_DISABLE, // ALWAYS OFF.  Ignore certificate expiration date validation. 
                                // If a certificate is expired or there is no configured system time, 
                                // the SSL connection SUCCEEDs.

    SSL_CERT_EXP_CHECK_ENABLE,  // ALWAYS ON.  Validate certificate expiration date. 
                                // If a certificate is expired or there is no configured 
                                // system time, the SSL connection FAILs.

    SSL_CERT_EXP_CHECK_EN_IF_SYS_TIME  // CONDITIONAL VALIDATION (Default setting at startup).
                                       // Validate the certificate expiration date only 
                                       // if there is a configured system time.  If there 
                                       // is no configured system time, the certificate 
                                       // expiration is bypassed and the SSL connection SUCCEEDs.
} t_m2mSslCertExpSettings;

// TLS certificate revocation list.  Certificate data for inclusion in a revocation list (CRL)
typedef struct 
{
    uint8_t    dataLen;                    // Length of certificate data (maximum is M2M_WIFI_TLS_CRL_DATA_MAX_LEN)
    uint8_t    data[M2M_WIFI_TLS_CRL_DATA_MAX_LEN]; // certificate data
    uint8_t    padding[3];                 // do not use
} t_m2mWifiTlsCrlEntry;

// Certificate revocation list details
typedef struct 
{
    uint8_t            crlType;                     // Type of certificate data contained in list
                                                    // M2M_TLS_CRL_TYPE_NONE or TLS_CRL_TYPE_HASH
    uint8_t            padding[3];                  // do not use 
    t_m2mWifiTlsCrlEntry    tlsCrl[M2M_WIFI_TLS_CRL_MAX_ENTRIES]; // list of CRL's
} tstrTlsCrlInfo;

typedef enum
{
    M2M_PING_SUCCESS          = 0,
    M2M_PING_DEST_UNREACHABLE = 1,
    M2M_PING_TIMEOUT          = 2
} t_m2mPingErrorCode;

typedef enum
{
    SOCK_ERR_NO_ERROR               =  0,       // Successful socket operation
    SOCK_ERR_INVALID_ADDRESS        = -1,       // Socket address is invalid. The socket operation cannot 
                                                //  be completed successfully without specifying a specific 
                                                //  address For example: bind is called without specifying a 
                                                //  port number
    SOCK_ERR_ADDR_ALREADY_IN_USE    = -2,       // Socket operation cannot bind on the given address. 
                                                //  With socket operations, only one IP address per socket 
                                                //  is permitted. Any attempt for a new socket to bind 
                                                //  with an IP address already bound to another open 
                                                //  socket will return the following error code. 
    SOCK_ERR_MAX_TCP_SOCK           = -3,       // Exceeded the maximum number of TCP sockets. See TCP_SOCK_MAX.        
    SOCK_ERR_MAX_UDP_SOCK           = -4,       // Exceeded the maximum number of UDP sockets.  See UDP_SOCK_MAX.             
    SOCK_ERR_INVALID_ARG            = -6,       // Invalid argument is passed to a function socket()            
    SOCK_ERR_MAX_LISTEN_SOCK        = -7,       // Exceeded the maximum number of TCP passive listening sockets            
    SOCK_ERR_INVALID                = -9,       // The requested socket operation is not valid in the
                                                //  current socket state. For example, it would not be valid
                                                //  to call accept before calling bind or listen.
    SOCK_ERR_ADDR_IS_REQUIRED       = -11,      // Destination address is required. Failed to provide the socket 
                                                //  address required for the socket operation to be completed.
                                                //  This error is generated if sendto function called when 
                                                //  the address required to send the data to is not known. 
    SOCK_ERR_CONN_ABORTED           = -12,      // The socket is closed by the peer. The local socket is
                                                //  closed also.
    SOCK_ERR_TIMEOUT                = -13,      // The socket pending operation has  timed out   
    SOCK_ERR_BUFFER_FULL            = -14,      // No buffer space available to be used for the requested 
                                                //  socket operation            
} t_socketError;

//==============================================================================
// MACROS
//==============================================================================    
#ifdef HOST_MCU_BIG_ENDIAN
    #define _htonl(x)   (x)
    #define _htons(x)   (x)
#else
    #define _htonl(x) ((((uint32_t)(x) & 0xff000000) >> 24) |    \
                       (((uint32_t)(x) & 0x00ff0000) >> 8)  |    \
                       (((uint32_t)(x) & 0x0000ff00) << 8)  |    \
                       (((uint32_t)(x) & 0x000000ff) << 24))

    #define _htons(x) ((((uint16_t)(x) & 0xff00) >> 8) |     \
                       (((uint16_t)(x) & 0x00ff) << 8))
#endif

#define _ntohl              _htonl
#define _ntohs              _htons
         

//==============================================================================
// DATA TYPES
//==============================================================================    
   
// Pointers to socket address structures are typically cast to pointers of this type before
// use in socket function calls    
struct sockaddr
{
    uint16_t    sa_family;      // Socket address family (always AF_INET)
    uint8_t     sa_data[14];    // Maximum size of all the different socket address structures
};

// Used in other socket structures
typedef struct
{
    uint32_t            s_addr;  // Network Byte Order representation of the IPv4 address. For example,
                                 //  the address "192.168.0.10" is represented as 0x0A00A8C0.
} in_addr;

// Socket address structure for IPV4 addresses. Used to specify socket address information 
// to which to connect to.  Can be cast to sockaddr structure.
struct sockaddr_in
{
    uint16_t    sin_family;     // Must be AF_INET (only ipV4 supported)
    uint16_t    sin_port;       // Port number of socket.  Network sockets are 
                                //  identified by a pair of IP addresses and port number.
                                //  It must be set in the Network Byte Order format.  
                                //  Cannot have a zero value.
    
    in_addr     sin_addr;       // IP Address of the socket.  See in_addr structure.
                                //  Can be set to 0 to accept any IP address for server
                                //  option.  Otherwise must be non-zero.

    uint8_t     padding[8];     // Padding to make structure the same size as sockaddr struct
};


// One of the event structures in t_socketEventData used when a remote host has 
// accepted a connection.  Corresponds to the M2M_SOCKET_ACCEPT_EVENT.
typedef struct
{
    SOCKET              sock;    // On a successful accept operation, the return information 
                                 //  is the socket ID for the accepted connection with the 
                                 //  remote peer. Otherwise a negative error code is returned 
                                 //  to indicate failure of the accept operation.
    
    struct sockaddr_in  strAddr; //Socket address structure for the remote peer.
} t_socketAccept;

// One of the event structures in t_socketEventData used when a connection has 
// occurred.  Corresponds to the M2M_SOCKET_CONNECT_EVENT.
typedef struct
{
    SOCKET    sock;        // Socket ID referring to the socket passed to the connect function call
    int8_t    error;       // Connect error code.  Zero if successful, else negative.
                           //  See t_socketError
} t_socketConnect;

// One of the event structures in t_socketEventData used when a DNS reply is received.
// Corresponds to the M2M_SOCKET_DNS_RESOLVE_EVENT.
typedef struct
{
    char        hostName[M2M_HOSTNAME_MAX_SIZE]; // host name string
    uint32_t    hostIp;                         // host IP address (big-endian)
} t_dnsReply;

// One of the event structures in t_socketEventData used when socket data is received.
// Corresponds to the M2M_SOCKET_RECV_EVENT or M2M_SOCKET_RECVFROM_EVENT events.
// In case the received data from the remote peer is larger than the user buffer size defined 
// during the asynchronous call to the recv function, the data is delivered to the user in a 
// number of consecutive chunks according to the user Buffer size.  A negative or zero buffer 
// size indicates an error with the following code:
//    SOCK_ERR_NO_ERROR      - Socket connection  closed
//    SOCK_ERR_CONN_ABORTED  - Socket connection aborted
//    SOCK_ERR_TIMEOUT       - Socket receive timed out
typedef struct
{
    uint8_t     *p_rxBuf;       // Pointer to the user buffer (passed to recv or recvfrom function) 
                                //  containing the received data chunk

    int16_t     bufSize;        // The received data chunk size.  Holds a negative value 
                                //  if there is a receive error or ZERO on success upon reception 
                                //  of close socket message.

    uint16_t    remainingSize;  // The number of bytes remaining in the current recv operation

    struct sockaddr_in  ai_addr; // Only valid for M2M_SOCKET_RECVFROM_EVENT event (UDP sockets).  
                                 // Socket address structure for the remote peer. 
} t_socketRecv;

// One of the event structures in t_socketEventData used when a Ping response
// is received.  Corresponds to the M2M_SOCKET_PING_RESPONSE_EVENT.
typedef struct
{
    uint32_t              u32StaticIp;   // IP address of ping responder
    uint32_t              rtt;           // round trip time of ping
    t_m2mPingErrorCode    errorCode;     // M2M_PING_SUCCESS if successful
} t_pingReply;


typedef union t_socketEventData
{
    int8_t            bindStatus;             // 0 if bind successful, else error code
    int8_t            listenStatus;           // 0 if listen successful, else error code
    int16_t           numSendBytes;           
    t_socketAccept    acceptResponse;   
    t_socketConnect   connectResponse;
    t_dnsReply        dnsReply;
    t_socketRecv      recvMsg;
    t_pingReply       pingReply;
} t_socketEventData;

typedef void (*tpfAppSocketCb) (SOCKET sock, uint8_t u8Msg, void *pvMsg);
typedef void (*tpfAppResolveCb) (char* pu8DomainName, uint32_t u32ServerIP);

//==============================================================================
// FUNCTION PROTOTYPES
//==============================================================================    
 
/*******************************************************************************
  Function:
    SOCKET socket(uint16_t u16Domain, uint8_t u8Type, uint8_t u8Flags)

  Summary:
    Creates a socket.

  Description:
    Creates a socket on which other operations can be performed.

  Parameters:
    domain -- Socket family. The only allowed value is AF_INET (IPv4) for TCP/UDP sockets
    type   -- Socket type (SOCK_DGRAM or SOCK_STREAM) 
    flags  -- Used to specify the socket creation. It should be set to zero for normal 
              TCP/UDP sockets.  If creating an SSL session use SOCKET_FLAGS_SSL (only allowed
              if type is SOCK_STREAM.
 
  Returns:
    On successful socket creation, a non-blocking socket type is created and a 
    socket ID is returned.  If this function fails, a negative value is returned.
    See t_socketError.
 *****************************************************************************/
SOCKET socket(uint16_t u16Domain, uint8_t u8Type, uint8_t u8Flags);

/*******************************************************************************
  Function:
    int8_t bind(SOCKET sock, struct sockaddr *my_addr, uint8_t addrlen)

  Summary:
    Associates the provided address and local port to a socket.

  Description:
    The function must be used with both TCP and UDP sockets before starting any 
    UDP or TCP server operation. Upon socket bind completion the application 
    will receive a M2M_SOCKET_BIND_EVENT in m2m_socket_handle_events().

  Parameters:
    sock     -- Socket ID returned from socket().  Must be non-negative.
    sockaddr -- Pointer to sockaddr_in structure
    addrlen  -- Size, in bytes, of sockaddr_in structure
 
  Returns:
    The function returns 0 on success and a negative value otherwise.
    See t_socketError. 
 *****************************************************************************/
int8_t bind(SOCKET sock, struct sockaddr *my_addr, uint8_t addrlen);

/*******************************************************************************
  Function:
    int8_t listen(SOCKET sock, uint8_t backlog)

  Summary:
    Waits for an incoming connection.

  Description:
    After the bind() call, this function can be called to listen (wait) for an 
    incoming connection.  Upon the listen succeeding, the application will  
    receive a M2M_SOCKET_LISTEN_EVENT in m2m_socket_handle_events().  If 
    successful, the TCP server operation is active and is ready to accept connections.
  
  Parameters:
    sock     -- Socket ID returned from socket().  Must be non-negative.
    backlog  -- Not used in the WINC1500 driver.  
 
  Returns:
    The function returns zero on success and a negative value otherwise.
    See t_socketError. 
 *****************************************************************************/
int8_t listen(SOCKET sock, uint8_t backlog);

/*******************************************************************************
  Function:
    int8_t accept(SOCKET sock, struct sockaddr *addr, uint8_t *addrlen)

  Summary:
    Place holder for BSD accept function

  Description:
    The function, in the WINC1500 driver, does not perform any work.  It is present
    in the driver to support legacy code, or, to write code that adheres to the 'normal'
    BSD socket interface.  Normally, accept() is called after listen().  It is not 
    required to call this function.  When a client connects to the WINC1500, the 
    application will receive a M2M_SOCKET_ACCEPT_EVENT in m2m_socket_handle_events().
   
  Parameters:
    sock    -- Socket ID returned from socket().  Must be non-negative.
    addr    -- Not used in the WINC1500 driver 
    addrlen -- Not used in the WINC1500 driver
 
  Returns:
    The function returns zero on success and a negative value otherwise.
    See t_socketError.  Only the sock parameter is checked. 
 *****************************************************************************/
int8_t accept(SOCKET sock, struct sockaddr *addr, uint8_t *addrlen);

/*******************************************************************************
  Function:
    int8_t connect(SOCKET sock, struct sockaddr *server_addr, uint8_t addrlen)

  Summary:
    Connects to a remote server.

  Description:
    Once a socket has been created this function can be called to connect to a 
    remote server.  If bind was not called previously, the socket is bound to the
    WINC1500 IP address and a random local port number.  This is the typical usage.
    If bind is called prior to connect, then the client socket will use the designated
    IP address and port number.
 
    When the connection completes the application will receive a M2M_SOCKET_CONNECT_EVENT 
    in m2m_socket_handle_events(); at that point the TCP session is active and 
    data can be sent and received.
   
  Parameters:
    sock         -- Socket ID returned from socket().  Must be non-negative.
    server_addr  -- Pointer to socket address structure sockaddr_in (address of remote server)
    addrlen      -- Size of the given socket address structure in bytes. Not currently used, 
                    implemented for BSD compatibility only.
 
  Returns:
    The function returns zero on success and a negative value otherwise.
    See t_socketError.
 *****************************************************************************/
int8_t connect(SOCKET sock, struct sockaddr *server_addr, uint8_t addrlen);


/*******************************************************************************
  Function:
    int16_t recv(SOCKET sock, void *buf, uint16_t len, uint32_t timeout)

  Summary:
    Receives data from a TCP socket.

  Description:
    Once a TCP socket has connected, this function can be called to await incoming
    data from the remote server.  When data is received the application will 
    receive a M2M_SOCKET_RECV_EVENT in m2m_socket_handle_events()
 
  Parameters:
    sock    -- Socket ID returned from socket().  Must be non-negative.
    buf     -- Pointer to application buffer that the received data will be written to
    len     -- size of buf, in bytes
    timeout -- Time, in milliseconds, to wait for receive data.  If the timeout 
               parameter is set to 0 then the socket will wait forever for data to be received.
 
               If a timeout occurs, the M2M_SOCKET_RECV_EVENT is still generated, but, 
               the event data field 'bufSize' (see t_socketRecv) will be a negative 
               value equal to SOCK_ERR_TIMEOUT.  
 
  Returns:
    The function returns zero on success and a negative value otherwise.
    See t_socketError.
 *****************************************************************************/
int8_t recv(SOCKET sock, void *buf, uint16_t len, uint32_t timeout);


/*******************************************************************************
  Function:
    int16_t recvfrom(SOCKET sock, void *buf, uint16_t len, uint32_t timeout)

  Summary:
    Receives data from a UDP socket.

  Description:
    Once a UDP socket has been created and bound (see bind()) this function can
    be called to await incoming data.  When data is received the the application will 
    receive a M2M_SOCKET_RECVFROM_EVENT in m2m_socket_handle_events().
 
  Parameters:
    sock    -- Socket ID returned from socket().  Must be non-negative.
    buf     -- Pointer to application buffer that the received data will be written to
    len     -- size of buf, in bytes
    timeout -- Time, in milliseconds, to wait for receive data.  If the timeout 
               parameter is set to 0 then the socket will wait forever for data to be received.
 
               If a timeout occurs, the M2M_SOCKET_RECVFROM_EVENT is still generated, but, 
               the event data field 'bufSize' (see t_socketRecv) will be a negative 
               value equal to SOCK_ERR_TIMEOUT.  
 
  Returns:
    The function returns zero on success and a negative value otherwise.
    See t_socketError.
 *****************************************************************************/
int8_t recvfrom(SOCKET sock, void *buf, uint16_t len, uint32_t timeout);


/*******************************************************************************
  Function:
    int16_t send(SOCKET sock, void *buf, uint16_t len, uint16_t flags)

  Summary:
    Sends data from a locally TCP created socket to a remote TCP socket.

  Description:
    This function is used when sending from a TCP socket to a remote TCP socket.
    Once a TCP socket has been created and connected to a remote host this function can
    be called to send data to the remote host.  After the data is sent the application
    will receive a M2M_SOCKET_SEND_EVENT in m2m_socket_handle_events().
 
  Parameters:
    sock  -- Socket ID returned from socket().  Must be non-negative.
    buf   -- Pointer to application buffer containing data to be sent
    len   -- Number of bytes to send.  Must be less than or equal to SOCKET_BUFFER_MAX_LENGTH
    flags -- Not used by WINC1500 driver
 
  Returns:
    The function returns zero on success and a negative value otherwise.
    See t_socketError.
 *****************************************************************************/
int8_t send(SOCKET sock, void *buf, uint16_t len, uint16_t flags);


/*******************************************************************************
  Function:
    int16_t sendto(SOCKET sock, void *buf, uint16_t len, uint16_t flags, struct sockaddr *to, uint8_t addrlen);


  Summary:
    Sends data from a locally UDP created socket to a remote UDP socket.

  Description:
    This function is used when sending from a UDP socket to a remote UDP socket.
    Once a UDP socket has been created and bound this function can be called to 
    send data to the remote host.  After the data is sent the application will 
    receive a M2M_SOCKET_SENDTO_EVENT in m2m_socket_handle_events().
 
  Parameters:
    sock  -- Socket ID returned from socket().  Must be non-negative.
    buf   -- Pointer to application buffer containing data to be sent
    len   -- Number of bytes to send.  Must be less than or equal to SOCKET_BUFFER_MAX_LENGTH
    flags -- Not used by WINC1500 driver
    to    -- Destination address
    tolen -- to  length in bytes.  Not used by WINC1500 driver; only included for 
             BSD compatibility 
 
  Returns:
    The function returns zero on success and a negative value otherwise.
    See t_socketError.
 *****************************************************************************/
int8_t sendto(SOCKET sock, void *buf, uint16_t len, uint16_t flags, struct sockaddr *to, uint8_t tolen);


/*******************************************************************************
  Function:
    int8_t close(SOCKET sock)

  Summary:
    Closes previously created socket.

  Description:
    Closes socket.  If close() is called while there are still pending messages 
    (sent or received ) they will be discarded.
 
  Parameters:
    sock  -- Socket ID returned from socket().  Must be non-negative.
 
  Returns:
    The function returns zero on success and a negative value otherwise.
    See t_socketError.
 *****************************************************************************/
int8_t close(SOCKET sock);

/*******************************************************************************
  Function:
    int8_t setsockopt(SOCKET socket, uint8_t level, uint8_t optname, const void *optval, uint16_t optlen)

  Summary:
    Sets socket options.

  Description:
    Sets socket options.
 
  Parameters:
    socket  -- Socket ID returned from socket().  Must be non-negative.
    level   -- Protocol level (must be set to SOL_SOCKET or SOL_SSL_SOCKET)
    optname -- Option to be set.  
               If level is SOL_SOCKET then values are:
                <table>
                 SO_SET_UDP_SEND_CALLBACK       Enable/disable M2M_SOCKET_SEND_EVENT for UDP sockets.
                                                 Since UDP is unreliable the application may not be
                                                 interested in this event.  Enabled if optval points to a 
                                                 true value; disabled if optval points to a false value.
                 IP_ADD_MEMBERSHIP              Applies only to UDP sockets.  This option is used to 
                                                 receive frames sent to a multicast group.  The desired
                                                 multicast IP address should be in a uint32_t pointed 
                                                 to by optval.
                 IP_DROP_MEMBERSHIP             Applies only to UDP sockets.  This option is used to 
                                                 stop receiving frames from a multicast group.  optval 
                                                 should point to a uint32_t multicast IP address.
                </table>
                
               If level is SOL_SSL_SOCKET then values are:
                <table>
                 SO_SSL_BYPASS_X509_VERIF       This option allows an opened SSL socket to bypass the 
                                                 X509 certificate verification process.  It is highly 
                                                 recommended to NOT use this socket option in production
                                                 software; it should only be used for debugging.  The optval
                                                 should point to an int boolean value.
                 SO_SSL_SNI                     This option sets the Server Name Indicator (SNI) for an 
                                                 SSL socket.  optval should point to a null-terminated 
                                                 string containing the server name associated with the 
                                                 connection.  The name length must be less than or equal to 
                                                 M2M_HOSTNAME_MAX_SIZE.   
                 SO_SSL_ENABLE_SESSION_CACHING  This option allows the TLS to cache the session information
                                                 for faster TLS session establishment in future connections
                                                 using the TLS Protocol session resume feature.
                </table>
 
    optval -- Pointer to the option value
    optlen -- Length of optval, in bytes
 
  Returns:
    The function returns zero on success and a negative value otherwise.
    See t_socketError.
 *****************************************************************************/
int8_t setsockopt(SOCKET        socket, 
                  uint8_t       level, 
                  uint8_t       optname,
                  const void    *optval, 
                  uint16_t      optlen);

/*******************************************************************************
  Function:
    int8_t getsockopt(SOCKET sock, uint8_t level, uint8_t optname, const void *optval, uint8_t *optlen)

  Summary:
    Gets socket options.

  Description:
    Gets socket options.
 
  Parameters:
    socket  -- Socket ID returned from socket().  Must be non-negative.
    level   -- Protocol level (must be set to SOL_SOCKET or SOL_SSL_SOCKET)
    optname -- Option to be get (see setsockopt())
    optval  -- Pointer to buffer where option value will be written
    optlen  -- Size of buffer pointed to by optval,in bytes (used to prevent overflow) 
 
  Returns:
    The function returns zero on success and a negative value otherwise.
    See t_socketError.
 *****************************************************************************/
int8_t getsockopt(SOCKET sock, uint8_t level, uint8_t optname, const void *optval, uint8_t *optlen);


/*******************************************************************************
  Function:
    int8_t gethostbyname(const char *name)

  Summary:
    Requests a DNS look-up to get the IP address of the specified host name.

  Description:
    Requests a DNS look-up of a host name.  After the WINC1500 resolves the name the 
    application will receive a M2M_SOCKET_DNS_RESOLVE_EVENT in m2m_socket_handle_events().
 
  Parameters:
    name  -- Name to resolve (a null-terminated string).  The name must be less than 
             or equal to M2M_HOSTNAME_MAX_SIZE.
 
  Returns:
    The function returns zero on success and a negative value otherwise.
    See t_socketError.
 *****************************************************************************/
int8_t gethostbyname(const char *name);


/*******************************************************************************
  Function:
    void m2m_ping_req(uint32_t destIpAddress, uint8_t ttl)

  Summary:
    Sends a ping request to the specified IP address.

  Description:
    Sends a ping request to the specified IP address.
 
  Parameters:
    destIpAddress -- Destination IP address in big-endian format.  
    ttl           -- IP TTL value for the ping request. If set to zero then default value will be used.
 
  Returns:
    None
 *****************************************************************************/
void m2m_ping_req(uint32_t destIpAddress, uint8_t ttl);

/*******************************************************************************
  Function:
    int8_t sslEnableCertExpirationCheck(uint8_t enable)

  Summary:
    Enable/Disable the WINC1500 SSL certificate expiration check. 

  Description:
    Enable/Disable the WINC1500 SSL certificate expiration check. 
 
  Parameters:
    enable -- 0: Ignore certificate expiration time check while handling 
                  TLS.Handshake.Certificate Message. If there is no system time 
                  available or the certificate is expired, the TLS Connection 
                  shall succeed.
 
              1: Validate the certificate expiration time.  If there is no system 
                 time or the certificate expiration is detected, the TLS Connection 
                 shall fail.
 
  Returns:
    The function returns zero on success and a negative value otherwise.
    See t_socketError.
 *****************************************************************************/
int8_t sslEnableCertExpirationCheck(uint8_t enable);





#ifdef  __cplusplus
    }
#endif /* __cplusplus */

#endif // __WF_SOCKET_H

