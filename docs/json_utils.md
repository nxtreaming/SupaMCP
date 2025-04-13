# JSON Utility Functions Documentation

## Overview

This document describes the JSON utility functions in the MCP Server. These functions provide a convenient API for working with JSON data, particularly for checking types and extracting values.

## Key Features

1. **Type Checking**: Functions to check the type of a JSON value (string, number, boolean, null, array, object).
2. **Value Extraction**: Functions to extract values from JSON objects with type safety.
3. **Object Manipulation**: Functions to work with JSON objects, such as getting properties and iterating over them.
4. **Array Manipulation**: Functions to work with JSON arrays, such as getting elements and iterating over them.

## API Reference

### Type Checking

```c
/**
 * @brief Check if a JSON value is a string
 *
 * @param json The JSON value to check
 * @return true if the value is a string, false otherwise
 */
bool mcp_json_is_string(const mcp_json_t* json);

/**
 * @brief Check if a JSON value is a number
 *
 * @param json The JSON value to check
 * @return true if the value is a number, false otherwise
 */
bool mcp_json_is_number(const mcp_json_t* json);

/**
 * @brief Check if a JSON value is a boolean
 *
 * @param json The JSON value to check
 * @return true if the value is a boolean, false otherwise
 */
bool mcp_json_is_boolean(const mcp_json_t* json);

/**
 * @brief Check if a JSON value is null
 *
 * @param json The JSON value to check
 * @return true if the value is null, false otherwise
 */
bool mcp_json_is_null(const mcp_json_t* json);

/**
 * @brief Check if a JSON value is an array
 *
 * @param json The JSON value to check
 * @return true if the value is an array, false otherwise
 */
bool mcp_json_is_array(const mcp_json_t* json);

/**
 * @brief Check if a JSON value is an object
 *
 * @param json The JSON value to check
 * @return true if the value is an object, false otherwise
 */
bool mcp_json_is_object(const mcp_json_t* json);
```

### Value Extraction

```c
/**
 * @brief Get the string value of a JSON string
 *
 * @param json The JSON string to get the value of
 * @return The string value, or NULL if the JSON value is not a string
 */
const char* mcp_json_string_value(const mcp_json_t* json);

/**
 * @brief Get the number value of a JSON number
 *
 * @param json The JSON number to get the value of
 * @return The number value, or 0.0 if the JSON value is not a number
 */
double mcp_json_number_value(const mcp_json_t* json);

/**
 * @brief Get the boolean value of a JSON boolean
 *
 * @param json The JSON boolean to get the value of
 * @return The boolean value, or false if the JSON value is not a boolean
 */
bool mcp_json_boolean_value(const mcp_json_t* json);
```

### Object Manipulation

```c
/**
 * @brief Get the size of a JSON object
 *
 * @param json The JSON object to get the size of
 * @return The number of properties in the object, or 0 if the JSON value is not an object
 */
size_t mcp_json_object_size(const mcp_json_t* json);

/**
 * @brief Get a property from a JSON object by index
 *
 * @param json The JSON object to get the property from
 * @param index The index of the property to get
 * @param name Pointer to store the property name
 * @param value Pointer to store the property value
 * @return 0 on success, non-zero on error
 */
int mcp_json_object_get_at(const mcp_json_t* json, size_t index, const char** name, mcp_json_t** value);
```

## Usage Examples

### Type Checking and Value Extraction

```c
// Check if a JSON value is a string and get its value
if (mcp_json_is_string(json)) {
    const char* str = mcp_json_string_value(json);
    printf("String value: %s\n", str);
}

// Check if a JSON value is a number and get its value
if (mcp_json_is_number(json)) {
    double num = mcp_json_number_value(json);
    printf("Number value: %f\n", num);
}

// Check if a JSON value is a boolean and get its value
if (mcp_json_is_boolean(json)) {
    bool b = mcp_json_boolean_value(json);
    printf("Boolean value: %s\n", b ? "true" : "false");
}

// Check if a JSON value is null
if (mcp_json_is_null(json)) {
    printf("JSON value is null\n");
}
```

### Working with JSON Objects

```c
// Check if a JSON value is an object
if (mcp_json_is_object(json)) {
    // Get the size of the object
    size_t size = mcp_json_object_size(json);
    printf("Object has %zu properties\n", size);
    
    // Iterate over the object properties
    for (size_t i = 0; i < size; i++) {
        const char* name = NULL;
        mcp_json_t* value = NULL;
        if (mcp_json_object_get_at(json, i, &name, &value) == 0) {
            printf("Property %zu: %s\n", i, name);
            
            // Check the type of the property value
            if (mcp_json_is_string(value)) {
                printf("  String value: %s\n", mcp_json_string_value(value));
            } else if (mcp_json_is_number(value)) {
                printf("  Number value: %f\n", mcp_json_number_value(value));
            } else if (mcp_json_is_boolean(value)) {
                printf("  Boolean value: %s\n", mcp_json_boolean_value(value) ? "true" : "false");
            } else if (mcp_json_is_null(value)) {
                printf("  Null value\n");
            } else if (mcp_json_is_object(value)) {
                printf("  Object value\n");
            } else if (mcp_json_is_array(value)) {
                printf("  Array value\n");
            }
        }
    }
}
```

### Working with JSON Arrays

```c
// Check if a JSON value is an array
if (mcp_json_is_array(json)) {
    // Get the size of the array
    size_t size = mcp_json_array_size(json);
    printf("Array has %zu elements\n", size);
    
    // Iterate over the array elements
    for (size_t i = 0; i < size; i++) {
        mcp_json_t* element = mcp_json_array_get(json, i);
        if (element != NULL) {
            printf("Element %zu:\n", i);
            
            // Check the type of the element
            if (mcp_json_is_string(element)) {
                printf("  String value: %s\n", mcp_json_string_value(element));
            } else if (mcp_json_is_number(element)) {
                printf("  Number value: %f\n", mcp_json_number_value(element));
            } else if (mcp_json_is_boolean(element)) {
                printf("  Boolean value: %s\n", mcp_json_boolean_value(element) ? "true" : "false");
            } else if (mcp_json_is_null(element)) {
                printf("  Null value\n");
            } else if (mcp_json_is_object(element)) {
                printf("  Object value\n");
            } else if (mcp_json_is_array(element)) {
                printf("  Array value\n");
            }
        }
    }
}
```

## Implementation Details

The JSON utility functions are implemented in `src/json/mcp_json_utils_impl.c`. They build on the core JSON API provided by `mcp_json.h` to provide a more convenient and type-safe API for working with JSON data.

The implementation follows these principles:

1. **Type Safety**: Functions check the type of JSON values before operating on them, returning appropriate default values if the type is not as expected.
2. **Null Safety**: Functions handle NULL JSON values gracefully, typically returning default values or false for type checks.
3. **Convenience**: Functions provide a more convenient API than the core JSON API, reducing the amount of code needed to work with JSON data.
4. **Performance**: Functions are designed to be efficient, avoiding unnecessary allocations and operations.

## Limitations

1. **Error Handling**: The functions return default values when errors occur, which may make it difficult to distinguish between valid default values and errors.
2. **Memory Management**: The functions do not manage memory for JSON values; callers are responsible for creating and destroying JSON values.
3. **Thread Safety**: The functions are not thread-safe; callers are responsible for ensuring thread safety when working with JSON values.

## Future Improvements

1. **Error Reporting**: Add functions to get detailed error information when operations fail.
2. **Path-Based Access**: Add functions to access JSON values using path expressions, such as "user.address.city".
3. **Schema Validation**: Add functions to validate JSON values against schemas.
4. **Serialization Options**: Add functions to control serialization options, such as pretty printing and escaping.
5. **Stream Processing**: Add functions to process JSON data in a streaming fashion, for large JSON documents.
