#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mcp_template.h"
#include "mcp_json.h"
#include "mcp_log.h"
#include "mcp_thread_local.h"

int main(int argc, char** argv) {
    (void)argc; // Suppress unused parameter warning
    (void)argv; // Suppress unused parameter warning
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);

    // Initialize thread-local arena for JSON allocation
    if (mcp_arena_init_current_thread(0) != 0) {
        printf("Failed to initialize thread-local arena\n");
        return 1;
    }

    printf("MCP Template Demo\n");
    printf("================\n\n");

    // Test 1: Simple template
    printf("Test 1: Simple template\n");
    const char* template1 = "example://{name}/resource";

    // Create JSON object directly instead of parsing
    mcp_json_t* params1 = mcp_json_object_create();
    if (params1 == NULL) {
        printf("Failed to create JSON object\n");
        return 1;
    }

    mcp_json_t* name_value1 = mcp_json_string_create("test");
    if (name_value1 == NULL) {
        printf("Failed to create JSON string\n");
        mcp_json_destroy(params1);
        return 1;
    }

    if (mcp_json_object_set_property(params1, "name", name_value1) != 0) {
        printf("Failed to set JSON property\n");
        mcp_json_destroy(params1);
        return 1;
    }

    // Check the type of the JSON object
    mcp_json_type_t type = mcp_json_get_type(params1);
    printf("JSON object type: %d\n", type);

    char* expanded1 = mcp_template_expand(template1, params1);
    printf("Template: %s\n", template1);
    printf("Params: {\"name\":\"test\"}\n");
    printf("Expanded: %s\n\n", expanded1 ? expanded1 : "(null)");

    free(expanded1);
    mcp_json_destroy(params1);

    // Test 2: Optional parameter
    printf("Test 2: Optional parameter\n");
    const char* template2 = "example://{name}/{version?}";

    // Create JSON object directly instead of parsing
    mcp_json_t* params2 = mcp_json_object_create();
    mcp_json_t* name_value2 = mcp_json_string_create("test");
    mcp_json_object_set_property(params2, "name", name_value2);

    char* expanded2 = mcp_template_expand(template2, params2);
    printf("Template: %s\n", template2);
    printf("Params: {\"name\":\"test\"}\n");
    printf("Expanded: %s\n\n", expanded2 ? expanded2 : "(null)");

    free(expanded2);
    mcp_json_destroy(params2);

    // Test 3: Default value
    printf("Test 3: Default value\n");
    const char* template3 = "example://{name}/{version=1.0}";

    // Create JSON object directly instead of parsing
    mcp_json_t* params3 = mcp_json_object_create();
    mcp_json_t* name_value3 = mcp_json_string_create("test");
    mcp_json_object_set_property(params3, "name", name_value3);

    char* expanded3 = mcp_template_expand(template3, params3);
    printf("Template: %s\n", template3);
    printf("Params: {\"name\":\"test\"}\n");
    printf("Expanded: %s\n\n", expanded3 ? expanded3 : "(null)");

    free(expanded3);
    mcp_json_destroy(params3);

    // Test 4: Typed parameter (int)
    printf("Test 4: Typed parameter (int)\n");
    const char* template4 = "example://{name}/{id:int}";

    // Create JSON object directly instead of parsing
    mcp_json_t* params4 = mcp_json_object_create();
    mcp_json_t* name_value4 = mcp_json_string_create("test");
    mcp_json_t* id_value4 = mcp_json_string_create("123");
    mcp_json_object_set_property(params4, "name", name_value4);
    mcp_json_object_set_property(params4, "id", id_value4);

    char* expanded4 = mcp_template_expand(template4, params4);
    printf("Template: %s\n", template4);
    printf("Params: {\"name\":\"test\",\"id\":\"123\"}\n");
    printf("Expanded: %s\n\n", expanded4 ? expanded4 : "(null)");

    free(expanded4);
    mcp_json_destroy(params4);

    // Test 5: Combined features
    printf("Test 5: Combined features\n");
    const char* template5 = "example://{name}/{version:float=1.0}/{id:int?}";

    // Create JSON object directly instead of parsing
    mcp_json_t* params5 = mcp_json_object_create();
    mcp_json_t* name_value5 = mcp_json_string_create("test");
    mcp_json_t* version_value5 = mcp_json_string_create("2.5");
    mcp_json_t* id_value5 = mcp_json_string_create("42");
    mcp_json_object_set_property(params5, "name", name_value5);
    mcp_json_object_set_property(params5, "version", version_value5);
    mcp_json_object_set_property(params5, "id", id_value5);

    char* expanded5 = mcp_template_expand(template5, params5);
    printf("Template: %s\n", template5);
    printf("Params: {\"name\":\"test\",\"version\":\"2.5\",\"id\":\"42\"}\n");
    printf("Expanded: %s\n\n", expanded5 ? expanded5 : "(null)");

    free(expanded5);
    mcp_json_destroy(params5);

    // Test 6: Parameter extraction
    printf("Test 6: Parameter extraction\n");
    const char* template6 = "example://{name}/{version:float}/{id:int}";
    const char* uri6 = "example://test/2.5/123";

    mcp_json_t* extracted_params = mcp_template_extract_params(uri6, template6);
    char* extracted_json = mcp_json_stringify(extracted_params);

    printf("Template: %s\n", template6);
    printf("URI: %s\n", uri6);
    printf("Extracted params: %s\n\n", extracted_json);

    free(extracted_json);
    mcp_json_destroy(extracted_params);

    // Test 6b: Pattern matching
    printf("Test 6b: Pattern matching\n");
    const char* template6b = "example://{name}/{type:pattern:i*e}";

    // Create JSON object directly instead of parsing
    mcp_json_t* params6b = mcp_json_object_create();
    mcp_json_t* name_value6b = mcp_json_string_create("test");
    mcp_json_t* type_value6b = mcp_json_string_create("image");
    mcp_json_object_set_property(params6b, "name", name_value6b);
    mcp_json_object_set_property(params6b, "type", type_value6b);

    char* expanded6b = mcp_template_expand(template6b, params6b);
    printf("Template: %s\n", template6b);
    printf("Params: {\"name\":\"test\",\"type\":\"image\"}\n");
    printf("Expanded: %s\n\n", expanded6b ? expanded6b : "(null)");

    free(expanded6b);
    mcp_json_destroy(params6b);

    // Test 7: Template matching
    printf("Test 7: Template matching\n");
    const char* template7 = "example://{name}/{version:float?}/{id:int=0}";
    const char* uri7a = "example://test/2.5/123";
    const char* uri7b = "example://test/123";
    const char* uri7c = "example://test";

    printf("Template: %s\n", template7);
    printf("URI 1: %s - Match: %s\n", uri7a, mcp_template_matches(uri7a, template7) ? "Yes" : "No");
    printf("URI 2: %s - Match: %s\n", uri7b, mcp_template_matches(uri7b, template7) ? "Yes" : "No");
    printf("URI 3: %s - Match: %s\n", uri7c, mcp_template_matches(uri7c, template7) ? "Yes" : "No");

    mcp_log_close();
    return 0;
}
