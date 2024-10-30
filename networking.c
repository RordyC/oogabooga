typedef enum { ADDRESS_IPV4, ADDRESS_IPV6, ADDRESS_INVALID} addressType;

typedef struct {
    addressType type;
    union {uint8_t ipv4[4]; uint16_t ipv6[8];} data;
    unsigned short port;

} address;

const uint32_t ProtocolID = 0x27052004;

typedef enum
{
    UNRELIABLE,
    CONNECT,
    RESPONSE,
    REJECT,
    HEARTBEAT,
    DISCONNECT,
    NUM_PACKET_TYPES
} PacketType;

typedef struct{
    u8 protocolID;
    PacketType type;
    u32 size;
} packet;

typedef struct{
    PacketType type;
    uint64_t clientSalt;
    u8 padding[512];
} connectionRequestPacket;

typedef struct{
    PacketType type;
    uint64_t serverSalt;
    uint64_t clientSalt;
} connectionChallengePacket;

typedef struct{
    PacketType type;
    uint64_t challengeSalt; // XOR of client and server salt values.
    u8 padding[512];
} connectionResponsePacket;

#define MAX_CLIENTS 1

typedef struct{
    address clientAddress;
    uint64_t clientSalt;
    uint64_t serverSalt;
    double timeSinceRequest;
} pendingClientConnection;

typedef struct {
    double time;
    int maxClients;
    int numClientsConnected;
    bool isClientConnected[MAX_CLIENTS];
    double clientsLastPacketRecievedTime[MAX_CLIENTS];
    address clientsAddress[MAX_CLIENTS];
    pendingClientConnection pendingConnections[MAX_CLIENTS];
    int pendingConnectionsCount;
    address serverAddress;
    SOCKET serverSocket;
} server;

typedef enum {
   CLIENT_DISCONNECTED,
   CLIENT_REQUESTING_CONNECTION,
   CLIENT_SENDING_CHALLENGE_RESPONSE,
   CLIENT_CONNECTED,
} clientState;

typedef struct {
    SOCKET clientSocket;
    uint64_t clientSalt;
    uint64_t serverSalt;
    double lastPacketSendTime;
    double lastPacketRecieveTime;
    address serverAddress;
    clientState state;
} client;

SOCKET createSocketUDP(address addr)
{
    printf("Creating UDP  Socket\n");

    SOCKET newSocket = INVALID_SOCKET;

    // Create a SOCKET for the server to listen for client connections
    newSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (newSocket == INVALID_SOCKET)
    {
        printf("Socket endpoint creation failed with error: %ld\n", WSAGetLastError());
        WSACleanup();
        return INVALID_SOCKET;
    }

    // Address in dotted decimal format.
    // TODO: IVP6 support??
    unsigned int addrDD = (addr.data.ipv4[0] << 24) |
                          (addr.data.ipv4[1] << 16) |
                          (addr.data.ipv4[2] << 8 ) |
                          (addr.data.ipv4[3]);

    struct sockaddr_in service;
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = htonl(addrDD);
    service.sin_port = htons(addr.port);

    if (bind(newSocket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR)
    {
        printf("Bind failed with error: %d\n", WSAGetLastError());
        closesocket(newSocket);
        WSACleanup();
        return INVALID_SOCKET;
    }

    // Set socket as non-blocking
    DWORD nonBlocking = 1;
    if (ioctlsocket(newSocket, FIONBIO, &nonBlocking) != 0)
    {
        printf("Failed to setup non-blocking with error: %d\n", WSAGetLastError());
        closesocket(newSocket);
        WSACleanup();
        return INVALID_SOCKET;
    }

    return newSocket;
 
}

address addressIPV4(char * addressStr, unsigned short port)
{
    unsigned long addr = ntohl(inet_addr(addressStr));
    if (addr != -1)
    {
        return (address){ADDRESS_IPV4, {addr >> 24, addr >> 16 & 0xFF, addr >> 8 & 0xFF, addr & 0xFF}, port};
    }
    return (address){ADDRESS_INVALID, {}, 0};
}

// Creates an IPV4 address struct from a decimal dot notation address and a port number.
address addressIPV4DD(unsigned long addressL, unsigned short port)
{
    unsigned long addr = addressL;
    if (addr != -1)
    {
        return (address){ADDRESS_IPV4, {addr >> 24, addr >> 16 & 0xFF, addr >> 8 & 0xFF, addr & 0xFF}, port};
    }
    return (address){ADDRESS_INVALID, {}, 0};
}

int addressEqual(address a, address b)
{
    if (a.type != b.type || a.port != b.port) { return 0; }

    if (a.type == ADDRESS_IPV4)
    {
        for (int i = 0; i < 4; i++)
        {
            if (a.data.ipv4[i] != b.data.ipv4[i]) { return 0;}
        }
        return 1;
    }
    return 0;
}

int serverFindClientIndex(server * server, address addr)
{
    for (int i = 0; i < server->maxClients; i++)
    {
        if (server->isClientConnected[i] && addressEqual(server->clientsAddress[i], addr))
        {
            return i;
        }
    }
    return -1;
}
#define SERVER_CONNECTION_TIMEOUT_TIME 4
void serverCheckPendingConnectionsTimeout(server * SERVER)
{
    for (int i = 0; i < SERVER->pendingConnectionsCount; i++)
    {
        pendingClientConnection * connection = &(SERVER->pendingConnections[i]);
        if (SERVER->time > (connection->timeSinceRequest + SERVER_CONNECTION_TIMEOUT_TIME)) 
        {
            // "Remove Connection" (shift elements if needed)
            for (int j = i + 1; j < SERVER->pendingConnectionsCount; i++)
            {
                SERVER->pendingConnections[i] = SERVER->pendingConnections[j];
            }
            SERVER->pendingConnectionsCount--;
        }
    }
}

void serverProcessChallengeResponsePacket(server * SERVER, address from, void * payload, unsigned int size)
{
    // TODO:
    for (int i = 0; i < SERVER->maxClients; i++)
    {
        if (!SERVER->isClientConnected[i])
        {
            SERVER->isClientConnected[i] = 1;
            SERVER->clientsAddress[i] = from;
            SERVER->numClientsConnected++;
            printf("SERVER: Client connected at index: (%d)\n", i);
            // SEND ACCEPT PACKET!!!!
        }
    }
}
void serverProcessPacket(server * server, address from, void * payload, unsigned int size)
{
    PacketType type = *((u8*)payload);
    switch (type)
    {
        case CONNECT:
            printf("SERVER: Received CONNECT Packet from (%d.%d.%d.%d):%d\n",
                from.data.ipv4[0], from.data.ipv4[1], from.data.ipv4[2], from.data.ipv4[3],
                from.port);
            
            // Check if client is already connected.
            int existingClientIndex = serverFindClientIndex(server, from);
            if (existingClientIndex >= 0)
            {
                printf("SERVER: Client (%d) already connected, denying connection.\n", existingClientIndex);
                // Send connection denied (already connected) packet.
            }

            // Send reject packet if server is full.
            if (server->numClientsConnected == server->maxClients)
            {
                printf("SERVER: Server full, denying connection.\n", existingClientIndex);
                // Send connection denied (server full) packet.
            }
            else // Find/add pending connection.
            {
                uint64_t clientSalt = *((uint64_t*)(((u8*)payload) + 1));
                for (int i = 0; i < server->pendingConnectionsCount; i++)
                {
                    pendingClientConnection * pendingConn = &server->pendingConnections[i];
                        // Check for matching address + salt.
                    if (addressEqual(pendingConn->clientAddress, from) && pendingConn->clientSalt == clientSalt)
                    {
                        // Resend challenge packet to client.
                        break;
                    }
                }
                if (server->pendingConnectionsCount == server->maxClients)
                {
                    // Send connection denied (max connection requests).
                    break;
                }

                pendingClientConnection * pendingConn = &server->pendingConnections[server->pendingConnectionsCount];
                pendingConn->clientAddress = from;
                pendingConn->clientSalt = clientSalt;
                pendingConn->timeSinceRequest = server->time;
                // GENERATE RANDOM SERVER SALT. pendingConn->serverSalt = ???
                // SEND CHALLENGE PACKET.
            }
            break;
        case RESPONSE:
            // Process connection response packet:
            uint64_t clientSalt = *((uint64_t*)(((u8*)payload) + 1));
            uint64_t serverSalt = *((uint64_t*)(((u8*)payload) + 1 + 8));

            // Check if already connected
            int existingClientIndex = serverFindClientIndex(server, from);
            if (existingClientIndex >= 0)
            {
                printf("SERVER: Client (%d) already connected, resending connection accepted packet.\n", existingClientIndex);
                // Send connection denied (already connected) packet.
            }

            for (int i = 0; i < server->pendingConnectionsCount; i++)
            {
                pendingClientConnection * pendingConn = server->pendingConnections[i];
                if (addressEqual(pendingConn->clientAddress, from) && pendingConn->clientSalt == clientSalt && pendingConn->serverSalt == serverSalt)
                {
                    // SEND CONNECTION ACCEPTED PACKET FINALLY.
                    // remove pending conn from thing.
                }
            }
            break;
        default:
            break;
    }
}

void serverRecieve(server * Server)
{
    while ( true )
    {
        unsigned char packetData[128];
        unsigned int maxPacketSize = sizeof(packetData);

        struct sockaddr_in from;
        int fromLength = sizeof(from);

        int bytes = recvfrom(Server->serverSocket, 
                              (char*)packetData, 
                              maxPacketSize,
                              0, 
                              (SOCKADDR*)&from, 
                              &fromLength);

        if ( bytes <= 0 )
            break;

        // Check protocol ID.
        uint32_t protocol = *(uint32_t*)&packetData[0];
        if (protocol == ProtocolID)
        {
            // Process packet.
            unsigned long from_address = ntohl( from.sin_addr.s_addr );
            unsigned int from_port = ntohs( from.sin_port );
            address from = addressIPV4DD(from_address, from_port);

            printf("SERVER: Received Packet of size (%d) from (%d.%d.%d.%d):%d\n",
                bytes,
                from.data.ipv4[0], from.data.ipv4[1], from.data.ipv4[2], from.data.ipv4[3],
                from_port);

            serverProcessPacket(Server, from, (void*)&packetData[4], bytes - 4);
            // Check packet is from connected client.
            // process packet
        }
    }
}

server startServer(address serverAddress, unsigned int maxConnections)
{
    server newServer;
    
    newServer.numClientsConnected = 0;
    newServer.maxClients = maxConnections;
    newServer.serverAddress = serverAddress;
    newServer.serverSocket = createSocketUDP(serverAddress);
    newServer.pendingConnectionsCount = 0;
    printf("SERVER STARTED.\n");
    return newServer;
}

void serverUpdate(server * SERVER, double time)
{
    serverCheckPendingConnectionsTimeout(SERVER);
    
    serverRecieve(SERVER);
}

client startClient(address clientAddress)
{
    client newClient;
    newClient.clientSocket = createSocketUDP(clientAddress);
    newClient.state = CLIENT_DISCONNECTED;

    printf("CLIENT STARTED.\n");
    return newClient;
}

void clientConnect(client * CLIENT, address serverAddress)
{
    assert(CLIENT->state == CLIENT_DISCONNECTED);
    CLIENT->clientSalt = 0xDEADBEEF;
    CLIENT->state = CLIENT_REQUESTING_CONNECTION;
    CLIENT->serverAddress = serverAddress;
}

void clientUpdate(client * CLIENT, double time)
{
    // Start recieving stuff/process packets.
    // clientReceive(CLIENT);
    switch (CLIENT->state)
    {
        case CLIENT_REQUESTING_CONNECTION:
            // Check if time since last packet is greater than connection request resend rate.
            if (CLIENT->lastPacketRecieveTime > 5)
            {
                // timeout
            }

            if (CLIENT->lastPacketSendTime >= 0.1)
            {
                // send packet again!
            }
            break;
        case CLIENT_SENDING_CHALLENGE_RESPONSE:
            if (CLIENT->lastPacketRecieveTime > 5)
            {
                // timeout
            }
            if (CLIENT->lastPacketSendTime >= 0.1)
            {
                // send packet again!
            }
            break;
        case CLIENT_CONNECTED:
            
            break;
        default:
            break;
    }
}

void socketSend(SOCKET socket, char * packetData, unsigned int packetSize, address addr)
{
    unsigned int ipv4 = (addr.data.ipv4[0] << 24) |
                        (addr.data.ipv4[1] << 16) |
                        (addr.data.ipv4[2] << 8 ) |
                        (addr.data.ipv4[3]);

    struct sockaddr_in socketAddress;
    socketAddress.sin_family = AF_INET;
    socketAddress.sin_addr.s_addr = htonl(ipv4);
    socketAddress.sin_port = htons(addr.port);

   int sentBytes = sendto(socket, 
                (const char*)packetData, 
                packetSize,
                0, 
                (SOCKADDR*)&socketAddress, 
                sizeof(SOCKADDR));
    
    assert(sentBytes == packetSize)
}

void clientProcessPacket(client * CLIENT, void * packet, int size)
{

}
void clientRecieve(client * CLIENT)
{
       
}

typedef struct{
    void * data;
    int size;
    int index;
} buffer;


void writeInteger(buffer * buf, u32 value)
{
    assert( buf->index + 4 <= buf->size);
    #ifdef BIG_ENDIAN
        *((uint32_t*)(buf->data + buf->index)) = bswap( value ); 
    #else // #ifdef BIG_ENDIAN
        *((uint32_t*)(buf->data + buf->index)) = value; 
    #endif // #ifdef BIG_ENDIAN
    buf->index += 4;
}

int readInteger(buffer * buf)
{
    assert(buf->index + 4 <= buf->size);

    uint32_t value = *((uint32_t*)(buf->data + buf->index));
    #ifdef BIG_ENDIAN
        value = bswap(value)
    #else // #ifdef BIG_ENDIAN
       // Do nothing
    #endif // #ifdef BIG_ENDIAN
     
    buf->index += 4;
    return value;
}

void writeU8(buffer * buf, uint8_t value)
{
    assert( buf->index + 4 <= buf->size)
    #ifdef BIG_ENDIAN
        *((uint8_t*)(buf->data + buf->index)) = bswap(value);
    #else
        *((uint8_t*)(buf->data + buf->index)) = value;
    #endif
}