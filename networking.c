void windowsLogConsole(int isServer, const char * fmt, ...)
{
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (isServer)
    {
        SetConsoleTextAttribute(hConsole, 0xC);
        printf("[SERVER]: ");
    }
    else
    {
        SetConsoleTextAttribute(hConsole, 0x9);
        printf("[CLIENT]: ");
    }
    SetConsoleTextAttribute(hConsole, 0x7);

    char message[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    printf("%s\n", message);
}


#ifndef LOG_SERVER
#define LOG_SERVER(message, ...) windowsLogConsole(1, message, __VA_ARGS__)
#endif

#ifndef LOG_CLIENT
#define LOG_CLIENT(message, ...) windowsLogConsole(0, message, __VA_ARGS__)
#endif

#define freefunction(data) dealloc(get_heap_allocator(), (void*)data)

typedef enum { ADDRESS_IPV4, ADDRESS_IPV6, ADDRESS_INVALID} addressType;

typedef struct {
    addressType type;
    union {uint8_t ipv4[4]; uint16_t ipv6[8]; uint64_t l;} data;
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
    uint64_t clientSalt;
    uint64_t challengeSalt;
} serverClientSlot;

typedef struct {
    double time;
    int maxClients;
    int numClientsConnected;
    bool isClientConnected[MAX_CLIENTS];
    double clientsLastPacketReceivedTime[MAX_CLIENTS];
    double clientsLastPacketSendTime[MAX_CLIENTS];
    address clientsAddress[MAX_CLIENTS];
    uint64_t clientSalts[MAX_CLIENTS];
    uint64_t challengeSalts[MAX_CLIENTS];
    pendingClientConnection pendingConnections[MAX_CLIENTS];
    int pendingConnectionsCount;
    address serverAddress;
    SOCKET serverSocket;
} server;

int networkingInitialize()
{
    // Windows Setup ---------
	WSADATA wsaData;
    int wsaerr;
    // Request latest version (2.2)
    WORD wVersionRequested = MAKEWORD(2, 2); // A 16-bit unsigned integer. The range is 0 through 65535 decimal.
    wsaerr = WSAStartup(wVersionRequested, &wsaData);
    if (wsaerr != 0)
    {
        printf("WSAStartup failed: %d\n", wsaerr);
        return 1;
    }
    else
    {
        printf("WSAStartup success.\n");
        return 0;
    }
}

void networkingShutdown()
{
    // Clean up Windows 
    WSACleanup();
}

SOCKET createSocketUDP(address addr)
{
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

address getLocalAddress(unsigned short port)
{
    address localAddr;
    char hostname[255];
    gethostname(hostname, sizeof hostname);
    char ipString[INET6_ADDRSTRLEN];
    ADDRINFOA hints, *addr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(hostname, NULL, &hints, &addr) != 0)
    {
        // COOKED
        printf("Failed to get local address.\n");
        return (address){ADDRESS_INVALID, {0,0,0,0}, 0};
    }

    for (struct addrinfo * p = addr; p != NULL; p = p->ai_next)
    {
        if (p->ai_family == AF_INET)
        {
            localAddr.type = ADDRESS_IPV4;
            localAddr.port = port;

            struct sockaddr_in * ipv4 = (struct sockaddr_in*)p->ai_addr;
            inet_ntop(p->ai_family, &ipv4->sin_addr, ipString, sizeof(ipString));
            printf("Address: %s\n", ipString);
        }
        else if (p->ai_family == AF_INET6)
        {
            localAddr.type = ADDRESS_IPV6;
            localAddr.port = port;
            struct sockaddr_in6 * ipv6 = (struct sockaddr_in6*)p->ai_addr;
            inet_ntop(p->ai_family, &(ipv6->sin6_addr), ipString, sizeof(ipString));
            printf("Address: %s\n", ipString);
        }
    }
    return localAddr;
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
    assert( buf->index + sizeof(uint64_t) <= buf->size)
    *((uint64_t*)(buf->data + buf->index)) = htonll(value);
    buf->index += sizeof(uint64_t);
}

uint32_t readU32(void * data)
{
    return ntohl(*((uint32_t*)(data)));
}

u_int64 readU64(void * data)
{
    return ntohll(*((uint64_t*)(data)));
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

// Returns the index
int serverFindEmptyClientSlot(server * SERVER)
{
    for (int i = 0; i < SERVER->maxClients; i++)
    {
        int clientConnected = SERVER->isClientConnected[i];
        if (!clientConnected)
        {
            assert(SERVER->numClientsConnected != SERVER->maxClients);
            return i;
        }
    }
    // Server must be full.
    assert(SERVER->numClientsConnected == SERVER->maxClients);
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

    int existingClientIndex = serverFindClientIndex(SERVER, from);

    if (existingClientIndex >= 0)
    {
        printf("SERVER: Client already connected. Sending heartbeat packet.\n");
        
        packet heartbeatPacket = createHeartbeatPacket(ProtocolID,
                                 SERVER->clientSalts[existingClientIndex] ^ SERVER->challengeSalts[existingClientIndex],
                                 existingClientIndex);
        socketSend(SERVER->serverSocket, heartbeatPacket.data, heartbeatPacket.size, SERVER->clientsAddress[existingClientIndex]);
        SERVER->clientsLastPacketSendTime[existingClientIndex] = SERVER->time;
        dealloc(get_heap_allocator(),heartbeatPacket.data);
        return;
    }

    pendingClientConnection * pendingConn = null;
    int pendingConnIndex;
    for (int i = 0; i < SERVER->pendingConnectionsCount; i++)
    {
        if (addressEqual(SERVER->pendingConnections[i].clientAddress, from))
        {
            pendingConn = &SERVER->pendingConnections[i];
            pendingConnIndex = i;
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
    int clientSlot = serverFindEmptyClientSlot(SERVER);
    if (clientSlot == -1)
    {
        printf("SERVER: Could not find available slot to connect client (server full). Sending connection rejected packet.\n");
        // TODO: Send connection rejected packet.
        return;
    }

    SERVER->isClientConnected[clientSlot] = 1;
    SERVER->clientsAddress[clientSlot] = from;
    SERVER->clientSalts[clientSlot] = pendingConn->clientSalt;
    SERVER->challengeSalts[clientSlot] = pendingConn->serverSalt;
    SERVER->clientsLastPacketReceivedTime[clientSlot] = SERVER->time;
    SERVER->clientsLastPacketSendTime[clientSlot] = SERVER->time;
    SERVER->numClientsConnected++;

    // Remove pending connection.
    SERVER->pendingConnectionsCount--;
    for (int i = pendingConnIndex; i < SERVER->pendingConnectionsCount; i++)
    {
        SERVER->pendingConnections[i] = SERVER->pendingConnections[i + 1];
    }

    LOG_SERVER("Client connected at index: (%d)", clientSlot);
    // TODO: Send heartbeat packet.
}

void serverProcessConnectPacket(server * SERVER, address from, void * payload, unsigned int size)
{
    printf("SERVER: Received CONNECT Packet from (%d.%d.%d.%d):%d\n",
           from.data.ipv4[0], from.data.ipv4[1], from.data.ipv4[2], from.data.ipv4[3],
           from.port);

    if (size != 508)
    {
        printf("SERVER: Received CONNECT Packet of incorrect size.\n");
        return;
    }
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
                freefunction(challengePacket.data);
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
            serverProcessChallengeResponsePacket(server, from, payload, size);
            break;
        default:
            LOG_SERVER("Invalid packet type recieved.");
            break;
    }
}

void serverReceive(server * Server)
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

            LOG_SERVER("Received Packet of size (%d) from (%d.%d.%d.%d):%d",
                bytes,
                from.data.ipv4[0], from.data.ipv4[1], from.data.ipv4[2], from.data.ipv4[3],
                from_port);

            serverProcessPacket(Server, from, (void*)&packetData[4], bytes - 4);
        }
    }
}

server startServer(address serverAddress, unsigned int maxConnections)
{
    server newServer = {0};
    newServer.numClientsConnected = 0;
    newServer.pendingConnectionsCount = 0;

    newServer.maxClients = maxConnections;
    newServer.serverAddress = serverAddress;

    newServer.serverSocket = createSocketUDP(serverAddress);
    newServer.pendingConnectionsCount = 0;
    return newServer;
}

void serverUpdate(server * SERVER, double time)
{
    SERVER->time = time;
    // serverCheckPendingConnectionsTimeout(SERVER);
    serverReceive(SERVER);

    // Send payload messages every update rate.

    // Check for client timeout
    for (int i = 0; i < SERVER->maxClients; i++)
    {
        if (SERVER->isClientConnected[i] && (SERVER->time - SERVER->clientsLastPacketReceivedTime[i]) > 5.0)
        {
            SERVER->isClientConnected[i] = 0;
            LOG_SERVER("Client at index (%d) timed out. Sending disconnect packets.", i);
        }
    }    
}