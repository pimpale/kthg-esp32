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
const int PLAYBACK_BUTTON_PIN = 26;

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

size_t global_scratch_size = 32 * 1024;
uint8_t *global_scratch_buf = NULL;

void setup()
{
  Serial.begin(9600);

  global_scratch_buf = new uint8_t[global_scratch_size];

  connectWiFi();

  pinMode(RECORD_BUTTON_PIN, INPUT);
  pinMode(PLAYBACK_BUTTON_PIN, INPUT);
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
  while (digitalRead(RECORD_BUTTON_PIN) == 1)
  {
    int i;
    for (i = 0; i < global_scratch_size && digitalRead(RECORD_BUTTON_PIN) == 1; i++)
    {
      {
        float temp = analogRead(MICROPHONE_PIN);
        uint8_t val = uint8_t(clamp((temp - 1000) / 2500) * 255);
        global_scratch_buf[i] = val;
        delayMicroseconds(50);
      }
    }
    boolean successful = client.sendBinary((const char *)global_scratch_buf, i);
    if (!successful)
    {
      Serial.println("SubmitUserData: encountered error");
      break;
    }
  }
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

void queryParamsSleepEventNew(int creatorUserId)
{
  char pathbuf[PATHBUFLEN];
  sprintf(pathbuf, "/public/query_params_sleep_event_new?creatorUserId=%d", creatorUserId);

  HTTPClient http;

  http.begin(server_host, server_port, pathbuf);
  http.addHeader("Accept", "*/*");

  // Send HTTP GET request
  int httpResponseCode = http.GET();

  Serial.print("SleepEventNew: Got code: ");
  Serial.println(httpResponseCode);
  String respTxt = http.getString();
  Serial.print("SleepEventNew: received: ");
  Serial.println(respTxt);
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
        dacWrite(25, data[i]);
        delayMicroseconds(130);
      }
    }
  }
  Serial.println("ReceiveUserData: exiting");
  client.close();
}



void loop()
{
  int photoValue = analogRead(PHOTORESISTOR_PIN);
  if (photoValue > 1300)
  {
    queryParamsSleepEventNew(THIS_USER_ID);
    delay(500);
  }

  if (digitalRead(PLAYBACK_BUTTON_PIN) == 1)
  {
    recieveUserMessage(getRecentUserMessageId(THIS_USER_ID));
  }

  if (digitalRead(RECORD_BUTTON_PIN) == 1)
  {
    submitUserMessage();
  }
}
