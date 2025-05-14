#pragma once
#include <SD.h>
#include <SPI.h>
String fileToSend = "";
uint8_t currentTarget = 0x00;
#define SD_CS   10
#define SD_MOSI 12
#define SD_MISO 13
#define SD_SCK  11

#define RS485 Serial2

#define ACK 0x06
#define NAK 0x15
#define REQ_SEND 0xB1
#define OK_READY 0xB2
#define EOF_FLAG 0xEE

SPIClass mySPI(FSPI);
uint8_t thisNode = 0;

void setupCommon(uint8_t nodeID) {
  Serial.begin(115200);
  RS485.begin(115200, SERIAL_8N1, 6, 7); // RX, TX
  mySPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, mySPI)) {
    Serial.println("SD init failed! Halting.");
    while (1);
  }
  thisNode = nodeID;
  Serial.print("Node "); Serial.print(nodeID); Serial.println(" ready.");
}

uint16_t crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0;
  while (len--) {
    crc ^= (*data++) << 8;
    for (uint8_t i = 0; i < 8; i++)
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}

void sendDataPacket(uint8_t* payload, uint8_t len, uint8_t targetID) {
  uint8_t packet[128], i = 0;
  packet[i++] = 0xAB; packet[i++] = 0xCD;
  packet[i++] = thisNode; packet[i++] = targetID;
  packet[i++] = len;
  memcpy(&packet[i], payload, len); i += len;
  uint16_t crc = crc16(&packet[2], 3 + len);
  packet[i++] = crc >> 8; packet[i++] = crc & 0xFF;
  packet[i++] = 0xDC; packet[i++] = 0xBA;
  RS485.write(packet, i);
  RS485.flush();
}

bool waitForAck() {
  unsigned long start = millis();
  while (millis() - start < 1000) {
    if (RS485.available()) {
      uint8_t r = RS485.read();
      if (r == ACK) return true;
      if (r == NAK) return false;
    }
  }
  return false;
}

void sendFile(const char* filename, uint8_t toNode) {
  File file = SD.open(filename);
  if (!file) {
    Serial.println("File open failed.");
    return;
  }

  RS485.write(REQ_SEND);
  RS485.write(toNode);
  RS485.flush();

  // Wait for OK_READY
  unsigned long start = millis();
  while (millis() - start < 1000) {
    if (RS485.available() && RS485.read() == OK_READY) break;
  }

  uint8_t buffer[64];
  while (file.available()) {
    int len = file.read(buffer, sizeof(buffer));
    sendDataPacket(buffer, len, toNode);

    int retries = 0;
    while (!waitForAck()) {
      if (++retries > 5) {
        Serial.println("Too many NAKs. Aborting.");
        file.close(); return;
      }
      sendDataPacket(buffer, len, toNode);
    }
  }

  RS485.write(EOF_FLAG);
  file.close();
  Serial.println("File sent.");
}

void receiveFile(const char* destFile = "/recv.txt") {
  File outFile = SD.open(destFile, FILE_WRITE);
  if (!outFile) {
    Serial.println("Cannot open file.");
    return;
  }

  RS485.write(OK_READY);
  RS485.flush();

  bool receiving = true;
  while (receiving) {
    while (RS485.available() < 8) {
      if (RS485.available() && RS485.peek() == EOF_FLAG) {
        RS485.read(); receiving = false;
        break;
      }
    }
    if (!receiving) break;

    if (RS485.read() != 0xAB || RS485.read() != 0xCD) continue;

    uint8_t sender = RS485.read();
    uint8_t target = RS485.read();
    uint8_t len = RS485.read();
    if (target != thisNode) continue;

    uint8_t data[64];
    RS485.readBytes(data, len);
    uint8_t crcHi = RS485.read();
    uint8_t crcLo = RS485.read();
    RS485.read(); RS485.read(); // skip 0xDC, 0xBA

    uint8_t check[3 + len];
    check[0] = sender; check[1] = target; check[2] = len;
    memcpy(&check[3], data, len);
    uint16_t crc = crc16(check, 3 + len);
    uint16_t recvCRC = (crcHi << 8) | crcLo;

    if (crc == recvCRC) {
      outFile.write(data, len);
      RS485.write(ACK);
    } else {
      RS485.write(NAK);
    }
    RS485.flush();
  }

  outFile.close();
  Serial.println("File received.");
}

void processCommand() {
  static String cmd = "";
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      cmd.trim();
      if (cmd == "LIST") {
        File root = SD.open("/");
        while (true) {
          File f = root.openNextFile();
          if (!f) break;
          Serial.println(f.name());
          f.close();
        }
      } else if (cmd.startsWith("READ ")) {
        File f = SD.open("/" + cmd.substring(5));
        while (f && f.available()) Serial.write(f.read());
        f.close();
      } else if (cmd.startsWith("SEND ")) {
        int space = cmd.indexOf(' ', 5);
        if (space != -1) {
          uint8_t target = cmd.substring(5, space).toInt();
          String fname = cmd.substring(space + 1);
          extern uint8_t currentTarget;
          extern String fileToSend;
          currentTarget = target;
          fileToSend = "/" + fname;
          Serial.print("Queued file to send to Node "); Serial.println(target);
        }
      } else if (cmd == "RECV") {
        receiveFile();
      } else {
        Serial.println("Commands: LIST, READ <f>, SEND <id> <f>, RECV");
      }
      cmd = "";
    } else {
      cmd += c;
    }
  }
}
