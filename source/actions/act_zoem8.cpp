/* mbed Microcontroller Library
 * Copyright (c) 2006-2018 u-blox Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <mbed.h>
#include <eh_utilities.h> // for MTX_LOCK()/MTX_UNLOCK()
#include <eh_i2c.h>
#include <gnss.h> // For GnssParser
#include <act_position.h>
#include <act_zoem8.h>

/**************************************************************************
 * MANIFEST CONSTANTS
 *************************************************************************/

/** The default message buffer size when getting a position fix.
 */
#define DEFAULT_BUFFER_SIZE 256

/** The offset at the start of a UBX protocol message.
 */
#define UBX_PROTOCOL_HEADER_SIZE 6

/**************************************************************************
 * TYPES
 *************************************************************************/

/** Our GNSS class, just sufficient to integrate GnssParser.
 */
class XGnssParser : public GnssParser
{
public:
    /** Constructor.
     *
     * @param i2cAddress the I2C address of the GNSS chip.
     * @param rxSize     the length of RX buffer to use.
     */
    XGnssParser(char i2cAddress, int rxSize = DEFAULT_BUFFER_SIZE);

    /** Destructor.
     */
    virtual ~XGnssParser();

    /** Init.
     */
    virtual bool init(PinName pn = NC);

    /** Get a message from the GNSS chip.
     *
     * @param pBuf to put the message into.
     * @param len  the length of pBuf.
     * @return     the number of bytes retrieved.
     */
    virtual int getMessage(char *pBuf, int len);

    /** Send an NMEA message to the GNSS chip.
     *
     * @param pBuf the message payload to write.
     * @param len  size of the message payload to write.
     * @return     total bytes written.
     */
    virtual int sendNmea(const char *pBuf, int len);

    /** Send a UBX message to the chip.
     *
     * @param cls  the UBX class id.
     * @param id   the UBX message id.
     * @param pBuf the message payload to write.
     * @param len  size of the message payload to write.
     * @return     total bytes written.
     */
    virtual int sendUbx(unsigned char cls, unsigned char id,
                        const void *pBuf = NULL, int len = 0);

    /** Check that there is an ack for a UBX message.
     *
     * @param cls  the UBX class id.
     * @param id   the UBX message id.
     * @return     true if the ack is present and correct.
     */
    bool checkUbxAck(unsigned char cls, unsigned char id);

protected:
    /** Flag so that we know if we've been initialised.
     */
    bool _initialised;

    /** The I2C address of the GNSS chip.
     */
    char _i2cAddress;

    /** The receive pipe.
     */
    Pipe<char> _pipe;

    /** Write bytes to the chip.
     *
     * @param pBuf the buffer of bytes to write.
     * @param len  the number of bytes to write.
     * @return     the number of bytes written.
     */
    virtual int _send(const void *pBuf, int len);

    /** Read bytes from the chip.
     * @param pBuf the buffer to read into.
     * @param len  size of the read buffer .
     * @return     bytes read.
     */
    int _get(char *pBuf, int len);
};


/**************************************************************************
 * LOCAL VARIABLES
 *************************************************************************/

/** The instance of XGnssParser.
 */
static XGnssParser *gpGnssParser = NULL;

/** Mutex to protect the against multiple accessors.
 */
static Mutex gMtx;

/** A general purpose buffer, used to receive
 * a position fix from the GNSS module.
 */
static char gMsgBuffer[DEFAULT_BUFFER_SIZE];

/** A buffer for _pipe to use.
 */
static char gPipeBuffer[DEFAULT_BUFFER_SIZE];

/**************************************************************************
 * CLASSES
 *************************************************************************/

// Constructor.
XGnssParser::XGnssParser(char i2cAddress, int rxSize) :
        _pipe(sizeof(gPipeBuffer), gPipeBuffer)
{
    _i2cAddress = i2cAddress;
    _initialised = false;
}

// Destructor
XGnssParser::~XGnssParser()
{
    // TODO
}

// Init
bool XGnssParser::init(PinName pn)
{
    char data = 0xFF;  // REGSTREAM
    int x;
    bool gotAck = false;

    _powerOn();
    // Sometimes the device can take a little while
    // to start up, so give it a number of goes
    for (x = 0; !_initialised && (x < 5); x++) {
        // Need to wait for a while after switching on the power
        Thread::wait(500);
        _initialised = (i2cSendReceive(_i2cAddress, &data, 1, NULL, 0) == 0);
    }

    if (_initialised) {
        x = 0;
        while (!gotAck && (x < 3)) {
            // Switch on only UBX messages with the 20 byte CFG-PRT message
            // to save bandwidth (see section 32.11.23.5 of the u-blox
            // M8 receiver manual)
            memset(gMsgBuffer, 0, 20);
            gMsgBuffer[4] = _i2cAddress << 1; // The I2C address
            gMsgBuffer[12] = 0x01; // UBX protocol only
            gMsgBuffer[14] = 0x01; // UBX protocol only
            // Try this a few times as sometimes the ack
            // can be lost in NMEA messages being spewed out
            // by the GNSS module
            if (sendUbx(0x06, 0x00, gMsgBuffer, 20) > 0) {
                // This message will send an ack, check it.
                gotAck = checkUbxAck(0x06, 0x00);
            }
            x++;
        }
        _initialised = gotAck;
    }

    return _initialised;
}

// Get a message from the GNSS chip.
int XGnssParser::getMessage(char *pBuf, int len)
{
    int sz;
    int returnValue = NOT_FOUND;

    if (_initialised) {
        // Fill the pipe
        sz = _pipe.free();
        if (sz) {
            sz = _get(pBuf, sz);
        }
        if (sz) {
            _pipe.put(pBuf, sz);
        }

        // Now parse it
        returnValue = _getMessage(&_pipe, pBuf, sz);
    }

    return returnValue;
}

// Send an NMEA message to the GNSS chip.
int XGnssParser::sendNmea(const char *pBuf, int len)
{
    int sent = 0;
    char data = 0xFF; // REGSTREAM

    if (_initialised) {
        if (_send(&data, 1) == 1) {
            sent = GnssParser::sendNmea(pBuf, len);
        }

        i2cStop();
    }

    return sent;
}

// Send a UBX message to the GNSS chip.
int XGnssParser::sendUbx(unsigned char cls, unsigned char id, const void *pBuf, int len)
{
    int sent = 0;
    char data = 0xFF; // REGSTREAM

    if (_initialised) {
        if (_send(&data, 1) == 1) {
            sent = GnssParser::sendUbx(cls, id, pBuf, len);
        }

        i2cStop();
    }

    return sent;
}

// Check an ack.
bool XGnssParser::checkUbxAck(unsigned char cls, unsigned char id)
{
    bool success = false;
    int returnCode;

    returnCode = getMessage(gMsgBuffer, sizeof(gMsgBuffer));
    if (PROTOCOL(returnCode) == GnssParser::UBX) {
        returnCode = LENGTH(returnCode);
        // The ack is 10 bytes long and contains the message
        // class and message ID of the original message,
        // see section 32.9 of the u-blox M8 receiver manual
        // Ack is  0xb5-62-05-00-02-00-cls-id-crcA-crcB
        // Nack is 0xb5-62-05-01-02-00-cls-id-crcA-crcB
        if ((returnCode == 10) && (gMsgBuffer[3] == 0x01) &&
            (gMsgBuffer[4] == 0x02) && (gMsgBuffer[5] == 0x00) &&
            (gMsgBuffer[6] == cls) && (gMsgBuffer[7] == id) && (gMsgBuffer[2] == 0x05)) {
            success = true;
        }
    }

    return success;
}

// Fetch up to len characters into pBuf
int XGnssParser::_get(char *pBuf, int len)
{
    int read = 0;
    int size = 0;
    char data[3];
    Timer timer;

    if (_initialised) {
        timer.reset();
        timer.start();
        data[0] = 0xFD; // REGLEN
        while ((size == 0) && (timer.read_ms() < ZOEM8_GET_WAIT_TIME_MS)) {
            if (i2cSendReceive(_i2cAddress, data, 1, &(data[1]), 2) == 2) {
                size = (((unsigned int) data[1]) << 8) + data[2];
                if (size > len) {
                    size = len;
                }
                if (size > 0) {
                    data[0] = 0xFF; // REGSTREAM
                    if (i2cSendReceive(_i2cAddress, data, 1, pBuf, size) == size) {
                        read = size;
                    }
                }
            }
            Thread::wait(100); // Relax a little
        }
        timer.stop();
    }

    return read;
}

// Send.
int XGnssParser::_send(const void *pBuf, int len)
{
    int returnValue = 0;

    if (_initialised) {
        returnValue = (i2cSend(_i2cAddress, (const char *) pBuf, len, true) == 0) ? len : 0;
    }

    return returnValue;
}

/**************************************************************************
 * STATIC FUNCTIONS
 *************************************************************************/

/// Return an unsigned int from a pointer to a little-endian unsigned int
// in memory.
static unsigned int littleEndianUint(char *pByte)
{
    unsigned int retValue;

    retValue = *pByte;
    retValue += ((unsigned int) *(pByte + 1)) << 8;
    retValue += ((unsigned int) *(pByte + 2)) << 16;
    retValue += ((unsigned int) *(pByte + 3)) << 24;

    return  retValue;
}

/**************************************************************************
 * PUBLIC FUNCTIONS
 *************************************************************************/

// Initialise the Zoe M8 GNSS chip.
// TODO proper configuration for lower power mode.
ActionDriver zoem8Init(char i2cAddress)
{
    ActionDriver result;

    MTX_LOCK(gMtx);

    result = ACTION_DRIVER_OK;

    // Instantiate GnssParser and the pipe it uses
    if (gpGnssParser == NULL) {
        gpGnssParser = new XGnssParser(i2cAddress);
        if (gpGnssParser != NULL) {
            if (!gpGnssParser->init()) {
                result = ACTION_DRIVER_ERROR_DEVICE_NOT_PRESENT;
                delete gpGnssParser;
                gpGnssParser = NULL;
            }
        } else {
            result = ACTION_DRIVER_ERROR_OUT_OF_MEMORY;
        }
    }

    MTX_UNLOCK(gMtx);

    return result;
}

// Shut-down the Zoe M8 GNSS chip.
void zoem8Deinit()
{
    MTX_LOCK(gMtx);

    if (gpGnssParser != NULL) {
        delete gpGnssParser;
        gpGnssParser = NULL;
    }

    MTX_UNLOCK(gMtx);
}

// Read the position
ActionDriver getPosition(int *pLatitudeX10e7, int *pLongitudeX10e7,
                         int *pRadiusMetres, int *pAltitudeMetres,
                         unsigned char *pSpeedMPS, unsigned char *pSVs)
{
    ActionDriver result;
    int returnCode;

    MTX_LOCK(gMtx);

    result = ACTION_DRIVER_ERROR_NOT_INITIALISED;

    if (gpGnssParser != NULL) {
        // See ublox8-M8_ReceiverDescrProtSpec, section 32.18.14 (NAV-PVT)
        result = ACTION_DRIVER_ERROR_I2C_WRITE;
        if (gpGnssParser->sendUbx(0x01, 0x07, NULL, 0) > 0) {
            result = ACTION_DRIVER_ERROR_NO_DATA;
            returnCode = gpGnssParser->getMessage(gMsgBuffer, sizeof(gMsgBuffer));
            if (PROTOCOL(returnCode) == GnssParser::UBX) {
                returnCode = LENGTH(returnCode);
                if (returnCode > 0) {
                    result = ACTION_DRIVER_ERROR_NO_VALID_DATA;
                    // Have we got a fix?
                    if ((gMsgBuffer[21 + UBX_PROTOCOL_HEADER_SIZE] & 0x01) != 0) {
                        result = ACTION_DRIVER_OK;
                        if (pSVs != NULL) {
                            *pSVs = (unsigned char) gMsgBuffer[23 + UBX_PROTOCOL_HEADER_SIZE];
                        }
                        if (pLongitudeX10e7 != NULL) {
                            *pLongitudeX10e7 = (int) littleEndianUint(&(gMsgBuffer[24 + UBX_PROTOCOL_HEADER_SIZE]));
                        }
                        if (pLatitudeX10e7 != NULL) {
                            *pLatitudeX10e7 = (int) littleEndianUint(&(gMsgBuffer[28 + UBX_PROTOCOL_HEADER_SIZE]));
                        }
                        if (pAltitudeMetres != NULL) {
                            *pAltitudeMetres = ((int) littleEndianUint(&(gMsgBuffer[36 + UBX_PROTOCOL_HEADER_SIZE]))) / 1000;
                        }
                        if (pRadiusMetres != NULL) {
                            *pRadiusMetres = ((int) littleEndianUint(&(gMsgBuffer[40 + UBX_PROTOCOL_HEADER_SIZE]))) / 1000;
                        }
                        if (pSpeedMPS != NULL) {
                            *pSpeedMPS = ((int) littleEndianUint(&(gMsgBuffer[60 + UBX_PROTOCOL_HEADER_SIZE]))) / 1000;
                        }
                    }
                }
            }
        }
    }

    MTX_UNLOCK(gMtx);

    return result;
}

// Read the time
ActionDriver getTime(time_t *pTimeUTC)
{
    ActionDriver result;
    time_t timeUTC = 0;
    unsigned int months;
    unsigned int year;
    int returnCode;

    MTX_LOCK(gMtx);

    result = ACTION_DRIVER_ERROR_NOT_INITIALISED;

    if (gpGnssParser != NULL) {
        // See ublox8-M8_ReceiverDescrProtSpec, section 32.18.28 (NAV-TIMEUTC)
        result = ACTION_DRIVER_ERROR_I2C_WRITE;
        if (gpGnssParser->sendUbx(0x01, 0x21, NULL, 0) > 0) {
            result = ACTION_DRIVER_ERROR_NO_DATA;
            returnCode = gpGnssParser->getMessage(gMsgBuffer, sizeof(gMsgBuffer));
            if (PROTOCOL(returnCode) == GnssParser::UBX) {
                returnCode = LENGTH(returnCode);
                if (returnCode > 0) {
                    result = ACTION_DRIVER_ERROR_NO_VALID_DATA;
                    // Have we got valid UTC time?
                    if ((gMsgBuffer[19 + UBX_PROTOCOL_HEADER_SIZE] & 0x04) != 0) {
                        result = ACTION_DRIVER_OK;
                        // Year 1999-2099, so need to adjust to get year since 1970
                        year = ((unsigned int) (gMsgBuffer[12 + UBX_PROTOCOL_HEADER_SIZE])) +
                                ((unsigned int) (gMsgBuffer[13 + UBX_PROTOCOL_HEADER_SIZE]) << 8) - 1999 + 29;
                        // Month (1 to 12), so take away 1 to make it zero-based
                        months = gMsgBuffer[14 + UBX_PROTOCOL_HEADER_SIZE] - 1;
                        months += year * 12;
                        // Work out the number of seconds due to the year/month count
                        for (unsigned int x = 0; x < months; x++) {
                            if (isLeapYear((x / 12) + 1970)) {
                                timeUTC += gDaysInMonthLeapYear[x % 12] * 3600 * 24;
                            } else {
                                timeUTC += gDaysInMonth[x % 12] * 3600 * 24;
                            }
                        }
                        // Day (1 to 31)
                        timeUTC += ((unsigned int) gMsgBuffer[15 + UBX_PROTOCOL_HEADER_SIZE] - 1) * 3600 * 24;
                        // Hour (0 to 23)
                        timeUTC += ((unsigned int) gMsgBuffer[16 + UBX_PROTOCOL_HEADER_SIZE]) * 3600;
                        // Minute (0 to 59)
                        timeUTC += ((unsigned int) gMsgBuffer[17 + UBX_PROTOCOL_HEADER_SIZE]) * 60;
                        // Second (0 to 60)
                        timeUTC += gMsgBuffer[18 + UBX_PROTOCOL_HEADER_SIZE];

                        if (pTimeUTC != NULL) {
                            *pTimeUTC = timeUTC;
                        }
                    }
                }
            }
        }
    }

    MTX_UNLOCK(gMtx);

    return result;
}
// End of file
