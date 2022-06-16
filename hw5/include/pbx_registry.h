// Telephone unit structure.
struct tu {
	int fd;             // File descriptor of network connection, doubles as extension of telephone unit.
	TU* target;         // Telephone unit that chat messages will be sent to (only NON-NULL when TU_CONNECTED).
	volatile int state; // Current state of telephone unit: TU_ON_HOOK, TU_RINGING, TU_DIAL_TONE, TU_RING_BACK, TU_BUSY_SIGNAL, TU_CONNECTED, TU_ERROR.
	int ref_count;      // Reference count on telephone unit.
	sem_t mutex;        // Mutex for the telephone unit such that it can only be accessed by one thread at a time.
};

// Private branch exchange structure.
struct pbx {
	TU* PBX_REGISTRY[PBX_MAX_EXTENSIONS];	// Array containing all telephone units.
	sem_t mutex;                            // Mutex for private branch exchange such that the array of telephone units can only be accessed by one thread at a time.
};