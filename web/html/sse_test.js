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
    
    // Add last event ID if available for reconnection
    if (window.lastEventId) {
        params.push(`lastEventId=${encodeURIComponent(window.lastEventId)}`);
    }
    
    // Append parameters to URL
    if (params.length > 0) {
        url += '?' + params.join('&');
    }
    
    // Create new EventSource with the URL
    eventSource = new EventSource(url);
    
    // Update connection status
    const statusSpan = document.getElementById('connection-status');
    if (statusSpan) {
        statusSpan.textContent = 'Connecting...';
        statusSpan.className = 'connecting';
    }
    
    eventSource.onopen = function() {
        addEvent('info', 'Connected to SSE stream');
        if (statusSpan) {
            statusSpan.textContent = 'Connected';
            statusSpan.className = 'connected';
        }
    };
    
    eventSource.onerror = function(error) {
        addEvent('error', 'SSE connection error, reconnecting...');
        if (statusSpan) {
            statusSpan.textContent = 'Reconnecting...';
            statusSpan.className = 'reconnecting';
        }
        // The browser will automatically try to reconnect
    };
    
    eventSource.onmessage = function(event) {
        addEvent('message', event.data, event.lastEventId);
    };
    
    // Listen for specific event types
    eventSource.addEventListener('echo', function(event) {
        addEvent('echo', event.data, event.lastEventId);
    });
    
    eventSource.addEventListener('reverse', function(event) {
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
    
    // Echo button
    document.getElementById('echo-btn').addEventListener('click', function() {
        const text = document.getElementById('text-input').value;
        fetch('/call_tool', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                jsonrpc: '2.0',
                id: 1,
                method: 'call_tool',
                params: {
                    name: 'echo',
                    arguments: {
                        text: text
                    }
                }
            })
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
        fetch('/call_tool', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                jsonrpc: '2.0',
                id: 2,
                method: 'call_tool',
                params: {
                    name: 'reverse',
                    arguments: {
                        text: text
                    }
                }
            })
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
