// Lin_Interface.h
//
// Provides a Hardware LIN Interface
// inherited from the "HardwareSerial"
//
// * Frame sending [Break, Sync, PID, Data, Checksum]
// * Frame request [Break, Sync, PID] and wait for data from slave + verify checksum
//
// * WriteBreak for initiation of a Lin-Frame
// * Convert FID to PID
// * Calculation of Checksum (to be send, or to be verified)

#pragma once

#include <Arduino.h>

class Lin_Interface : protected HardwareSerial
{
public:
    //inherit constructor from HardwareSerial (Parameter: int uart_nr)
    using HardwareSerial::HardwareSerial;

    int verboseMode = -1;
    unsigned long baud = 19200;
    int8_t rxPin = -1;
    int8_t txPin = -1;

    // 8 Data Bytes + ChkSum + some space for receiving complete frames
    uint8_t LinMessage[8 + 1 + 4] = {0};
    uint8_t LinMessageID = 0;
    uint8_t LinMessageSize = 0;

    // set up Serial communication for receiving data.
    void setupSerial(void);
    bool listenBus(void);
    bool readFrame(uint8_t FrameID);

    void writeFrame(uint8_t FrameID, uint8_t datalen);
    void writeFrameClassic(uint8_t FrameID, uint8_t datalen);

protected:
    uint32_t m_bitCycles;
    void startTransmission(uint8_t ProtectedID);
    size_t writeBreak();
    uint8_t getProtectedID(uint8_t FrameID);
    uint8_t getChecksum(uint8_t ProtectedID, uint8_t datalen);
};
