#include "mesh/eth/ethEspClient.h"
#if HAS_ESP_ETHERNET
#include "NodeDB.h"
#include "RTC.h"
#include "concurrency/Periodic.h"
#include "configuration.h"
#include "main.h"
#include "mesh/api/WiFiServerAPI.h"
#include <ETH.h>
#if !MESHTASTIC_EXCLUDE_WEBSERVER
#include "mesh/http/WebServer.h"
#endif
#if !MESHTASTIC_EXCLUDE_MQTT
#include "mqtt/MQTT.h"
#endif
#include "target_specific.h"
#include <ESPmDNS.h>

#if HAS_NETWORKING

#ifndef DISABLE_NTP
#include <NTPClient.h>

// NTP
WiFiUDP ntpUDPEth;
NTPClient timeEthClient(ntpUDPEth, config.network.ntp_server);
uint32_t ntp_eth_renew = 0;
#endif

WiFiUDP syslogEthClient;
Syslog syslogEth(syslogEthClient);

// Stores our hostname
char ourEthHost[16];

bool ethStartupComplete = 0;
bool ethConnected = 0;
uint8_t ethCounter = 0;

using namespace concurrency;

static Periodic *ethEvent;

void onEvent(arduino_event_id_t event)
{
    switch (event) {
    case ARDUINO_EVENT_ETH_START:
        Serial.println("ETH Started");
        uint8_t dmac[6];
        getMacAddr(dmac);
        snprintf(ourEthHost, sizeof(ourEthHost), "Meshtastic-%02x%02x", dmac[4], dmac[5]);
        ETH.setHostname(ourEthHost);
        break;
    case ARDUINO_EVENT_ETH_GOT_IP:
        LOG_INFO("Ethernet obtained IP address: %s\n", ETH.localIP().toString().c_str());
        ethConnected = true;
        break;
    case ARDUINO_EVENT_ETH_GOT_IP6:
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
        LOG_INFO("Obtained Local IP6 address: %s", WiFi.linkLocalIPv6().toString().c_str());
        LOG_INFO("Obtained GlobalIP6 address: %s", WiFi.globalIPv6().toString().c_str());
#else
        LOG_INFO("Obtained IP6 address: %s", WiFi.localIPv6().toString().c_str());
#endif
        break;
    case ARDUINO_EVENT_ETH_CONNECTED:
        LOG_INFO("Ethernet connected");
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
        LOG_INFO("Ethernet disconnected");
        ethConnected = false;
        break;
    case ARDUINO_EVENT_ETH_STOP:
        LOG_INFO("Ethernet stopped");
        ethConnected = false;
        break;
    default:
        break;
    }
}

static int32_t reconnectETH()
{
    if (config.network.eth_enabled) {
        if (!ethStartupComplete) {
            // Start web server
            LOG_INFO("Starting ESP32 Ethernet network services");
            // start mdns
            if (!MDNS.begin("Meshtastic")) {
                LOG_ERROR("Error setting up MDNS responder!");
            } else {
                LOG_INFO("mDNS responder started");
                LOG_INFO("mDNS Host: Meshtastic.local");
                MDNS.addService("http", "tcp", 80);
                MDNS.addService("https", "tcp", 443);
            }
#ifndef DISABLE_NTP
            LOG_INFO("Starting NTP time client");
            timeEthClient.begin();
            timeEthClient.setUpdateInterval(60 * 60); // Update once an hour
#endif

            if (config.network.rsyslog_server[0]) {
                LOG_INFO("Starting Syslog client");
                // Defaults
                int serverPort = 514;
                const char *serverAddr = config.network.rsyslog_server;
                String server = String(serverAddr);
                int delimIndex = server.indexOf(':');
                if (delimIndex > 0) {
                    String port = server.substring(delimIndex + 1, server.length());
                    server[delimIndex] = 0;
                    serverPort = port.toInt();
                    serverAddr = server.c_str();
                }
                syslogEth.server(serverAddr, serverPort);
                syslogEth.deviceHostname(getDeviceName());
                syslogEth.appName("Meshtastic");
                syslogEth.defaultPriority(LOGLEVEL_USER);
                syslogEth.enable();
            }
#if !MESHTASTIC_EXCLUDE_WEBSERVER
            // initWebServer();
#endif
            initApiServer();

            ethStartupComplete = true;
        }
#if !MESHTASTIC_EXCLUDE_MQTT
        // FIXME this is kinda yucky, instead we should just have an observable for 'wifireconnected'
        if (mqtt && !moduleConfig.mqtt.proxy_to_client_enabled && !mqtt->isConnectedDirectly()) {
            mqtt->reconnect();
        }
#endif
    }

#ifndef DISABLE_NTP
    if (isEspEthernetAvailable() && (ntp_eth_renew < millis())) {

        LOG_INFO("Updating NTP time from %s", config.network.ntp_server);
        if (timeEthClient.update()) {
            LOG_DEBUG("NTP Request Success - Setting RTCQualityNTP if needed");

            struct timeval tv;
            tv.tv_sec = timeEthClient.getEpochTime();
            tv.tv_usec = 0;

            perhapsSetRTC(RTCQualityNTP, &tv);

            ntp_eth_renew = millis() + 43200 * 1000; // success, refresh every 12 hours
        } else {
            LOG_ERROR("NTP Update failed");
            ntp_eth_renew = millis() + 300 * 1000; // failure, retry every 5 minutes
        }
    }
#endif

    return 5000; // every 5 seconds
}

// Startup Ethernet
bool initEspEthernet()
{
    if (config.network.eth_enabled) {
        // Old SDK 4.x this have in WiFi class even for Ethernet events
        WiFi.onEvent(onEvent);

        if (config.network.address_mode == meshtastic_Config_NetworkConfig_AddressMode_DHCP) {
            LOG_INFO("starting Ethernet DHCP");
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
            ETH.begin(ETH_TYPE, ETH_PHY_ADDR, ETH_CS, ETH_IRQ, ETH_RST, SPI);
#else
            ETH.begin(ETH_PHY_ADDR, ETH_PWR, ETH_MDC, ETH_MDIO, ETH_TYPE, ETH_CLKTYPE);
#endif
        } else if (config.network.address_mode == meshtastic_Config_NetworkConfig_AddressMode_STATIC) {
            // TODO: Support Static IP in ESP32
            LOG_WARN("Static ethernet not yet supported for ESP32");
            // config.network.ipv4_config.ip
            // config.network.ipv4_config.dns
            // config.network.ipv4_config.gateway
            // config.network.ipv4_config.subnet;
            return false;
        } else {
            LOG_INFO("Ethernet Disabled");
            return false;
        }

        while (!ethConnected) {
            delay(1000);
            LOG_DEBUG("Connecting to ethernet");
            ethCounter++;
            if (ethCounter > 10) {
                LOG_ERROR("Ethernet connection failed");
                return false;
            }
        }

        ethEvent = new Periodic("ethEspConnect", reconnectETH);

        return true;
    } else {
        LOG_INFO("Not using Ethernet");
        return false;
    }
}

bool isEspEthernetAvailable()
{
    if (!config.network.eth_enabled) {
        syslogEth.disable();
        return false;
    } else if (!ethConnected) {
        syslogEth.disable();
        return false;
    } else {
        return true;
    }
}

#endif
#endif