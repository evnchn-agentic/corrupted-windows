/*
 * IPKVM USB HID Keyboard - ESP32-S3
 * With WiFi TCP server + OTA + Serial fallback
 *
 * USB port (native) → emulates USB keyboard to DeskPC
 * TCP :23 → receives commands from Mac (Claude agent) via WiFi
 * Serial (COM) → fallback command input
 *
 * Protocol (over TCP or serial):
 *   TYPE:hello world     → types "hello world"
 *   KEY:enter            → sends Enter key
 *   KEY:f12              → sends F12 (boot menu)
 *   COMBO:ctrl+alt+del   → Ctrl+Alt+Delete
 *   COMBO:shift+f10      → Shift+F10 (WinPE cmd prompt)
 *   DELAY:1000           → wait 1000ms
 *   PING                 → responds PONG
 *   RELEASE              → release all keys
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "USB.h"
#include "USBHIDKeyboard.h"

// WiFi credentials — set these for your network
const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASS";
const char* OTA_PASS = "YOUR_OTA_PASS";

// TCP server on port 23 (telnet-style)
WiFiServer tcpServer(23);
WiFiClient tcpClient;

USBHIDKeyboard Keyboard;

// Map key names to HID key codes
uint8_t getKeyCode(String key) {
    key.toLowerCase();
    if (key == "enter" || key == "return") return KEY_RETURN;
    if (key == "tab") return KEY_TAB;
    if (key == "escape" || key == "esc") return KEY_ESC;
    if (key == "backspace" || key == "bs") return KEY_BACKSPACE;
    if (key == "delete" || key == "del") return KEY_DELETE;
    if (key == "space") return ' ';
    if (key == "up") return KEY_UP_ARROW;
    if (key == "down") return KEY_DOWN_ARROW;
    if (key == "left") return KEY_LEFT_ARROW;
    if (key == "right") return KEY_RIGHT_ARROW;
    if (key == "home") return KEY_HOME;
    if (key == "end") return KEY_END;
    if (key == "pageup" || key == "pgup") return KEY_PAGE_UP;
    if (key == "pagedown" || key == "pgdn") return KEY_PAGE_DOWN;
    if (key == "insert" || key == "ins") return KEY_INSERT;
    if (key == "f1") return KEY_F1;
    if (key == "f2") return KEY_F2;
    if (key == "f3") return KEY_F3;
    if (key == "f4") return KEY_F4;
    if (key == "f5") return KEY_F5;
    if (key == "f6") return KEY_F6;
    if (key == "f7") return KEY_F7;
    if (key == "f8") return KEY_F8;
    if (key == "f9") return KEY_F9;
    if (key == "f10") return KEY_F10;
    if (key == "f11") return KEY_F11;
    if (key == "f12") return KEY_F12;
    if (key == "capslock") return KEY_CAPS_LOCK;
    return 0;
}

uint8_t getModifier(String mod) {
    mod.toLowerCase();
    if (mod == "ctrl" || mod == "control") return KEY_LEFT_CTRL;
    if (mod == "alt") return KEY_LEFT_ALT;
    if (mod == "shift") return KEY_LEFT_SHIFT;
    if (mod == "win" || mod == "gui" || mod == "super") return KEY_LEFT_GUI;
    if (mod == "rctrl") return KEY_RIGHT_CTRL;
    if (mod == "ralt") return KEY_RIGHT_ALT;
    if (mod == "rshift") return KEY_RIGHT_SHIFT;
    return 0;
}

// Send response to whichever channel the command came from
void respond(const String& msg, bool fromTcp) {
    if (fromTcp && tcpClient.connected()) {
        tcpClient.println(msg);
    }
    Serial.println(msg);
}

void handleCommand(String cmd, bool fromTcp) {
    cmd.trim();
    if (cmd.length() == 0) return;

    if (cmd == "PING") {
        respond("PONG", fromTcp);
        return;
    }

    if (cmd.startsWith("TYPE:")) {
        String text = cmd.substring(5);
        for (unsigned int i = 0; i < text.length(); i++) {
            Keyboard.press(text[i]);
            delay(5);
            Keyboard.release(text[i]);
            delay(5);
        }
        respond("OK:TYPE", fromTcp);
        return;
    }

    if (cmd.startsWith("KEY:")) {
        String keyName = cmd.substring(4);
        uint8_t code = getKeyCode(keyName);
        if (code != 0) {
            Keyboard.press(code);
            delay(50);
            Keyboard.release(code);
            respond("OK:KEY:" + keyName, fromTcp);
        } else {
            respond("ERR:UNKNOWN_KEY:" + keyName, fromTcp);
        }
        return;
    }

    if (cmd.startsWith("COMBO:")) {
        String combo = cmd.substring(6);
        int partCount = 0;
        String parts[5];
        int start = 0;
        for (unsigned int i = 0; i <= combo.length(); i++) {
            if (i == combo.length() || combo[i] == '+') {
                parts[partCount++] = combo.substring(start, i);
                start = i + 1;
                if (partCount >= 5) break;
            }
        }
        for (int i = 0; i < partCount - 1; i++) {
            uint8_t mod = getModifier(parts[i]);
            if (mod) Keyboard.press(mod);
        }
        String finalKey = parts[partCount - 1];
        uint8_t code = getKeyCode(finalKey);
        if (code != 0) {
            Keyboard.press(code);
        } else if (finalKey.length() == 1) {
            Keyboard.press(finalKey[0]);
        }
        delay(100);
        Keyboard.releaseAll();
        respond("OK:COMBO:" + combo, fromTcp);
        return;
    }

    if (cmd.startsWith("DELAY:")) {
        int ms = cmd.substring(6).toInt();
        if (ms > 0 && ms <= 30000) {
            delay(ms);
            respond("OK:DELAY:" + String(ms), fromTcp);
        } else {
            respond("ERR:DELAY_RANGE", fromTcp);
        }
        return;
    }

    if (cmd == "RELEASE") {
        Keyboard.releaseAll();
        respond("OK:RELEASE", fromTcp);
        return;
    }

    if (cmd == "IP") {
        respond("IP:" + WiFi.localIP().toString(), fromTcp);
        return;
    }

    respond("ERR:UNKNOWN_CMD:" + cmd, fromTcp);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("IPKVM starting...");

    // USB HID Keyboard
    Keyboard.begin();
    USB.begin();
    Serial.println("USB HID ready");

    // WiFi
    WiFi.setHostname("ipkvm");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("WiFi connecting");
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
        delay(500);
        Serial.print(".");
        timeout++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.println("WiFi: " + WiFi.localIP().toString());

        // TCP server
        tcpServer.begin();
        Serial.println("TCP server on port 23");

        // OTA
        ArduinoOTA.setHostname("ipkvm");
        ArduinoOTA.setPassword(OTA_PASS);
        ArduinoOTA.onStart([]() { Serial.println("OTA start"); });
        ArduinoOTA.onEnd([]() { Serial.println("OTA done"); });
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            Serial.printf("OTA: %u%%\r", (progress / (total / 100)));
        });
        ArduinoOTA.onError([](ota_error_t error) { Serial.printf("OTA error: %u\n", error); });
        ArduinoOTA.begin();
        Serial.println("OTA ready");
    } else {
        Serial.println("\nWiFi FAILED - serial only mode");
    }

    Serial.println("IPKVM_READY");
}

String serialBuffer = "";
String tcpBuffer = "";

void loop() {
    ArduinoOTA.handle();

    // Accept new TCP clients
    if (tcpServer.hasClient()) {
        if (tcpClient && tcpClient.connected()) {
            tcpClient.stop(); // drop old connection
        }
        tcpClient = tcpServer.available();
        tcpClient.println("IPKVM_CONNECTED");
        Serial.println("TCP client connected: " + tcpClient.remoteIP().toString());
    }

    // Read from TCP
    if (tcpClient && tcpClient.connected()) {
        while (tcpClient.available()) {
            char c = tcpClient.read();
            if (c == '\n' || c == '\r') {
                if (tcpBuffer.length() > 0) {
                    handleCommand(tcpBuffer, true);
                    tcpBuffer = "";
                }
            } else {
                tcpBuffer += c;
            }
        }
    }

    // Read from Serial (fallback)
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (serialBuffer.length() > 0) {
                handleCommand(serialBuffer, false);
                serialBuffer = "";
            }
        } else {
            serialBuffer += c;
        }
    }
}
