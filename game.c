
#include "networking.c"
#define DEFAULT_PORT 7777

#define MAX_KEYS_PER_BINDING 3


typedef enum Action {
	ACTION_RED,
	ACTION_BLUE,
	ACTION_GREEN,

	ACTION_MAX
} Action;
typedef struct Key_Bind {
	Input_Key_Code codes[MAX_KEYS_PER_BINDING];
} Key_Bind;


// Index with action into key bind
Key_Bind key_binds[ACTION_MAX] = {0};

bool is_action_just_pressed(Action action) {
	for (u64 i = 0; i < MAX_KEYS_PER_BINDING; i++) {
		Input_Key_Code code = key_binds[action].codes[i];
		if (code == 0) continue;
		
		if (is_key_just_pressed(code)) return true;
	}
	return false;
}



int entry(int argc, char **argv)
{
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
    }

	// This is how we (optionally) configure the window.
	// To see all the settable window properties, ctrl+f "struct Os_Window" in os_interface.c
	window.title = STR("Minimal Game Example");

    address serverAddress = addressIPV4("127.0.0.1", 7777);
    address clientAddress = addressIPV4("127.0.0.1", 7778);

    SOCKET serverSocket = INVALID_SOCKET;
    SOCKET clientSocket = INVALID_SOCKET;

    server SERVER;
    SERVER = startServer(serverAddress, 4);

    client CLIENT;
    CLIENT = startClient(clientAddress);
    clientConnect(&CLIENT, serverAddress);
    int activeServer = 1;
    int activeClient = 1;

    // Initialize WSA variables
    if (argc == 2)
    {
        if (strcmp(argv[1], "server") == 0)
        {

        }
        else if (strcmp(argv[1], "client") == 0)
        {

        }
    }


    key_binds[ACTION_RED].codes[0]   = 'R';
    key_binds[ACTION_GREEN].codes[0] = 'G';
	key_binds[ACTION_BLUE].codes[0]  = 'B';
    Vector4 dylPickelColor = COLOR_WHITE;

    Gfx_Image * player = load_image_from_disk(fixed_string("res/player.png"), get_heap_allocator());
    assert(player);
    float64 currentTime = os_get_elapsed_seconds();

    	
	Gfx_Font *font = load_font_from_disk(STR("C:/windows/fonts/arial.ttf"), get_heap_allocator());
	assert(font, "Failed loading arial.ttf");
	
	const u32 font_height = 48;
	
	while (!window.should_close) {
		reset_temporary_storage();
		

		float64 now = os_get_elapsed_seconds();
        float64 frameTime = now - currentTime;
        currentTime = now;

        clientUpdate(&CLIENT, now);
        serverRecieve(&SERVER);
        if (activeServer)
        {
            // Check if pending clients and send connection approved messages.

        }


        if (activeClient)
        {
            char packet1[128] = "Hello Server!";
            char packet2[64] = "NETCHAD!!!";
            unsigned char packet3[128];

            buffer buf = {&packet3, sizeof(packet3), 0};

            u16 protocol = 12;
            u8 type = 1;

            writeInteger(&buf, ProtocolID);
            writeInteger(&buf, type);

		    if (is_action_just_pressed(ACTION_RED))   
            {
                if (CLIENT.state == CLIENT_CONNECTED)
                {
                    //clientSend(clientSocket, packet1, 128, serverAddress);
                }
            }
            if (is_action_just_pressed(ACTION_GREEN)) 
            {
                //clientSend(clientSocket, packet2, 64, serverAddress);
            }
            //if (is_action_just_pressed(ACTION_BLUE))  clientSend(clientSocket, buf.data, 128, serverAddress);
        }



		Matrix4 rect_xform = m4_scalar(1.0);
		rect_xform         = m4_rotate_z(rect_xform, (f32)now);
		rect_xform         = m4_translate(rect_xform, v3(-125, -125, 0));
		draw_rect_xform(rect_xform, v2(250, 250), COLOR_GREEN);
		
		draw_image(player,v2(sin(now)*window.width*0.4-60, -60), v2(120, 120), dylPickelColor);
        draw_text(font, STR("I am text"), font_height, v2(-75, 0), v2(1, 1), COLOR_BLACK);
		os_update(); 
		gfx_update();
	}

    WSACleanup();
	return 0;
}