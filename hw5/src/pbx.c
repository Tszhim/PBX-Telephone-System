/*
 * PBX: simulates a Private Branch Exchange.
 */
#include <stdlib.h>
#include <sys/socket.h>
#include <semaphore.h>

#include "pbx.h"
#include "debug.h"
#include "pbx_registry.h"
#include "csapp.h"

/*
 * Initialize a new PBX.
 *
 * @return the newly initialized PBX, or NULL if initialization fails.
 */
PBX *pbx_init() {
    // Allocate space for pbx object.
    pbx = malloc(sizeof(struct pbx));
    if(!pbx) {
        return NULL;
    }
    // Initialize pbx registry.
    for(int i = 0; i < PBX_MAX_EXTENSIONS; i++) {
        pbx->PBX_REGISTRY[i] = NULL;
    }
    // Initialize the mutex.
    Sem_init(&(pbx->mutex), 0, 1);
    return pbx;
}

/*
 * Shut down a pbx, shutting down all network connections, waiting for all server
 * threads to terminate, and freeing all associated resources.
 * If there are any registered extensions, the associated network connections are
 * shut down, which will cause the server threads to terminate.
 * Once all the server threads have terminated, any remaining resources associated
 * with the PBX are freed.  The PBX object itself is freed, and should not be used again.
 *
 * @param pbx  The PBX to be shut down.
 */
void pbx_shutdown(PBX *pbx) {
    // Perform shutdown call on all sockets to prevent further communiations.
    for(int i = 0; i < PBX_MAX_EXTENSIONS; i++) {
        if(pbx->PBX_REGISTRY[i]) {
            shutdown(pbx->PBX_REGISTRY[i]->fd, SHUT_RDWR);
        }
    }
    // Waiting for all client threads to finish.
    while(1) {
        int finished = 0;
        for(int i = 0; i < PBX_MAX_EXTENSIONS; i++) {
            if(pbx->PBX_REGISTRY[i]) {
                finished = -1;
            }
        }
        if(finished == 0) {
            break;
        }
        sleep(0.1);
    }
    // Threads finished, destroy mutex & free pbx object and exit.
    Sem_destroy(&(pbx->mutex));
    free(pbx);
}

/*
 * Register a telephone unit with a PBX at a specified extension number.
 * This amounts to "plugging a telephone unit into the PBX".
 * The TU is initialized to the TU_ON_HOOK state.
 * The reference count of the TU is increased and the PBX retains this reference
 *for as long as the TU remains registered.
 * A notification of the assigned extension number is sent to the underlying network
 * client.
 *
 * @param pbx  The PBX registry.
 * @param tu  The TU to be registered.
 * @param ext  The extension number on which the TU is to be registered.
 * @return 0 if registration succeeds, otherwise -1.
 */
int pbx_register(PBX *pbx, TU *tu, int ext) {
    // Impose lock so that two spaces are not selected at once.
    P(&(pbx->mutex));
    for(int i = 0; i < PBX_MAX_EXTENSIONS; i++) {
        // Found empty space to register.
        if(!pbx->PBX_REGISTRY[i]) {
            // Register, then release lock.
            pbx->PBX_REGISTRY[i] = tu;
            dprintf(ext, "ON HOOK %d\r\n", ext);
            V(&(pbx->mutex));
            return 0;
        }
    }
    // Error, release lock.
    V(&(pbx->mutex));
    return -1;
}

/*
 * Unregister a TU from a PBX.
 * This amounts to "unplugging a telephone unit from the PBX".
 * The TU is disassociated from its extension number.
 * Then a hangup operation is performed on the TU to cancel any
 * call that might be in progress.
 * Finally, the reference held by the PBX to the TU is released.
 *
 * @param pbx  The PBX.
 * @param tu  The TU to be unregistered.
 * @return 0 if unregistration succeeds, otherwise -1.
 */
int pbx_unregister(PBX *pbx, TU *tu) {
    // Impose lock.
    P(&(pbx->mutex));
    for(int i = 0; i < PBX_MAX_EXTENSIONS; i++) {
        // Found telephone unit in registry.
        if(pbx->PBX_REGISTRY[i] == tu) {
            // Unregister, then release lock.
            if(tu->state != TU_CONNECTED && tu->state != TU_RINGING && tu->state != TU_RING_BACK) {
                tu_hangup(tu);
                tu_unref(tu, "Unregistering telephone unit.\n");
            }
            else {
                tu_hangup(tu);
            }
            pbx->PBX_REGISTRY[i] = NULL;
            V(&(pbx->mutex));
            return 0;
        }
    }
    // Error, release lock.
    V(&(pbx->mutex));
    return -1;
}

/*
 * Use the PBX to initiate a call from a specified TU to a specified extension.
 *
 * @param pbx  The PBX registry.
 * @param tu  The TU that is initiating the call.
 * @param ext  The extension number to be called.
 * @return 0 if dialing succeeds, otherwise -1.
 */
int pbx_dial(PBX *pbx, TU *tu, int ext) {
    // Impose lock.
    P(&(pbx->mutex));
    for(int i = 0; i < PBX_MAX_EXTENSIONS; i++) {
        // Found telephone unit initiating call.
        if(pbx->PBX_REGISTRY[i] == tu) {
            // Find telephone unit to be called.
            for(int j = 0; j < PBX_MAX_EXTENSIONS; j++) {
                //Found telephone unit to call.
                if(tu_fileno(pbx->PBX_REGISTRY[j]) == ext) {
                    tu_dial(tu, pbx->PBX_REGISTRY[j]);
                    V(&(pbx->mutex));
                    return 0;
                }
            }
            // Could not determine telephone unit to be called.
            tu_dial(tu, NULL);
            V(&(pbx->mutex));
            return 0;
        }
    }
    // Did not find telephone unit initiating call.
    V(&(pbx->mutex));
    return -1;
}
