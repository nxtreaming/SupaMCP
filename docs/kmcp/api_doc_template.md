# KMCP API Documentation Template

## Function Documentation Template

All KMCP API functions should use the following documentation format:

```c
/**
 * @brief Brief description of the function (one sentence)
 *
 * Detailed description of the function, its purpose, and behavior. If the function
 * has special behavior or limitations, they should be mentioned here.
 *
 * @param param1 Description of parameter 1
 * @param param2 Description of parameter 2
 * @return Description of the return value, including possible error codes
 *
 * @note Optional note providing additional information
 * @warning Optional warning to alert users about important considerations
 * @see Optional reference to related functions
 * @example Optional usage example
 */
```

## Structure Documentation Template

All KMCP structures should use the following documentation format:

```c
/**
 * @brief Brief description of the structure (one sentence)
 *
 * Detailed description of the structure, its purpose, and behavior.
 */
typedef struct {
    int field1;      /**< Description of field 1 */
    char* field2;    /**< Description of field 2 */
    bool field3;     /**< Description of field 3 */
} kmcp_struct_t;
```

## Enumeration Documentation Template

All KMCP enumerations should use the following documentation format:

```c
/**
 * @brief Brief description of the enumeration (one sentence)
 *
 * Detailed description of the enumeration, its purpose, and behavior.
 */
typedef enum {
    KMCP_ENUM_VALUE1 = 0,    /**< Description of enum value 1 */
    KMCP_ENUM_VALUE2 = 1,    /**< Description of enum value 2 */
    KMCP_ENUM_VALUE3 = 2     /**< Description of enum value 3 */
} kmcp_enum_t;
```

## Naming Conventions

1. All KMCP API functions should have the `kmcp_` prefix
2. All KMCP structure types should have the `kmcp_` prefix and the `_t` suffix
3. All KMCP enumeration types should have the `kmcp_` prefix and the `_t` suffix
4. All KMCP enumeration values should have the `KMCP_` prefix and be in uppercase
5. All KMCP macros should have the `KMCP_` prefix and be in uppercase

## Parameter Checking

All KMCP API functions should check the validity of parameters at the beginning of the function and return `KMCP_ERROR_INVALID_PARAMETER` error code if parameters are invalid.

## Error Handling

All KMCP API functions should return `kmcp_error_t` error codes and use the `kmcp_error_log` function to log error information when errors occur.

## Resource Management

All KMCP API functions should ensure proper resource cleanup in all error paths to avoid resource leaks.

## Thread Safety

All KMCP API functions should explicitly state their thread safety in the documentation. If a function is not thread-safe, it should be clearly stated in the documentation.

## Version Compatibility

All KMCP API functions should explicitly state their version compatibility in the documentation. If a function behaves differently in different versions, it should be clearly stated in the documentation.
