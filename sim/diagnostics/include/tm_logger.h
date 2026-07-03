#ifndef TM_LOGGER_H
#define TM_LOGGER_H

#include <string>
#include <fstream>
#include <vector>
#include <sstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include "vehicle_state.h"

class TelemetryLogger 
{
    private:
        std::ofstream outFile;
        std::ostringstream buffer;
        int frameCount = 0;
        const int FLUSH_INTERVAL = 60;

        // Telemetry sampling period in sim seconds. The sim may step at 30-60 Hz,
        // but the analysis pipeline only needs ~10 Hz rows, so frames arriving
        // sooner than this since the last logged frame are skipped.
        const float LOG_INTERVAL_SECONDS = 0.1f;
        float lastLogTime = -1.0f; // negative sentinel: always log the first frame

        // Threading components
        std::thread workerThread;
        std::mutex queueMutex;
        std::condition_variable cv;
        std::queue<std::string> writeQueue;
        std::atomic<bool> isRunning;

        // The background worker loop
        void processQueue() 
        {
            while (true) 
            {
                std::string dataChunk;

                {
                    std::unique_lock<std::mutex> lock(queueMutex);
                    // Wait until there is data in the queue, or we are shutting down
                    cv.wait(lock, [this]() { return !writeQueue.empty() || !isRunning; });

                    // If shutting down and the queue is empty, exit the thread
                    if (!isRunning && writeQueue.empty()) 
                    {
                        break;
                    }

                    // Grab the next chunk of data
                    dataChunk = std::move(writeQueue.front());
                    writeQueue.pop();
                }

                // Write to disk OUTSIDE the mutex lock so the main thread isn't blocked
                if (outFile.is_open()) 
                {
                    outFile << dataChunk;
                    outFile.flush(); // Optional: force write to disk immediately
                }
            }
        }

    public:
        TelemetryLogger(const std::string& filename) : isRunning(true)
        {
            outFile.open(filename);
            if (outFile.is_open()) 
            {
                outFile << "Time,VehicleID,EdgeID,LaneIndex,Speed_mps,Accel_mps2,"
                        << "Pos_m,RouteIndex,WaitTime_s,OriginID,DestID\n";
            }
            
            // Start the background thread
            workerThread = std::thread(&TelemetryLogger::processQueue, this);
        }

        ~TelemetryLogger() 
        {
            flush(); // Ensure the last main-thread buffer is queued

            // Signal the background thread to stop and wake it up
            isRunning = false;
            cv.notify_one();

            // Wait for the background thread to finish writing everything
            if (workerThread.joinable()) 
            {
                workerThread.join();
            }

            if (outFile.is_open()) 
            {
                outFile.close();
            }
        }

        void logFrame(float currentTime, const std::vector<VehicleState*>& activeCars)
        {
            // Downsample to LOG_INTERVAL_SECONDS. The 1ms tolerance keeps float
            // drift in the summed sim clock from postponing a sample one step.
            if (lastLogTime >= 0.0f && (currentTime - lastLogTime) < (LOG_INTERVAL_SECONDS - 0.001f))
            {
                return;
            }
            lastLogTime = currentTime;

            // The main thread only writes to memory (fast string building)
            for (const VehicleState* vhcl : activeCars) 
            {
                buffer << currentTime << ","
                       << vhcl->getId() << ","
                       << vhcl->getEdgeId() << ","
                       << vhcl->getLane() << ","
                       << vhcl->getSpeed() << ","
                       << vhcl->getAcceleration() << ","
                       << vhcl->getPos() << ","
                       << vhcl->currentRouteIndex << ","
                       << vhcl->getWaitTime() << ","
                       << vhcl->getOrigin() << ","
                       << vhcl->getDestination() << "\n";
            }

            frameCount++;
            if (frameCount >= FLUSH_INTERVAL) 
            {
                flush();
            }
        }

        void flush() 
        {
            if (buffer.tellp() > 0) 
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                writeQueue.push(buffer.str()); // Copy buffer to queue
                buffer.str(""); // Clear the main thread buffer
                buffer.clear();
                frameCount = 0;
            }
            // Wake up the background thread to process the new data
            cv.notify_one(); 
        }
};

#endif