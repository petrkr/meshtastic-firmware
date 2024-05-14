#include "configuration.h"

#if (HAS_WIFI == 0)

bool initWifi()
{
    return false;
}

void deinitWifi() {}

bool isWifiAvailable()
{
    return false;
}

#endif

#if (HAS_ETHERNET == 0)

bool initEthernet()
{
    return false;
}

bool isEthernetAvailable()
{
    return false;
}

#endif

#if (HAS_ESP_ETHERNET == 0)

bool initEspEthernet()
{
    return false;
}

bool isEspEthernetAvailable()
{
    return false;
}

#endif