#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <Vector.h>
#include <WiFiClient.h>

#include "secrets.h"

// PROD
#define SERVER_NAME "http://duster.arikardasis.com/api"
#define RUN_TIMEOUT_MILLIS 600000 // 10 minutes

// DEVELOP
// #define SERVER_NAME "http://mbp.local/api"
// #define RUN_TIMEOUT_MILLIS 3000 // 10 seconds

#define HALL_EFFECT_SENSOR_PIN 2
#define TICKSTAMP_BUFFER_LENGTH 500
#define DEBOUNCE_MILLIS 10 // 10 millis
#define MIN_DATA_POST_INTERVAL 1000

typedef unsigned long tickstamp_t;
tickstamp_t storage_array[TICKSTAMP_BUFFER_LENGTH];
Vector<tickstamp_t> vector(storage_array);

tickstamp_t lastTickstamp;
tickstamp_t lastDataPostTickstamp;
tickstamp_t currentTickstamp;

String serverName = SERVER_NAME;
String runId = "";

bool vector_lock = false;

HTTPClient http;
WiFiClient wifiClient;

void connectWifi() {
  // Save boot up time by not configuring them if they haven't changed
  if (WiFi.SSID() != WIFI_SSID) {
    Serial.println(F("Initialising Wifi..."));
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    WiFi.persistent(true);
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
  }

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println(F("Connection Failed!"));
  }

  String serverPath = serverName + "/device_connected";
  http.begin(wifiClient, serverPath.c_str());
  http.addHeader("Content-Type", "application/json");
  String data = "{\"mac_address\": \"" + WiFi.macAddress() + "\"}";
  http.POST(data);
  http.end();
}

void fetchRunId() {
  Serial.println("fetching runId");
  if (WiFi.status() == WL_CONNECTED) {
    String serverPath = serverName + "/runs";
    http.begin(wifiClient, serverPath.c_str());
    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.POST("");
    if (httpResponseCode > 0) {
      runId = http.getString().substring(7, 43);
      Serial.print("fetched run id: ");
      Serial.println(runId);
    } else {
      Serial.print("Failed to fetch runId.  Error code: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("WiFi Disconnected. Retrying");
    connectWifi();
  }
}

void IRAM_ATTR hall_effect_isr() {
  currentTickstamp = millis();
  if (currentTickstamp - lastTickstamp > DEBOUNCE_MILLIS) {
    lastTickstamp = currentTickstamp;
    while (vector_lock)
      ;
    vector.push_back(millis());
  }
}

String getPostData() {
  if (vector.size() == 0 ||
      millis() - lastDataPostTickstamp < MIN_DATA_POST_INTERVAL) {
    return "";
  }

  vector_lock = true;

  String res = "{\"data\": \"";

  for (unsigned int i = 0; i < vector.size(); i++) {
    res = res + "," + vector.at(i);
  }
  vector.clear();

  vector_lock = false;
  return res + "\"}";
}

void sendData() {
  if (WiFi.status() == WL_CONNECTED) {
    bool runIdEmpty = runId.isEmpty();
    bool timeoutReached = millis() - lastTickstamp > RUN_TIMEOUT_MILLIS;
    if (!runIdEmpty && timeoutReached) {
      Serial.println("finalizing run");

      String serverPath = serverName + "/runs/" + runId + "/run_summaries";
      http.begin(wifiClient, serverPath.c_str());
      http.addHeader("Content-Type", "application/json");
      http.POST("");
      http.end();
      runId = "";
    } else {
      String data = getPostData();
      if (!data.isEmpty()) {
        while (runId == "") {
          fetchRunId();
          delay(10);
        }
        String serverPath = serverName + "/run/" + runId + "/datapoints";
        http.begin(wifiClient, serverPath.c_str());
        http.addHeader("Content-Type", "application/json");
        http.POST(data);
        http.end();
        lastDataPostTickstamp = millis();
      }
    }
  } else {
    Serial.println("WiFi Disconnected. Retrying");
    connectWifi();
  }
}

void setup() {
  Serial.begin(115200);
  connectWifi();

  pinMode(HALL_EFFECT_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(HALL_EFFECT_SENSOR_PIN, hall_effect_isr, RISING);

  lastDataPostTickstamp = millis();
  lastTickstamp = millis();
  currentTickstamp = millis();
}

void loop() {
  sendData();
  delay(1);
}