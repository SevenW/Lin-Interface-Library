// Lin-Interface.cpp
//
// Utilizes a UART to provide a Lin-Interface
// This class is inherited from the "HardwareSerial" call
//
// Copyright mestrode <ardlib@mestro.de>
// Original Source: "https://github.com/mestrode/Lin-Interface-Library"

#include "Lin_Interface.hpp"

#include <Arduino.h>

void Lin_Interface::setupSerial(void)
{
    Serial.printf("Serial settings to be applied: %d, %d, %d, %d\n", baud, SERIAL_8N1, rxPin, txPin);
    // start UART
    if (rxPin < 0 && txPin < 0)
    {
        // no custom pins are defined
        HardwareSerial::begin(baud, SERIAL_8N1);
    }
    else
    {
        HardwareSerial::begin(baud, SERIAL_8N1, rxPin, txPin);
        Serial.printf("Serial settings: %d, %d, %d, %d\n", baud, SERIAL_8N1, rxPin, txPin);
    }
    delay(20);
}

/// @brief listen to data on the linbus
/// @details Start frame and read answer from bus device
/// Listen until a frame or unanswered master request is recieved
/// The received data will be passed to the Lin_Interface::LinMessage[] array
/// Receives as much as possible, but maximum 8 data byte + checksum
/// Verify Checksum according to LIN 2.0 rules
/// @returns verification of checksum was succesful
bool Lin_Interface::listenBus()
{
    unsigned long timeout = 500; // ms
    unsigned long timeoutAt = millis() + timeout;
    uint8_t ProtectedID = 0x00;
    bool ChecksumValid = false;
    // Lin_Interface::setupSerial();
    HardwareSerial::flush();

    // wait for available data
    bool timedOut = false;
    while (!HardwareSerial::available() && !timedOut)
    {
        timedOut = (timeoutAt < millis());
    }

    // Break, Sync and ProtectedID will be received --> discard them
    int bytes_received = -4;
    while (HardwareSerial::available())
    {
        if (bytes_received >= (8 + 1)) // max 8x Data + 1x Checksum
        {
            // receive max 9 Bytes: 8 Data + 1 Chksum
            break;
        }
        switch (bytes_received)
        {
        case -4: //??
        case -3: // break = 0x00
        case -2: // sync = 0x55
        case -1: // Protected ID
        {
            // discard Break and Sync, detect PID
            uint8_t buffer = HardwareSerial::read();
            // Serial.printf("-%02X", buffer);
            //  Sync and PID may to be verified here
            if (buffer == 0x00)
            { // break
                bytes_received = -3;
            }
            else if ((buffer == 0x55) && (bytes_received == -3))
            { // sync
                bytes_received = -2;
            }
            else if ((buffer != 0x00) && (bytes_received == -3))
            {
                bytes_received = -4;
            }
            else if (bytes_received == -2)
            { // PID
                ProtectedID = buffer;
                bytes_received = 0;
            }
            else
            {
                bytes_received = -4;
            }
            break;
        }
        default: // Data 0...7, Checksum
            // Receive and save only Data Byte (send by slave)
            LinMessage[bytes_received] = HardwareSerial::read();
            // Serial.printf("*%02X", LinMessage[bytes_received]);
            bytes_received++;
        }
        if (!HardwareSerial::available())
            delay(2);
    }

#ifdef LINSIMULATION
    if (timedOut)
    {
        // crete simulated frame
        uint8_t buf[12] = {0xAB, 0x84, 0x1E, 0xF4, 0x2E, 0x84, 0x7A, 0x55, 0x00, 0x00, 0x00, 0x00};
        uint8_t Id = 0x22;
        memcpy(LinMessage, buf, 12);
        LinMessageID = Id;
        LinMessageSize = 9;
        bytes_received = 9;
        ProtectedID = Id;
        LinMessage[8] = getChecksum(Id, bytes_received - 1);
        timedOut = false;
    }
#endif

    // erase data in buffer, in case a 9th or 10th Byte was received
    // HardwareSerial::flush();
    while (HardwareSerial::available())
    {
        HardwareSerial::read();
        // Serial.printf(",%02X", HardwareSerial::read());
        if (verboseMode > 0)
        {
            // Serial.print("additional byte discarded\n");
            // Serial.print(".");
        }
    }

    if (bytes_received > 0)
    {
        // verify Checksum
        uint8_t Checksum = LinMessage[bytes_received - 1];
        // bytes_received--;
        ChecksumValid = (0xFF == (uint8_t)(Checksum + ~getChecksum(ProtectedID, bytes_received - 1)));
        LinMessageSize = bytes_received;
        LinMessageID = ProtectedID & 0x3F;
    }
    else
    {
        LinMessageSize = 0;
        LinMessageID = ProtectedID & 0x3F;
    }

    // HardwareSerial::end();

    if (verboseMode > 0)
    {
        if (timedOut)
        {
            Serial.printf("listenBus timed out\n");
        }
        else if (bytes_received < 0)
        {
            Serial.printf("no valid 0x00 0x55 PID header detected\n");
        }
        else
        {
            Serial.printf("00 55 %02X (%02X), ", ProtectedID & 0x3F, ProtectedID);
            for (int i = 0; i < 8; ++i)
            {
                if (i > bytes_received)
                    break;
                Serial.printf("%02X ", LinMessage[i]);
            }
            if (bytes_received > 1)
            {
                Serial.printf("|%02X ", LinMessage[bytes_received - 1]);
                if (!ChecksumValid)
                {
                    Serial.printf("Checksum failed");
                }
                Serial.printf("\n");
            }
            else
            {
                Serial.printf("no repsonse\n");
            }
        }
    }
    return ChecksumValid;
} // bool listenBus()

/// @brief reads data from a lin device by requesting a specific FrameID
/// @details Start frame and read answer from bus device
/// The received data will be passed to the Lin_Interface::LinMessage[] array
/// Receives as much as possible, but maximum 8 data byte + checksum
/// Verify Checksum according to LIN 2.0 rules
/// @param FrameID ID of frame (will be converted to protected ID)
/// @returns verification of checksum was succesful
bool Lin_Interface::readFrame(uint8_t FrameID)
{
    uint8_t ProtectedID = getProtectedID(FrameID);
    startTransmission(ProtectedID);
    HardwareSerial::flush();
    bool ChecksumValid = listenBus();
    HardwareSerial::end();
    delay(20);
    return ChecksumValid;
} // bool readFrame()

/// @brief write a complete LIN2.0 frame without request of data to the lin-bus
/// @details write LIN Frame (Break, Synk, PID, Data, Checksum) to the Bus, and hope somebody will read this
/// Checksum Calculations regarding LIN 2.0
/// The data of this frame is 'dataLen' long and incuded in the Lin_Interface::LinMessage[] array
/// @param FrameID ID of frame (will be converted to protected ID)
/// @param dataLen count of data within the LinMessage array (containing only the data) should be transmitted
void Lin_Interface::writeFrame(uint8_t FrameID, uint8_t dataLen)
{
    uint8_t ProtectedID = getProtectedID(FrameID);
    uint8_t cksum = getChecksum(ProtectedID, dataLen);

    startTransmission(ProtectedID);

    for (int i = 0; i < dataLen; ++i)
    {
        HardwareSerial::write(LinMessage[i]); // Message (array from 1..8)
    }
    HardwareSerial::write(cksum);

    // wait for available data
    delay(10);

    /// TODO: read back of the break needs to be verified
    verboseMode = 1;

    // Read Break and discard
    if (HardwareSerial::available())
    {
        HardwareSerial::read();
    }
    // Read Sync
    uint8_t RX_Sync = 0x00;
    if (HardwareSerial::available())
    {
        RX_Sync = HardwareSerial::read();
    }
    // Read PID
    uint8_t RX_ProtectedID = 0x00;
    if (HardwareSerial::available())
    {
        RX_ProtectedID = HardwareSerial::read();
    }
    // read DATA + CHKSUM
    bool moreData = false;
    int bytes_received = 0;
    while (HardwareSerial::available())
    {
        if (bytes_received >= 8 + 1 + 4)
        {
            // receive max 9 Bytes = 8 Data + 1 Chksum
            moreData = true;
            break;
        }
        // Receive Byte from Bus (Slave)
        LinMessage[bytes_received] = HardwareSerial::read();
        bytes_received++;
    }
    uint8_t Checksum_received = LinMessage[bytes_received - 1];
    bytes_received--;

    // erase data in buffer, in case a 9th or 10th Byte was received
    HardwareSerial::flush();
    HardwareSerial::end();

    // use received PID  for verification
    uint8_t ChkSumCalc = getChecksum(RX_ProtectedID, bytes_received);

    if (verboseMode > 0)
    {
        Serial.printf(" <<<<<<--- FID %02Xh (%02X)   = %02X|%02X|", FrameID, ProtectedID, RX_Sync, RX_ProtectedID);
        for (int i = 0; i < 8 + 1 + 4; ++i)
        {
            if (i >= bytes_received)
                break;
            Serial.printf("%02X ", LinMessage[i]);
        }

        Serial.printf("\b|%02X", Checksum_received);
        if (Checksum_received != ChkSumCalc)
        {
            Serial.printf("\b != ChkSum calc %02Xh| TX %02Xh ", ChkSumCalc, cksum);
        }

        if (moreData)
        {
            Serial.print("more Bytes available");
        }

        Serial.println();
    }
} // void writeFrame()

/// TODO: function needs to be verified
/// send Frame (Break, Synk, PID, Data, Classic-Checksum) to the Bus
/// Checksum Calculations regarding LIN 1.x
void Lin_Interface::writeFrameClassic(uint8_t FrameID, uint8_t dataLen)
{
    uint8_t ProtectedID = getProtectedID(FrameID);
    uint8_t cksum = getChecksum(0x00, dataLen);

    startTransmission(ProtectedID);

    for (int i = 0; i < dataLen; ++i)
    {
        HardwareSerial::write(LinMessage[i]); // Message (array from 1..8)
    }
    HardwareSerial::write(cksum);
    HardwareSerial::flush();

    /// TODO: verification of written data (see Lin_Interface::writeFrame)

    HardwareSerial::end();
} // void Lin_Interface::writeFrameClassic

/// Introduce Frame (Start UART, Break, Sync, PID)
void Lin_Interface::startTransmission(uint8_t ProtectedID)
{
    // start UART
    // begin serial is required after operating the TJA1020 mode ????
    setupSerial();
    writeBreak();                       // initiate Frame with a Break
    HardwareSerial::write(0x55);        // Sync
    HardwareSerial::write(ProtectedID); // PID
}

/// Send a Break for introduction of a Frame
/// This is done by sending a Byte (0x00) + Stop Bit by using half baud rate
/// @returns if the 0x00 has been send
size_t Lin_Interface::writeBreak()
{
    HardwareSerial::flush();
    // configure to half baudrate --> a t_bit will be doubled
    HardwareSerial::updateBaudRate(baud >> 1);
    // write 0x00, including Stop-Bit (=1),
    // qualifies when writing in slow motion like a Break in normal speed
    size_t ret = write(uint8_t(0x00));
    // ensure this is send
    HardwareSerial::flush();
    // restore normal speed
    HardwareSerial::updateBaudRate(baud);
    return ret;
}

/// get Protected ID by calculating parity bits and combine with Frame ID
/// @param FrameID to be converted
/// @return Protected ID
uint8_t Lin_Interface::getProtectedID(uint8_t FrameID)
{
    // calc Parity Bit 0
    uint8_t p0 = bitRead(FrameID, 0) ^ bitRead(FrameID, 1) ^ bitRead(FrameID, 2) ^ bitRead(FrameID, 4);
    // calc Parity Bit 1
    uint8_t p1 = ~(bitRead(FrameID, 1) ^ bitRead(FrameID, 3) ^ bitRead(FrameID, 4) ^ bitRead(FrameID, 5));
    // combine bits to protected ID
    // 0..5 id is limited between 0x00..0x3F
    // 6    parity bit 0
    // 7    parity bit 1
    return ((p1 << 7) | (p0 << 6) | (FrameID & 0x3F));
}

/// @brief Checksum calculation for LIN Frame
/// @details
/// EnhancedChecksum considers ProtectedID
///     LIN 2.0 only for FrameID between 0x00..0x3B
///     LIN 2.0 uses for 0x3C and above ClassicChecksum for legacy (auto detected)
/// ClassicChecksum
///     LIN 1.x in general (use 'ProtectedID' = 0x00 to ensure that)
/// see LIN Specification 2.2A (2021-12-31) for details
///     https://microchipdeveloper.com/local--files/lin:specification/LIN-Spec_2.2_Rev_A.PDF
///     2.8.3 Example of Checksum Calculation
/// @param ProtectedID initial Byte, set to 0x00, when calc Checksum for classic LIN Frame
/// @param dataLen length of Frame (only Data Bytes)
/// @returns calculated checksum
uint8_t Lin_Interface::getChecksum(uint8_t ProtectedID, uint8_t dataLen)
{
    uint16_t sum = ProtectedID;
    // test FrameID bits for classicChecksum
    if ((sum & 0x3F) >= 0x3C)
    {
        // LIN 1.x: legacy
        // LIN 2.0: don't include PID for ChkSum calculation on configuration and reserved frames
        sum = 0x00;
    }
    // sum up all bytes (including carryover to the high byte)
    // ID allready considered
    while (dataLen-- > 0)
        sum += LinMessage[dataLen];
    // add high byte (carry over) to the low byte
    while (sum >> 8)
        sum = (sum & 0xFF) + (sum >> 8);
    // inverting result
    return (~sum);
}
