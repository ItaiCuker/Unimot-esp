#include "ir.h"

const uint16_t kFrequency = 38000;  // in Hz. e.g. 38kHz.
const uint16_t kCaptureBufferSize = 1024;
const uint8_t kTimeout = 15;
const uint16_t kMinUnknownSize = 12;
const uint8_t kTolerancePercentage = kTolerance;  // kTolerance is normally 25%

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
    decode_type_t protocol = results.decode_type;
    uint16_t size = results.bits;
    bool success = true;
    // Is it a protocol we don't understand?
    if (protocol == decode_type_t::UNKNOWN || true) {  // Yes.
      // Convert the results into an array suitable for sendRaw().
      // resultToRawArray() allocates the memory we need for the array.
      uint16_t *raw_array = resultToRawArray(&results);
      // Find out how many elements are in the array.
      size = getCorrectedRawLength(&results);
      irsend.sendRaw(raw_array, size, kFrequency);
      // Deallocate the memory allocated by resultToRawArray().
      delete [] raw_array;
    } 
  //   else if (hasACState(protocol)) {  // Does the message require a state[]?
  //     // It does, so send with bytes instead.
  //     success = irsend.send(protocol, results.state, size / 8);
  //   } 
  //   else {  // Anything else must be a simple message protocol. ie. <= 64 bits
  //     success = irsend.send(protocol, results.value, size);
  //   }
  //   ESP_LOGI(TAG, "A %d-bit %s message was %ssuccessfully retransmitted.\n", size, typeToString(protocol).c_str(),success ? "" : "un");
  }
}

void initReadCode() {
  irrecv.setUnknownThreshold(kMinUnknownSize);
  irrecv.setTolerance(kTolerancePercentage);  // Override the default tolerance.

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