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

int testTests();

bool is_action_just_pressed(Action action) {
	for (u64 i = 0; i < MAX_KEYS_PER_BINDING; i++) {
		Input_Key_Code code = key_binds[action].codes[i];
		if (code == 0) continue;
		
		if (is_key_just_pressed(code)) return true;
	}
	return false;
}

Gfx_Image * playerImages[4];
Gfx_Font * font;

typedef struct {
    int id;
    char name[32];
    Vector2 position;
    bool connected;
} Player;

void drawPlayer(int ID,const char * name, Vector2 position)
{
    draw_image(playerImages[ID], position, v2(120, 120), COLOR_WHITE);
    draw_text(font, STR(name), 32, v2(position.x, position.y + 128), v2(1, 1), COLOR_BLACK);
}

int entry(int argc, char **argv)
{

    if (argc < 2){
        printf("Must specify client or server with -c or -s arguments on run.\n");
        return -1;
    }

    if (strlen(argv[1]) == 2)
    {
        char dash, startMode;
        dash = argv[1][0];
        startMode = argv[1][1];
        if (dash == '-')
        {
            if (startMode == 'c')
            {
                printf("Running Program as Client.\n");
            }
            else if (startMode == 's')
            {

                printf("Running Program as Server.\n");
            }
        }
    }

    if (networkingInitialize() != 0)
    {
        printf("Failed to initalize networking library!\n");
        return 1;
    }

	// This is how we (optionally) configure the window.
	// To see all the settable window properties, ctrl+f "struct Os_Window" in os_interface.c
	window.title = STR("Minimal Game Example");
    address serverAddress = addressIPV4("127.0.0.1", 7777);
    address clientAddress = addressIPV4("127.0.0.1", 7778);

    server SERVER;
    SERVER = startServer(serverAddress, 4);

    client CLIENT;
    CLIENT = startClient(clientAddress);
    clientConnect(&CLIENT, serverAddress);

    key_binds[ACTION_RED].codes[0]   = 'R';
    key_binds[ACTION_GREEN].codes[0] = 'G';
	key_binds[ACTION_BLUE].codes[0]  = 'B';

    playerImages[0] = load_image_from_disk(fixed_string("res/player.png"), get_heap_allocator());
    playerImages[1] = load_image_from_disk(fixed_string("res/glimbo.png"), get_heap_allocator());
    playerImages[2] = load_image_from_disk(fixed_string("res/pear.jpg"), get_heap_allocator());
    playerImages[3] = load_image_from_disk(fixed_string("res/cat.jpg"), get_heap_allocator());

    assert(playerImages[0]);
    assert(playerImages[1]);
    assert(playerImages[2]);
    assert(playerImages[3]);
    float64 currentTime = os_get_elapsed_seconds();

	font = load_font_from_disk(STR("C:/windows/fonts/arial.ttf"), get_heap_allocator());
	assert(font, "Failed loading arial.ttf");
	
	const u32 font_height = 48;

    while (!window.should_close) {
		reset_temporary_storage();
		

		float64 now = os_get_elapsed_seconds();
        float64 frameTime = now - currentTime;
        currentTime = now;

        clientUpdate(&CLIENT, now);
        serverUpdate(&SERVER, now);

        if (CLIENT.state == CLIENT_CONNECTED)
        {
            // Send messages at 15hz.
            // Create test message.
            // Send message to server.
            // clientSend(&CLIENT, packet, packet size, SEND_UNRELIABLE)

            // Poll for new packets.
        }

        Matrix4 rect_xform = m4_scalar(1.0);
		rect_xform         = m4_rotate_z(rect_xform, (f32)now);
		rect_xform         = m4_translate(rect_xform, v3(-125, -125, 0));
		draw_rect_xform(rect_xform, v2(250, 250), COLOR_GREEN);
        draw_text(font, STR("I am text"), font_height, v2(-75, 0), v2(1, 1), COLOR_BLACK);
		drawPlayer(0,"rordo", v2(sin(now)*1000*0.4-60, -60));
		drawPlayer(1,"dylan", v2(cos(now)*1000*0.4-60, -60));
		drawPlayer(2,"flo", v2(sin(now)*-1000*0.4-60, -60));
		drawPlayer(3,"gabi", v2(cos(now)*-1000*0.4-60, -60));
		os_update(); 
		gfx_update();
	}

    networkingShutdown();
	return 0;
}

int testTests()
{
    address serverAddress = addressIPV4("127.0.0.1", 7777);
    address clientAddress = addressIPV4("127.0.0.1", 7778);

    server SERVER;
    SERVER = startServer(serverAddress, 4);


    client clients[16];

    for (int i = 0; i < 16; i++)
    {
        clients[i] = startClient(clientAddress);
    }

    return 0;
}