#include <iostream>
#include "RF24.h"
#include "RF24Network.h"
#include "config.h"
#include "SensorMessage.h"
#ifndef WIN32
#include <curl/curl.h>
#endif // WIN32

#include <wiringPi.h>

#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include <vector>
#include <string.h>
#include <stdlib.h>
#include <sstream>
#include <ostream>
#include <ctime>

#include <fcntl.h>
#include <sys/mman.h>


using namespace std;
using namespace rapidjson;

class SensorLogValue
{
public:
    SensorLogValue(string sensor, double value, string rawValue):sensor(sensor),value(value),rawValue(rawValue) {};

    SensorLogValue(const Value &value)
    {
        this->sensor = string(value["sensor"].GetString());
        this->value = value["value"].GetDouble();
        this->rawValue = string(value["rasValue"].GetString());
    }

    ~SensorLogValue() {};
    string sensor;
    double value;
    string rawValue;


    template <typename Writer>
    void seralize(Writer &writer)
    {
        writer.StartObject();
        writer.String("sensor");
        writer.String(sensor.c_str());

        writer.String("value");
        writer.Double(value);

        writer.String("rawValue");
        writer.String(rawValue.c_str());
        writer.EndObject();
    }
};

class NodeLog
{
public:
    NodeLog(long nodeNumber, string logTimestamp):nodeNumber(nodeNumber),logTimestamp(logTimestamp)
    {

    }

    void addSensorValue(string sensor, double value, string rawValue)
    {
        SensorLogValue slv(sensor,value,rawValue);
        sensorLogValues.push_back(slv);
    }

    ~NodeLog() {};
    long nodeNumber;
    string logTimestamp;
    vector<SensorLogValue> sensorLogValues;



    template <typename Writer>
    void seralize(Writer &writer)
    {
        writer.StartObject();
        writer.String("nodeNumber");
        writer.Int64(nodeNumber);

        writer.String("logTimestamp");
        writer.String(logTimestamp.c_str());

        writer.String("sensorLogValues");
        writer.StartArray();
        for(size_t d = 0; d<sensorLogValues.size(); d++)
        {
            sensorLogValues[d].seralize(writer);

        }
        writer.EndArray();
        writer.EndObject();
    }

};

class DataLog
{
public:
    DataLog() {};
    vector<NodeLog> nodeLogValues;

    void addDataLog(NodeLog nodeLog)
    {
        nodeLogValues.push_back(nodeLog);
    }

    template <typename Writer>
    void seralize(Writer &writer)
    {
        writer.StartObject();
        writer.String("nodeLogs");
        writer.StartArray();
        for(size_t d = 0; d<nodeLogValues.size(); d++)
        {
            nodeLogValues[d].seralize(writer);

        }
        writer.EndArray();
        writer.EndObject();
    }

};

template <typename Encoding>
struct GenericStdStringStream
{
    typedef typename Encoding::Ch Ch;

    GenericStdStringStream() : src_(0), dst_(0), head_(0) {}

    // Read
    Ch Peek()
    {
        return data[src_];
    }
    Ch Take()
    {
        return data[src_++];
    }
    size_t Tell()
    {
        return src_ - head_;
    }

    // Write
    Ch* PutBegin()
    {
        return data[dst_];
    }
    void Put(Ch c)
    {
        data += c;
        dst_++;
    }
    size_t PutEnd(Ch* begin)
    {
        return dst_ - begin;
    }

    std::string data;
    int src_;
    int dst_;
    int head_;
};

typedef GenericStdStringStream<rapidjson::UTF8<> > StdStringStream;

void initLED()
{
  pinMode(1,OUTPUT);         // aka BCM_GPIO pin 17
  pinMode(2,OUTPUT);
  pinMode(3,OUTPUT);
}

void ledRed(bool on)
{
  digitalWrite(2,on);
}

void ledYellow(bool on)
{
  digitalWrite(3,on);
}

void ledGreen(bool on)
{
  digitalWrite(1,on);
}


int main()
{
    //cout << "Hello world!" << endl;


    curl_global_init(CURL_GLOBAL_ALL);

    wiringPiSetup();
    initLED();

    RF24 radio("/dev/spidev0.0",8000000 , 6);  //spi device, speed and CSN,only CSN is NEEDED in RPI
    RF24Network network(radio);

    radio.begin();
    network.begin(CHANNEL,0,DATA_RATE,LEVEL);
    radio.powerUp();
    //cout << "starting network watch " << endl;
    ledGreen(false);
    ledRed(false);
    ledYellow(false);
    while(true)
    {
        network.update();
        while(network.available())
        {
          ledGreen(true);
          ledRed(false);
            RF24NetworkHeader header;
            SensorMessage payload;
            network.read(header,&payload,sizeof(payload));

            cout << payload.location << "," <<
                 payload.temperature_reading << "," <<
                 payload.humidity_reading << "," <<
                 payload.voltage_reading << "," <<
                 payload.pressure_reading << "," <<
                 payload.light_reading << endl;
            time_t rawtime;
            struct tm * timeinfo;


            time (&rawtime);
            timeinfo = gmtime (&rawtime);
            char tsstr[100];
            strftime(tsstr,100,"%F %T",timeinfo);
            printf("Time %s\n",tsstr);

            DataLog dataLog;
            NodeLog nodeLog(payload.location,tsstr);
            char str[200];
            if(payload.temperature_reading < 0xffff)
            {
                sprintf(str,"%d",payload.temperature_reading);
                int16_t value = (int16_t) payload.temperature_reading;
                nodeLog.addSensorValue("T",value / 100.0,string(str));
            }
            if(payload.humidity_reading < 0xffff)
            {
                sprintf(str,"%d",payload.humidity_reading);
                nodeLog.addSensorValue("H",payload.humidity_reading / 10.0,string(str));
            }
            if(payload.voltage_reading < 0xffff)
            {
                sprintf(str,"%d",payload.voltage_reading);
                nodeLog.addSensorValue("V",payload.voltage_reading / 1000.0,string(str));
            }
            if(payload.pressure_reading < 0xffff)
            {
                sprintf(str,"%d",payload.pressure_reading);
                nodeLog.addSensorValue("B",payload.pressure_reading / 100.0,string(str));
            }
            if(payload.light_reading < 0xffff)
            {
                sprintf(str,"%d",payload.light_reading);
                nodeLog.addSensorValue("L",payload.light_reading,string(str));
            }

            dataLog.addDataLog(nodeLog);

            StdStringStream oss;
            rapidjson::PrettyWriter<StdStringStream> writer(oss);


            dataLog.seralize(writer);

            //string strsw = oss.data;
            //cout << strsw << endl;
            CURL *easyhandle = curl_easy_init();
            curl_easy_setopt(easyhandle, CURLOPT_URL, "http://somewebapp/posturl");

            struct curl_slist *headers=NULL;
            headers = curl_slist_append(headers, "Content-Type: text/json");

            /* post binary data */
            curl_easy_setopt(easyhandle, CURLOPT_POSTFIELDS, oss.data.c_str());

            /* set the size of the postfields data */
            curl_easy_setopt(easyhandle, CURLOPT_POSTFIELDSIZE, oss.data.length());

            /* pass our list of custom made headers */
            curl_easy_setopt(easyhandle, CURLOPT_HTTPHEADER, headers);

            CURLcode result = curl_easy_perform(easyhandle); /* post away! */
            ledGreen(false);
            if(result != CURLE_OK)
            {
              ledRed(true);
            }

            curl_slist_free_all(headers); /* free the header list */
            curl_easy_cleanup(easyhandle);
        }
        sleep(1);
    }
    return 0;
}

