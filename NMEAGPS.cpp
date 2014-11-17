/**
 * @file NMEAGPS.cpp
 * @version 2.0
 *
 * @section License
 * Copyright (C) 2014, Thomas Lohmueller
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include "NMEAGPS.h"

#include "Cosa/IOStream/Driver/UART.hh"

#ifndef CR
#define CR (13)
#endif
#ifndef LF
#define LF (10)
#endif

/**
 * parseHEX(char a)
 * Parses a single character as HEX and returns byte value.
 */
inline static uint8_t parseHEX(char a) {
    a |= 0x20; // make it lowercase
    if (('a' <= a) && (a <= 'f'))
        return a - 'a' + 10;
    else
        return a - '0';
}


void NMEAGPS::rxBegin()
{
    crc = 0;
    nmeaMessage = NMEA_UNKNOWN;
    rxState = NMEA_RECEIVING_DATA;
    fieldIndex = 0;
    chrCount = 0;
}


void NMEAGPS::rxEnd( bool ok )
{
  rxState = NMEA_IDLE; // just in case Event::Handler cares about rxState

  if (ok) {
    // mark specific received data as valid   
    switch (nmeaMessage) {
      case NMEA_RMC:
        if (m_fix.status == GPS_FIX_NONE)
          ok = false;
        else {
          m_fix.valid.location = true;
          m_fix.valid.dateTime = true;
          m_fix.valid.speed    = true; 
          m_fix.valid.heading  = true;
        }
        break;

      case NMEA_GGA:
        if (m_fix.status == GPS_FIX_NONE)
          ok = false;
        else {
          m_fix.valid.altitude = true;
          m_fix.valid.location = true;
          m_fix.valid.dateTime = true;
        }
        break;

      case NMEA_GLL:
        if (m_fix.status == GPS_FIX_NONE)
          ok = false;
        else {
          m_fix.valid.location = true;
          m_fix.valid.dateTime = true;
        }
        break;

      case NMEA_VTG:
        if (m_fix.status == GPS_FIX_NONE)
          ok = false;
        else {
          m_fix.valid.speed    = true; 
          m_fix.valid.heading  = true;
        }
        break;

      case NMEA_ZDA:
        if (!(*const_cast<const gps_fix_t *>(&m_fix)).dateTime.is_valid())
          ok = false;
        else
          m_fix.valid.dateTime = true;
        break;
      default:
        break;
    }
  }

#ifdef NEOGPS_STATS
    statistics.parser_ok++;
#endif

  if (ok)
    Event::push( Event::RECEIVE_COMPLETED_TYPE, this, nmeaMessage );
}


int NMEAGPS::putchar( char c )
{
  if (c == '$') {  // Always restarts
    rxBegin();

  } else {
    switch (rxState) {
      case NMEA_IDLE:
          break;

          // Wait until complete line is received
      case NMEA_RECEIVING_DATA:
          if (c == '*') {   // Line finished, CRC follows
              rxState = NMEA_RECEIVING_CRC1;

          } else if ((c == CR) || (c == LF)) { // Line finished, no CRC
              rxEnd( true );

          } else if ((c < ' ') || ('~' < c)) { // Invalid char
              rxEnd( false );

          } else {            // normal data character
              crc ^= c;
              parseField(c);
              if (c == ',') { // a comma marks the next field
                  fieldIndex++;
                  chrCount = 0;
              } else {
                  chrCount++;
              }
          }
          break;
          
          
          // Receiving first CRC character
      case NMEA_RECEIVING_CRC1:
          if (crc>>4 != parseHEX(c)) { // mismatch, count as CRC error
#ifdef NEOGPS_STATS
              statistics.parser_crcerr++;
#endif
              rxEnd( false );
          } else  // valid first CRC nibble
              rxState = NMEA_RECEIVING_CRC2;
          break;
          
          
          // Receiving second CRC character, parse line if CRC matches
      case NMEA_RECEIVING_CRC2:
          if ((crc & 0x0F) != parseHEX(c)) {// CRCs do not match
#ifdef NEOGPS_STATS
              statistics.parser_crcerr++;
#endif
              rxEnd( false );
          } else  // valid second CRC nibble
              rxEnd( true );
          break;
    }
  }

  return c;
}

bool NMEAGPS::parseCommand( char c )
{
  // comma marks end of command name
  if ((c == ',') && (chrCount == 5) && (nmeaMessage != NMEA_UNKNOWN)) {
    // Indicate that the fix is no longer coherent
    m_fix.valid.as_byte = 0;

  } else {
    // A nested FSM to handle the command name
    switch (chrCount) {
      case 0:
        if (c != 'G')
          rxState = NMEA_IDLE;
        break;
      case 1:
        if (c != 'P')
          rxState = NMEA_IDLE;
        break;
      case 2:
        if      (c == 'G') ; // ok but several choices
        else if (c == 'Z') nmeaMessage = NMEA_ZDA;
        else if (c == 'R') nmeaMessage = NMEA_RMC;
        else if (c == 'V') nmeaMessage = NMEA_VTG;
        else
          rxState = NMEA_IDLE;
        break;
      case 3:
        if (((c == 'D') && (nmeaMessage == NMEA_ZDA)) ||
            ((c == 'M') && (nmeaMessage == NMEA_RMC)) ||
            ((c == 'T') && (nmeaMessage == NMEA_VTG))
           )
          ; // ok so far...
        else if (nmeaMessage == NMEA_UNKNOWN) {
          if (c == 'G')
            nmeaMessage = NMEA_GGA;
          else if (c == 'S')
            nmeaMessage = NMEA_GSA;
          else if (c == 'L')
            nmeaMessage = NMEA_GLL;
          else
            rxState = NMEA_IDLE;
        } else
          rxState = NMEA_IDLE;
        break;
      case 4:
        if (((c == 'A') &&
             ((nmeaMessage == NMEA_GGA) ||
              (nmeaMessage == NMEA_GSA) ||
              (nmeaMessage == NMEA_ZDA)))             ||
            ((c == 'C') && (nmeaMessage == NMEA_RMC)) ||
            ((c == 'L') && (nmeaMessage == NMEA_GLL)) ||
            ((c == 'G') && (nmeaMessage == NMEA_VTG)))
          ; // ok so far, comma better be next...
        else if ((c == 'V') && (nmeaMessage == NMEA_GSA))
          nmeaMessage = NMEA_GSV; // not GSA after all
        else
          rxState = NMEA_IDLE;
        break;
      case 5:
        // Too many letters?
        rxState = NMEA_IDLE;
        break;
    }
  }

  return (rxState == NMEA_RECEIVING_DATA);

} // parseCommand


bool NMEAGPS::parseField(char chr)
{
    if (fieldIndex == 0)
      return parseCommand( chr );

    bool ok = true;
    switch (nmeaMessage) {

        case NMEA_GGA:
            switch (fieldIndex) {
                case 1: goto parseTime;
                case 2: goto parseLat;
                case 3: goto parseNS;
                case 4: goto parseLon;
                case 5: goto parseEW;
                case 6:                 // position fix status
                    if (chr != ',') {
                      m_fix.status_satellites = (enum gps_fix_status_t) (chr - '0');
                      // m_fix.satellites = 0;
                    }
                    break;
                case 7:                 // number of satellites
                    if (chr != ',')
                      m_fix.satellites = m_fix.satellites*10 + (chr - '0');
                    break;
                case 8:                 // HDOP
                    if (chrCount == 0)
                      m_fix.hdop = 0;
                    if ((chr != '.') && (chr != ','))
                      m_fix.hdop = m_fix.hdop*10 + (chr - '0');
                    break;
                case 9:                 // Altitude
                    parseFloat( *const_cast<whole_frac *>(&m_fix.alt), chr, 2 );
                    break;
            }
            break;

        case NMEA_GLL:
            switch (fieldIndex) {
                case 1: goto parseLat;
                case 2: goto parseNS;
                case 3: goto parseLon;
                case 4: goto parseEW;
                case 5: goto parseTime;
                case 6:
                case 7: goto parseFix;
            }
            break;

        case NMEA_GSA:
        case NMEA_GSV:
            break;
                  
        case NMEA_RMC:
            switch (fieldIndex) {
                case 1: goto parseTime;
                case 2: goto parseFix;
                case 3: goto parseLat;
                case 4: goto parseNS;
                case 5: goto parseLon;
                case 6: goto parseEW;
                case 7: goto parseSpeed;
                case 8: goto parseHeading;
                case 9:                 // DDMMYY
                    switch (chrCount) {
                      case 0: m_fix.dateTime.date   = (chr - '0')*10; break;
                      case 1: m_fix.dateTime.date  += (chr - '0');    break;
                      case 2: m_fix.dateTime.month  = (chr - '0')*10; break;
                      case 3: m_fix.dateTime.month += (chr - '0');    break;
                      case 4: m_fix.dateTime.year   = (chr - '0')*10; break;
                      case 5: m_fix.dateTime.year  += (chr - '0');    break;
                      case 6: ok = (chr == ',');                      break;
                      default: ok = false;                            break;
                    }
                    break;
                case 10: goto parseFix;
            }
            break;

        case NMEA_VTG:
            switch (fieldIndex) {
                case 1: goto parseHeading;
                case 2:
                    ok = (chr == ',') || ((chr == 'T') && (chrCount == 0));
                    break;
                case 3: // don't care about magnetic heading
                case 4:
                    break;
                case 5: goto parseSpeed;
                case 6:
                    ok = (chr == ',') || ((chr == 'N') && (chrCount == 0));
                    break;
                case 7: // don't care about speed in kph
                case 8:
                    break;
                case 9: goto parseFix;
            }
            break;

        case NMEA_ZDA:
            switch (fieldIndex) {
                case 1:
                    m_fix.dateTime.date  = 0;
                    m_fix.dateTime.month = 0;
                    m_fix.dateTime.year  = 0;
                    goto parseTime;
                case 2:                         // Date
                    if (chr != ',')
                      m_fix.dateTime.date  = (m_fix.dateTime.date *10) + (chr - '0');
                    break;
                case 3:                         // Month
                    if (chr != ',')
                      m_fix.dateTime.month = (m_fix.dateTime.month*10) + (chr - '0');
                    break;
                case 4:                         // Year
                    if ((2 <= chrCount) && (chrCount <= 3))
                      m_fix.dateTime.year  = (m_fix.dateTime.year *10) + (chr - '0');
                    break;
            }
            break;

        default:
            ok = false;
            break;
    }

done:
    return ok;

parseTime: return parseTimeField(chr);
parseLat:  parseDDMM( m_fix.lat, chr ); goto done;
parseNS:
  if (chr == 'S')
    m_fix.lat = -m_fix.lat;
  goto done;
parseLon: parseDDMM( m_fix.lon, chr ); goto done;
parseEW:
  if (chr == 'W')
    m_fix.lon = -m_fix.lon;
  goto done;
parseSpeed: parseFloat( *const_cast<whole_frac *>(&m_fix.spd), chr, 3 ); goto done;
parseHeading: parseFloat( *const_cast<whole_frac *>(&m_fix.hdg), chr, 2 ); goto done;
parseFix: return parseFix( chr );
}


inline bool NMEAGPS::parseTimeField(char chr)
{
  bool ok = true;

  switch (chrCount) {
      case 0: m_fix.dateTime.hours    = (chr - '0')*10; break;
      case 1: m_fix.dateTime.hours   += (chr - '0');    break;
      case 2: m_fix.dateTime.minutes  = (chr - '0')*10; break;
      case 3: m_fix.dateTime.minutes += (chr - '0');    break;
      case 4: m_fix.dateTime.seconds  = (chr - '0')*10; break;
      case 5: m_fix.dateTime.seconds += (chr - '0');    break;
      case 6: if (chr != '.') ok = false;               break;
      case 7: m_fix.dateTime_cs       = (chr - '0')*10; break;
      case 8: m_fix.dateTime_cs      += (chr - '0');    break;
      case 9: if (chr != ',') ok = false;               break;
  }

  return ok;
}

bool NMEAGPS::parseFix( char chr )
{
  bool ok = true;

  if (chrCount == 0) {
    if (chr == 'A')
      m_fix.status = GPS_FIX_STD;
    else if ((chr == 'N') || (chr == 'V'))
      m_fix.status = GPS_FIX_NONE;
    else if (chr == 'D')
      m_fix.status = GPS_FIX_DGPS;
    else if (chr == 'E')
      m_fix.status = GPS_FIX_EST;
    else
      ok = false;
  } else if ((chrCount == 1) && (chr != ',')) {
    ok = false;
  }

  return ok;
}

void NMEAGPS::parseFloat( whole_frac & val, char chr, uint8_t max_decimal )
{
  if (chrCount == 0) {
    val.init();
    decimal = 0;
    negative = (chr == '-');
    if (negative) return;
  }

  if (chr == ',') {
    // End of field, make sure it's scaled up
    if (!decimal)
      decimal = 1;
    while (decimal++ <= max_decimal)
      val.frac *= 10;
    if (negative) {
      val.frac = -val.frac;
      val.whole = -val.whole;
    }
  } else if (chr == '.') {
    decimal = 1;
  } else if (!decimal) {
    val.whole = val.whole*10 + (chr - '0');
  } else if (decimal++ <= max_decimal) {
    val.frac = val.frac*10 + (chr - '0');
  }
}


void NMEAGPS::poll( nmea_msg_t msg ) const
{
  //  Only the ublox documentation references talker ID "EI".  
  //  Other manufacturer's devices use "II" and "GP" talker IDs for the GPQ sentence.
  //  However, "GP" is reserved for the GPS device, so it seems inconsistent
  //  to use that talker ID when requesting something from the GPS device.
  static const char pm0[] __PROGMEM = "EIGPQ,GGA";
  static const char pm1[] __PROGMEM = "EIGPQ,GLL";
  static const char pm2[] __PROGMEM = "EIGPQ,GSA";
  static const char pm3[] __PROGMEM = "EIGPQ,GSV";
  static const char pm4[] __PROGMEM = "EIGPQ,RMC";
  static const char pm5[] __PROGMEM = "EIGPQ,VTG";
  static const char pm6[] __PROGMEM = "EIGPQ,ZDA";
  static const char * const poll_msgs[] __PROGMEM = { pm0, pm1, pm2, pm3, pm4, pm5, pm6 };

  if ((NMEA_FIRST_MSG <= msg) && (msg <= NMEA_LAST_MSG))
    send_P( (const char *) pgm_read_word(&poll_msgs[msg-NMEA_FIRST_MSG]) );
}


static char toHexDigit( uint8_t val )
{
  val &= 0x0F;
  return (val >= 10) ? ((val - 10) + 'A') : (val + '0');
}


bool NMEAGPS::send_header( const char * & msg ) const
{
  if (msg && *msg) {
    m_device->putchar('$');
    if (*msg == '$')
      msg++;
    return true;
  }
  return false;
}


void NMEAGPS::send_trailer( uint8_t crc ) const
{
  m_device->putchar('*');

  char hexDigit = toHexDigit( crc>>4 );
  m_device->putchar( hexDigit );

  hexDigit = toHexDigit( crc );
  m_device->putchar( hexDigit );

  m_device->putchar( CR );
  m_device->putchar( LF );
}


void NMEAGPS::send( const char *msg ) const
{
  if (send_header( msg )) {
    uint8_t crc = 0;
    while (*msg) {
      crc ^= *msg;
      m_device->putchar( *msg++ );
    }

    send_trailer( crc );
  }
}

void NMEAGPS::send_P( const char *msg ) const
{
  if (send_header( msg )) {
    uint8_t crc = 0;
    for(;;) {
      uint8_t chr = pgm_read_byte(msg);
      if (!chr)
        break;
      crc ^= chr;
      m_device->putchar( chr );
      msg++;
    }

    send_trailer( crc );
  }
}
