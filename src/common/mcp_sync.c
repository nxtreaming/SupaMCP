#include "mcp_sync.h"
#include <stdlib.h>
#include <errno.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#endif

// --- Internal Structures ---

struct mcp_mutex_s {
#ifdef _WIN32
    CRITICAL_SECTION cs;
#else
    pthread_mutex_t mutex;
#endif
};

struct mcp_cond_s {
#ifdef _WIN32
    CONDITION_VARIABLE cv;
#else
    pthread_cond_t cond;
#endif
};

// --- Mutex Implementation ---

mcp_mutex_t* mcp_mutex_create(void) {
    mcp_mutex_t* mutex = (mcp_mutex_t*)malloc(sizeof(mcp_mutex_t));
    if (!mutex) {
        return NULL;
    }

#ifdef _WIN32
    InitializeCriticalSection(&mutex->cs);
    // Assume success for InitializeCriticalSection as it's void
    return mutex;
#else
    if (pthread_mutex_init(&mutex->mutex, NULL) != 0) {
        free(mutex);
        return NULL;
    }
    return mutex;
#endif
}

void mcp_mutex_destroy(mcp_mutex_t* mutex) {
    if (!mutex) {
        return;
    }
#ifdef _WIN32
    DeleteCriticalSection(&mutex->cs);
#else
    pthread_mutex_destroy(&mutex->mutex);
#endif
    free(mutex);
}

int mcp_mutex_lock(mcp_mutex_t* mutex) {
    if (!mutex) {
        return -1; // Or some error code
    }
#ifdef _WIN32
    EnterCriticalSection(&mutex->cs);
    return 0; // EnterCriticalSection is void
#else
    return pthread_mutex_lock(&mutex->mutex);
#endif
}

int mcp_mutex_unlock(mcp_mutex_t* mutex) {
    if (!mutex) {
        return -1;
    }
#ifdef _WIN32
    LeaveCriticalSection(&mutex->cs);
    return 0; // LeaveCriticalSection is void
#else
    return pthread_mutex_unlock(&mutex->mutex);
#endif
}

// --- Condition Variable Implementation ---

mcp_cond_t* mcp_cond_create(void) {
    mcp_cond_t* cond = (mcp_cond_t*)malloc(sizeof(mcp_cond_t));
    if (!cond) {
        return NULL;
    }

#ifdef _WIN32
    InitializeConditionVariable(&cond->cv);
    // Assume success
    return cond;
#else
    if (pthread_cond_init(&cond->cond, NULL) != 0) {
        free(cond);
        return NULL;
    }
    return cond;
#endif
}

void mcp_cond_destroy(mcp_cond_t* cond) {
    if (!cond) {
        return;
    }
#ifdef _WIN32
    // No explicit destruction function for CONDITION_VARIABLE
#else
    pthread_cond_destroy(&cond->cond);
#endif
    free(cond);
}

int mcp_cond_wait(mcp_cond_t* cond, mcp_mutex_t* mutex) {
    if (!cond || !mutex) {
        return -1;
    }
#ifdef _WIN32
    // SleepConditionVariableCS returns TRUE on success, FALSE on failure.
    if (!SleepConditionVariableCS(&cond->cv, &mutex->cs, INFINITE)) {
        return -1; // Indicate error
    }
    return 0; // Success
#else
    return pthread_cond_wait(&cond->cond, &mutex->mutex);
#endif
}

int mcp_cond_timedwait(mcp_cond_t* cond, mcp_mutex_t* mutex, uint32_t timeout_ms) {
    if (!cond || !mutex) {
        return -1;
    }

#ifdef _WIN32
    // SleepConditionVariableCS returns TRUE on success, FALSE on failure/timeout.
    if (!SleepConditionVariableCS(&cond->cv, &mutex->cs, timeout_ms)) {
        if (GetLastError() == ERROR_TIMEOUT) {
            return -2; // Indicate timeout
        }
        return -1; // Indicate other error
    }
    return 0; // Success
#else
    struct timespec ts;
    struct timeval tv;

    // Get current time
    gettimeofday(&tv, NULL); // Using gettimeofday as clock_gettime might not be available everywhere (e.g., older macOS)

    // Calculate absolute timeout time
    uint64_t nsec = (uint64_t)tv.tv_usec * 1000 + (uint64_t)(timeout_ms % 1000) * 1000000;
    ts.tv_sec = tv.tv_sec + (timeout_ms / 1000) + (nsec / 1000000000);
    ts.tv_nsec = nsec % 1000000000;

    int ret = pthread_cond_timedwait(&cond->cond, &mutex->mutex, &ts);
    if (ret == ETIMEDOUT) {
        return -2; // Indicate timeout
    } else if (ret != 0) {
        return -1; // Indicate other error
    }
    return 0; // Success
#endif
}

int mcp_cond_signal(mcp_cond_t* cond) {
    if (!cond) {
        return -1;
    }
#ifdef _WIN32
    WakeConditionVariable(&cond->cv);
    return 0; // WakeConditionVariable is void
#else
    return pthread_cond_signal(&cond->cond);
#endif
}

int mcp_cond_broadcast(mcp_cond_t* cond) {
    if (!cond) {
        return -1;
    }
#ifdef _WIN32
    WakeAllConditionVariable(&cond->cv);
    return 0; // WakeAllConditionVariable is void
#else
    return pthread_cond_broadcast(&cond->cond);
#endif
}

// --- Thread Implementation ---

int mcp_thread_create(mcp_thread_t* thread_handle, mcp_thread_func_t start_routine, void* arg) {
    if (!thread_handle || !start_routine) {
        return -1;
    }
#ifdef _WIN32
    // Note: Windows thread function signature is different (DWORD WINAPI func(LPVOID)),
    // but casting mcp_thread_func_t often works if the calling conventions match.
    // A safer approach might involve a wrapper function.
    HANDLE handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)start_routine, arg, 0, NULL);
    if (handle == NULL) {
        *thread_handle = NULL;
        return -1;
    }
    *thread_handle = handle;
    return 0;
#else
    pthread_t tid;
    int ret = pthread_create(&tid, NULL, start_routine, arg);
    if (ret != 0) {
        *thread_handle = 0; // Or some invalid value
        return -1;
    }
    *thread_handle = tid;
    return 0;
#endif
}

int mcp_thread_join(mcp_thread_t thread_handle, void** retval) {
#ifdef _WIN32
    // WaitForSingleObject waits for the thread to terminate.
    // Getting the actual return value is more complex and not directly supported here.
    if (WaitForSingleObject((HANDLE)thread_handle, INFINITE) == WAIT_FAILED) {
        return -1;
    }
    // Close the handle after joining
    CloseHandle((HANDLE)thread_handle);
    if (retval) *retval = NULL; // Indicate return value retrieval not supported
    return 0;
#else
    return pthread_join((pthread_t)thread_handle, retval);
#endif
}

