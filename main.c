
int oogabooga_main(int argc, char **argv) {
	
	print(cstr("This is our print! %i\n"), 5);
	
	// alloc calls to context.allocator.proc which by default is set to the
	// heap allocator in memory.c
	int *a = (int*)alloc(sizeof(int));
	dealloc(a);
	
	// We can do an old school memory dump to save our game with the global variables
	// (void*) program_memory and (u64) program_memory_size
	// program_memory will ALWAYS have the same virtual memory base address, so all
	// pointers will remain valid when read back from disk.
	
	// We can push allocator like jai
	push_allocator(temp);
	// all calls to alloc() here will be with the temporary allocator
	pop_allocator(); // But this is C so we have to pop it manually!
	
	// or we can just do this for temporary allocation
	int *b = talloc(sizeof(int));
	(void)b;
	
	// Call each frame
	reset_temporary_storage();
	
	Context c = context;
	// ... modify c
	push_context(c);
	// ... do stuff in modified context
	pop_context();
	
	// For now, context only has the allocator and CONTEXT_EXTRA which can be defined
	// to add some custom stuff to context
	// But it needs to be in build.c before including oogabooga.c.
	// #define CONTEXT_EXTRA struct { int monkee; }
	context.extra.monkee = 69;
	

	int hello;
	hello = 5;
	(void)hello;

#ifdef DEBUG
	printf("Hello, balls! (debug)\n");
#endif

#ifdef RELEASE
	printf("Hello, balls! (release)\n");
#endif

	printf("Program exit as expected\n");

  return 0;
}
