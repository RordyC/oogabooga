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
    uint32_t clientIndex;
    double time;
    double lastPacketSendTime;
    double lastPacketRecieveTime;
    address serverAddress;
    clientState state;
} client;

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
    // TODO: Assert valid server address
    CLIENT->clientSalt = generateSalt();
    CLIENT->state = CLIENT_REQUESTING_CONNECTION;
    CLIENT->serverAddress = serverAddress;
    CLIENT->lastPacketRecieveTime = CLIENT->time;
}

void clientTimeout(client * CLIENT)
{
    CLIENT->state = CLIENT_DISCONNECTED;
    printf("CLIENT TIMED OUT.\n");
}

void clientProcessPacket(client * CLIENT, address from, void * payload, unsigned int size)
{
    PacketType type = *((u8*)payload);
    switch (type)
    {
        case PACKET_REJECT:
            if (CLIENT->state == CLIENT_REQUESTING_CONNECTION)
            {
                CLIENT->state = CLIENT_DISCONNECTED;
                printf("CLIENT: Connnection request rejected by server.\n");
            }
        case PACKET_CHALLENGE:
            if (CLIENT->state == CLIENT_REQUESTING_CONNECTION)
            {
                uint64_t clientSalt = readU64(((u8*)payload) + 1);
                uint64_t serverSalt = readU64(((u8*)payload) + 5);

                if (clientSalt == CLIENT->clientSalt)
                {
                    CLIENT->serverSalt == serverSalt;
                    // SEND CHALLENGE RESPONSE.
                }
                else
                {
                    printf("CLIENT: Received challenge packet with wrong client salt.\n");
                }
            }
            break;
        case PACKET_HEARTBEAT:
            if (CLIENT->state == CLIENT_SENDING_CHALLENGE_RESPONSE)
            {
                // Need to receive heartbeat packet with the client index before the client can transition to connected state.
                uint64_t salts = readU64(((u8*)payload) + 1);
                if (CLIENT->clientSalt ^ CLIENT->serverSalt == salts)
                {
                    CLIENT->clientIndex = 0; // TODO: Read client index.
                    CLIENT->state == CLIENT_CONNECTED;
                }
                else
                {
                    printf("CLIENT: Received packet with incorrect salt values.\n");
                }
            }
            else
            {

            }
            break;
        default:
            break;
    }
}

void clientUpdate(client * CLIENT, double currentTime)
{
    CLIENT->time = currentTime; 
    // clientReceive(CLIENT);
    switch (CLIENT->state)
    {
        case CLIENT_REQUESTING_CONNECTION:
            // Check if time since last packet is greater than connection request resend rate.
            if ((CLIENT->time - CLIENT->lastPacketRecieveTime) > 5.0)
            {
               clientTimeout(CLIENT); 
            }

            // Keep sending request packets.
            if (CLIENT->time - CLIENT->lastPacketSendTime >= 0.1)
            {
                CLIENT->lastPacketSendTime = CLIENT->time;
                packet packet = createConnectionRequestPacket(ProtocolID, CLIENT->clientSalt);

                printf("CLIENT: Sending Request Packet to Server.\n");

                socketSend(CLIENT->clientSocket, packet.data, packet.size, CLIENT->serverAddress);
                dealloc(get_heap_allocator(), packet.data);
            }
            break;
        case CLIENT_SENDING_CHALLENGE_RESPONSE:
            if ((CLIENT->time - CLIENT->lastPacketRecieveTime) > 5.0)
            {
               clientTimeout(CLIENT); 
            }

            if (CLIENT->time - CLIENT->lastPacketSendTime >= 0.1)
            {
                // send packet again!
                CLIENT->lastPacketSendTime = CLIENT->time;
                packet response = createChallengeResponsePacket(ProtocolID, CLIENT->clientSalt ^ CLIENT->serverSalt);

                printf("CLIENT: Sending Challenge Response Packet to Server.\n");

                socketSend(CLIENT->clientSocket, response.data, response.size, CLIENT->serverAddress);
                dealloc(get_heap_allocator(), response.data);
            }
            break;
        case CLIENT_DISCONNECTED:
            break;
        case CLIENT_CONNECTED:
            // DO STUFF
            break;
        default:
            break;
    }
}

void clientReceive(client * CLIENT)
{
    while ( true )
    {
        unsigned char packetData[1024];
        unsigned int maxPacketSize = sizeof(packetData);

        struct sockaddr_in from;
        int fromLength = sizeof(from);

        int bytes = recvfrom(CLIENT->clientSocket, 
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
            unsigned long from_address = ntohl(from.sin_addr.s_addr);
            unsigned int from_port = ntohs(from.sin_port);
            address from = addressIPV4DD(from_address, from_port);

            if (addressEqual(from, CLIENT->serverAddress))
            {
                printf("CLIENT: Received Packet of size (%d) from (%d.%d.%d.%d):%d\n",
                    bytes,
                    from.data.ipv4[0], from.data.ipv4[1], from.data.ipv4[2], from.data.ipv4[3],
                    from_port);
                CLIENT->lastPacketRecieveTime = CLIENT->time;
                clientProcessPacket(CLIENT, from, (void*)&packetData[4], bytes - 4);
            }
            else
            {
                printf("CLIENT: Received packet from different server address.\n");
            }
        }
        else
        {
            printf("CLIENT: Received packet with invalid protocolID.\n");
        }
    }

}