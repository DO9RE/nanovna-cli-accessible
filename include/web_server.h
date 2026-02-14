#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <map>
#include <functional>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <errno.h>
// Define Windows-compatible types for POSIX
typedef int SOCKET;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

// Forward declarations
class Logger;
class TranslationManager;

/**
 * @brief Simple HTTP server for web-based terminal interface
 * 
 * Provides local network access to the NanoVNA application via browser.
 * Features:
 * - Accessible terminal interface with ARIA live regions
 * - Keyboard input (line-based and immediate getch mode)
 * - Audio streaming (WAV format)
 * - Local network only (no HTTPS, no authentication)
 */
class WebServer {
public:
    WebServer(Logger* logger = nullptr);
    ~WebServer();

    /**
     * @brief Start the web server on specified port
     * @param port Port number (default 8080)
     * @param bindAddress IP address to bind (default "0.0.0.0" for all interfaces)
     * @return true if server started successfully
     */
    bool start(int port = 8080, const std::string& bindAddress = "0.0.0.0");

    /**
     * @brief Stop the web server and clean up resources
     */
    void stop();

    /**
     * @brief Check if server is running
     */
    bool isRunning() const { return running; }

    /**
     * @brief Get the server URL for display
     */
    std::string getServerURL() const;

    /**
     * @brief Get local IP addresses for display
     */
    std::vector<std::string> getLocalIPAddresses() const;

    /**
     * @brief Send output to all connected clients
     * @param text Text to send to browser terminals
     */
    void sendOutput(const std::string& text);

    /**
     * @brief Send UI context update to all connected clients
     * @param contextJSON JSON string with current context and available actions
     */
    void sendContext(const std::string& contextJSON);

    /**
     * @brief Check if there's keyboard input from browser
     * @return true if input is available
     */
    bool hasInput() const;

    /**
     * @brief Read keyboard input from browser
     * @return Input string (may contain terminal sequences)
     */
    std::string readInput();
    
    /**
     * @brief Queue input for later processing (for multi-character input)
     * @param input Input string to queue
     */
    void queueInput(const std::string& input);

    /**
     * @brief Send audio data to browser
     * @param audioData WAV audio data
     * @param size Size of audio data in bytes
     */
    void sendAudio(const uint8_t* audioData, size_t size);

private:
    Logger* logger;
    std::atomic<bool> running;
    std::atomic<bool> shouldStop;
    int serverPort;
    std::string bindAddr;
    
    SOCKET listenSocket;
    std::vector<SOCKET> clientSockets;
    
    std::thread serverThread;
    std::mutex clientMutex;
    mutable std::mutex inputMutex;  // mutable to allow locking in const methods
    std::mutex outputMutex;
    
    // Input queue from browser
    std::vector<std::string> inputQueue;
    
    // Per-client output queues for SSE clients
    // Each SSE client gets its own buffer so all clients receive all output
    std::map<int, std::string> clientOutputBuffers;
    std::condition_variable outputCV;  // Signals when new output is available
    std::vector<int> sseClients;  // Client sockets for Server-Sent Events
    
    // Shared output buffer for initial connection (before SSE client is registered)
    std::string outputBuffer;
    
    // Current UI context (available actions) for web interface
    std::string currentContextJSON;
    
    // Server main loop
    void serverLoop();
    
    // Handle HTTP request
    void handleClient(int clientSocket);
    
    // Parse HTTP request
    bool parseHTTPRequest(const std::string& request, std::string& method, 
                          std::string& path, std::string& body);
    
    // Generate HTTP response
    std::string generateHTTPResponse(int statusCode, const std::string& contentType, 
                                     const std::string& body, bool isSSE = false);
    
    // Handle SSE connection
    void handleSSE(int clientSocket);
    
    // Serve HTML page
    std::string getHTMLPage();
    
    // Serve JavaScript
    std::string getJavaScript();
    
    // Initialize Windows sockets
    bool initWinsock();
    
    // Cleanup Windows sockets
    void cleanupWinsock();
};
