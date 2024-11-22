typedef enum { ADDRESS_IPV4, ADDRESS_IPV6, ADDRESS_INVALID} addressType;

typedef struct {
    addressType type;
    union {uint8_t ipv4[4]; uint16_t ipv6[8];} data;
    unsigned short port;
} address;

const uint32_t ProtocolID = 0x27052004;

typedef enum
{
    CONNECT,
    PACKET_REJECT,
    PACKET_CHALLENGE,
    PACKET_RESPONSE,
    PACKET_HEARTBEAT,
    PACKET_PAYLOAD,
    DISCONNECT,
    NUM_PACKET_TYPES
} PacketType;

typedef struct{
    PacketType type;
    u32 size;
    void * data;
} packet;

typedef struct{
    PacketType type;
    uint64_t clientSalt;
    u8 padding[500];
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
    bool isConnected;
    double lastPacketReceivedTime;
    address clientAddress;
} serverClientSlot;

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
    // unsigned long addr = ntohl(inet_addr(addressStr)); NOTE: inet_addr function is deprecated when using Winsock2.
    unsigned long addr;
    inet_pton(AF_INET, addressStr, &addr);
    addr = ntohl(addr);

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

uint64_t generateSalt()
{
    return  (((uint64_t)rand() <<  0) & 0x000000000000FFFFull) |
            (((uint64_t)rand() << 16) & 0x00000000FFFF0000ull) |
            (((uint64_t)rand() << 32) & 0x0000FFFF00000000ull) |
            (((uint64_t)rand() << 48) & 0xFFFF000000000000ull);
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

typedef struct{
    void * data;
    int size;
    int index;
} buffer;


void writeU32(buffer * buf, uint32_t value)
{
    assert(buf->index + sizeof(uint32_t) <= buf->size);
    *((uint32_t*)(buf->data + buf->index)) = htonl(value); 
    buf->index += sizeof(uint32_t);
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
    assert( buf->index + sizeof(uint8_t) <= buf->size)
    *((uint8_t*)(buf->data + buf->index)) = value;
    buf->index += sizeof(uint8_t);
}

void writeU64(buffer * buf, uint64_t value)
{
    htonl(0);
    assert( buf->index + sizeof(uint64_t) <= buf->size)
    *((uint64_t*)(buf->data + buf->index)) = htonll(value);
    buf->index += sizeof(uint64_t);
}

u_int64 readU64(void * data)
{
    return ntohll(*((uint64_t*)((u8*)data)));
}

packet createConnectionRequestPacket(u32 protocolID, uint64_t clientSalt)
{
    void * data = alloc(get_heap_allocator(), 512);
    buffer buf = {data, 512, 0};

    writeU32(&buf, protocolID); // TODO: CRC32
    writeU8(&buf, CONNECT);     // Packet Type
    writeU64(&buf, clientSalt); // Client Salt

    for (int i = 0; i < 499; i++)
    {
        writeU8(&buf, 0xF);
    }

    return (packet){CONNECT, 512, data};
}

packet createServerChallengePacket(u32 protocolID, uint64_t clientSalt, uint64_t serverSalt)
{
    void * data = alloc(get_heap_allocator(), 21);
    buffer buf = {data, 21, 0};

    writeU32(&buf, protocolID);      // TODO: CRC32
    writeU8(&buf, PACKET_CHALLENGE); // Packet Type
    writeU64(&buf, clientSalt);      // Client Salt
    writeU64(&buf, serverSalt);      // Server Salt

    return (packet){PACKET_CHALLENGE, buf.size, data};
}

packet createChallengeResponsePacket(uint32_t protocolID, uint64_t salts)
{
    void * data = alloc(get_heap_allocator(), 512);
    buffer buf = {data, 512, 0};

    writeU32(&buf, protocolID);      // TODO: CRC32
    writeU8(&buf, PACKET_RESPONSE);  // Packet Type
    writeU64(&buf, salts);           // XOR of client and server salts

    // Fill the rest of the buffer with 1s
    for (int i = buf.index; i < buf.size; i++)
    {
        writeU8(&buf, 0xF);
    }

    return (packet){PACKET_RESPONSE, buf.size, data};
}

packet createHeartbeatPacket(u32 protocolID, uint64_t salts, uint32_t index)
{
    void * data = alloc(get_heap_allocator(), 17);
    buffer buf = {data, 17, 0};

    writeU32(&buf, protocolID);      // TODO: CRC32
    writeU8(&buf, PACKET_HEARTBEAT); // Packet Type
    writeU64(&buf, salts);           // XOR of client and server salts
    writeU32(&buf, index);
    return (packet){PACKET_HEARTBEAT, buf.size, data};
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
    uint64_t salts = readU64((u8*)payload + 1);
    pendingClientConnection * pendingConn = null;
    for (int i = 0; i < SERVER->pendingConnectionsCount; i++)
    {
        if (addressEqual(SERVER->pendingConnections[i].clientAddress, from))
        {
            pendingConn = &SERVER->pendingConnections[i];
        }
    }

    if (!pendingConn)
    {
        printf("SERVER: Pending connection with matching address could not be found.\n");
        return;
    }

    if ((pendingConn->clientSalt ^ pendingConn->serverSalt) != salts)
    {
        printf("SERVER: Pending connection salt values not not match.\n");
        return;
    }
    
    // Pending client can be connected.
    for (int i = 0; i < SERVER->maxClients; i++)
    {
        if (!SERVER->isClientConnected[i])
        {
            SERVER->isClientConnected[i] = 1;
            SERVER->clientsAddress[i] = from;
            SERVER->numClientsConnected++;
            printf("SERVER: Client connected at index: (%d)\n", i);
            // TODO: Send heartbeat packet.
            // TODO: Remove pending connection.
            return;
        }
    }

    printf("SERVER: Could not find available slot to connect client.\n");
    // TODO: Send connection rejected packet.
}

void serverProcessConnectPacket(server * SERVER, address from, void * payload, unsigned int size)
{
    printf("SERVER: Received CONNECT Packet from (%d.%d.%d.%d):%d\n",
           from.data.ipv4[0], from.data.ipv4[1], from.data.ipv4[2], from.data.ipv4[3],
           from.port);

    // Check if client is already connected.
    int existingClientIndex = serverFindClientIndex(SERVER, from);
    if (existingClientIndex >= 0)
    {
        printf("SERVER: Client (%d) already connected, denying connection.\n", existingClientIndex);
        // Send connection denied (already connected) packet.
    }

    // Send reject packet if server is full.
    if (SERVER->numClientsConnected == SERVER->maxClients)
    {
        printf("SERVER: Server full, denying connection.\n", existingClientIndex);
        // Send connection denied (server full) packet.
    }
    else // Find/add pending connection.
    {
        uint64_t clientSalt = readU64((u8*)payload + 1);
        for (int i = 0; i < SERVER->pendingConnectionsCount; i++)
        {
            pendingClientConnection * pendingConn = &SERVER->pendingConnections[i];
            // Check for matching address + salt.
            if (addressEqual(pendingConn->clientAddress, from) && pendingConn->clientSalt == clientSalt)
            {
                // Resend challenge packet to client.
                printf("SERVER: Resending challenge packet.\n");
                packet challengePacket = createServerChallengePacket(ProtocolID, pendingConn->clientSalt, pendingConn->serverSalt);
                socketSend(SERVER->serverSocket, challengePacket.data, challengePacket.size, pendingConn->clientAddress);
                dealloc(get_heap_allocator(), challengePacket.data);
                return;
            }
        }

        if (SERVER->pendingConnectionsCount == SERVER->maxClients)
        {
            // Send connection denied (max connection requests).
            return;
        }

        pendingClientConnection * pendingConn = &SERVER->pendingConnections[SERVER->pendingConnectionsCount];
        pendingConn->clientAddress = from;
        pendingConn->clientSalt = clientSalt;
        pendingConn->timeSinceRequest = SERVER->time;
        pendingConn->serverSalt = generateSalt();
        SERVER->pendingConnectionsCount++;

        printf("SERVER: Sending challenge packet.\n");
        packet challengePacket = createServerChallengePacket(ProtocolID, pendingConn->clientSalt, pendingConn->serverSalt);
        socketSend(SERVER->serverSocket, challengePacket.data, challengePacket.size, pendingConn->clientAddress);
        dealloc(get_heap_allocator(), challengePacket.data);
        // SEND CHALLENGE PACKET.
    }
}

void serverProcessPacket(server * server, address from, void * payload, unsigned int size)
{
    PacketType type = *((u8*)payload);
    switch (type)
    {
        case CONNECT:
            serverProcessConnectPacket(server, from, payload, size);
            break;
        case PACKET_RESPONSE:
            // Process connection response packet:
            uint64_t clientSalt = readU64((u8*)payload + 1);
            uint64_t serverSalt = readU64((u8*)payload + 1 + 8);

            // Check if already connected
            int index = serverFindClientIndex(server, from);
            if (index >= 0)
            {
                printf("SERVER: Client (%d) already connected, resending connection accepted packet.\n", index);
                // Send connection denied (already connected) packet.
            };

            for (int i = 0; i < server->pendingConnectionsCount; i++)
            {
                pendingClientConnection * pendingConn = &server->pendingConnections[i];
                if (addressEqual(pendingConn->clientAddress, from) && pendingConn->clientSalt == clientSalt && pendingConn->serverSalt == serverSalt)
                {
                    // SEND CONNECTION ACCEPTED PACKET FINALLY.
                    // remove pending conn from thing.
                }
            }
            break;
        default:
            printf("SERVER: Invalid packet type recieved.\n", index);
            break;
    }
}

void serverRecieve(server * Server)
{
    while ( true )
    {
        unsigned char packetData[1024];
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
        uint32_t protocol = ntohl(*(uint32_t*)&packetData[0]);
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