#ifndef IR_H
#define IR_H

#include "defines.h"

#include "main.h"

#include "IRrecv.h"
#include "IRsend.h"
#include "IRutils.h"

extern bool isReading;

void testCode();
void sendCommand(uint16_t *command, size_t commandLen);

void startRead();
void stopRead();

extern size_t rawDataLen;
extern uint16_t *rawData;

void initSendCode();
void initReadCode();
void readCode(void *pvParameters);

#endif