#include "ir.h"

const uint16_t kFrequency = 38000;  // in Hz. e.g. 38kHz.
const uint16_t kCaptureBufferSize = 1024;
const uint8_t kTimeout = 15;
const uint16_t kMinUnknownSize = 12;
const uint8_t kTolerancePercentage = kTolerance;  // kTolerance is normally 25%

decode_results results;  // Somewhere to store the results

bool isReading = false;

size_t rawDataLen;
uint16_t *rawData;

TaskHandle_t xHandleReadCode;

IRsend irsend(GPIO_TX, true);  // Set the GPIO to be used to sending the message.
IRrecv irrecv(GPIO_RX, kCaptureBufferSize, kTimeout, true);

void initSendCode(){
    irsend.begin();
}

void sendCode() {
  if (!isReading)    
    irsend.sendRaw(rawData, rawDataLen, kFrequency);
}

void initReadCode() {
  irrecv.setUnknownThreshold(kMinUnknownSize);
  irrecv.setTolerance(kTolerancePercentage);  // Override the default tolerance.

}

void readCode(void *pvParameters){
  while (true){
    if (irrecv.decode(&results)){
      // Display the basic output of what we found.
      ESP_LOGI("ir result", "%s", resultToHumanReadableBasic(&results).c_str());
      rawData = resultToRawArray(&results);
      rawDataLen = getCorrectedRawLength(&results);
    }
    vPortYield();
  }
}

void startRead() {
    isReading = true;
    irrecv.enableIRIn();
    xTaskCreate(&readCode, "readCode", 2048, NULL, 1, &xHandleReadCode);
}

void stopRead() {
    isReading = false;
    irrecv.disableIRIn();
    if (xHandleReadCode != NULL)
        vTaskDelete(xHandleReadCode);
}