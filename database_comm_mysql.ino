#include <WiFi.h>
#include <MySQL_Connection.h>
#include <MySQL_Cursor.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// WiFi settings
const char* ssid = "OPPO A78";
const char* password = "0114001975";

// MySQL server settings
IPAddress server_addr(192, 168, 11);  // Change to your MySQL server IP
char user[] = "root";
char password_mysql[] = "Database_2";
char db[] = "sensor_data_db";

// NTP Client for timestamp
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0); // Adjust timezone offset if needed

// Initialize WiFi and MySQL clients
WiFiClient client;
MySQL_Connection conn((Client *)&client);

// Buffer for incoming data
String inputString = "";
bool stringComplete = false;

// Struct to hold sensor data
struct SensorData {
    float temperature;
    int humidity;
    float heat_index;
    int noise;
    float methane;    // MQ2 reading (interpreted as methane)
    float co2;        // MQ3 reading (interpreted as CO2)
    float no2;        // MQ135 reading (interpreted as NO2)
    float dust;
};

void setup() {
    // Start serial communications
    Serial.begin(115200);  // High speed for ESP32
    Serial2.begin(9600);   // Communication with Arduino (use Arduino's baud rate)

    // Connect to WiFi with timeout
    connectWiFi();

    // Initialize NTP
    timeClient.begin();
    timeClient.update();
    Serial.print("NTP Time: ");
    Serial.println(timeClient.getFormattedTime());

    // Connect to MySQL with timeout
    connectDatabase();
}

void loop() {
    // Update NTP client
    timeClient.update();

    // Monitor Wi-Fi and MySQL connection health
    checkConnections();

    // Read data from Arduino if available
    if (Serial2.available()) {
        char c = Serial2.read();
        if (c == '\n') {
            stringComplete = true;
        } else {
            inputString += c;
        }
    }

    // Process complete data string
    if (stringComplete) {
        SensorData data;
        if (parseSensorData(inputString, &data)) {
            String timestamp = getFormattedTime();
            char query[512];
            snprintf(query, sizeof(query),
                     "INSERT INTO sensor_data "
                     "(timestamp, temperature, humidity, heat_index, noise, CO2, methane, NO2, dust) "
                     "VALUES ('%s', %.2f, %d, %.2f, %d, %.2f, %.2f, %.2f, %.2f)",
                     timestamp.c_str(),
                     data.temperature,
                     data.humidity,
                     data.heat_index,
                     data.noise,
                     data.co2,
                     data.methane,
                     data.no2,
                     data.dust);

            if (executeQuery(query)) {
                Serial.println("Data logged successfully at " + timestamp);
                printSensorData(data);
            }
        } else {
            Serial.println("Error parsing sensor data");
        }
        inputString = "";
        stringComplete = false;
    }
}

void connectWiFi() {
    Serial.print("Connecting to Wi-Fi");
    WiFi.begin(ssid, password);
    int wifiTimeout = 20; // 10 seconds timeout
    while (WiFi.status() != WL_CONNECTED && wifiTimeout > 0) {
        delay(500);
        Serial.print(".");
        wifiTimeout--;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWi-Fi connected!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWi-Fi connection failed. Restarting...");
        ESP.restart();
    }
}

void connectDatabase() {
    Serial.print("Connecting to MySQL database");
    int dbTimeout = 10; // 50 seconds timeout
    while (!conn.connect(server_addr, 3306, user, password_mysql, db) && dbTimeout > 0) {
        Serial.println("\nDatabase connection failed. Retrying...");
        delay(5000);
        dbTimeout--;
    }

    if (conn.connected()) {
        Serial.println("Database connected!");
    } else {
        Serial.println("Database connection failed. Restarting...");
        ESP.restart();
    }
}

void checkConnections() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi disconnected. Reconnecting...");
        connectWiFi();
    }

    if (!conn.connected()) {
        Serial.println("Database disconnected. Reconnecting...");
        connectDatabase();
    }
}

bool parseSensorData(String data, SensorData* sensorData) {
    int index = 0;
    int lastIndex = 0;
    int valueCount = 0;

    while ((index = data.indexOf(',', lastIndex)) != -1) {
        String value = data.substring(lastIndex, index);
        switch (valueCount) {
            case 0: sensorData->humidity = value.toFloat(); break;
            case 1: sensorData->temperature = value.toFloat(); break;
            case 2: sensorData->heat_index = value.toFloat(); break;
            case 3: sensorData->noise = value.toInt(); break;
            case 4: sensorData->methane = value.toFloat(); break;
            case 5: sensorData->co2 = value.toFloat(); break;
            case 6: sensorData->no2 = value.toFloat(); break;
        }
        lastIndex = index + 1;
        valueCount++;
    }

    if (lastIndex < data.length()) {
        sensorData->dust = data.substring(lastIndex).toFloat();
        valueCount++;
    }

    return (valueCount == 8);
}

String getFormattedTime() {
    time_t epochTime = timeClient.getEpochTime();
    struct tm *ptm = gmtime((time_t *)&epochTime);
    char buffer[25];
    sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d",
            ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
            ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
    return String(buffer);
}

bool executeQuery(const char* query) {
    MySQL_Cursor *cur_mem = new MySQL_Cursor(&conn);
    bool success = false;
    int retryCount = 0;

    while (!success && retryCount < 3) {
        if (cur_mem->execute(query)) {
            success = true;
        } else {
            Serial.println("Query failed, retrying...");
            delay(1000);
            retryCount++;
        }
    }

    delete cur_mem;
    return success;
}

void printSensorData(const SensorData& data) {
    Serial.println("Sensor Readings:");
    Serial.println("Temperature: " + String(data.temperature) + "°C");
    Serial.println("Humidity: " + String(data.humidity) + "%");
    Serial.println("Heat Index: " + String(data.heat_index) + "°C");
    Serial.println("Noise Level: " + String(data.noise));
    Serial.println("Methane (MQ2): " + String(data.methane) + " ppm");
    Serial.println("CO2 (MQ3): " + String(data.co2) + " ppm");
    Serial.println("NO2 (MQ135): " + String(data.no2) + " ppm");
    Serial.println("Dust: " + String(data.dust) + " mg/m³");
    Serial.println("-------------------");
}
