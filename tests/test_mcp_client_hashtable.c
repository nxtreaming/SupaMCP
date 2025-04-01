#include "unity.h"
#include "mcp_client.h"
#include <stdlib.h>
#include <stdio.h>

// --- !!! WARNING: Including .c file is generally bad practice !!! ---
// Done here ONLY to access static functions and internal types for testing the hash table.
// A better approach would be to refactor the hash table into its own module.
#include "../client/src/mcp_client.c"
// --- End Warning ---


// --- Test Globals / Setup / Teardown ---

// We need a dummy mcp_client struct to hold the hash table state for testing
static mcp_client_t test_client;
// Dummy transport needed for create/destroy, won't actually be used
static mcp_transport_t dummy_transport;

void setUp_client_hashtable(void) {
    // Manually initialize the parts of mcp_client needed for hash table tests
    // We don't need a real transport or full config here.
    memset(&test_client, 0, sizeof(mcp_client_t));
    test_client.transport = &dummy_transport; // Avoid NULL check in destroy

#ifdef _WIN32
    InitializeCriticalSection(&test_client.pending_requests_mutex);
#else
    pthread_mutex_init(&test_client.pending_requests_mutex, NULL);
#endif

    test_client.pending_requests_capacity = INITIAL_PENDING_REQUESTS_CAPACITY;
    test_client.pending_requests_count = 0;
    test_client.pending_requests_table = (pending_request_entry_t*)calloc(
        test_client.pending_requests_capacity, sizeof(pending_request_entry_t));
    TEST_ASSERT_NOT_NULL(test_client.pending_requests_table);
     for (size_t i = 0; i < test_client.pending_requests_capacity; ++i) {
         test_client.pending_requests_table[i].request.status = PENDING_REQUEST_INVALID;
    }
}

void tearDown_client_hashtable(void) {
    // Clean up hash table resources
    if (test_client.pending_requests_table != NULL) {
         // Manually destroy condition variables if they were initialized by tests
         // (Actual add/remove tests will handle CV init/destroy)
         for (size_t i = 0; i < test_client.pending_requests_capacity; ++i) {
             if (test_client.pending_requests_table[i].id != 0 &&
                 test_client.pending_requests_table[i].request.status != PENDING_REQUEST_INVALID) {
#ifndef _WIN32
                 // Attempt destroy only if status suggests it might be initialized
                 if (test_client.pending_requests_table[i].request.status != PENDING_REQUEST_WAITING) {
                    // pthread_cond_destroy(&test_client.pending_requests_table[i].request.cv);
                    // Note: Cannot reliably destroy here without knowing which tests initialized CVs.
                    // Tests that use CVs should clean them up.
                 }
#endif
             }
         }
        free(test_client.pending_requests_table);
        test_client.pending_requests_table = NULL;
    }
#ifdef _WIN32
    DeleteCriticalSection(&test_client.pending_requests_mutex);
#else
    pthread_mutex_destroy(&test_client.pending_requests_mutex);
#endif
}

// --- Test Cases ---

// Helper to create a dummy pending_request_t
static pending_request_t create_dummy_request(uint64_t id) {
    pending_request_t req;
    req.id = id;
    req.status = PENDING_REQUEST_WAITING; // Default status
    req.result_ptr = NULL;
    req.error_code_ptr = NULL;
    req.error_message_ptr = NULL;
    // Note: CV is NOT initialized here, tests needing it must init/destroy
    return req;
}

// Test adding a single entry
void test_hashtable_add_single(void) {
    pending_request_t req = create_dummy_request(101);
    TEST_ASSERT_EQUAL_INT(0, add_pending_request_entry(&test_client, req.id, &req));
    TEST_ASSERT_EQUAL_size_t(1, test_client.pending_requests_count);

    pending_request_entry_t* entry = find_pending_request_entry(&test_client, 101, false);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_UINT64(101, entry->id);
    TEST_ASSERT_EQUAL(PENDING_REQUEST_WAITING, entry->request.status);
}

// Test finding a non-existent entry
void test_hashtable_find_non_existent(void) {
    TEST_ASSERT_NULL(find_pending_request_entry(&test_client, 999, false));
}

// Test adding multiple entries (no collision assumed initially)
void test_hashtable_add_multiple(void) {
    pending_request_t req1 = create_dummy_request(101);
    pending_request_t req2 = create_dummy_request(102);
    pending_request_t req3 = create_dummy_request(103);

    TEST_ASSERT_EQUAL_INT(0, add_pending_request_entry(&test_client, req1.id, &req1));
    TEST_ASSERT_EQUAL_INT(0, add_pending_request_entry(&test_client, req2.id, &req2));
    TEST_ASSERT_EQUAL_INT(0, add_pending_request_entry(&test_client, req3.id, &req3));
    TEST_ASSERT_EQUAL_size_t(3, test_client.pending_requests_count);

    TEST_ASSERT_NOT_NULL(find_pending_request_entry(&test_client, 101, false));
    TEST_ASSERT_NOT_NULL(find_pending_request_entry(&test_client, 102, false));
    TEST_ASSERT_NOT_NULL(find_pending_request_entry(&test_client, 103, false));
}

// Test adding a duplicate ID (should fail)
void test_hashtable_add_duplicate(void) {
    pending_request_t req1 = create_dummy_request(101);
    TEST_ASSERT_EQUAL_INT(0, add_pending_request_entry(&test_client, req1.id, &req1));
    TEST_ASSERT_EQUAL_size_t(1, test_client.pending_requests_count);

    pending_request_t req2 = create_dummy_request(101); // Same ID
    // This should ideally return an error, but current implementation might overwrite or error later.
    // Let's assume it prevents adding duplicates based on the check inside add_pending_request_entry
     TEST_ASSERT_EQUAL_INT(-1, add_pending_request_entry(&test_client, req2.id, &req2));
     TEST_ASSERT_EQUAL_size_t(1, test_client.pending_requests_count); // Count should not increase
}

// Test removing an entry
void test_hashtable_remove_entry(void) {
    pending_request_t req1 = create_dummy_request(101);
#ifndef _WIN32 // Initialize CV for remove test on POSIX
    pthread_cond_init(&req1.cv, NULL);
#endif
    TEST_ASSERT_EQUAL_INT(0, add_pending_request_entry(&test_client, req1.id, &req1));
    TEST_ASSERT_EQUAL_size_t(1, test_client.pending_requests_count);

    // Remove requires CV to be initialized before calling remove
    pending_request_entry_t* entry = find_pending_request_entry(&test_client, 101, false);
    TEST_ASSERT_NOT_NULL(entry);
    // Manually init CV here as add_dummy doesn't
#ifndef _WIN32
    // pthread_cond_init(&entry->request.cv, NULL); // Already done above before add
#endif

    TEST_ASSERT_EQUAL_INT(0, remove_pending_request_entry(&test_client, 101));
    TEST_ASSERT_EQUAL_size_t(0, test_client.pending_requests_count);
    TEST_ASSERT_NULL(find_pending_request_entry(&test_client, 101, false)); // Should not find after remove

    // Test removing non-existent
    TEST_ASSERT_EQUAL_INT(-1, remove_pending_request_entry(&test_client, 999));
}

// Test adding after removing (reuse slot)
void test_hashtable_add_after_remove(void) {
     pending_request_t req1 = create_dummy_request(101);
#ifndef _WIN32
     pthread_cond_init(&req1.cv, NULL);
#endif
     TEST_ASSERT_EQUAL_INT(0, add_pending_request_entry(&test_client, req1.id, &req1));
     TEST_ASSERT_EQUAL_INT(0, remove_pending_request_entry(&test_client, 101));
     TEST_ASSERT_EQUAL_size_t(0, test_client.pending_requests_count);

     pending_request_t req2 = create_dummy_request(102);
     TEST_ASSERT_EQUAL_INT(0, add_pending_request_entry(&test_client, req2.id, &req2));
     TEST_ASSERT_EQUAL_size_t(1, test_client.pending_requests_count);
     TEST_ASSERT_NOT_NULL(find_pending_request_entry(&test_client, 102, false));
}

// Test collision handling (add items that hash to the same bucket)
// Assuming INITIAL_PENDING_REQUESTS_CAPACITY is 16
void test_hashtable_collision(void) {
     uint64_t id1 = 5;
     uint64_t id2 = 5 + INITIAL_PENDING_REQUESTS_CAPACITY; // Should collide
     uint64_t id3 = 5 + 2 * INITIAL_PENDING_REQUESTS_CAPACITY; // Should also collide

     pending_request_t req1 = create_dummy_request(id1);
     pending_request_t req2 = create_dummy_request(id2);
     pending_request_t req3 = create_dummy_request(id3);

#ifndef _WIN32 // Init CVs for removal later
    pthread_cond_init(&req1.cv, NULL);
    pthread_cond_init(&req2.cv, NULL);
    pthread_cond_init(&req3.cv, NULL);
#endif

     TEST_ASSERT_EQUAL_INT(0, add_pending_request_entry(&test_client, req1.id, &req1));
     TEST_ASSERT_EQUAL_INT(0, add_pending_request_entry(&test_client, req2.id, &req2));
     TEST_ASSERT_EQUAL_INT(0, add_pending_request_entry(&test_client, req3.id, &req3));
     TEST_ASSERT_EQUAL_size_t(3, test_client.pending_requests_count);

     // Verify all can be found
     TEST_ASSERT_NOT_NULL(find_pending_request_entry(&test_client, id1, false));
     TEST_ASSERT_NOT_NULL(find_pending_request_entry(&test_client, id2, false));
     TEST_ASSERT_NOT_NULL(find_pending_request_entry(&test_client, id3, false));

     // Remove middle one
     TEST_ASSERT_EQUAL_INT(0, remove_pending_request_entry(&test_client, id2));
     TEST_ASSERT_EQUAL_size_t(2, test_client.pending_requests_count);
     TEST_ASSERT_NULL(find_pending_request_entry(&test_client, id2, false));
     TEST_ASSERT_NOT_NULL(find_pending_request_entry(&test_client, id1, false)); // Others still present
     TEST_ASSERT_NOT_NULL(find_pending_request_entry(&test_client, id3, false));

     // Remove first one
     TEST_ASSERT_EQUAL_INT(0, remove_pending_request_entry(&test_client, id1));
     TEST_ASSERT_EQUAL_size_t(1, test_client.pending_requests_count);
     TEST_ASSERT_NULL(find_pending_request_entry(&test_client, id1, false));
     TEST_ASSERT_NOT_NULL(find_pending_request_entry(&test_client, id3, false)); // Last one still present

     // Remove last one
     TEST_ASSERT_EQUAL_INT(0, remove_pending_request_entry(&test_client, id3));
     TEST_ASSERT_EQUAL_size_t(0, test_client.pending_requests_count);
     TEST_ASSERT_NULL(find_pending_request_entry(&test_client, id3, false));
}
