#include <WimpMultiHopProtocol.h>

#define len 512


char data[len];
int tent = 0;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();

  delay(10000);
  
  WIMP::initialize();

}

void loop() {
  // put your main code here, to run repeatedly:
  int n = WIMP::read(data);

  if (n > 0) {
    Serial.println("Received data:");
    data[n] = '\0';
    Serial.printf("%s\n", data);  
  }

  if (n < 0) {
    Serial.println("Error in read!");  
  }

  delay(5000);
  tent++;
  if (tent == 2) {
    WIMP::manage_network();
    tent = 0;
  }
}
