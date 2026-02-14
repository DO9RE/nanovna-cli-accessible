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
    : logger(logger_), running(false), shouldStop(false), serverPort(8080), 
      listenSocket(INVALID_SOCKET) {
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
    // No initialization needed for POSIX sockets
    return true;
#endif
}

void WebServer::cleanupWinsock() {
#if defined(_WIN32)
    WSACleanup();
#else
    // No cleanup needed for POSIX sockets
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

    // Create socket
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
#if defined(_WIN32)
        if (logger) logger->log("WEBSERVER", "Failed to create socket: " + std::to_string(WSAGetLastError()));
#else
        if (logger) logger->log("WEBSERVER", "Failed to create socket: " + std::string(strerror(errno)));
#endif
        cleanupWinsock();
        return false;
    }

    // Allow socket reuse
#if defined(_WIN32)
    char opt = 1;
    if (setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == SOCKET_ERROR) {
        if (logger) logger->log("WEBSERVER", "Warning: Failed to set SO_REUSEADDR: " + std::to_string(WSAGetLastError()));
#else
    int opt = 1;
    if (setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == SOCKET_ERROR) {
        if (logger) logger->log("WEBSERVER", "Warning: Failed to set SO_REUSEADDR: " + std::string(strerror(errno)));
#endif
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
#if defined(_WIN32)
        if (logger) logger->log("WEBSERVER", "Failed to bind socket: " + std::to_string(WSAGetLastError()));
        closesocket(listenSocket);
#else
        if (logger) logger->log("WEBSERVER", "Failed to bind socket: " + std::string(strerror(errno)));
        close(listenSocket);
#endif
        listenSocket = INVALID_SOCKET;
        cleanupWinsock();
        return false;
    }

    // Listen
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
#if defined(_WIN32)
        if (logger) logger->log("WEBSERVER", "Failed to listen: " + std::to_string(WSAGetLastError()));
        closesocket(listenSocket);
#else
        if (logger) logger->log("WEBSERVER", "Failed to listen: " + std::string(strerror(errno)));
        close(listenSocket);
#endif
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
}

void WebServer::stop() {
    if (!running) {
        return;
    }

    shouldStop = true;

    // Give SSE threads time to notice shouldStop and exit cleanly
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Close all SSE client sockets first
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        for (SOCKET sock : sseClients) {
#if defined(_WIN32)
            shutdown(sock, SD_BOTH);  // Graceful shutdown before close
            closesocket(sock);
#else
            shutdown(sock, SHUT_RDWR);  // Graceful shutdown before close
            close(sock);
#endif
        }
        sseClients.clear();
    }
    
    // Give SSE threads time to exit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Close all client sockets
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        for (SOCKET sock : clientSockets) {
#if defined(_WIN32)
            shutdown(sock, SD_BOTH);
            closesocket(sock);
#else
            shutdown(sock, SHUT_RDWR);
            close(sock);
#endif
        }
        clientSockets.clear();
    }

    // Close listen socket
    if (listenSocket != INVALID_SOCKET) {
#if defined(_WIN32)
        shutdown(listenSocket, SD_BOTH);
        closesocket(listenSocket);
#else
        shutdown(listenSocket, SHUT_RDWR);
        close(listenSocket);
#endif
        listenSocket = INVALID_SOCKET;
    }

    // Wait for server thread to finish
    if (serverThread.joinable()) {
        serverThread.join();
    }

    cleanupWinsock();
    running = false;

    if (logger) logger->log("WEBSERVER", "Server stopped");
}

void WebServer::serverLoop() {
    while (!shouldStop) {
        // Set timeout for accept
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSocket, &readSet);

        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

#if defined(_WIN32)
        int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);
        if (selectResult == SOCKET_ERROR) {
            if (logger) logger->log("WEBSERVER", "Select error: " + std::to_string(WSAGetLastError()));
#else
        int selectResult = select(listenSocket + 1, &readSet, nullptr, nullptr, &timeout);
        if (selectResult == SOCKET_ERROR) {
            if (logger) logger->log("WEBSERVER", "Select error: " + std::string(strerror(errno)));
#endif
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
#if defined(_WIN32)
                if (logger) logger->log("WEBSERVER", "Accept failed: " + std::to_string(WSAGetLastError()));
#else
                if (logger) logger->log("WEBSERVER", "Accept failed: " + std::string(strerror(errno)));
#endif
            }
            continue;
        }

        // Handle client in a separate thread to avoid blocking
        std::thread clientThread([this, clientSocket]() {
            handleClient(clientSocket);
#if defined(_WIN32)
            closesocket(clientSocket);
#else
            close(clientSocket);
#endif
        });
        clientThread.detach();
    }
}

void WebServer::handleClient(int clientSocket) {
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
    else if (method == "GET" && path == "/context") {
        // Return current UI context (available actions) as JSON
        std::string contextData;
        {
            std::lock_guard<std::mutex> lock(outputMutex);
            contextData = currentContextJSON;
        }
        if (contextData.empty()) {
            contextData = "{\"context\":\"\",\"actions\":[]}";
        }
        std::string response = generateHTTPResponse(200, "application/json", contextData);
        send(clientSocket, response.c_str(), response.length(), 0);
    }
    else {
        std::string response = generateHTTPResponse(404, "text/plain", "Not Found");
        send(clientSocket, response.c_str(), response.length(), 0);
    }
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
            height: 50vh;
            overflow-y: auto;
            white-space: pre-wrap;
            word-wrap: break-word;
            margin-bottom: 10px;
        }
        
        #input-container {
            display: flex;
            gap: 10px;
            margin-bottom: 10px;
            align-items: center;
        }
        
        .nav-arrows {
            display: flex;
            gap: 6px;
            margin-bottom: 4px;
        }
        
        button.nav-btn {
            min-width: 90px;
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
        
        button:focus {
            outline: 3px solid #ff0;
            outline-offset: 2px;
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
        
        /* Action buttons panel */
        #actions-panel {
            margin-bottom: 10px;
            padding: 10px;
            border: 2px solid #0ff;
            background: #001;
        }
        
        #actions-panel h2 {
            font-size: 1em;
            color: #0ff;
            margin-bottom: 8px;
        }
        
        #actions-container {
            display: flex;
            flex-wrap: wrap;
            gap: 6px;
        }
        
        button.action-btn {
            background: #030;
            color: #0f0;
            border: 1px solid #0f0;
            padding: 6px 12px;
            font-size: 0.9em;
            font-family: 'Courier New', monospace;
            cursor: pointer;
            min-width: 80px;
            text-align: center;
        }
        
        button.action-btn:hover {
            background: #050;
            border-color: #0ff;
            color: #0ff;
        }
        
        button.action-btn:focus {
            outline: 3px solid #ff0;
            outline-offset: 2px;
        }
        
        button.action-btn .action-key {
            font-weight: bold;
            color: #0ff;
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
    
    <nav id="actions-panel" aria-label="Available Actions">
        <h2 id="actions-heading">Available Actions</h2>
        <div id="actions-container" role="toolbar" aria-labelledby="actions-heading">
            <p style="color: #888;">Connecting...</p>
        </div>
    </nav>
    
    <div id="input-container">
        <button onclick="sendSpecialKey('\r')">Enter</button>
        <button onclick="sendSpecialKey('\x1B')" class="secondary">ESC</button>
    </div>
    
    <div id="debug-controls">
        <button class="secondary" onclick="toggleDebug()" id="debug-toggle">Show Debug</button>
        <button class="secondary" onclick="copyDebugToClipboard()" id="debug-copy">Copy Debug Log</button>
        <span class="debug-label" id="debug-status"></span>
    </div>
    
    <div id="debug-output"></div>
    
    <audio id="audio-player" style="display: none;"></audio>
    
    <script src="/app.js"></script>
</body>
</html>
)html";
}

std::string WebServer::getJavaScript() {
    return R"js(
// NanoVNA Remote Terminal JavaScript

let outputDiv = document.getElementById('terminal-output');
let audioPlayer = document.getElementById('audio-player');
let debugOutputDiv = document.getElementById('debug-output');
let debugToggleBtn = document.getElementById('debug-toggle');
let debugStatusSpan = document.getElementById('debug-status');
let actionsContainer = document.getElementById('actions-container');
let actionsHeading = document.getElementById('actions-heading');
let eventSource = null;
let contextPollTimer = null;
let lastContextJSON = '';

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

// Handle keyboard input - ALL keys are sent immediately (matching console behavior)
// Listen on document so keys work without focusing any specific element
document.addEventListener('keydown', function(e) {
    // Don't capture keys when a button has focus (let Enter/Space activate it)
    if (e.target.tagName === 'BUTTON') {
        if (e.key === 'Enter' || e.key === ' ') return;
    }
    
    logDebug('INPUT', 'Key pressed', { key: e.key, ctrlKey: e.ctrlKey, altKey: e.altKey });
    
    // Send keys immediately to match console single-keypress behavior
    if (e.key === 'Enter') {
        e.preventDefault();
        sendSpecialKey('\r');
    } else if (e.key === 'Escape') {
        e.preventDefault();
        sendSpecialKey('\x1B');
    } else if (e.key === 'ArrowUp') {
        e.preventDefault();
        sendSpecialKey('\x1B[A');
    } else if (e.key === 'ArrowDown') {
        e.preventDefault();
        sendSpecialKey('\x1B[B');
    } else if (e.key === 'ArrowRight') {
        e.preventDefault();
        sendSpecialKey('\x1B[C');
    } else if (e.key === 'ArrowLeft') {
        e.preventDefault();
        sendSpecialKey('\x1B[D');
    } else if (e.key === 'Backspace') {
        e.preventDefault();
        sendSpecialKey('\x08');
    } else if (e.key === 'Delete') {
        e.preventDefault();
        sendSpecialKey('\x1B[3~');
    } else if (e.key === 'Home') {
        e.preventDefault();
        sendSpecialKey('\x1B[H');
    } else if (e.key === 'End') {
        e.preventDefault();
        sendSpecialKey('\x1B[F');
    } else if (e.key === 'Tab') {
        // Allow Tab for accessibility navigation - don't capture
        return;
    } else if (e.key === 'PageUp') {
        e.preventDefault();
        sendSpecialKey('\x1B[5~');
    } else if (e.key === 'PageDown') {
        e.preventDefault();
        sendSpecialKey('\x1B[6~');
    } else if (e.key.length === 1 && !e.ctrlKey && !e.altKey && !e.metaKey && e.key.charCodeAt(0) >= 32) {
        // Single printable character - send immediately (no Enter needed)
        e.preventDefault();
        sendSpecialKey(e.key);
    }
    // Ignore modifier-only keys (Shift, Ctrl, Alt, Meta) and browser shortcuts
});

// sendInput is no longer needed - all keys are sent immediately via sendSpecialKey
// The Enter button in the UI calls sendSpecialKey('\r') directly

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

// Send an action: key press, optionally followed by Enter
function sendAction(key, needsEnter) {
    logDebug('ACTION', 'Action button clicked', { key: key, needsEnter: needsEnter });
    sendSpecialKey(key);
    if (needsEnter) {
        // Small delay then send Enter for multi-digit selections
        setTimeout(function() {
            sendSpecialKey('\r');
        }, 100);
    }
}

// Find the last SPAN element in the terminal output (for line overwrites)
function findLastSpan() {
    var nodes = outputDiv.childNodes;
    for (var i = nodes.length - 1; i >= 0; i--) {
        if (nodes[i].nodeName === 'SPAN') {
            return nodes[i];
        }
    }
    return null;
}

// Replace or create the last line with new HTML content
function replaceLastLine(htmlContent) {
    var lastSpan = findLastSpan();
    if (lastSpan) {
        lastSpan.innerHTML = htmlContent;
    } else {
        var span = document.createElement('span');
        span.innerHTML = htmlContent;
        outputDiv.appendChild(span);
    }
}

function appendOutput(text) {
    logDebug('OUTPUT', 'Received output', { length: text.length, preview: text.substring(0, 50) });
    
    // Check for screen clear ANSI sequence (ESC[2J)
    if (text.indexOf('\x1B[2J') !== -1) {
        outputDiv.innerHTML = '';
        logDebug('OUTPUT', 'Screen cleared');
        // Remove the clear sequence and cursor home from text
        text = text.replace(/\x1B\[2J\x1B\[H/g, '');
        if (!text) return;
    }
    
    // Handle carriage return marker (\x01) - overwrite current line
    // Server encodes \r as \x01 because SSE treats raw \r as line terminator
    if (text.indexOf('\x01') !== -1) {
        var segments = text.split('\x01');
        for (var i = 0; i < segments.length; i++) {
            var segment = segments[i];
            if (i > 0 && segment.length > 0) {
                // Segment after \r overwrites the last line (terminal carriage return)
                replaceLastLine(ansiToHtml(segment));
            } else if (segment.length > 0) {
                // Normal text before any \r
                appendNormalText(segment);
            }
        }
    }
    // Handle backspace (\b) - cursor movement in text editing
    else if (text.indexOf('\b') !== -1) {
        // Strip backspace chars and update the last line with redrawn content
        var cleanText = text.replace(/\b/g, '');
        if (cleanText.length > 0) {
            replaceLastLine(ansiToHtml(cleanText));
        }
    }
    else {
        // Normal output without control characters
        appendNormalText(text);
    }
    
    // Auto-scroll to bottom
    outputDiv.scrollTop = outputDiv.scrollHeight;
    
    // Poll for context update after receiving new output
    pollContext();
}

function appendNormalText(text) {
    // Process ANSI color codes for HTML display
    var htmlText = ansiToHtml(text);
    
    // Split by newlines and create separate elements for proper ARIA announcements
    var lines = htmlText.split('\n');
    
    for (var i = 0; i < lines.length; i++) {
        var line = lines[i];
        
        if (i > 0) {
            // Add line break between lines
            outputDiv.appendChild(document.createElement('br'));
        }
        
        if (line) {
            // Create span for each line to trigger ARIA updates
            var span = document.createElement('span');
            span.innerHTML = line;
            outputDiv.appendChild(span);
        }
    }
}

// Convert ANSI color codes to HTML spans
// Only processes codes generated by the application (bold, foreground colors)
function ansiToHtml(text) {
    // Map ANSI codes to CSS styles
    const ansiMap = {
        '1': 'font-weight:bold',
        '30': 'color:#000', '31': 'color:#f00', '32': 'color:#0f0',
        '33': 'color:#ff0', '34': 'color:#55f', '35': 'color:#f0f',
        '36': 'color:#0ff', '37': 'color:#fff',
        '90': 'color:#888', '91': 'color:#f88', '92': 'color:#8f8',
        '93': 'color:#ff8', '94': 'color:#88f', '95': 'color:#f8f',
        '96': 'color:#8ff', '97': 'color:#fff'
    };
    
    let result = '';
    let openSpans = 0;  // Track open <span> tags to avoid orphan close tags
    let i = 0;
    while (i < text.length) {
        if (text[i] === '\x1B' && i + 1 < text.length && text[i+1] === '[') {
            // Parse ANSI escape sequence
            let j = i + 2;
            while (j < text.length && text[j] !== 'm') j++;
            if (j < text.length) {
                const codes = text.substring(i + 2, j).split(';');
                if (codes.length === 1 && codes[0] === '0') {
                    // Reset: close all open spans
                    while (openSpans > 0) {
                        result += '</span>';
                        openSpans--;
                    }
                } else {
                    let styles = [];
                    for (const code of codes) {
                        if (ansiMap[code]) styles.push(ansiMap[code]);
                    }
                    if (styles.length > 0) {
                        result += '<span style="' + styles.join(';') + '">';
                        openSpans++;
                    }
                }
                i = j + 1;
                continue;
            }
        }
        // Escape HTML special characters to prevent XSS
        if (text[i] === '<') result += '&lt;';
        else if (text[i] === '>') result += '&gt;';
        else if (text[i] === '&') result += '&amp;';
        else if (text[i] === '"') result += '&quot;';
        else result += text[i];
        i++;
    }
    // Close any remaining open spans
    while (openSpans > 0) {
        result += '</span>';
        openSpans--;
    }
    return result;
}

// Poll the /context endpoint for available actions
function pollContext() {
    fetch('/context')
        .then(response => response.json())
        .then(data => {
            const contextStr = JSON.stringify(data);
            if (contextStr !== lastContextJSON) {
                lastContextJSON = contextStr;
                updateActionButtons(data);
                logDebug('CONTEXT', 'Context updated', { context: data.context, actionCount: data.actions.length });
            }
        })
        .catch(err => {
            logDebug('CONTEXT', 'Failed to poll context', { error: err.toString() });
        });
}

// Update the action buttons based on context data
function updateActionButtons(contextData) {
    if (!actionsContainer) return;
    
    // Clear existing buttons
    actionsContainer.innerHTML = '';
    
    if (!contextData.actions || contextData.actions.length === 0) {
        const msg = document.createElement('p');
        msg.style.color = '#888';
        msg.textContent = 'No actions available in current context.';
        actionsContainer.appendChild(msg);
        return;
    }
    
    // Update heading with context name
    if (actionsHeading && contextData.context) {
        const contextName = contextData.context.replace(/_/g, ' ');
        actionsHeading.textContent = 'Actions: ' + contextName.charAt(0).toUpperCase() + contextName.slice(1);
    }
    
    // Add navigation arrow buttons for navigation mode (acoustic analysis)
    if (contextData.inputMode === 'navigation') {
        var navDiv = document.createElement('div');
        navDiv.className = 'nav-arrows';
        navDiv.setAttribute('role', 'group');
        navDiv.setAttribute('aria-label', 'Navigation controls');
        
        var arrows = [
            {key: '\x1B[D', label: '\u25C0 Left', ariaLabel: 'Move position left'},
            {key: '\x1B[C', label: 'Right \u25B6', ariaLabel: 'Move position right'},
            {key: '\x1B[A', label: '\u25B2 Jump +', ariaLabel: 'Increase jump width'},
            {key: '\x1B[B', label: '\u25BC Jump -', ariaLabel: 'Decrease jump width'}
        ];
        
        arrows.forEach(function(arrow) {
            var btn = document.createElement('button');
            btn.className = 'action-btn nav-btn';
            btn.setAttribute('type', 'button');
            btn.setAttribute('aria-label', arrow.ariaLabel);
            btn.textContent = arrow.label;
            btn.addEventListener('click', function() {
                sendSpecialKey(arrow.key);
            });
            navDiv.appendChild(btn);
        });
        
        actionsContainer.appendChild(navDiv);
        
        // Add separator
        var sep = document.createElement('hr');
        sep.style.margin = '8px 0';
        sep.style.borderColor = '#555';
        actionsContainer.appendChild(sep);
    }
    
    // Create a button for each available action
    contextData.actions.forEach(function(action) {
        const btn = document.createElement('button');
        btn.className = 'action-btn';
        btn.setAttribute('type', 'button');
        
        // Extract display text: remove the (X) prefix if present in the label
        let displayLabel = action.label;
        let keyDisplay = action.key.toUpperCase();
        
        // Handle special keys for display
        if (action.key === '\r' || action.key === '\n') {
            keyDisplay = 'ENTER';
        }
        
        // Try to extract the short form, e.g. "(S)ummary" -> key "S", label "Summary"
        const match = displayLabel.match(/^\((.+?)\)\s*(.*)$/);
        if (match) {
            keyDisplay = match[1];
            displayLabel = match[2] || keyDisplay;
        }
        
        // Create accessible button content
        const keySpan = document.createElement('span');
        keySpan.className = 'action-key';
        keySpan.textContent = '[' + keyDisplay + '] ';
        
        const labelSpan = document.createElement('span');
        labelSpan.textContent = displayLabel;
        
        btn.appendChild(keySpan);
        btn.appendChild(labelSpan);
        
        // Accessible label
        btn.setAttribute('aria-label', displayLabel + ' (key: ' + keyDisplay + ')');
        
        // Click handler - send the key, optionally followed by Enter
        const actionKey = action.key;
        const needsEnter = action.needsEnter;
        btn.addEventListener('click', function() {
            sendAction(actionKey, needsEnter);
        });
        
        actionsContainer.appendChild(btn);
    });
    
    // Add ESC button if not in main menu
    if (contextData.context && contextData.context !== 'main_menu') {
        const escBtn = document.createElement('button');
        escBtn.className = 'action-btn';
        escBtn.setAttribute('type', 'button');
        escBtn.setAttribute('aria-label', 'Back (Escape key)');
        
        const keySpan = document.createElement('span');
        keySpan.className = 'action-key';
        keySpan.textContent = '[ESC] ';
        const labelSpan = document.createElement('span');
        labelSpan.textContent = 'Back';
        escBtn.appendChild(keySpan);
        escBtn.appendChild(labelSpan);
        
        escBtn.addEventListener('click', function() {
            sendSpecialKey('\x1B');
        });
        actionsContainer.appendChild(escBtn);
    }
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

// Start SSE connection and context polling when page loads
window.addEventListener('load', function() {
    logDebug('PAGE', 'Page loaded, initializing');
    connectSSE();
    
    // Initial context poll
    pollContext();
    
    // Poll context periodically (every 2 seconds as fallback)
    contextPollTimer = setInterval(pollContext, 2000);
});

// Cleanup on page unload
window.addEventListener('beforeunload', function() {
    logDebug('PAGE', 'Page unloading, closing connections');
    if (eventSource) {
        eventSource.close();
    }
    if (contextPollTimer) {
        clearInterval(contextPollTimer);
    }
});
)js";
}

std::string WebServer::getServerURL() const {
    return "http://localhost:" + std::to_string(serverPort);
}

std::vector<std::string> WebServer::getLocalIPAddresses() const {
    std::vector<std::string> addresses;
    
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct addrinfo hints, *result;
#if defined(_WIN32)
        ZeroMemory(&hints, sizeof(hints));
#else
        memset(&hints, 0, sizeof(hints));
#endif
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
    
    return addresses;
}

void WebServer::handleSSE(int clientSocket) {
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
            // Preserve \r as \\r so the JS client can handle carriage return semantics
            std::istringstream dataStream(data);
            std::string line;
            while (std::getline(dataStream, line)) {
                // Check if original line had \r (carriage return for line overwrite)
                bool hasCR = (!line.empty() && line.back() == '\r');
                if (hasCR) {
                    line.pop_back();
                }
                // Also check for leading \r (used by acoustic status messages)
                bool hasLeadingCR = (!line.empty() && line.front() == '\r');
                if (hasLeadingCR) {
                    line.erase(0, 1);
                }
                // Encode \r as \x01 marker for JS client
                // SSE protocol treats raw \r as a line terminator, so we can't use it
                if (hasCR || hasLeadingCR) {
                    event << "data: \x01" << line << "\n";
                } else {
                    event << "data: " << line << "\n";
                }
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
}

void WebServer::sendOutput(const std::string& text) {
    std::lock_guard<std::mutex> lock(outputMutex);
    outputBuffer += text;
    
    if (logger) {
        logger->log("WEBSERVER", "Output added to buffer. Length: " + std::to_string(text.length()) + 
                    ", Buffer size: " + std::to_string(outputBuffer.length()));
    }
}

void WebServer::sendContext(const std::string& contextJSON) {
    std::lock_guard<std::mutex> lock(outputMutex);
    currentContextJSON = contextJSON;
    
    if (logger) {
        logger->log("WEBSERVER", "Context updated: " + contextJSON.substr(0, 100));
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
