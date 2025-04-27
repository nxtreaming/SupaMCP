// Function to add an event to the events div
function addEvent(type, data, eventId = null) {
    const eventsDiv = document.getElementById('events');
    const eventDiv = document.createElement('div');
    eventDiv.className = `event ${type}`;

    const now = new Date();
    const timestamp = now.toLocaleTimeString();

    // Update last event ID if provided
    if (eventId) {
        window.lastEventId = eventId;
        const lastEventIdSpan = document.getElementById('last-event-id');
        if (lastEventIdSpan) {
            lastEventIdSpan.textContent = eventId;
        }
    }

    let jsonData;
    try {
        jsonData = JSON.parse(data);
        let content = `[${timestamp}]`;
        if (eventId) {
            content += ` [ID:${eventId}]`;
        }
        content += ` ${type}: ${jsonData.text}`;
        eventDiv.textContent = content;
    } catch (e) {
        let content = `[${timestamp}]`;
        if (eventId) {
            content += ` [ID:${eventId}]`;
        }
        content += ` ${type}: ${data}`;
        eventDiv.textContent = content;
    }

    eventsDiv.appendChild(eventDiv);
    eventsDiv.scrollTop = eventsDiv.scrollHeight;
}

// Set up SSE connection
let eventSource;

function connectSSE(filter = null) {
    // Close existing connection if any
    if (eventSource) {
        eventSource.close();
    }

    // Build the URL with parameters
    let url = '/events';
    let params = [];

    // Add filter if specified
    if (filter) {
        params.push(`filter=${encodeURIComponent(filter)}`);
    }

    // Add session ID if specified
    const sessionId = document.getElementById('session-id').value.trim();
    if (sessionId) {
        const encodedSessionId = encodeURIComponent(sessionId);
        params.push(`session_id=${encodedSessionId}`);
        console.log(`Connecting to SSE with session_id: ${sessionId}`);
        console.log(`URL-encoded session_id: ${encodedSessionId}`);

        // Log each character in the session ID for debugging
        console.log('Session ID character dump:');
        for (let i = 0; i < sessionId.length; i++) {
            console.log(`  sessionId[${i}] = '${sessionId[i]}' (0x${sessionId.charCodeAt(i).toString(16).padStart(2, '0')})`);
        }

        // Log each character in the encoded session ID for debugging
        console.log('Encoded Session ID character dump:');
        for (let i = 0; i < encodedSessionId.length; i++) {
            console.log(`  encodedSessionId[${i}] = '${encodedSessionId[i]}' (0x${encodedSessionId.charCodeAt(i).toString(16).padStart(2, '0')})`);
        }
    } else {
        console.log('Connecting to SSE without session_id');
    }

    // Add last event ID if available for reconnection
    if (window.lastEventId) {
        params.push(`lastEventId=${encodeURIComponent(window.lastEventId)}`);
    }

    // Append parameters to URL
    if (params.length > 0) {
        url += '?' + params.join('&');
    }

    // Log the full URL for debugging
    console.log(`Connecting to SSE endpoint: ${url}`);

    // Close existing EventSource if any
    if (eventSource) {
        console.log('Closing existing SSE connection');
        eventSource.close();
        eventSource = null;
    }

    // Create new EventSource with the URL
    console.log(`Creating new EventSource with URL: ${url}`);
    eventSource = new EventSource(url);

    // Update connection status
    const statusSpan = document.getElementById('connection-status');
    if (statusSpan) {
        statusSpan.textContent = 'Connecting...';
        statusSpan.className = 'connecting';
    }

    eventSource.onopen = function(event) {
        console.log('SSE connection opened:', event);
        addEvent('info', 'Connected to SSE stream');
        if (statusSpan) {
            statusSpan.textContent = 'Connected';
            statusSpan.className = 'connected';
        }

        // Log the current URL and session ID
        const currentUrl = eventSource.url;
        const sessionId = document.getElementById('session-id').value.trim();
        console.log(`SSE connection established with URL: ${currentUrl}`);
        console.log(`Current session ID in input field: ${sessionId}`);

        // Check if the URL contains the session ID
        if (sessionId && currentUrl.includes(`session_id=${encodeURIComponent(sessionId)}`)) {
            console.log('Session ID is correctly included in the SSE URL');
        } else if (sessionId) {
            console.warn('Session ID is NOT included in the SSE URL!');
        }
    };

    eventSource.onerror = function(event) {
        console.error('SSE connection error:', event);
        addEvent('error', 'SSE connection error, reconnecting...');
        if (statusSpan) {
            statusSpan.textContent = 'Reconnecting...';
            statusSpan.className = 'reconnecting';
        }
        // The browser will automatically try to reconnect
    };

    eventSource.onmessage = function(event) {
        console.log('Received SSE message:', event);
        addEvent('message', event.data, event.lastEventId);
    };

    // Listen for specific event types
    eventSource.addEventListener('echo', function(event) {
        console.log('Received echo event:', event);
        addEvent('echo', event.data, event.lastEventId);
    });

    eventSource.addEventListener('reverse', function(event) {
        console.log('Received reverse event:', event);
        addEvent('reverse', event.data, event.lastEventId);
    });

    eventSource.addEventListener('tool_call', function(event) {
        addEvent('tool_call', event.data, event.lastEventId);
    });

    eventSource.addEventListener('tool_result', function(event) {
        addEvent('tool_result', event.data, event.lastEventId);
    });
}

// Set up button click handlers
function setupButtons() {
    // Connect button
    document.getElementById('connect-btn').addEventListener('click', function() {
        const filter = document.getElementById('event-filter').value;
        connectSSE(filter);
    });

    // Disconnect button
    document.getElementById('disconnect-btn').addEventListener('click', function() {
        if (eventSource) {
            eventSource.close();
            eventSource = null;
            addEvent('info', 'Disconnected from SSE stream');

            const statusSpan = document.getElementById('connection-status');
            if (statusSpan) {
                statusSpan.textContent = 'Disconnected';
                statusSpan.className = 'disconnected';
            }
        }
    });

    // Apply filter button
    document.getElementById('apply-filter-btn').addEventListener('click', function() {
        const filter = document.getElementById('event-filter').value;
        connectSSE(filter);
    });

    // Add handler for session ID changes
    document.getElementById('session-id').addEventListener('change', function() {
        // Notify user that they need to reconnect for the session ID to take effect
        addEvent('info', 'Session ID changed. Click "Reconnect" to apply the new session ID.');
    });

    // Add handler for generate session ID button
    document.getElementById('generate-session-id-btn').addEventListener('click', function() {
        document.getElementById('session-id').value = generateRandomSessionId();
        // Notify user that they need to reconnect for the session ID to take effect
        addEvent('info', 'Session ID generated. Click "Reconnect" to apply the new session ID.');
    });

    // Add handler for reconnect button
    document.getElementById('reconnect-btn').addEventListener('click', function() {
        const filter = document.getElementById('event-filter').value;
        connectSSE(filter);
        addEvent('info', 'Reconnected with current filter and session ID settings.');
    });

    // Echo button
    document.getElementById('echo-btn').addEventListener('click', function() {
        const text = document.getElementById('text-input').value;
        const sessionId = document.getElementById('session-id').value.trim();

        // Prepare request body
        const requestBody = {
            jsonrpc: '2.0',
            id: 1,
            method: 'call_tool',
            params: {
                name: 'echo',
                arguments: {
                    text: text
                }
            }
        };

        // Add session_id if provided
        if (sessionId) {
            requestBody.params.arguments.session_id = sessionId;
            console.log(`Adding session_id to echo request: ${sessionId}`);
        } else {
            console.log('No session_id provided for echo request');
        }

        fetch('/call_tool', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(requestBody)
        })
        .then(response => response.json())
        .then(data => {
            console.log('Echo response:', data);
        })
        .catch(error => {
            console.error('Error calling echo tool:', error);
            addEvent('error', 'Error calling echo tool: ' + error.message);
        });
    });

    // Reverse button
    document.getElementById('reverse-btn').addEventListener('click', function() {
        const text = document.getElementById('text-input').value;
        const sessionId = document.getElementById('session-id').value.trim();

        // Prepare request body
        const requestBody = {
            jsonrpc: '2.0',
            id: 2,
            method: 'call_tool',
            params: {
                name: 'reverse',
                arguments: {
                    text: text
                }
            }
        };

        // Add session_id if provided
        if (sessionId) {
            requestBody.params.arguments.session_id = sessionId;
        }

        fetch('/call_tool', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(requestBody)
        })
        .then(response => response.json())
        .then(data => {
            console.log('Reverse response:', data);
        })
        .catch(error => {
            console.error('Error calling reverse tool:', error);
            addEvent('error', 'Error calling reverse tool: ' + error.message);
        });
    });

    // Clear button
    document.getElementById('clear-btn').addEventListener('click', function() {
        document.getElementById('events').innerHTML = '';
    });

    // Reload button
    document.getElementById('reload-btn').addEventListener('click', function() {
        // Clear cache and reload page
        window.location.reload(true);
    });
}

// Function to generate a random session ID
function generateRandomSessionId() {
    const chars = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789';
    let result = '';
    for (let i = 0; i < 10; i++) {
        result += chars.charAt(Math.floor(Math.random() * chars.length));
    }
    return result;
}

// Initialize when the page loads
document.addEventListener('DOMContentLoaded', function() {
    connectSSE();
    setupButtons();

    // Clean up when the page is unloaded
    window.addEventListener('beforeunload', function() {
        if (eventSource) {
            eventSource.close();
        }
    });
});
