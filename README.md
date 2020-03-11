# HouseLights

Before building the project, be sure to set these variables to match your device & network

`static const char* const wifi_ssid = "YOUR_SSID";`
`static const char* const wifi_password = "YOUR_PASSWORD";`
`static const char* const latitude = "YOUR_LATITUDE";`
`static const char* const longitude = "YOUR_LONGITUDE";`

Remove the define for this one if you don't plan to use the API
Then set timezone_offset to whatever your local timezone is
If you use the API, let it equal to 1
```
#define USE_TIMEZONE_API  
static int timezone_offset = 1;`
```

Whether or not the relay used needs 0V to output power
`#define OUTPUT_REVERSED false`

Network settings
Last line is to set the API's port, you connect to it for example using the address
http://192.168.0.223:223
```
static const IPAddress dns(8, 8, 8, 8);
static const IPAddress localIP(192, 168, 0, 223);
static const IPAddress gateway(192, 168, 0, 1);
static const IPAddress subnet(255, 255, 255, 0);
static ESP8266WebServer apiServer(223);
```