#include "/home/tommaso/WimpMultiHopProtocol.h"

char* data; //TODO: to be changed
int timer_check = 0;

void setup() {
  // put your setup code here, to run once:
  WIMP::manage_network();
}

void loop() {
  // put your main code here, to run repeatedly:
  esp_read(data);

  delay(200);
  timer_check++;

  //giusto per tardare la gestione della rete che è pesante...
  if (timer_check == 100) {
    timer_check = 0;
    WIMP::manage_network();  
  }
}

bool esp_send(char* mx) {
  WIMP::send(mx);
}

int esp_read(char* buf) {
  int r = WIMP::read(buf);
  if (r > 0) {
    //give data to arduino
  }
}

