# Deadlock-Detection-and-Recovery-Using-Wait-For-Graph-in-C++
This project implements a console-based Deadlock Detection and Recovery Simulator in C++ using multithreading and a 
resource allocation model. Multiple worker threads simulate concurrent processes that dynamically request and release 
resources. The system tracks these interactions using matrices for allocation, request, and available resources. 
 
A background deadlock detection thread periodically checks for unsafe states by evaluating if any thread’s request can be 
fulfilled. If none can proceed, the system identifies a deadlock. To recover, a simple recovery policy terminates one of the 
deadlocked threads and reclaims its resources, restoring the system to a safe state. 
 
Thread synchronization is ensured using std::mutex to prevent race conditions when accessing shared resource data. All 
events, including allocations, detections, and terminations, are logged to the console in real time. 
 
This simulator provides a functional demonstration of key Operating Systems concepts such as deadlock, resource 
management, and thread synchronization. It also include a GUI interface using SFML for real-time visualization of thread 
states and resource graphs.
