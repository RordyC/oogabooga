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
        case PACKET_CHALLENGE:
            if (CLIENT->state == CLIENT_REQUESTING_CONNECTION && addressEqual(from, CLIENT->serverAddress))
            {

            }
            break;
        case PACKET_HEARTBEAT:
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

void clientRecieve(client * CLIENT)
{
       
}