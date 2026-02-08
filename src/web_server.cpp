#include "web_server.h"
#include "logger.h"
#include <sstream>
#include <cstring>
#include <iostream>
#include <algorithm>

#if defined(_WIN32)
#pragma comment(lib, "ws2_32.lib")
#endif

WebServer::WebServer(Logger* logger_) 
    : logger(logger_), running(false), shouldStop(false), serverPort(8080) {
#if defined(_WIN32)
    listenSocket = INVALID_SOCKET;
#endif
}

WebServer::~WebServer() {
    stop();
}

bool WebServer::initWinsock() {
#if defined(_WIN32)
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        if (logger) logger->log("WEBSERVER", "WSAStartup failed: " + std::to_string(result));
        return false;
    }
    return true;
#else
    return false;  // Not supported on non-Windows
#endif
}

void WebServer::cleanupWinsock() {
#if defined(_WIN32)
    WSACleanup();
#endif
}

bool WebServer::start(int port, const std::string& bindAddress) {
    if (running) {
        if (logger) logger->log("WEBSERVER", "Server already running");
        return false;
    }

    if (!initWinsock()) {
        return false;
    }

    serverPort = port;
    bindAddr = bindAddress;

#if defined(_WIN32)
    // Create socket
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        if (logger) logger->log("WEBSERVER", "Failed to create socket: " + std::to_string(WSAGetLastError()));
        cleanupWinsock();
        return false;
    }

    // Allow socket reuse
    char opt = 1;
    if (setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == SOCKET_ERROR) {
        if (logger) logger->log("WEBSERVER", "Warning: Failed to set SO_REUSEADDR: " + std::to_string(WSAGetLastError()));
        // Continue anyway - not critical
    }

    // Bind socket
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    
    if (bindAddress == "0.0.0.0" || bindAddress.empty()) {
        serverAddr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, bindAddress.c_str(), &serverAddr.sin_addr);
    }

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        if (logger) logger->log("WEBSERVER", "Failed to bind socket: " + std::to_string(WSAGetLastError()));
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
        cleanupWinsock();
        return false;
    }

    // Listen
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        if (logger) logger->log("WEBSERVER", "Failed to listen: " + std::to_string(WSAGetLastError()));
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
        cleanupWinsock();
        return false;
    }

    // Start server thread
    running = true;
    shouldStop = false;
    serverThread = std::thread(&WebServer::serverLoop, this);

    if (logger) logger->log("WEBSERVER", "Server started on port " + std::to_string(port));
    return true;
#else
    return false;  // Not supported on non-Windows
#endif
}

void WebServer::stop() {
    if (!running) {
        return;
    }

    shouldStop = true;

#if defined(_WIN32)
    // Give SSE threads time to notice shouldStop and exit cleanly
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Close all SSE client sockets first
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        for (SOCKET sock : sseClients) {
            shutdown(sock, SD_BOTH);  // Graceful shutdown before close
            closesocket(sock);
        }
        sseClients.clear();
    }
    
    // Give SSE threads time to exit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Close all client sockets
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        for (SOCKET sock : clientSockets) {
            shutdown(sock, SD_BOTH);
            closesocket(sock);
        }
        clientSockets.clear();
    }

    // Close listen socket
    if (listenSocket != INVALID_SOCKET) {
        shutdown(listenSocket, SD_BOTH);
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
    }
#endif

    // Wait for server thread to finish
    if (serverThread.joinable()) {
        serverThread.join();
    }

    cleanupWinsock();
    running = false;

    if (logger) logger->log("WEBSERVER", "Server stopped");
}

void WebServer::serverLoop() {
#if defined(_WIN32)
    while (!shouldStop) {
        // Set timeout for accept
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSocket, &readSet);

        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);
        if (selectResult == SOCKET_ERROR) {
            if (logger) logger->log("WEBSERVER", "Select error: " + std::to_string(WSAGetLastError()));
            break;
        }

        if (selectResult == 0) {
            // Timeout, continue loop
            continue;
        }

        // Accept new connection
        SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            if (!shouldStop) {
                if (logger) logger->log("WEBSERVER", "Accept failed: " + std::to_string(WSAGetLastError()));
            }
            continue;
        }

        // Handle client in a separate thread to avoid blocking
        std::thread clientThread([this, clientSocket]() {
            handleClient(clientSocket);
            closesocket(clientSocket);
        });
        clientThread.detach();
    }
#endif
}

void WebServer::handleClient(int clientSocket) {
#if defined(_WIN32)
    char buffer[4096];
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytesReceived <= 0) {
        return;
    }

    buffer[bytesReceived] = '\0';
    std::string request(buffer);

    if (logger) logger->log("WEBSERVER", "Received request: " + request.substr(0, 100));

    // Parse request
    std::string method, path, body;
    if (!parseHTTPRequest(request, method, path, body)) {
        std::string response = generateHTTPResponse(400, "text/plain", "Bad Request");
        send(clientSocket, response.c_str(), response.length(), 0);
        return;
    }

    // Route request
    if (method == "GET" && path == "/") {
        std::string html = getHTMLPage();
        std::string response = generateHTTPResponse(200, "text/html; charset=utf-8", html);
        send(clientSocket, response.c_str(), response.length(), 0);
    }
    else if (method == "GET" && path == "/app.js") {
        std::string js = getJavaScript();
        std::string response = generateHTTPResponse(200, "application/javascript", js);
        send(clientSocket, response.c_str(), response.length(), 0);
    }
    else if (method == "POST" && path == "/input") {
        // Handle keyboard input from browser
        if (logger) {
            logger->log("WEBSERVER", "POST /input - Body length: " + std::to_string(body.length()));
            logger->log("WEBSERVER", "POST /input - Body content: [" + body + "]");
        }
        
        std::lock_guard<std::mutex> lock(inputMutex);
        inputQueue.push_back(body);
        
        if (logger) {
            logger->log("WEBSERVER", "Input added to queue. Queue size: " + std::to_string(inputQueue.size()));
        }
        
        std::string response = generateHTTPResponse(200, "text/plain", "OK");
        send(clientSocket, response.c_str(), response.length(), 0);
    }
    else if (method == "GET" && path == "/events") {
        // Server-Sent Events for real-time output
        handleSSE(clientSocket);
    }
    else {
        std::string response = generateHTTPResponse(404, "text/plain", "Not Found");
        send(clientSocket, response.c_str(), response.length(), 0);
    }
#endif
}

bool WebServer::parseHTTPRequest(const std::string& request, std::string& method, 
                                 std::string& path, std::string& body) {
    std::istringstream stream(request);
    std::string line;
    
    // Parse first line: METHOD PATH HTTP/VERSION
    if (!std::getline(stream, line)) {
        return false;
    }
    
    std::istringstream firstLine(line);
    std::string httpVersion;
    if (!(firstLine >> method >> path >> httpVersion)) {
        return false;
    }
    
    // Find body (after empty line)
    bool foundEmptyLine = false;
    while (std::getline(stream, line)) {
        if (line == "\r" || line.empty()) {
            foundEmptyLine = true;
            break;
        }
    }
    
    if (foundEmptyLine) {
        std::ostringstream bodyStream;
        while (std::getline(stream, line)) {
            bodyStream << line;
        }
        body = bodyStream.str();
    }
    
    return true;
}

std::string WebServer::generateHTTPResponse(int statusCode, const std::string& contentType, 
                                            const std::string& body, bool isSSE) {
    std::ostringstream response;
    
    std::string statusText;
    switch (statusCode) {
        case 200: statusText = "OK"; break;
        case 400: statusText = "Bad Request"; break;
        case 404: statusText = "Not Found"; break;
        default: statusText = "Unknown"; break;
    }
    
    response << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
    response << "Content-Type: " << contentType << "\r\n";
    
    if (isSSE) {
        response << "Cache-Control: no-cache\r\n";
        response << "Connection: keep-alive\r\n";
    } else {
        response << "Content-Length: " << body.length() << "\r\n";
        response << "Connection: close\r\n";
    }
    
    response << "Access-Control-Allow-Origin: *\r\n";
    response << "\r\n";
    response << body;
    
    return response.str();
}

std::string WebServer::getHTMLPage() {
    return R"html(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>NanoVNA Remote Terminal</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: 'Courier New', monospace;
            background: #000;
            color: #0f0;
            padding: 20px;
        }
        
        h1 {
            font-size: 1.2em;
            margin-bottom: 10px;
            color: #0ff;
        }
        
        #terminal-output {
            background: #000;
            border: 2px solid #0f0;
            padding: 10px;
            height: 60vh;
            overflow-y: auto;
            white-space: pre-wrap;
            word-wrap: break-word;
            margin-bottom: 10px;
        }
        
        #input-container {
            display: flex;
            gap: 10px;
            margin-bottom: 10px;
        }
        
        #terminal-input {
            flex: 1;
            background: #000;
            color: #0f0;
            border: 2px solid #0f0;
            padding: 10px;
            font-family: 'Courier New', monospace;
            font-size: 1em;
        }
        
        #terminal-input:focus {
            outline: 2px solid #0ff;
        }
        
        button {
            background: #0f0;
            color: #000;
            border: none;
            padding: 10px 20px;
            font-family: 'Courier New', monospace;
            cursor: pointer;
            font-weight: bold;
        }
        
        button:hover {
            background: #0ff;
        }
        
        button.secondary {
            background: #666;
            color: #fff;
            padding: 5px 10px;
            font-size: 0.9em;
        }
        
        button.secondary:hover {
            background: #888;
        }
        
        #debug-controls {
            display: flex;
            gap: 10px;
            margin-bottom: 10px;
            align-items: center;
        }
        
        #debug-output {
            background: #111;
            border: 2px solid #666;
            padding: 10px;
            height: 200px;
            overflow-y: auto;
            white-space: pre-wrap;
            word-wrap: break-word;
            margin-top: 10px;
            font-size: 0.85em;
            color: #888;
            display: none;
        }
        
        #debug-output.visible {
            display: block;
        }
        
        .info {
            color: #888;
            font-size: 0.9em;
            margin-top: 10px;
        }
        
        .debug-label {
            color: #888;
            font-size: 0.9em;
        }
    </style>
</head>
<body>
    <h1>NanoVNA Remote Terminal - Accessible Interface</h1>
    
    <div id="terminal-output" 
         role="log" 
         aria-live="polite" 
         aria-atomic="false" 
         aria-relevant="additions text"
         tabindex="0"></div>
    
    <div id="input-container">
        <input type="text" 
               id="terminal-input" 
               placeholder="Type command and press Enter..."
               aria-label="Terminal input"
               autocomplete="off">
        <button onclick="sendInput()">Send</button>
    </div>
    
    <div id="debug-controls">
        <button class="secondary" onclick="toggleDebug()" id="debug-toggle">Show Debug</button>
        <button class="secondary" onclick="copyDebugToClipboard()" id="debug-copy">Copy Debug Log</button>
        <span class="debug-label" id="debug-status"></span>
    </div>
    
    <div id="debug-output"></div>
    
    <div class="info">
        <p>Screen reader optimized. All output is announced automatically.</p>
        <p>Keyboard shortcuts: Arrow keys, Escape, and special keys are supported.</p>
    </div>
    
    <audio id="audio-player" style="display: none;"></audio>
    
    <script src="/app.js"></script>
</body>
</html>
)html";
}

std::string WebServer::getJavaScript() {
    return R"js(
// NanoVNA Remote Terminal JavaScript

let inputField = document.getElementById('terminal-input');
let outputDiv = document.getElementById('terminal-output');
let audioPlayer = document.getElementById('audio-player');
let debugOutputDiv = document.getElementById('debug-output');
let debugToggleBtn = document.getElementById('debug-toggle');
let debugStatusSpan = document.getElementById('debug-status');
let eventSource = null;

// Debug logging
let debugLog = [];
let debugEnabled = false;

function logDebug(category, message, data = null) {
    const timestamp = new Date().toISOString();
    const logEntry = {
        timestamp: timestamp,
        category: category,
        message: message,
        data: data
    };
    debugLog.push(logEntry);
    
    // Format for display
    let displayText = `[${timestamp}] [${category}] ${message}`;
    if (data !== null) {
        displayText += ` | Data: ${JSON.stringify(data)}`;
    }
    
    // Update debug output if visible
    if (debugEnabled && debugOutputDiv) {
        const line = document.createElement('div');
        line.textContent = displayText;
        debugOutputDiv.appendChild(line);
        debugOutputDiv.scrollTop = debugOutputDiv.scrollHeight;
    }
    
    // Also log to console
    console.log(`[${category}] ${message}`, data || '');
    
    // Update status
    if (debugStatusSpan) {
        debugStatusSpan.textContent = `Log entries: ${debugLog.length}`;
    }
}

function toggleDebug() {
    debugEnabled = !debugEnabled;
    if (debugEnabled) {
        debugOutputDiv.classList.add('visible');
        debugToggleBtn.textContent = 'Hide Debug';
        logDebug('DEBUG', 'Debug output enabled');
        
        // Replay existing log entries
        debugOutputDiv.innerHTML = '';
        debugLog.forEach(entry => {
            let displayText = `[${entry.timestamp}] [${entry.category}] ${entry.message}`;
            if (entry.data !== null) {
                displayText += ` | Data: ${JSON.stringify(entry.data)}`;
            }
            const line = document.createElement('div');
            line.textContent = displayText;
            debugOutputDiv.appendChild(line);
        });
        debugOutputDiv.scrollTop = debugOutputDiv.scrollHeight;
    } else {
        debugOutputDiv.classList.remove('visible');
        debugToggleBtn.textContent = 'Show Debug';
        logDebug('DEBUG', 'Debug output hidden');
    }
}

function copyDebugToClipboard() {
    const logText = debugLog.map(entry => {
        let text = `[${entry.timestamp}] [${entry.category}] ${entry.message}`;
        if (entry.data !== null) {
            text += ` | Data: ${JSON.stringify(entry.data)}`;
        }
        return text;
    }).join('\n');
    
    navigator.clipboard.writeText(logText).then(() => {
        logDebug('DEBUG', 'Debug log copied to clipboard', { entries: debugLog.length });
        alert(`Debug log copied to clipboard (${debugLog.length} entries)`);
    }).catch(err => {
        logDebug('DEBUG', 'Failed to copy debug log to clipboard', { error: err.toString() });
        alert('Failed to copy to clipboard: ' + err);
    });
}

// Handle keyboard input and map special keys to terminal sequences
inputField.addEventListener('keydown', function(e) {
    logDebug('INPUT', 'Key pressed', { key: e.key, ctrlKey: e.ctrlKey, altKey: e.altKey });
    
    // Handle special keys immediately without waiting for Enter
    if (e.key === 'Enter') {
        e.preventDefault();
        sendInput();
    } else if (e.key === 'Escape') {
        e.preventDefault();
        sendSpecialKey('\x1B');  // ESC
    } else if (e.key === 'ArrowUp') {
        e.preventDefault();
        sendSpecialKey('\x1B[A');  // Up arrow
    } else if (e.key === 'ArrowDown') {
        e.preventDefault();
        sendSpecialKey('\x1B[B');  // Down arrow
    } else if (e.key === 'ArrowRight') {
        e.preventDefault();
        sendSpecialKey('\x1B[C');  // Right arrow
    } else if (e.key === 'ArrowLeft') {
        e.preventDefault();
        sendSpecialKey('\x1B[D');  // Left arrow
    } else if (e.key === ' ' && !e.ctrlKey && !e.altKey) {
        // Space key for immediate input (without modifiers)
        e.preventDefault();
        sendSpecialKey(' ');
    }
});

function sendInput() {
    const text = inputField.value;
    if (!text.trim()) return;
    
    // Add newline for line-based input
    const data = text + '\n';
    
    logDebug('INPUT', 'Sending line input', { text: text, length: data.length });
    
    fetch('/input', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: data
    })
    .then(response => {
        if (response.ok) {
            logDebug('INPUT', 'Line input sent successfully');
            inputField.value = '';
            // Server will echo the input, so no need for local echo
        } else {
            logDebug('INPUT', 'Line input failed', { status: response.status });
        }
    })
    .catch(err => {
        logDebug('INPUT', 'Error sending line input', { error: err.toString() });
        console.error('Error sending input:', err);
    });
}

function sendSpecialKey(sequence) {
    logDebug('INPUT', 'Sending special key', { 
        sequence: sequence, 
        charCodes: Array.from(sequence).map(c => c.charCodeAt(0))
    });
    
    fetch('/input', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: sequence
    })
    .then(response => {
        if (response.ok) {
            logDebug('INPUT', 'Special key sent successfully');
        } else {
            logDebug('INPUT', 'Special key send failed', { status: response.status });
        }
    })
    .catch(err => {
        logDebug('INPUT', 'Error sending special key', { error: err.toString() });
        console.error('Error sending special key:', err);
    });
}

function appendOutput(text) {
    logDebug('OUTPUT', 'Received output', { length: text.length, preview: text.substring(0, 50) });
    
    // Split by newlines and create separate elements for proper ARIA announcements
    const lines = text.split('\n');
    
    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        
        if (i > 0) {
            // Add line break between lines
            outputDiv.appendChild(document.createElement('br'));
        }
        
        if (line) {
            // Create span for each line to trigger ARIA updates
            const span = document.createElement('span');
            span.textContent = line;
            outputDiv.appendChild(span);
        }
    }
    
    // Auto-scroll to bottom
    outputDiv.scrollTop = outputDiv.scrollHeight;
}

// Connect to Server-Sent Events for real-time output
function connectSSE() {
    // Close existing connection if any
    if (eventSource) {
        eventSource.close();
        eventSource = null;
    }
    
    logDebug('SSE', 'Connecting to server events');
    eventSource = new EventSource('/events');
    
    eventSource.onopen = function() {
        logDebug('SSE', 'Connection opened successfully');
        console.log('SSE connection opened');
    };
    
    eventSource.onmessage = function(event) {
        if (event.data) {
            logDebug('SSE', 'Message received', { 
                dataLength: event.data.length, 
                preview: event.data.substring(0, 50) 
            });
            appendOutput(event.data);
        } else {
            logDebug('SSE', 'Empty message received');
        }
    };
    
    eventSource.onerror = function(err) {
        logDebug('SSE', 'Connection error', { error: err.toString() });
        console.error('SSE error:', err);
        eventSource.close();
        eventSource = null;
        // Try to reconnect after 3 seconds
        logDebug('SSE', 'Will reconnect in 3 seconds');
        setTimeout(connectSSE, 3000);
    };
}

// Start SSE connection when page loads
window.addEventListener('load', function() {
    logDebug('PAGE', 'Page loaded, initializing');
    inputField.focus();
    appendOutput('Connected to NanoVNA terminal.\n');
    appendOutput('Type commands or press keys for navigation.\n\n');
    connectSSE();
});

// Cleanup on page unload
window.addEventListener('beforeunload', function() {
    logDebug('PAGE', 'Page unloading, closing connections');
    if (eventSource) {
        eventSource.close();
    }
});
)js";
}

std::string WebServer::getServerURL() const {
    return "http://localhost:" + std::to_string(serverPort);
}

std::vector<std::string> WebServer::getLocalIPAddresses() const {
    std::vector<std::string> addresses;
    
#if defined(_WIN32)
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct addrinfo hints, *result;
        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        if (getaddrinfo(hostname, nullptr, &hints, &result) == 0) {
            for (struct addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
                sockaddr_in* sockaddr_ipv4 = (sockaddr_in*)ptr->ai_addr;
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &sockaddr_ipv4->sin_addr, ipStr, INET_ADDRSTRLEN);
                addresses.push_back(std::string(ipStr));
            }
            freeaddrinfo(result);
        }
    }
#endif
    
    return addresses;
}

void WebServer::handleSSE(int clientSocket) {
#if defined(_WIN32)
    // Send SSE headers
    std::string headers = generateHTTPResponse(200, "text/event-stream", "", true);
    send(clientSocket, headers.c_str(), headers.length(), 0);
    
    // Add this client to SSE clients list
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        sseClients.push_back(clientSocket);
    }
    
    if (logger) logger->log("WEBSERVER", "SSE client connected");
    
    // Send initial welcome message
    {
        std::ostringstream event;
        event << "data: Web interface connected. Waiting for application output...\n\n";
        send(clientSocket, event.str().c_str(), event.str().length(), 0);
        if (logger) logger->log("WEBSERVER", "Sent welcome message to SSE client");
    }
    
    // Send any buffered output to new client, then clear buffer
    {
        std::lock_guard<std::mutex> lock(outputMutex);
        if (!outputBuffer.empty()) {
            std::ostringstream event;
            event << "data: " << outputBuffer << "\n\n";
            send(clientSocket, event.str().c_str(), event.str().length(), 0);
            if (logger) logger->log("WEBSERVER", "Sent buffered output to new client: " + std::to_string(outputBuffer.length()) + " bytes");
            outputBuffer.clear();  // Clear buffer after sending to prevent stale data
        }
    }
    
    // Keep connection alive and send events
    while (!shouldStop) {
        std::string data;
        {
            std::lock_guard<std::mutex> lock(outputMutex);
            if (!outputBuffer.empty()) {
                data = outputBuffer;
                outputBuffer.clear();  // Clear immediately after reading
            }
        }
        
        if (!data.empty()) {
            // Send SSE event - handle multi-line data properly
            // SSE requires each line to be prefixed with "data: "
            std::ostringstream event;
            
            // Split data by newlines and prefix each line with "data: "
            std::istringstream dataStream(data);
            std::string line;
            while (std::getline(dataStream, line)) {
                // Remove carriage return if present
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                event << "data: " << line << "\n";
            }
            // Add final empty line to complete SSE message
            event << "\n";
            
            if (logger) {
                logger->log("WEBSERVER", "Sending SSE data: length=" + std::to_string(data.length()) + 
                            ", lines=" + std::to_string(std::count(data.begin(), data.end(), '\n')) + 
                            ", content=[" + data.substr(0, std::min(size_t(100), data.length())) + "]");
            }
            
            int result = send(clientSocket, event.str().c_str(), event.str().length(), 0);
            if (result == SOCKET_ERROR) {
                if (logger) logger->log("WEBSERVER", "SSE send failed - client disconnected");
                break;  // Client disconnected
            } else {
                if (logger) logger->log("WEBSERVER", "SSE data sent successfully, bytes: " + std::to_string(result));
            }
        }
        
        // Sleep to avoid excessive CPU usage
        // Note: This could be optimized with condition variables for better responsiveness
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Remove client from SSE clients list
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        sseClients.erase(std::remove(sseClients.begin(), sseClients.end(), clientSocket), sseClients.end());
    }
    
    if (logger) logger->log("WEBSERVER", "SSE client disconnected");
#endif
}

void WebServer::sendOutput(const std::string& text) {
    std::lock_guard<std::mutex> lock(outputMutex);
    outputBuffer += text;
    
    if (logger) {
        logger->log("WEBSERVER", "Output added to buffer. Length: " + std::to_string(text.length()) + 
                    ", Buffer size: " + std::to_string(outputBuffer.length()));
    }
}

bool WebServer::hasInput() const {
    std::lock_guard<std::mutex> lock(inputMutex);
    return !inputQueue.empty();
}

std::string WebServer::readInput() {
    std::lock_guard<std::mutex> lock(inputMutex);
    if (inputQueue.empty()) {
        return "";
    }
    std::string input = inputQueue.front();
    inputQueue.erase(inputQueue.begin());
    
    if (logger) {
        logger->log("WEBSERVER", "Input read from queue: [" + input + "], Remaining in queue: " + 
                    std::to_string(inputQueue.size()));
    }
    
    return input;
}

void WebServer::queueInput(const std::string& input) {
    std::lock_guard<std::mutex> lock(inputMutex);
    inputQueue.push_back(input);
    
    if (logger) {
        logger->log("WEBSERVER", "Input queued: [" + input + "], Queue size: " + 
                    std::to_string(inputQueue.size()));
    }
}

void WebServer::sendAudio(const uint8_t* audioData, size_t size) {
    // Audio streaming will be implemented in a future update
    // This would involve:
    // 1. Buffering audio data from WaveOut
    // 2. Converting to WAV format if needed
    // 3. Streaming to browser via separate endpoint
    // 4. HTML5 audio element playback on client side
    (void)audioData;  // Suppress unused parameter warning
    (void)size;
}
