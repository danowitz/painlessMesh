#define ARDUINOJSON_ENABLE_STD_STRING 1

#ifdef ESP32
#include <SPIFFS.h>
#include <Update.h>
#else
#include <FS.h>
#endif

#include "base64.h"
#include "painlessMesh.h"

#define OTA_FN "/ota_fw.json"

#define MESH_PREFIX "whateverYouLike"
#define MESH_PASSWORD "somethingSneaky"
#define MESH_PORT 5555

Scheduler userScheduler;  // to control your personal task
painlessMesh mesh;

struct firmware_ota_t {
#ifdef ESP32
    String hardware = "ESP32";
#else
    String hardware = "ESP8266";
#endif
    String nodeType = "test";
    String md5;
    size_t noPart;
    size_t partNo = 0;
};

void createDataRequest(JsonObject &req, firmware_ota_t updateFW) {
    req["plugin"] = "ota";
    req["type"] = "request";
    req["hardware"] = updateFW.hardware;
    req["nodeType"] = updateFW.nodeType;
    req["md5"] = updateFW.md5;
    req["noPart"] = updateFW.noPart;
    req["partNo"] = updateFW.partNo;
}

void firmwareFromJSON(firmware_ota_t &fw, JsonObject &req) {
    fw.hardware = req["hardware"].as<String>();
    fw.nodeType = req["nodeType"].as<String>();
    fw.md5 = req["md5"].as<String>();
}

firmware_ota_t currentFW;
firmware_ota_t updateFW;
Task otaDataRequestTask;

// User stub
void sendMessage();  // Prototype so PlatformIO doesn't complain

Task taskSendMessage(TASK_SECOND * 1, TASK_FOREVER, &sendMessage);

void sendMessage() {
    String msg = "Hello from node ";
    msg += mesh.getNodeId();
    mesh.sendBroadcast(msg);
    taskSendMessage.setInterval(random(TASK_SECOND * 1, TASK_SECOND * 5));
}

// Needed for painless library
void receivedCallback(uint32_t from, String &msg) {
    bool isJSON = false;
#if ARDUINOJSON_VERSION_MAJOR == 6
    DynamicJsonDocument jsonBuffer(1024 + msg.length());
    DeserializationError error = deserializeJson(jsonBuffer, msg);
    if (!error) {
        isJSON = true;
    }
    JsonObject root = jsonBuffer.as<JsonObject>();
#else
    DynamicJsonBuffer jsonBuffer;
    JsonObject &root = jsonBuffer.parseObject(msg);
    if (root.success()) isJSON = true;
#endif
    if (isJSON && root.containsKey("plugin") &&
        String("ota").equals(root["plugin"].as<String>())) {
        Serial.printf("startHere: Received OTA msg from %u msg=%s\n", from,
                      msg.c_str());

        if (currentFW.nodeType.equals(root["nodeType"].as<String>()) &&
            currentFW.hardware.equals(root["hardware"].as<String>())) {
            if (String("version").equals(root["type"].as<String>())) {
                if (currentFW.md5.equals(root["md5"].as<String>()) ||
                    updateFW.md5.equals(root["md5"].as<String>()))
                    return;  // Announced version already known
                else {
                    // Setup new updatedFW
                    updateFW = currentFW;
                    updateFW.md5 = root["md5"].as<String>();
                    updateFW.partNo = 0;
                    updateFW.noPart = root["noPart"].as<size_t>();

                    // Setup otaDataRequestTask (enableIfNot)
                    otaDataRequestTask.set(30 * TASK_SECOND, 5, [from]() {
#if ARDUINOJSON_VERSION_MAJOR == 6
                        DynamicJsonDocument jsonBuffer(256);
                        auto req = jsonBuffer.to<JsonObject>();
#else
                        DynamicJsonBuffer jsonBuffer;
                        JsonObject& req = jsonBuffer.createObject();
#endif
                        createDataRequest(req, updateFW);
                        String msg;
#if ARDUINOJSON_VERSION_MAJOR == 6
                        serializeJson(req, msg);
#else
                        req.printTo(msg);
#endif
                        uint32_t cpyFrom = from;
                        mesh.sendSingle(cpyFrom, msg);
                    });
                    otaDataRequestTask.enableIfNot();
                    otaDataRequestTask.forceNextIteration();
                    Serial.printf("Requesting firmware update\n");
                }
            } else if (String("data").equals(root["type"].as<String>()) &&
                       updateFW.partNo == root["partNo"].as<size_t>()) {
                size_t partNo = root["partNo"];
                size_t noPart = root["noPart"];
                if (partNo == 0) {
                    String otaMD5 = root["md5"].as<String>();
#ifdef ESP32
                    uint32_t maxSketchSpace = UPDATE_SIZE_UNKNOWN;
#else
                    uint32_t maxSketchSpace =
                        (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
#endif
                    Serial.printf("Sketch size %d\n", maxSketchSpace);
                    if (Update.isRunning()) {
                        Update.end(false);
                    }
                    if (!Update.begin(
                            maxSketchSpace)) {  // start with max available size
                        Serial.println("handleOTA(): OTA start failed!");
                        Update.printError(Serial);
                        Update.end();
                    } else {
                        Update.setMD5(otaMD5.c_str());
                    }
                }
                //    write data
                auto b64data = root["data"].as<std::string>();
                auto b64Data = base64_decode(b64data);

                if (Update.write((uint8_t *)b64Data.c_str(),
                                 b64Data.length()) != b64Data.length()) {
                    Serial.println("handleOTA(): OTA write failed!");
                    Update.printError(Serial);
                    Update.end();
                    return;
                }

                if (partNo == noPart - 1) {
                    //       check md5, reboot
                    if (Update.end(true)) {  // true to set the size to the
                                             // current progress
                        Serial.printf("Update.MD5: %s\n",
                                      Update.md5String().c_str());
                        auto file = SPIFFS.open(OTA_FN, "w");
#if ARDUINOJSON_VERSION_MAJOR == 6
                        DynamicJsonDocument jsonBuffer(1024);
                        auto req = jsonBuffer.to<JsonObject>();
#else
                        DynamicJsonBuffer jsonBuffer;
                        JsonObject &req = jsonBuffer.createObject();
#endif
                        createDataRequest(req, updateFW);
                        String msg;
#if ARDUINOJSON_VERSION_MAJOR == 6
                        serializeJson(req, msg);
#else
                        req.printTo(msg);
#endif
                        file.print(msg);
                        file.close();

                        Serial.println("handleOTA(): OTA Success!");
                        ESP.restart();
                    } else {
                        Serial.println("handleOTA(): OTA failed!");
                        Update.printError(Serial);
                    }
                    otaDataRequestTask.disable();
                } else {
                    ++updateFW.partNo;
                    otaDataRequestTask.setIterations(5);
                    otaDataRequestTask.forceNextIteration();
                }
            }
        }
    }

    Serial.printf("ota: Received from %u msg=%s\n", from, msg.c_str());
}

void newConnectionCallback(uint32_t nodeId) {
    Serial.printf("--> ota: New Connection, nodeId = %u\n", nodeId);
}

void changedConnectionCallback() {
  Serial.printf("Changed connections\n");
}

void setup() {
    Serial.begin(115200);

    // set before init() so that you can see startup messages
    mesh.setDebugMsgTypes(ERROR | STARTUP);

    mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT,
              WIFI_AP_STA, 6);
    // mesh.init( MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
    mesh.onReceive(&receivedCallback);
    mesh.onNewConnection(&newConnectionCallback);
    mesh.onChangedConnections(&changedConnectionCallback);
    mesh.setContainsRoot(true);

    userScheduler.addTask(otaDataRequestTask);

    userScheduler.addTask(taskSendMessage);
    taskSendMessage.enable();

#ifdef ESP32
    SPIFFS.begin(true);  // Start the SPI Flash Files System
#else
    SPIFFS.begin();  // Start the SPI Flash Files System
#endif
    if (SPIFFS.exists(OTA_FN)) {
        auto file = SPIFFS.open(OTA_FN, "r");
        String msg = "";
        while (file.available()) {
            msg += (char)file.read();
        }

#if ARDUINOJSON_VERSION_MAJOR == 6
        DynamicJsonDocument jsonBuffer(1024);
        DeserializationError error = deserializeJson(jsonBuffer, msg);
        if (error) {
            Serial.printf("JSON DeserializationError\n");
        }
        JsonObject root = jsonBuffer.as<JsonObject>();
#else
        DynamicJsonBuffer jsonBuffer;
        JsonObject &root = jsonBuffer.parseObject(msg);
#endif
        firmwareFromJSON(currentFW, root);

        Serial.printf("Current firmware MD5: %s, type = %s, hardware = %s\n",
                      currentFW.md5.c_str(), currentFW.nodeType.c_str(),
                      currentFW.hardware.c_str());
        file.close();
    } else {
        Serial.printf("No OTA_FN found!\n");
    }
}

void loop() {
    userScheduler.execute();  // it will run mesh scheduler as well
    mesh.update();
}
