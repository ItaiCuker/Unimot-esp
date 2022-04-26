#include "ir.h"

const uint16_t kFrequency = 38000;  // in Hz. e.g. 38kHz.
const uint16_t kCaptureBufferSize = 1024;
const uint8_t kTimeout = 15;

decode_results results;  // Somewhere to store the results

bool isReading = false;
bool gotCode = false;

size_t rawDataLen;
uint16_t *rawData;

TaskHandle_t xHandleReadCode;

IRsend irsend(GPIO_TX);  // Set the GPIO to be used to sending the message.
IRrecv irrecv(GPIO_RX, kCaptureBufferSize, kTimeout, true);

void initSendCode(){
    irsend.begin();
}

void sendCode() {
  if (!isReading && gotCode)
  {
    // Convert the results into an array suitable for sendRaw().
    // resultToRawArray() allocates the memory we need for the array.
    rawData = resultToRawArray(&results);
    // Find out how many elements are in the array.
    rawDataLen = getCorrectedRawLength(&results);
    irsend.sendRaw(rawData, rawDataLen, kFrequency);
    // Deallocate the memory allocated by resultToRawArray().
    // delete [] raw_array;
    ESP_LOGI(TAG, "Sent code %s", resultToHumanReadableBasic(&results).c_str());
  }
}

void initReadCode() {
  pinMode(12, INPUT);
}

void readCode(void *pvParameters){
  while (true){
    if (irrecv.decode(&results)){
      gotCode = true;
      // Display the basic output of what we found.
      ESP_LOGI("ir result", "%s\n%s", resultToHumanReadableBasic(&results).c_str(), resultToSourceCode(&results).c_str());
    }
    vTaskDelay (10 / TICK);
  }
}

void startRead() {
    isReading = true;
    gotCode = false; //reseting state so I won't use previous saved code.
    irrecv.enableIRIn();
    xTaskCreate(&readCode, "readCode", 2048, NULL, 2, &xHandleReadCode);
}

void stopRead() {
    isReading = false;
    irrecv.disableIRIn();
    if (xHandleReadCode)
        vTaskDelete(xHandleReadCode);
}