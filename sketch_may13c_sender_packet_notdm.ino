// ============================
// Master Node (Node ID = 1)
// ============================
#include "TDMCommon.h"

#define NODE_ID 0x01  // Master node
#define NUM_SLAVES 2


uint8_t slaves[NUM_SLAVES] = {0x02, 0x03};

unsigned long lastPollTime = 0;
uint8_t currentSlave = 0;

void setup() {
  setupCommon(NODE_ID);
}

void loop() {
  processCommand(); // Handle SEND <id> <file> on PC side

  unsigned long now = millis();
  if (now - lastPollTime > 1000) {
    uint8_t slaveID = slaves[currentSlave];
    Serial.print("Polling Node "); Serial.println(slaveID, HEX);
    RS485.write(0xF0);     // Poll command
    RS485.write(slaveID);  // Which node to poll
    RS485.flush();
    currentSlave = (currentSlave + 1) % NUM_SLAVES;
    lastPollTime = now;
  }
}


