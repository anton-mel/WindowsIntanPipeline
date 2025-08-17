//----------------------------------------------------------------------------------
// main_windows_dual.cpp
//
// Windows version of Intan neural data acquisition with dual output:
// 1. FPGA processing via Python pipe (original functionality)  
// 2. Shared memory for real-time visualization (new Windows implementation)
//
// Based on Linux decoupled version but adapted for Windows shared memory
//----------------------------------------------------------------------------------

#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <time.h>
#include <stdio.h>
#include <windows.h>
#include <string>

using namespace std;

#include "rhd2000evalboardusb3.h"
#include "rhd2000registersusb3.h"
#include "rhd2000datablockusb3.h"
#include "okFrontPanelDLL.h"

#define NUM_TIMESTEPS 1000

// Shared memory data structures (matching Linux version)
struct IntanDataHeader { 
    uint32_t magic;
    uint32_t timestamp;
    uint32_t dataSize;
    uint32_t streamCount;
    uint32_t channelCount;
    uint32_t sampleRate;
};

struct IntanDataBlock { 
    uint32_t streamId;
    uint32_t channelId; 
    float value;
};

// Windows shared memory wrapper
class WindowsSharedMemory {
private:
    HANDLE hMapFile;
    void* pBuf;
    size_t size;
    
public:
    WindowsSharedMemory(const std::string& name, size_t shmSize) : hMapFile(NULL), pBuf(NULL), size(shmSize) {
        // Create shared memory mapping
        hMapFile = CreateFileMappingA(
            INVALID_HANDLE_VALUE,    // use paging file
            NULL,                    // default security
            PAGE_READWRITE,          // read/write access
            0,                       // maximum object size (high-order DWORD)
            shmSize,                 // maximum object size (low-order DWORD)
            name.c_str());           // name of mapping object

        if (hMapFile == NULL) {
            cerr << "Could not create file mapping object: " << GetLastError() << endl;
            return;
        }

        pBuf = MapViewOfFile(
            hMapFile,               // handle to map object
            FILE_MAP_ALL_ACCESS,    // read/write permission
            0,
            0,
            shmSize);

        if (pBuf == NULL) {
            cerr << "Could not map view of file: " << GetLastError() << endl;
            CloseHandle(hMapFile);
            hMapFile = NULL;
        }
    }
    
    ~WindowsSharedMemory() {
        if (pBuf) {
            UnmapViewOfFile(pBuf);
        }
        if (hMapFile) {
            CloseHandle(hMapFile);
        }
    }
    
    void* getBuffer() { return pBuf; }
    bool isValid() { return pBuf != NULL; }
};

int main(int argc, char* argv[])
{
    Rhd2000EvalBoardUsb3* evalBoard = new Rhd2000EvalBoardUsb3;

    // Open Opal Kelly XEM6310 board.
    cout << "Opening Intan USB3 device..." << endl;
    if (evalBoard->open() != 1) {
        cerr << "Failed to open Intan USB3 device" << endl;
        return 1;
    }

    // Upload FPGA bitfile before initialize (mirror decoupled app behavior)
    auto fileExists = [](const char* path) -> bool { 
        ifstream f(path);
        return f.good();
    };
    
    // Prefer explicit env var path, otherwise require local main.bit (known-good)
    bool bitfileUploaded = false;
    const char* envPath = getenv("RHD_BITFILE");
    if (envPath && fileExists(envPath)) {
        cout << "Uploading FPGA bitfile: " << envPath << endl;
        bitfileUploaded = evalBoard->uploadFpgaBitfile(string(envPath));
    } else if (fileExists("main.bit")) {
        cout << "Uploading FPGA bitfile: main.bit" << endl;
        bitfileUploaded = evalBoard->uploadFpgaBitfile(string("main.bit"));
    } else if (fileExists("FPGA-bitfiles/ConfigRHDInterfaceBoard.bit")) {
        // Single fallback commonly used with XEM6310
        const char* fb = "FPGA-bitfiles/ConfigRHDInterfaceBoard.bit";
        cout << "Uploading FPGA bitfile: " << fb << endl;
        bitfileUploaded = evalBoard->uploadFpgaBitfile(string(fb));
    }
    
    if (!bitfileUploaded) {
        cerr << "FPGA bitfile not found or upload failed. Place ConfigRHDInterfaceBoard.bit and rerun." << endl;
        return 1;
    }

    // Initialize board.
    evalBoard->initialize();

    // Select per-channel amplifier sampling rate.
    evalBoard->setSampleRate(Rhd2000EvalBoardUsb3::SampleRate30000Hz);
    evalBoard->setCableLengthFeet(Rhd2000EvalBoardUsb3::PortA, 3.0);
    evalBoard->enableDataStream(0, true);

    // Turn on LED to indicate program is running
    int ledArray[8] = {1, 0, 0, 0, 0, 0, 0, 0};
    evalBoard->setLedDisplay(ledArray);

    // Set up RHD2000 register object
    Rhd2000RegistersUsb3 *chipRegisters = new Rhd2000RegistersUsb3(evalBoard->getSampleRate());
    
    // Configure amplifier settings
    double dspCutoffFreq = chipRegisters->setDspCutoffFreq(10.0);
    cout << "Actual DSP cutoff frequency: " << dspCutoffFreq << " Hz" << endl;
    chipRegisters->setLowerBandwidth(1.0);
    chipRegisters->setUpperBandwidth(7500.0);

    // Create command lists for auxiliary command slots
    int commandSequenceLength;
    vector<int> commandList;

    // AuxCmd1: impedance testing
    commandSequenceLength = chipRegisters->createCommandListZcheckDac(commandList, 1000.0, 128.0);
    evalBoard->uploadCommandList(commandList, Rhd2000EvalBoardUsb3::AuxCmd1, 0);
    evalBoard->selectAuxCommandLength(Rhd2000EvalBoardUsb3::AuxCmd1, 0, commandSequenceLength - 1);
    evalBoard->selectAuxCommandBank(Rhd2000EvalBoardUsb3::PortA, Rhd2000EvalBoardUsb3::AuxCmd1, 0);

    // AuxCmd2: temperature sensor
    commandSequenceLength = chipRegisters->createCommandListTempSensor(commandList);
    evalBoard->uploadCommandList(commandList, Rhd2000EvalBoardUsb3::AuxCmd2, 0);
    evalBoard->selectAuxCommandLength(Rhd2000EvalBoardUsb3::AuxCmd2, 0, commandSequenceLength - 1);
    evalBoard->selectAuxCommandBank(Rhd2000EvalBoardUsb3::PortA, Rhd2000EvalBoardUsb3::AuxCmd2, 0);

    // AuxCmd3: register configuration
    commandSequenceLength = chipRegisters->createCommandListRegisterConfig(commandList, false);
    evalBoard->uploadCommandList(commandList, Rhd2000EvalBoardUsb3::AuxCmd3, 0);
    chipRegisters->createCommandListRegisterConfig(commandList, true);
    evalBoard->uploadCommandList(commandList, Rhd2000EvalBoardUsb3::AuxCmd3, 1);
    evalBoard->selectAuxCommandLength(Rhd2000EvalBoardUsb3::AuxCmd3, 0, commandSequenceLength - 1);
    evalBoard->selectAuxCommandBank(Rhd2000EvalBoardUsb3::PortA, Rhd2000EvalBoardUsb3::AuxCmd3, 1);

    // Run calibration
    evalBoard->setMaxTimeStep(128);
    evalBoard->setContinuousRunMode(false);
    evalBoard->run();
    while (evalBoard->isRunning()) { }
    
    Rhd2000DataBlockUsb3 *calibBlock = new Rhd2000DataBlockUsb3(evalBoard->getNumEnabledDataStreams());
    evalBoard->readDataBlock(calibBlock);
    delete calibBlock;
    
    // Switch to normal operation
    evalBoard->selectAuxCommandBank(Rhd2000EvalBoardUsb3::PortA, Rhd2000EvalBoardUsb3::AuxCmd3, 0);

    // Create filename for data logging
    char timeDateBuf[80];
    time_t now = time(0);
    struct tm tstruct = *localtime(&now);
    string fileName = "test_";
    strftime(timeDateBuf, sizeof(timeDateBuf), "%y%m%d_%H%M%S", &tstruct);
    fileName += timeDateBuf;
    fileName += ".dat";
    cout << "Save filename: " << fileName << endl;

    // Open file for saving
    ofstream saveOut(fileName, ios::binary | ios::out);

    // Set up Windows shared memory for visualization
    const int streams = evalBoard->getNumEnabledDataStreams();
    const int channelsPerStream = CHANNELS_PER_STREAM;
    const int samplesPerBlock = SAMPLES_PER_DATA_BLOCK;
    size_t blocks = (size_t)streams * channelsPerStream * samplesPerBlock;
    size_t shmSize = sizeof(IntanDataHeader) + blocks * sizeof(IntanDataBlock);
    
    cout << "Setting up shared memory: streams=" << streams << " channels=" << channelsPerStream << " samples=" << samplesPerBlock << endl;
    WindowsSharedMemory sharedMem("IntanRHXData", shmSize);
    
    IntanDataHeader* header = nullptr;
    IntanDataBlock* shmOutput = nullptr;
    
    if (sharedMem.isValid()) {
        header = reinterpret_cast<IntanDataHeader*>(sharedMem.getBuffer());
        shmOutput = reinterpret_cast<IntanDataBlock*>((uint8_t*)sharedMem.getBuffer() + sizeof(IntanDataHeader));
        
        // Initialize header
        header->magic = 0x494E5441;  // "INTA"
        header->streamCount = streams;
        header->channelCount = channelsPerStream;
        header->sampleRate = (uint32_t)evalBoard->getSampleRate();
        header->dataSize = (uint32_t)shmSize;
        header->timestamp = 0;
        
        cout << "Shared memory initialized successfully (size=" << shmSize << " bytes)" << endl;
    } else {
        cout << "Warning: Shared memory initialization failed, continuing without visualization" << endl;
    }

    // Set up Python pipe for FPGA processing (restored original functionality)
    SECURITY_ATTRIBUTES s_attr {sizeof(s_attr), nullptr, TRUE};
    HANDLE childStdinRead = nullptr, parentStdinWrite = nullptr;

    if (!CreatePipe(&childStdinRead, &parentStdinWrite, &s_attr, 0)) {
        cerr << "ERROR: CreatePipe failed" << endl;
        exit(1);
    }

    SetHandleInformation(parentStdinWrite, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = childStdinRead;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi {};
    char cmd[] = "py data_transfer.py";

    cout << "Starting Python FPGA processing..." << endl;
    if (!CreateProcessA(nullptr, cmd, nullptr, nullptr, TRUE, NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi)) {
        cout << "Warning: Python FPGA processing failed to start, continuing without FPGA processing" << endl;
        CloseHandle(childStdinRead);
        CloseHandle(parentStdinWrite);
        childStdinRead = nullptr;
        parentStdinWrite = nullptr;
    } else {
        CloseHandle(childStdinRead);
        cout << "Python FPGA processing started successfully" << endl;
    }

    // Start continuous data acquisition
    queue<Rhd2000DataBlockUsb3> dataQueue;
    evalBoard->setContinuousRunMode(true);
    evalBoard->run();

    cout << "Starting data acquisition..." << endl;
    cout << "Enabled data streams: " << evalBoard->getNumEnabledDataStreams() << endl;

    int total_num_samples = 0;
    int datain_index = 0;
    uint32_t timestamp = 0;
    unsigned long frameCount = 0;
    bool usbDataRead;
    
    do {
        usbDataRead = evalBoard->readDataBlocks(1, dataQueue);

        if (usbDataRead) {
            Rhd2000DataBlockUsb3 curr_data_block = dataQueue.front();
            dataQueue.pop();
            total_num_samples++;
            // cout << "total_num_samples so far: " << total_num_samples << endl;
            // cout << "new data queue size: " << dataQueue.size() << endl;

            // cout << "data block stream 0 amplifier data: " << endl;
            // curr_data_block.print(0);
            // for (int channel = 0; channel < CHANNELS_PER_STREAM; channel++)
            // {
            //     for (int t = 0; t < SAMPLES_PER_DATA_BLOCK; t++)
            //     {
            //         cout << "amplifierDataFast of stream=0, channel=" << channel << ", t=" << t << ": "
            //             << curr_data_block.amplifierDataFast[curr_data_block.fastIndex(0, channel, t)] << endl;
            //     } // for (int t = 0; t < SAMPLES_PER_DATA_BLOCK; t++)
            // } // for (int channel = 0; channel < CHANNELS_PER_STREAM; channel++)

            // 1. Save to file (original functionality - restored from main.cpp)
            curr_data_block.write(saveOut, evalBoard->getNumEnabledDataStreams());

            // 2. Send to FPGA via Python pipe (original functionality - restored from main.cpp)
            if (parentStdinWrite) {
                DWORD bytes_written;
                char* msg_to_send = (char*) curr_data_block.amplifierDataFast;
                DWORD bytes_to_write = CHANNELS_PER_STREAM * SAMPLES_PER_DATA_BLOCK * sizeof(int);
                WriteFile(parentStdinWrite, msg_to_send, bytes_to_write, &bytes_written, nullptr);
                // cout << "Wrote " << bytes_written << " bytes to python program" << endl;
            }

            // 3. Copy to shared memory for visualization (NEW!)
            if (shmOutput) {
                size_t w = 0;
                auto computeIndex = [streams](int s, int ch, int t) { 
                    return (t * streams * CHANNELS_PER_STREAM) + (ch * streams) + s; 
                };
                
                for (int t = 0; t < samplesPerBlock; ++t) {
                    for (int s = 0; s < streams; ++s) {
                        for (int ch = 0; ch < channelsPerStream; ++ch) {
                            int code = curr_data_block.amplifierDataFast[computeIndex(s, ch, t)];
                            float uV = (float)((code - 32768) * 0.195f);  // Convert to microvolts
                            shmOutput[w++] = { (uint32_t)s, (uint32_t)ch, uV };
                        }
                    }
                }
                timestamp += samplesPerBlock;
                header->timestamp = timestamp;
                
                if (((++frameCount) % 50) == 0) {
                    cout << "SHM Published frame " << frameCount << " ts=" << timestamp << " bytes=" << (blocks * sizeof(IntanDataBlock)) << endl;
                }
            }
        }
    } while (usbDataRead || evalBoard->isRunning());

    // cout << "Total samples collected: " << total_num_samples << endl;

    // Cleanup
    evalBoard->flush();
    saveOut.close();
    
    if (parentStdinWrite) {
        CloseHandle(parentStdinWrite);
    }

    // Turn off LED
    ledArray[0] = 0;
    evalBoard->setLedDisplay(ledArray);

    cout << "Done!" << endl;
    return 0;
}
