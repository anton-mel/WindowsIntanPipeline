//----------------------------------------------------------------------------------
// main.cpp
//
// Intan Technoloies RHD2000 Rhythm Interface API
// Version 1.2 (23 September 2013)
//
// Copyright (c) 2013 Intan Technologies LLC
//
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors be held liable for any damages arising from the
// use of this software.
//
// Permission is granted to anyone to use this software for any applications that
// use Intan Technologies integrated circuits, and to alter it and redistribute it
// freely.
//
// See http://www.intantech.com for documentation and product information.
//----------------------------------------------------------------------------------

// #include <QtCore> // used for Qt applications
#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <time.h>
#include <stdio.h>
#include <windows.h>

using namespace std;

#include "rhd2000evalboardusb3.h"
#include "rhd2000registersusb3.h"
#include "rhd2000datablockusb3.h"
#include "okFrontPanelDLL.h"

#define NUM_TIMESTEPS 1000

int main(int argc, char* argv[])
{
    // QCoreApplication a(argc, argv); // used for Qt console applications

    Rhd2000EvalBoardUsb3* evalBoard = new Rhd2000EvalBoardUsb3;

    // Open Opal Kelly XEM6310 board.
    evalBoard->open();

    // Load Rhythm FPGA configuration bitfile (provided by Intan Technologies).
    string bitfilename;
    bitfilename = "main.bit";  // Place main.bit in the executable directory, or add a complete path to file.
    evalBoard->uploadFpgaBitfile(bitfilename);

    // Initialize board.
    evalBoard->initialize();

    // Select per-channel amplifier sampling rate.
    evalBoard->setSampleRate(Rhd2000EvalBoardUsb3::SampleRate20000Hz);

    // Now that we have set our sampling rate, we can set the MISO sampling delay
    // which is dependent on the sample rate.  We assume a 3-foot cable.
    evalBoard->setCableLengthFeet(Rhd2000EvalBoardUsb3::PortA, 3.0);

    // Let's turn one LED on to indicate that the program is running.
    int ledArray[8] = {1, 0, 0, 0, 0, 0, 0, 0};
    evalBoard->setLedDisplay(ledArray);

    // Set up an RHD2000 register object using this sample rate to optimize MUX-related
    // register settings.
    Rhd2000RegistersUsb3 *chipRegisters;
    chipRegisters = new Rhd2000RegistersUsb3(evalBoard->getSampleRate());

    // Create command lists to be uploaded to auxiliary command slots.
    int commandSequenceLength;
    vector<int> commandList;

    // First, let's create a command list for the AuxCmd1 slot.  This command
    // sequence will create a 1 kHz, full-scale sine wave for impedance testing.
    commandSequenceLength = chipRegisters->createCommandListZcheckDac(commandList, 1000.0, 128.0); // 1000.0, 128.0
    evalBoard->uploadCommandList(commandList, Rhd2000EvalBoardUsb3::AuxCmd1, 0);
    evalBoard->selectAuxCommandLength(Rhd2000EvalBoardUsb3::AuxCmd1, 0, commandSequenceLength - 1);
    evalBoard->selectAuxCommandBank(Rhd2000EvalBoardUsb3::PortA, Rhd2000EvalBoardUsb3::AuxCmd1, 0);
    // evalBoard->printCommandList(commandList); // optionally, print command list

    // Next, we'll create a command list for the AuxCmd2 slot.  This command sequence
    // will sample the temperature sensor and other auxiliary ADC inputs.
    commandSequenceLength = chipRegisters->createCommandListTempSensor(commandList);
    evalBoard->uploadCommandList(commandList, Rhd2000EvalBoardUsb3::AuxCmd2, 0);
    evalBoard->selectAuxCommandLength(Rhd2000EvalBoardUsb3::AuxCmd2, 0, commandSequenceLength - 1);
    evalBoard->selectAuxCommandBank(Rhd2000EvalBoardUsb3::PortA, Rhd2000EvalBoardUsb3::AuxCmd2, 0);
    // evalBoard->printCommandList(commandList); // optionally, print command list

    // For the AuxCmd3 slot, we will create two command sequences.  Both sequences
    // will configure and read back the RHD2000 chip registers, but one sequence will
    // also run ADC calibration.

    // Before generating register configuration command sequences, set amplifier
    // bandwidth paramters.

    double dspCutoffFreq;
    dspCutoffFreq = chipRegisters->setDspCutoffFreq(10.0);
    cout << "Actual DSP cutoff frequency: " << dspCutoffFreq << " Hz" << endl;

    chipRegisters->setLowerBandwidth(1.0);
    chipRegisters->setUpperBandwidth(7500.0);

    commandSequenceLength = chipRegisters->createCommandListRegisterConfig(commandList, false);
    // Upload version with no ADC calibration to AuxCmd3 RAM Bank 0.
    evalBoard->uploadCommandList(commandList, Rhd2000EvalBoardUsb3::AuxCmd3, 0);

    chipRegisters->createCommandListRegisterConfig(commandList, true);
    // Upload version with ADC calibration to AuxCmd3 RAM Bank 1.
    evalBoard->uploadCommandList(commandList, Rhd2000EvalBoardUsb3::AuxCmd3, 1);

    evalBoard->selectAuxCommandLength(Rhd2000EvalBoardUsb3::AuxCmd3, 0, commandSequenceLength - 1);
    // Select RAM Bank 1 for AuxCmd3 initially, so the ADC is calibrated.
    evalBoard->selectAuxCommandBank(Rhd2000EvalBoardUsb3::PortA, Rhd2000EvalBoardUsb3::AuxCmd3, 1);
    // evalBoard->printCommandList(commandList); // optionally, print command list

    // Since our longest command sequence is 128 commands, let's just run the SPI
    // interface for 128 samples.
    evalBoard->setMaxTimeStep(128);
    evalBoard->setContinuousRunMode(false);

    cout << "Number of 16-bit words in FIFO: " << evalBoard->getNumWordsInFifo() << endl;

    // Start SPI interface.
    evalBoard->run();

    // Wait for the 128-sample run to complete.
    while (evalBoard->isRunning()) { }

    cout << "Number of 16-bit words in FIFO: " << evalBoard->getNumWordsInFifo() << endl;

    // Read the resulting single data block from the USB interface.
    Rhd2000DataBlockUsb3 *dataBlock = new Rhd2000DataBlockUsb3(evalBoard->getNumEnabledDataStreams());
    evalBoard->readDataBlock(dataBlock);

    // Display register contents from data stream 0.
    dataBlock->print(0);

    cout << "Number of 16-bit words in FIFO: " << evalBoard->getNumWordsInFifo() << endl;

    // Now that ADC calibration has been performed, we switch to the command sequence
    // that does not execute ADC calibration.
    evalBoard->selectAuxCommandBank(Rhd2000EvalBoardUsb3::PortA, Rhd2000EvalBoardUsb3::AuxCmd3, 0);


    // Grab current time and date for inclusion in filename
    char timeDateBuf[80];
    time_t now = time(0);
    struct tm tstruct;
    tstruct = *localtime(&now);

    // Construct filename
    string fileName;
    fileName = "C:\\Users\\rkt23\\Downloads\\";  // add your desired path here
    fileName += "test_";
    strftime(timeDateBuf, sizeof(timeDateBuf), "%y%m%d", &tstruct);
    fileName += timeDateBuf;
    fileName += "_";
    strftime(timeDateBuf, sizeof(timeDateBuf), "%H%M%S", &tstruct);
    fileName += timeDateBuf;
    fileName += ".dat";

    cout << endl << "Save filename:" << endl << "  " << fileName << endl << endl;

    // Let's save one second of data to a binary file on disk.
    ofstream saveOut;
    saveOut.open(fileName, ios::binary | ios::out);

    queue<Rhd2000DataBlockUsb3> dataQueue;

    // Run for specified number of timesteps.
    evalBoard->setMaxTimeStep(NUM_TIMESTEPS);
    cout << "Reading " << NUM_TIMESTEPS << " timesteps of RHD2000 data..." << endl;
    evalBoard->run();

    // Run continuously
    // evalBoard->setContinuousRunMode(true);
    // cout << "Reading data continuously" << endl;
    // evalBoard->run();

    cout << "number of enabled data streams: " << evalBoard->getNumEnabledDataStreams() << endl;


    // create pipe to pipe data from Intan to python program which sends said data to the FPGA board
    SECURITY_ATTRIBUTES s_attr {sizeof(s_attr), nullptr, TRUE};
    HANDLE childStdinRead = nullptr, parentStdinWrite = nullptr;

    if (!CreatePipe(&childStdinRead, &parentStdinWrite, &s_attr, 0))
    {
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
    char cmd[] = "python data_transfer.py";

    if (!CreateProcessA(nullptr, cmd, nullptr, nullptr, TRUE, NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi))
    {
        cerr << "ERROR: CreateProcessA failed" << endl;
        exit(1);
    }

    CloseHandle(childStdinRead);


    int total_num_samples = 0;
    int datain_index = 0;
    bool usbDataRead;
    do {
        usbDataRead = evalBoard->readDataBlocks(1, dataQueue);

        if (usbDataRead)
        {
            Rhd2000DataBlockUsb3 curr_data_block = dataQueue.front();
            dataQueue.pop();
            total_num_samples++;
            cout << "total_num_samples so far: " << total_num_samples << endl;
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

            cout << endl;

            // pipe data of channel 0 to python program
            DWORD bytes_written;
            char* msg_to_send = (char*) curr_data_block.amplifierDataFast;
            DWORD bytes_to_write = CHANNELS_PER_STREAM * SAMPLES_PER_DATA_BLOCK * sizeof(int);
            WriteFile(parentStdinWrite, msg_to_send, bytes_to_write, &bytes_written, nullptr);
            cout << "Wrote " << bytes_written << " bytes to python program" << endl;
        } // if (usbDataRead)
    } while (usbDataRead || evalBoard->isRunning());

    // total_num_samples += dataQueue.size();
    cout << "total number of samples collected: " << total_num_samples << endl;

    evalBoard->flush();

    saveOut.close();

    cout << "Done!" << endl << endl;

    // Turn off LED.
    ledArray[0] = 0;
    evalBoard->setLedDisplay(ledArray);

    // return a.exec();  // used for Qt applications
} // int main(int argc, char* argv[])
