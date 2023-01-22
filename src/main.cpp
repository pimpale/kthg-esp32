#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoWebsockets.h>

// wifi
#include "wifi_setup.hpp"

using namespace websockets;

#define PATHBUFLEN 512

const char *server_host = "192.168.141.191"; // Enter server adress
const uint16_t server_port = 8080;           // Enter server port

const char *API_KEY = "hi";
const int THIS_USER_ID = 1;
const int TARGET_USER_ID = 1;

const int MICROPHONE_PIN = 34;
const int RECORD_BUTTON_PIN = 33;
const int PHOTORESISTOR_PIN = 32;
const int SPEAKER_PIN = 25;

float clamp(float a)
{
  if (a < 0)
  {
    return 0;
  }
  if (a > 1)
  {
    return 1;
  }
  return a;
}

typedef struct {
  uint8_t buf[4096];
  size_t read;
  size_t write; 
} CircularBuffer;

bool cbFull(volatile CircularBuffer *x) {
  return x->write + 1 == x->read;
}
bool cbEmpty(volatile CircularBuffer *x) {
  return x->read == x->write;
}


void cbReset(volatile CircularBuffer *x) {
  x->write = 0;
  x->read = 0;
}

bool cbTryIns(volatile CircularBuffer *x, uint8_t val) {
  if(cbFull(x)) {
    return false;
  }
  x->buf[x->write] = val;
  x->write = (x->write+1) % sizeof(x->buf); 
  return true;
}

bool cbTryPop(volatile CircularBuffer *x, uint8_t *val) {
  if(cbEmpty(x)) {
    return false;
  }
  *val = x->buf[x->read];
  x->read = (x->read+1) % sizeof(x->buf); 
  return true;
} 

volatile bool global_submit_active = false;
volatile CircularBuffer global_submit_queue;

volatile bool global_receive_active = false;
volatile CircularBuffer global_receive_queue;


void setup()
{
  Serial.begin(9600);

  connectWiFi();

  pinMode(RECORD_BUTTON_PIN, INPUT);
}

void recordUserMessage(void*) {
  while(global_submit_active) {
    float temp =  analogRead(MICROPHONE_PIN);
    uint8_t val = uint8_t(clamp((temp - 1000) / 2500) * 255);
    bool success = cbTryIns(&global_submit_queue, val);
    if(!success) {
      Serial.println("BRUH");
    }
    delayMicroseconds(50);
  }
  vTaskDelete(NULL);
}

void playUserMessage(void*) {
  while(global_receive_active) {
    uint8_t val;
    boolean success = cbTryPop(&global_submit_queue, &val);
    if(success) {
      dacWrite(25, val);
    }
    delayMicroseconds(1000);
  }
  vTaskDelete( NULL );
}

void submitUserMessage()
{
  // buffer for the path and query
  char pathbuf[PATHBUFLEN] = {};
  // insert arguments
  sprintf(pathbuf, "/public/ws/submit_user_message?targetUserId=%d&apiKey=%s", TARGET_USER_ID, API_KEY);
  WebsocketsClient client;
  // try to connect
  bool connected = client.connect(server_host, server_port, pathbuf);

  if (!connected)
  {
    Serial.println("SubmitUserMessage: Failed to Connect!");
    return;
  }
  Serial.println("SubmitUserMessage: Connected!");
  
  // reset queue
  cbReset(&global_submit_queue);
  global_submit_active = true;
  // start recording
  xTaskCreatePinnedToCore (
    recordUserMessage,     // Function to implement the task
    "recordUserMessage",   // Name of the task
    1000,      // Stack size in bytes
    NULL,      // Task input parameter
    0,         // Priority of the task
    NULL,      // Task handle.
    0          // Core where the task should run
  );
  
  while (digitalRead(RECORD_BUTTON_PIN) == 1)
  {
    uint8_t buf[1024];
    for(int i = 0; i < sizeof(buf); i++) {
      uint8_t element;
      while(!cbTryPop(&global_submit_queue, &element)) {
        delayMicroseconds(10);
      }
      buf[i] = element;
    }
    // send the current buffer
    boolean successful = client.sendBinary((const char *)buf, sizeof(buf));
    if (!successful)
    {
      Serial.println("SubmitUserData: encountered error");
      break;
    }
  }
  global_submit_active = false;
  Serial.println("SubmitUserData: stopped sending data");
  client.close();
}

int getRecentUserMessageId(int targetUserId)
{
  char pathbuf[PATHBUFLEN];
  sprintf(pathbuf, "/public/get_recent_user_message_id?targetUserId=%d", targetUserId);

  HTTPClient http;

  http.begin(server_host, server_port, pathbuf);
  http.addHeader("Accept", "*/*");

  // Send HTTP GET request
  int httpResponseCode = http.GET();

  Serial.print("recentUserMessage: Got code: ");
  Serial.println(httpResponseCode);
  String respTxt = http.getString();
  Serial.print("recentUserMessage: received: ");
  Serial.println(respTxt);

  if (httpResponseCode != 200)
  {
    return -1;
  }

  return atoi(respTxt.c_str());
}

void recieveUserMessage(int userMessageId)
{
  // buffer for the path and query
  char pathbuf[PATHBUFLEN] = {};
  // insert arguments
  sprintf(pathbuf, "/public/ws/receive_user_message?userMessageId=%d&apiKey=%s", userMessageId, API_KEY);

  WebsocketsClient client;
  bool connected = client.connect(server_host, server_port, pathbuf);

  if (!connected)
  {
    Serial.println("ReceiveUserData: Failed to Connect!");
    return;
  }

  // reset queue
  cbReset(&global_receive_queue);
  global_receive_active = true;
  xTaskCreatePinnedToCore (
    playUserMessage,     // Function to implement the task
    "playUserMessage",   // Name of the task
    1000,      // Stack size in bytes
    NULL,      // Task input parameter
    0,         // Priority of the task
    NULL,      // Task handle.
    0          // Core where the task should run
  );

  int count = 0;

  while (client.available())
  {
    // recieve a bunch of data
    WebsocketsMessage msg = client.readBlocking();

    if (msg.isBinary())
    {
      count++;
      Serial.println(count);
      const int len = msg.data().length();
      const char *data = msg.data().c_str();
      for (int i = 0; i < len; i++)
      {
        while(!cbTryIns(&global_submit_queue, data[i])) {
          delayMicroseconds(10);
        }
      }
    }
  }
  global_receive_active = false;
  Serial.println("ReceiveUserData: exiting");
  client.close();
}

void loop()
{
  int photoValue = analogRead(PHOTORESISTOR_PIN);
  if (photoValue < 700)
  {
    recieveUserMessage(getRecentUserMessageId(THIS_USER_ID));
  }

  if (digitalRead(RECORD_BUTTON_PIN) == 1)
  {
    submitUserMessage();
  }
}
