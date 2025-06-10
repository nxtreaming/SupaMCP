/**
 * @file test_windows_text_mode.c
 * @brief Test to verify Windows text mode file I/O fixes
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#define unlink _unlink
#else
#include <unistd.h>
#endif

/**
 * @brief Test Windows text mode vs binary mode file I/O
 */
static void test_text_vs_binary_mode(void) {
    printf("Testing text mode vs binary mode file I/O...\n");
    
    const char* test_filename = "test_text_mode.txt";
    const char* test_data = "Line 1\r\nLine 2\r\nLine 3\r\n";
    size_t original_size = strlen(test_data);
    
    // Write test data in binary mode
    FILE* fp = fopen(test_filename, "wb");
    assert(fp != NULL);
    
    size_t written = fwrite(test_data, 1, original_size, fp);
    assert(written == original_size);
    fclose(fp);
    
    printf("Written %zu bytes in binary mode\n", written);
    
    // Test 1: Read in text mode (problematic on Windows)
    fp = fopen(test_filename, "r");
    assert(fp != NULL);
    
    char text_buffer[256] = {0};
    size_t text_read = fread(text_buffer, 1, sizeof(text_buffer) - 1, fp);
    fclose(fp);
    
    printf("Read %zu bytes in text mode\n", text_read);
    
    // Test 2: Read in binary mode (correct)
    fp = fopen(test_filename, "rb");
    assert(fp != NULL);
    
    char binary_buffer[256] = {0};
    size_t binary_read = fread(binary_buffer, 1, sizeof(binary_buffer) - 1, fp);
    fclose(fp);
    
    printf("Read %zu bytes in binary mode\n", binary_read);
    
    // On Windows, text mode should read fewer bytes due to \r\n -> \n conversion
    // On Unix, both should be the same
#ifdef _WIN32
    printf("Windows detected: text mode read %zu bytes, binary mode read %zu bytes\n", 
           text_read, binary_read);
    if (text_read < binary_read) {
        printf("Confirmed: Windows text mode converts \\r\\n to \\n (read %zu < %zu)\n", 
               text_read, binary_read);
    } else {
        printf("Warning: Expected text mode to read fewer bytes on Windows\n");
    }
#else
    printf("Unix/Linux detected: both modes should read the same number of bytes\n");
    assert(text_read == binary_read);
#endif
    
    // Verify binary mode reads the exact original data
    assert(binary_read == original_size);
    assert(memcmp(binary_buffer, test_data, original_size) == 0);
    
    // Cleanup
    remove(test_filename);
    
    printf("Text vs binary mode test completed\n");
}

/**
 * @brief Test file size vs actual read bytes
 */
static void test_file_size_vs_read_bytes(void) {
    printf("Testing file size vs actual read bytes...\n");
    
    const char* test_filename = "test_size_read.txt";
    const char* test_data = "Data with CRLF\r\nSecond line\r\nThird line\r\n";
    size_t original_size = strlen(test_data);
    
    // Write test data
    FILE* fp = fopen(test_filename, "wb");
    assert(fp != NULL);
    fwrite(test_data, 1, original_size, fp);
    fclose(fp);
    
    // Get file size using fseek/ftell
    fp = fopen(test_filename, "rb");
    assert(fp != NULL);
    
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    printf("File size reported by ftell(): %ld bytes\n", file_size);
    
    // Read the file and check actual bytes read
    char* buffer = malloc(file_size + 1);
    assert(buffer != NULL);
    
    size_t bytes_read = fread(buffer, 1, file_size, fp);
    buffer[bytes_read] = '\0';
    fclose(fp);
    
    printf("Actual bytes read by fread(): %zu bytes\n", bytes_read);
    
    // In binary mode, these should always match
    assert(bytes_read == (size_t)file_size);
    assert(bytes_read == original_size);
    
    free(buffer);
    remove(test_filename);
    
    printf("File size vs read bytes test completed\n");
}

/**
 * @brief Test the safe file read pattern we implemented
 */
static void test_safe_file_read_pattern(void) {
    printf("Testing safe file read pattern...\n");
    
    const char* test_filename = "test_safe_read.dat";
    const char* test_data = "Binary data\r\nwith mixed\r\nline endings\n";
    size_t original_size = strlen(test_data);
    
    // Write test data
    FILE* fp = fopen(test_filename, "wb");
    assert(fp != NULL);
    fwrite(test_data, 1, original_size, fp);
    fclose(fp);
    
    // Use the safe read pattern: binary mode + actual bytes read
    fp = fopen(test_filename, "rb");
    assert(fp != NULL);
    
    fseek(fp, 0, SEEK_END);
    long expected_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char* buffer = malloc(expected_size + 1);
    assert(buffer != NULL);
    
    size_t actual_read = fread(buffer, 1, expected_size, fp);
    buffer[actual_read] = '\0';  // Use actual bytes read, not expected size
    fclose(fp);
    
    printf("Expected size: %ld, actual read: %zu\n", expected_size, actual_read);
    
    // Verify we read the correct data
    assert(actual_read == original_size);
    assert(memcmp(buffer, test_data, original_size) == 0);
    
    free(buffer);
    remove(test_filename);
    
    printf("Safe file read pattern test completed\n");
}

/**
 * @brief Main test function
 */
int main(void) {
    printf("Starting Windows text mode file I/O tests...\n\n");
    
    test_text_vs_binary_mode();
    printf("\n");
    
    test_file_size_vs_read_bytes();
    printf("\n");
    
    test_safe_file_read_pattern();
    printf("\n");
    
    printf("All Windows text mode tests completed successfully!\n");
    printf("\nKey takeaways:\n");
    printf("1. Always use binary mode ('rb', 'wb') for data files\n");
    printf("2. Use actual bytes read from fread(), not expected file size\n");
    printf("3. On Windows, text mode can cause byte count mismatches\n");
    
    return 0;
}
