#include "unity.h"
#include "mcp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <share.h>
#endif

// --- Test Setup ---
// Redirect stderr to a temporary file to capture log output
static char tmp_log_filename[] = "temp_log_output.txt";

// Helper function to redirect stderr to a file
// Returns the original stderr fd on success, -1 on failure
static int redirect_stderr_to_file(const char* filename) {
    fflush(stderr);
#ifdef _WIN32
    int original_fd = _dup(_fileno(stderr));
    if (original_fd == -1) return -1;
    int temp_fd = -1;
    errno_t err = _sopen_s(&temp_fd, filename, _O_WRONLY | _O_CREAT | _O_TRUNC, _SH_DENYNO, _S_IREAD | _S_IWRITE);
    if (err != 0 || temp_fd == -1) {
        _close(original_fd);
        return -1;
    }
    if (_dup2(temp_fd, _fileno(stderr)) == -1) {
        _close(temp_fd);
        _close(original_fd);
        return -1;
    }
    _close(temp_fd); // Close the extra descriptor for the file
    return original_fd;
#else
    // Keep track of the original FILE* for POSIX freopen approach
    // Note: This approach is less robust than fd redirection if stderr is closed elsewhere.
    FILE* original_stderr_fp = stderr;
    FILE* temp_stderr = freopen(filename, "w", stderr);
    if (!temp_stderr) {
        return -1; // Indicate failure
    }
    // Return a dummy fd value for POSIX, as we manage FILE*
    // We store the original FILE* globally for restoration
    return fileno(original_stderr_fp); // Return original fd
#endif
}

// Helper function to restore stderr
static void restore_stderr(int original_fd) {
    fflush(stderr);
#ifdef _WIN32
    if (original_fd != -1) {
        _dup2(original_fd, _fileno(stderr));
        _close(original_fd);
    }
#else
    // On POSIX, freopen might have closed the original stream.
    // Re-opening /dev/stderr is the most reliable way.
    FILE* new_stderr = freopen("/dev/stderr", "a", stderr); // Or CON on Windows if needed
    if (!new_stderr) {
        // If reopening fails, maybe try assigning original FILE* back? Risky.
        fprintf(stdout, "Warning: Failed to reopen /dev/stderr\n");
    }
    (void)original_fd; // Suppress unused warning for POSIX
#endif
}

// Helper function to read the captured log output
char* read_log_file(void) {
    FILE* fp = fopen(tmp_log_filename, "r");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char* buffer = (char*)malloc(size + 1);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    fread(buffer, 1, size, fp);
    buffer[size] = '\0';
    fclose(fp);
    return buffer;
}

// --- Test Cases ---

void test_log_levels(void) {
    int original_fd = redirect_stderr_to_file(tmp_log_filename);
    TEST_ASSERT_NOT_EQUAL(-1, original_fd);

    // Ensure logging is enabled and set to a known level for testing
    mcp_log_set_level(MCP_LOG_LEVEL_TRACE);
    mcp_log_set_quiet(false);

    mcp_log_trace("Trace message %d", 1);
    mcp_log_debug("Debug message %s", "test");
    mcp_log_info("Info message");
    mcp_log_warn("Warning message");
    mcp_log_error("Error message");
    mcp_log_fatal("Fatal message"); // Note: Fatal doesn't exit in test mode

    char* log_output = read_log_file();
    TEST_ASSERT_NOT_NULL(log_output);

    // Check if messages appear (basic check, doesn't verify format strictly)
    TEST_ASSERT_TRUE(strstr(log_output, "TRACE") != NULL);
    TEST_ASSERT_TRUE(strstr(log_output, "Trace message 1") != NULL);
    TEST_ASSERT_TRUE(strstr(log_output, "DEBUG") != NULL);
    TEST_ASSERT_TRUE(strstr(log_output, "Debug message test") != NULL);
    TEST_ASSERT_TRUE(strstr(log_output, "INFO") != NULL);
    TEST_ASSERT_TRUE(strstr(log_output, "Info message") != NULL);
    TEST_ASSERT_TRUE(strstr(log_output, "WARN") != NULL);
    TEST_ASSERT_TRUE(strstr(log_output, "Warning message") != NULL);
    TEST_ASSERT_TRUE(strstr(log_output, "ERROR") != NULL);
    TEST_ASSERT_TRUE(strstr(log_output, "Error message") != NULL);
    TEST_ASSERT_TRUE(strstr(log_output, "FATAL") != NULL);
    TEST_ASSERT_TRUE(strstr(log_output, "Fatal message") != NULL);

    free(log_output);
    restore_stderr(original_fd);
    remove(tmp_log_filename);
}

void test_log_level_filtering(void) {
    int original_fd = redirect_stderr_to_file(tmp_log_filename);
    TEST_ASSERT_NOT_EQUAL(-1, original_fd);

    mcp_log_set_level(MCP_LOG_LEVEL_WARN);
    mcp_log_set_quiet(false);

    mcp_log_trace("Should not appear (trace)");
    mcp_log_debug("Should not appear (debug)");
    mcp_log_info("Should not appear (info)");
    mcp_log_warn("Should appear (warn)");
    mcp_log_error("Should appear (error)");

    char* log_output = read_log_file();
    TEST_ASSERT_NOT_NULL(log_output);

    TEST_ASSERT_NULL(strstr(log_output, "TRACE"));
    TEST_ASSERT_NULL(strstr(log_output, "DEBUG"));
    TEST_ASSERT_NULL(strstr(log_output, "INFO"));
    TEST_ASSERT_TRUE(strstr(log_output, "WARN") != NULL);
    TEST_ASSERT_TRUE(strstr(log_output, "Should appear (warn)") != NULL);
    TEST_ASSERT_TRUE(strstr(log_output, "ERROR") != NULL);
    TEST_ASSERT_TRUE(strstr(log_output, "Should appear (error)") != NULL);

    free(log_output);
    restore_stderr(original_fd);
    remove(tmp_log_filename);
}

void test_log_quiet_mode(void) {
    int original_fd = redirect_stderr_to_file(tmp_log_filename);
    TEST_ASSERT_NOT_EQUAL(-1, original_fd);

    mcp_log_set_level(MCP_LOG_LEVEL_TRACE); // Ensure level is low
    mcp_log_set_quiet(true); // Enable quiet mode

    mcp_log_info("This should not be printed in quiet mode.");
    mcp_log_error("Neither should this.");

    char* log_output = read_log_file();
    TEST_ASSERT_NOT_NULL(log_output);
    // The file should be empty
    TEST_ASSERT_TRUE(strlen(log_output) == 0 || log_output[0] == '\0');

    free(log_output);
    restore_stderr(original_fd);
    remove(tmp_log_filename);
}

// --- Test Group Runner ---
void run_mcp_log_tests(void) {
    RUN_TEST(test_log_levels);
    RUN_TEST(test_log_level_filtering);
    RUN_TEST(test_log_quiet_mode);
}
