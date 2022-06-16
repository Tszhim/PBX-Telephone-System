/*
 * TU: simulates a "telephone unit", which interfaces a client with the PBX.
 */
#include <stdlib.h>
#include <sys/socket.h>
#include <semaphore.h>

#include "pbx.h"
#include "debug.h"
#include "pbx_registry.h"
#include "csapp.h"

/*
 * Initialize a TU
 *
 * @param fd  The file descriptor of the underlying network connection.
 * @return  The TU, newly initialized and in the TU_ON_HOOK state, if initialization
 * was successful, otherwise NULL.
 */
TU *tu_init(int fd) {
    TU *tu = malloc(sizeof(struct tu));
    if(!tu) {
        return NULL;
    }
    tu->fd = fd;
    tu->target = NULL;
    tu->state = TU_ON_HOOK;
    tu->ref_count = 1;
    Sem_init(&(tu->mutex), 0 , 1);
    return tu;
}

/*
 * Increment the reference count on a TU.
 *
 * @param tu  The TU whose reference count is to be incremented
 * @param reason  A string describing the reason why the count is being incremented
 * (for debugging purposes).
 */
void tu_ref(TU *tu, char *reason) {
    // Critical section.
    P(&(tu->mutex));
    (tu->ref_count)++;
    //dprintf(tu->fd, "%s\r\n", reason);
    debug("%s", reason);
    V(&(tu->mutex));
}

/*
 * Decrement the reference count on a TU, freeing it if the count becomes 0.
 *
 * @param tu  The TU whose reference count is to be decremented
 * @param reason  A string describing the reason why the count is being decremented
 * (for debugging purposes).
 */
void tu_unref(TU *tu, char *reason) {
    // Critical section.
    P(&(tu->mutex));
    (tu->ref_count)--;
    //dprintf(tu->fd, "%s\r\n", reason);
    debug("%s", reason);
    V(&(tu->mutex));

    // Reference count is 0, free.
    if(tu->ref_count == 0) {
        Sem_destroy(&(tu->mutex));
        close(tu->fd);
        free(tu);
    }
}

/*
 * Get the file descriptor for the network connection underlying a TU.
 * This file descriptor should only be used by a server to read input from
 * the connection.  Output to the connection must only be performed within
 * the PBX functions.
 *
 * @param tu
 * @return the underlying file descriptor, if any, otherwise -1.
 */
int tu_fileno(TU *tu) {
    if(!tu) {
        return -1;
    }
    return tu->fd;
}

/*
 * Get the extension number for a TU.
 * This extension number is assigned by the PBX when a TU is registered
 * and it is used to identify a particular TU in calls to tu_dial().
 * The value returned might be the same as the value returned by tu_fileno(),
 * but is not necessarily so.
 *
 * @param tu
 * @return the extension number, if any, otherwise -1.
 */
int tu_extension(TU *tu) {
    return tu_fileno(tu);
}

/*
 * Set the extension number for a TU.
 * A notification is set to the client of the TU.
 * This function should be called at most once one any particular TU.
 *
 * @param tu  The TU whose extension is being set.
 */
int tu_set_extension(TU *tu, int ext) {
    if(!tu) {
        return -1;
    }
    // Critical section.
    P(&(tu->mutex));
    tu->fd = ext;
    V(&(tu->mutex));
    return 0;
}

/*
 * Initiate a call from a specified originating TU to a specified target TU.
 *   If the originating TU is not in the TU_DIAL_TONE state, then there is no effect.
 *   If the target TU is the same as the originating TU, then the TU transitions
 *     to the TU_BUSY_SIGNAL state.
 *   If the target TU already has a peer, or the target TU is not in the TU_ON_HOOK
 *     state, then the originating TU transitions to the TU_BUSY_SIGNAL state.
 *   Otherwise, the originating TU and the target TU are recorded as peers of each other
 *     (this causes the reference count of each of them to be incremented),
 *     the target TU transitions to the TU_RINGING state, and the originating TU
 *     transitions to the TU_RING_BACK state.
 *
 * In all cases, a notification of the resulting state of the originating TU is sent to
 * to the associated network client.  If the target TU has changed state, then its client
 * is also notified of its new state.
 *
 * If the caller of this function was unable to determine a target TU to be called,
 * it will pass NULL as the target TU.  In this case, the originating TU will transition
 * to the TU_ERROR state if it was in the TU_DIAL_TONE state, and there will be no
 * effect otherwise.  This situation is handled here, rather than in the caller,
 * because here we have knowledge of the current TU state and we do not want to introduce
 * the possibility of transitions to a TU_ERROR state from arbitrary other states,
 * especially in states where there could be a peer TU that would have to be dealt with.
 *
 * @param tu  The originating TU.
 * @param target  The target TU, or NULL if the caller of this function was unable to
 * identify a TU to be dialed.
 * @return 0 if successful, -1 if any error occurs that results in the originating
 * TU transitioning to the TU_ERROR state. 
 */
int tu_dial(TU *tu, TU *target) {
    // Impose lock on originating telephone unit.
    if(!tu) {
        return -1;
    }
    P(&(tu->mutex));

    // If caller does not know target.
    if(!target) {
        // If state is TU_DIAL_TONE, transition to TU_ERROR.
        if(tu->state == TU_DIAL_TONE) {
            tu->state = TU_ERROR;
            dprintf(tu->fd, "ERROR\r\n");
            V(&(tu->mutex));
            return -1;
        }
        // Otherwise, no effect.
        else {
            if(tu->state == TU_ON_HOOK) {
                dprintf(tu->fd, "ON HOOK %d\r\n", tu->fd);
            }
            else if(tu->state == TU_RINGING) {
                dprintf(tu->fd, "RINGING\r\n");
            }
            else if(tu->state == TU_RING_BACK) {
                dprintf(tu->fd, "RING BACK\r\n");
            }
            else if(tu->state == TU_BUSY_SIGNAL) {
                dprintf(tu->fd, "BUSY SIGNAL\r\n");
            }
            else if(tu->state == TU_CONNECTED) {
                dprintf(tu->fd, "CONNECTED %d\r\n", tu->target->fd);
            }
            else if(tu->state == TU_ERROR) {
                dprintf(tu->fd, "ERROR\r\n");
            }
            V(&(tu->mutex));
            return 0;
        }
    }
    // If originating telephone unit is the same as the target telephone unit.
    else if(tu == target) {
        tu->state = TU_BUSY_SIGNAL;
        dprintf(tu->fd, "BUSY SIGNAL\r\n");
        V(&(tu->mutex));
        return 0;
    }

    // Impose lock on target telephone unit.
    P(&(target->mutex));

    // If state is not TU_DIAL_TONE, no effect.
    if(tu->state != TU_DIAL_TONE) {
        if(tu->state == TU_ON_HOOK) {
            dprintf(tu->fd, "ON HOOK %d\r\n", tu->fd);
        }
        else if(tu->state == TU_RINGING) {
            dprintf(tu->fd, "RINGING\r\n");
        }
        else if(tu->state == TU_RING_BACK) {
            dprintf(tu->fd, "RING BACK\r\n");
        }
        else if(tu->state == TU_BUSY_SIGNAL) {
            dprintf(tu->fd, "BUSY SIGNAL\r\n");
        }
        else if(tu->state == TU_CONNECTED) {
            dprintf(tu->fd, "CONNECTED %d\r\n", tu->target->fd);
        }
        else if(tu->state == TU_ERROR) {
            dprintf(tu->fd, "ERROR\r\n");
        }
        V(&(tu->mutex));
        V(&(target->mutex));
        return 0;
    }
    // If the target telephone unit already has a peer or its state is not TU_ON_HOOK.
    else if(target->target || target->state != TU_ON_HOOK) {
        tu->state = TU_BUSY_SIGNAL;
        dprintf(tu->fd, "BUSY SIGNAL\r\n");
        V(&(tu->mutex));
        V(&(target->mutex));
        return 0;
    }
    // Otherwise, record originating telephone unit and target as peers of each other.
    else {
        tu->target = target;
        target->target = tu;
        tu->state = TU_RING_BACK;
        target->state = TU_RINGING;
        dprintf(tu->fd, "RING BACK\r\n");
        dprintf(target->fd, "RINGING\r\n");
        V(&(tu->mutex));
        tu_ref(tu, "Connected to peer.");
        V(&(target->mutex));
        tu_ref(target, "Connected to peer.");
        return 0;
    }
    //Unexpected error.
    V(&(tu->mutex));
    V(&(target->mutex));
    return -1;
}

/*
 * Take a TU receiver off-hook (i.e. pick up the handset).
 *   If the TU is in neither the TU_ON_HOOK state nor the TU_RINGING state,
 *     then there is no effect.
 *   If the TU is in the TU_ON_HOOK state, it goes to the TU_DIAL_TONE state.
 *   If the TU was in the TU_RINGING state, it goes to the TU_CONNECTED state,
 *     reflecting an answered call.  In this case, the calling TU simultaneously
 *     also transitions to the TU_CONNECTED state.
 *
 * In all cases, a notification of the resulting state of the specified TU is sent to
 * to the associated network client.  If a peer TU has changed state, then its client
 * is also notified of its new state.
 *
 * @param tu  The TU that is to be picked up.
 * @return 0 if successful, -1 if any error occurs that results in the originating
 * TU transitioning to the TU_ERROR state. 
 */
int tu_pickup(TU *tu) {
    // Impose lock on originating telephone unit.
    if(!tu) {
        return -1;
    }
    P(&(tu->mutex));

    // If telephone unit is not in TU_ON_HOOK nor TU_RINGING, no effect.
    if(tu->state != TU_ON_HOOK && tu->state != TU_RINGING) {
        if(tu->state == TU_DIAL_TONE) {
            dprintf(tu->fd, "DIAL TONE\r\n");
        }
        else if(tu->state == TU_RING_BACK) {
            dprintf(tu->fd, "RING BACK\r\n");
        }
        else if(tu->state == TU_BUSY_SIGNAL) {
            dprintf(tu->fd, "BUSY SIGNAL\r\n");
        }
        else if(tu->state == TU_CONNECTED) {
            dprintf(tu->fd, "CONNECTED %d\r\n", tu->target->fd);
        }
        else if(tu->state == TU_ERROR) {
            dprintf(tu->fd, "ERROR\r\n");
        }
        V(&(tu->mutex));
        return 0;
    }
    // If telephone unit is in TU_ON_HOOK, then transition to TU_DIAL_TONE.
    else if(tu->state == TU_ON_HOOK) {
        tu->state = TU_DIAL_TONE;
        dprintf(tu->fd, "DIAL TONE\r\n");
        V(&(tu->mutex));
        return 0;
    }
    // If telephone unit is in TU_RINGING, then transition to TU_CONNECTED for it and its target.
    else if(tu->state == TU_RINGING) {
        P(&(tu->target->mutex));
        tu->state = TU_CONNECTED;
        tu->target->state = TU_CONNECTED;
        dprintf(tu->fd, "CONNECTED %d\r\n", tu->target->fd);
        dprintf(tu->target->fd, "CONNECTED %d\r\n", tu->fd);
        V(&(tu->mutex));
        V(&(tu->target->mutex));
        return 0;
    }
    // Unexpected error.
    V(&(tu->mutex));
    return -1;
}

/*
 * Hang up a TU (i.e. replace the handset on the switchhook).
 *
 *   If the TU is in the TU_CONNECTED or TU_RINGING state, then it goes to the
 *     TU_ON_HOOK state.  In addition, in this case the peer TU (the one to which
 *     the call is currently connected) simultaneously transitions to the TU_DIAL_TONE
 *     state.
 *   If the TU was in the TU_RING_BACK state, then it goes to the TU_ON_HOOK state.
 *     In addition, in this case the calling TU (which is in the TU_RINGING state)
 *     simultaneously transitions to the TU_ON_HOOK state.
 *   If the TU was in the TU_DIAL_TONE, TU_BUSY_SIGNAL, or TU_ERROR state,
 *     then it goes to the TU_ON_HOOK state.
 *
 * In all cases, a notification of the resulting state of the specified TU is sent to
 * to the associated network client.  If a peer TU has changed state, then its client
 * is also notified of its new state.
 *
 * @param tu  The tu that is to be hung up.
 * @return 0 if successful, -1 if any error occurs that results in the originating
 * TU transitioning to the TU_ERROR state. 
 */
int tu_hangup(TU *tu) {
    // Impose lock on originating telephone unit.
    if(!tu) {
        return -1;
    }
    P(&(tu->mutex));

    // If telephone unit is in TU_CONNECTED or TU_RINGING, transition to TU_ON_HOOK and its target transitions to TU_DIAL_TONE.
    if(tu->state == TU_CONNECTED || tu->state == TU_RINGING) {
        // Lock target.
        P(&(tu->target->mutex));
        tu->state = TU_ON_HOOK;
        tu->target->state = TU_DIAL_TONE;
        tu->target->target = NULL;
        dprintf(tu->fd, "ON HOOK %d\r\n", tu->fd);
        dprintf(tu->target->fd, "DIAL TONE\r\n");
        // Unlock target, set reference to NULL.
        V(&(tu->target->mutex));
        tu_unref(tu->target, "Peer hung up.");
        tu->target = NULL;
        V(&(tu->mutex));
        tu_unref(tu, "Hung up from peer.");
        return 0;
    }
    // If telephone unit is in TU_RING_BACK, transition to TU_ON_HOOK and its target transitions to TU_ON_HOOK.
    else if(tu->state == TU_RING_BACK) {
        //Lock target.
        P(&(tu->target->mutex));
        tu->state = TU_ON_HOOK;
        tu->target->state = TU_ON_HOOK;
        tu->target->target = NULL;
        dprintf(tu->fd, "ON HOOK %d\r\n", tu->fd);
        dprintf(tu->target->fd, "ON HOOK %d\r\n", tu->target->fd);
        // Unlock target, set reference to NULL.
        V(&(tu->target->mutex));
        tu_unref(tu->target, "Stopped ringing.");
        tu->target = NULL;
        V(&(tu->mutex));
        tu_unref(tu, "Stopped dialing.");
        return 0;
    }
    else if(tu->state == TU_DIAL_TONE || tu->state == TU_BUSY_SIGNAL || tu->state == TU_ERROR) {
        tu->state = TU_ON_HOOK;
        dprintf(tu->fd, "ON HOOK %d\r\n", tu->fd);
        V(&(tu->mutex));
        return 0;
    }
    else if(tu->state == TU_ON_HOOK) {
        V(&(tu->mutex));
        return 0;
    }
    // Unexpected error.
    return -1;
}

/*
 * "Chat" over a connection.
 *
 * If the state of the TU is not TU_CONNECTED, then nothing is sent and -1 is returned.
 * Otherwise, the specified message is sent via the network connection to the peer TU.
 * In all cases, the states of the TUs are left unchanged and a notification containing
 * the current state is sent to the TU sending the chat.
 *
 * @param tu  The tu sending the chat.
 * @param msg  The message to be sent.
 * @return 0  If the chat was successfully sent, -1 if there is no call in progress
 * or some other error occurs.
 */

int tu_chat(TU *tu, char *msg) {
    // Impose lock on originating telephone unit.
    if(!tu) {
        return -1;
    }
    P(&(tu->mutex));

    // If not in TU_CONNECTED, no effect.
    if(tu->state != TU_CONNECTED) {
        V(&(tu->mutex));
        return -1;
    }
    // If in TU_CONNECTED, send message to target.
    P(&(tu->target->mutex));
    dprintf(tu->target->fd, "chat %s\r\n", msg);
    if(tu->state == TU_ON_HOOK) {
        dprintf(tu->fd, "ON HOOK %d\r\n", tu->fd);
    }
    else if(tu->state == TU_RINGING) {
        dprintf(tu->fd, "RINGING\r\n");
    }
    else if(tu->state == TU_DIAL_TONE) {
        dprintf(tu->fd, "DIAL TONE\r\n");
    }
    else if(tu->state == TU_RING_BACK) {
        dprintf(tu->fd, "RING BACK\r\n");
    }
    else if(tu->state == TU_BUSY_SIGNAL) {
        dprintf(tu->fd, "BUSY SIGNAL\r\n");
    }
    else if(tu->state == TU_CONNECTED) {
        dprintf(tu->fd, "CONNECTED %d\r\n", tu->target->fd);
    }
    else if(tu->state == TU_ERROR) {
        dprintf(tu->fd, "ERROR\r\n");
    }
    V(&(tu->mutex));
    V(&(tu->target->mutex));
    return 0;
}

