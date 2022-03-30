#ifndef IR_H
#define IR_H

#include "defines.h"

#include "IRrecv.h"
#include "IRsend.h"
#include "IRutils.h"

extern bool isReading;

void startRead();
void stopRead();

void initSendCode();
void sendCode();
void initReadCode();
void readCode(void *pvParameters);

#endif