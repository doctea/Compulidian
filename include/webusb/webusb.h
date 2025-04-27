#include "ParameterManager.h"

// USB WebUSB object
Adafruit_USBD_WebUSB usb_web;

// Landing Page: scheme (0: http, 1: https), url
// Page source can be found at https://github.com/hathach/tinyusb-webusb-page/tree/main/webusb-rgb
WEBUSB_URL_DEF(landingPage, 1 /*https*/, "doctea.co.uk/webusb");

LinkedList<String> *webusb_lines = new LinkedList<String>();

uint8_t usbweb_buffer[512];
uint8_t usbweb_buffer_index = 0;
void process_usb_web() {
    //Serial.println(F("process_usb_web()"));
    
    // read from the USB WebUSB interface, storing in usbweb_buffer. if a \n is found, process the command
    // and send a response back to the WebUSB client
    //Serial.printf("usb_web.available()=%u\n", usb_web.available());
    //size_t len = usb_web.read(usbweb_buffer, sizeof(usbweb_buffer));
    while (usb_web.available()) {
        char c = usb_web.read();
        if (c=='\n')
            continue;

        usbweb_buffer[usbweb_buffer_index++] = c;

        Serial.printf("usb_web.read()=%c (%ud or 0x%02x)\n", c, c, c);
        if (usbweb_buffer_index >= sizeof(usbweb_buffer)) {
            usbweb_buffer_index = 0; // reset buffer index if it overflows
        }
        //Serial.printf("usb_web.read()=%u\n", usbweb_buffer[usbweb_buffer_index-1]);
        //Serial.printf("usb_web_buffer[%u]=%c\n", usbweb_buffer_index-1, usbweb_buffer[usbweb_buffer_index-1]);

        if (c == '\n' || c == '\r') {
            usbweb_buffer[--usbweb_buffer_index] = '\0'; // null-terminate the string
            Serial.printf("Command received: '%s'\n", usbweb_buffer);
            usb_web.printf("Command received: '%s'\r\n", usbweb_buffer);

            // Process the command
            if (usbweb_buffer[0] == 's') {
                // list parameters
                Serial.println(F("Processing command 's'..."));
                Serial.flush();
                LinkedList<String> *lines = parameter_manager->add_all_save_lines(webusb_lines);
                for (int j = 0 ; j < lines->size() ; j++) {
                    //usb_web.write((const uint8_t *)lines->get(j).c_str(), lines->get(j).length()+1);
                    //usb_web.write((const uint8_t *)"\r\n", 2);
                    Serial.printf("got line % 3i to send: '%s'\n", j, lines->get(j).c_str());
                    Serial.flush();
                    //usb_web.printf("has a line to send\r\n");
                    //usb_web.flush();
                    usb_web.printf("%s\r\n",lines->get(j).c_str());
                    usb_web.flush();
                    //usb_web.printf("%3i length %2u\n", j, lines->get(j).length());
                    //usb_web.flush();
                }
                lines->clear();
            } else if (usbweb_buffer[0] == 'l') {
                // send a change command to a parameter
                Serial.printf("Processing command %s...\n", usbweb_buffer);
                usb_web.printf("Processing command %s...\r\n", usbweb_buffer);
                String t = String((char *)usbweb_buffer+2);
                int index = t.indexOf('=');
                String key = t.substring(0, index);
                String value = t.substring(index+1);
                bool success = parameter_manager->fast_load_parse_key_value(key, value);
                Serial.printf("Parsing '%s' => '%s' gave result %s\n", key.c_str(), value.c_str(), success ? "true" : "false"); 
                usb_web.printf("Parsing '%s' => '%s' gave result %s\r\n", key.c_str(), value.c_str(), success ? "true" : "false");
            } else {
                // unknown command
                Serial.printf("Unknown command: '%s'\n", usbweb_buffer);
                usb_web.write((const uint8_t *)"Unknown command\r\n", 17);
            }              

            usbweb_buffer_index = 0; // reset buffer index after processing command
            usb_web.flush(); // send the response to the client
        }
    }
    //Serial.println(F("done process_usb_web()"));
}