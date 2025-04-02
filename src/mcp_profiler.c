#include "mcp_profiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Only compile the implementation if profiling is enabled
#ifdef MCP_ENABLE_PROFILING

#define MAX_PROFILE_SECTIONS 64 // Adjust as needed

typedef struct {
    const char* name;       // Pointer to the constant string name
    clock_t start_time;     // Temporary start time for the current call
    double total_duration;  // Cumulative duration in seconds
    long long call_count;   // Cumulative number of calls
    bool active;            // Is this section currently being timed?
} profile_section_t;

static profile_section_t profile_data[MAX_PROFILE_SECTIONS];
static int profile_section_count = 0;
static bool profiler_initialized = false;

// Simple linear search to find or add a section
static profile_section_t* find_or_add_section(const char* section_name) {
    if (!profiler_initialized) {
        // Initialize on first use
        memset(profile_data, 0, sizeof(profile_data));
        profiler_initialized = true;
    }

    for (int i = 0; i < profile_section_count; ++i) {
        if (profile_data[i].name == section_name) { // Compare pointers directly as names are const strings
            return &profile_data[i];
        }
    }

    // Not found, add if space available
    if (profile_section_count < MAX_PROFILE_SECTIONS) {
        profile_data[profile_section_count].name = section_name;
        profile_data[profile_section_count].total_duration = 0.0;
        profile_data[profile_section_count].call_count = 0;
        profile_data[profile_section_count].active = false;
        return &profile_data[profile_section_count++];
    }

    // Table full
    fprintf(stderr, "Profiler Error: Exceeded maximum profile sections (%d). Cannot track '%s'.\n", MAX_PROFILE_SECTIONS, section_name);
    return NULL;
}

void mcp_profile_start(const char* section_name) {
    profile_section_t* section = find_or_add_section(section_name);
    if (section) {
        if (section->active) {
            // Warning: Starting an already active section (nested calls?) - ignoring inner start
            // A more complex implementation could handle nesting.
        } else {
            section->active = true;
            section->start_time = clock();
        }
    }
}

void mcp_profile_end(const char* section_name) {
    profile_section_t* section = find_or_add_section(section_name); // Find existing section
    if (section) {
        if (!section->active) {
            // Warning: Ending an inactive section - ignoring
        } else {
            clock_t end_time = clock();
            section->total_duration += (double)(end_time - section->start_time) / CLOCKS_PER_SEC;
            section->call_count++;
            section->active = false;
        }
    }
}

void mcp_profile_report(FILE* output) {
    if (!output) output = stdout; // Default to stdout

    fprintf(output, "\n--- Profiling Report ---\n");
    fprintf(output, "%-30s | %-15s | %-15s | %-15s\n", "Section", "Total Time (s)", "Call Count", "Avg Time (ms)");
    fprintf(output, "-------------------------------|-----------------|-----------------|-----------------\n");

    for (int i = 0; i < profile_section_count; ++i) {
        profile_section_t* section = &profile_data[i];
        double avg_time_ms = (section->call_count > 0) ? (section->total_duration * 1000.0 / section->call_count) : 0.0;
        fprintf(output, "%-30s | %15.6f | %15lld | %15.6f\n",
                section->name,
                section->total_duration,
                section->call_count,
                avg_time_ms);
        if (section->active) {
             fprintf(output, "  (Warning: Section '%s' was still active during report generation!)\n", section->name);
        }
    }
    fprintf(output, "----------------------------------------------------------------------------------\n");
}

void mcp_profile_reset() {
    // Simply reset the count, find_or_add_section will overwrite old data
    profile_section_count = 0;
    profiler_initialized = false; // Force re-initialization on next use
    // Optionally, explicitly zero out the memory again:
    // memset(profile_data, 0, sizeof(profile_data));
}

#else // MCP_ENABLE_PROFILING not defined

// Provide empty stub functions if profiling is disabled
void mcp_profile_start(const char* section_name) { (void)section_name; }
void mcp_profile_end(const char* section_name) { (void)section_name; }
void mcp_profile_report(FILE* output) { (void)output; }
void mcp_profile_reset() { }

#endif // MCP_ENABLE_PROFILING
