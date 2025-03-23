This is a **multi-threaded HTTP proxy server** with **LRU (Least Recently Used) caching** implemented in C.  

### **How It Works:**
1. **Accepting Client Requests**  
   - The server listens on a specified port for incoming client connections.  
   - When a client connects, a new thread is created to handle the request.  
   - The request is parsed to extract the HTTP method, host, path, and version.  

2. **Cache Lookup**  
   - Before forwarding the request, the proxy checks if the response is available in its cache.  
   - If found, the cached response is sent directly to the client.  
   - If not found, the request is forwarded to the remote server.  

3. **Forwarding Requests & Fetching Responses**  
   - The proxy establishes a connection with the remote server.  
   - It forwards the client’s request and receives the response.  
   - The response is then sent back to the client.  

4. **Caching the Response**  
   - If the response is within the allowed cache size, it is stored.  
   - The cache follows an **LRU (Least Recently Used) eviction policy**, meaning the oldest unused data is removed first when the cache is full.  

5. **Threading & Synchronization**  
   - The proxy uses **pthreads** to handle multiple client requests concurrently.  
   - **Semaphores and mutex locks** ensure safe access to shared resources like the cache.  

6. **Error Handling**  
   - The proxy detects and responds with appropriate HTTP error messages (e.g., **400 Bad Request, 404 Not Found, 500 Internal Server Error**).  

This proxy server improves performance by caching frequently requested data and allowing multiple clients to be served simultaneously. 🚀
