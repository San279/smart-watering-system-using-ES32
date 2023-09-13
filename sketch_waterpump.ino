 #include <Adafruit_ADS1015.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <string>
#include <cstring>
#include <EEPROM.h>

#define RT0 5000   // Ω
#define B 3000      // K +-0.75 3977
#define R 10000  //R=4.7KΩ
#define T0 318.15 //temp from datasheet celsius to kevin

const int pumpOnePin = 16;  //waterpump pin for mosfet driver


const char* ssid = "San 2.4G"; 
const char* password = "0611244569";
const char* mqtt_server = "192.168.1.21";

//int16_t adc2; 
int16_t adc1;  //temperature sensor
bool increment = true;  //increasing decreasing flow of waterpump 
bool start_flag = true;  //start pump with max power 
char mode_selector = '0'; //turn on or off waterpump
uint8_t max_adjust;  //adjust max range of the spring

int last_button_state = 0; //to check state of button
int prev_time = 0;
int hold_time_prev = 0;
int prev_Recontime = 0;  
int pwm = 0;  //pwm variable
//float humidity = 0;
float temperature = 0;
Adafruit_ADS1115 ads(0x48); //ADC moldule

WiFiClient espClient;
PubSubClient client(espClient);





void setup() {
  Serial.begin(115200);
  connect_wifi();
  EEPROM.begin(3);
  pinMode(15, INPUT_PULLUP);
  ledcSetup(0, 1000, 8);  //channel for pwm
  ads.begin();
  ads.setGain(GAIN_TWO); 
  max_adjust = EEPROM.readByte(0);
  ledcAttachPin(pumpOnePin, 0);
}

void connect_wifi() {
  delay(10);
  //Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  Serial.println("WiFi connected, IP address");
  Serial.println(WiFi.localIP());
}


void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  if(String(topic) == "smart_farm/switch"){
       mode_selector = message[0];
       Serial.println(mode_selector);
  }
  else if(String(topic) == "smart_farm/max"){     
    String max_new;
    for (int i = 0; i < length; i++) {
      max_new += (char)message[i]; 
    }
    
    if(max_adjust != max_new.toInt()){ //check if the max ajustment has new range from mqtt or no. If new, save it in storage permenently. 
      max_adjust = max_new.toInt();
      EEPROM.writeByte(0, max_adjust);
      EEPROM.commit();   
    }
  }
       
}

void connect_mqtt() {
    if (client.connect("ESP32Client")) {   
      Serial.println("Client connected");
      client.subscribe("smart_farm/switch");  //subscribe to mqtt client
      client.subscribe("smart_farm/max");
    }
}

float get_temp(float temper = 0){
  adc1 = ads.readADC_SingleEnded(1);
  float volt = ((adc1 * 0.0625)/1000);  //gain two using less than 3 volt
  float vr = 3.3 - volt;
  float RT = volt/(vr/R);
  float ln = log(RT / RT0);
  temper = (1 / ((ln / B) + (1 / T0)));
  temper = temper - 273.15;
  return temper;
}

/*
float get_humid(float humid = 0){
  adc2 = ads.readADC_SingleEnded(2);
  Serial.println(adc2);
  //humid = abs((adc3 - 6400)/20.2);
  humid = abs((adc2 * 0.0625)/1000);
  return humid;
}
*/



void publish_stat_temp_power(float temper, float map_val) {
  /*char publish_humidity[11];
  dtostrf(humid, -4, 2, publish_humidity); //convert to string
  client.publish("smart_farm/humidity", publish_humidity); //publish
  */

  if(mode_selector == '1'){
      char *on_text = "On "; //send pump status to dashboard
      client.publish("smart_farm/pump_condition", on_text); 
  }
  else{
      char *off_text = "Off "; //send pump status to dashboard
      client.publish("smart_farm/pump_condition", off_text); 
  }

  char publish_pump_power[5];   
  dtostrf(map_val, -4, 2, publish_pump_power);  //-4 left bias nimumum range of char, 0 = round at 0 decimal place, no decimals
  //Serial.println(map_val);
  client.publish("smart_farm/pump_power", publish_pump_power);  //publish current pump power

  
  char publish_temperature[11];
  dtostrf(temper, -4, 2, publish_temperature);  //-4 left bias nimumum range of char, 0 = round at 0 decimal place, no decimals
  //Serial.println(publish_temperature);
  client.publish("smart_farm/temperature", publish_temperature); 
}

void edge_detection(int current_state, int prev_state){
  if (current_state != prev_state){
    if (current_state == HIGH)  //turn on pump when switch is high
       mode_selector = '1';
 
    else if(current_state == LOW)
      mode_selector = '0';
        
  }
}


void loop(){
  int current_time = millis();
  temperature = (temperature + get_temp())/2;
//  humidity = (humidity + get_humid())/2;
  if (current_time - prev_time > 700){ 
    prev_time = current_time;
    Serial.print("temperature is ");
    Serial.println(temperature);
    
    if (!client.connected() && current_time - prev_Recontime > 12000) { //every 10 secs tries to connect to mqtt.(Useful when mqtt-server is not avaible) 
        prev_Recontime = current_time;
        Serial.println(client.state());
        client.setServer(mqtt_server, 1883);
        client.setCallback(callback);
        connect_mqtt();
        Serial.println(client.state());
      }
     else{
     client.loop();
     float map_val = map(pwm, 0,256,1,100);
     publish_stat_temp_power(temperature,map_val);
     }
   
  }
   int current_button_state = digitalRead(15);
   delay(50);
   edge_detection(current_button_state, last_button_state);
   Serial.println(mode_selector);
   
   if(start_flag == false && mode_selector == '1'){   //turn on the mosfet->on water pump
      if(pwm < 30)  //start incrementing if pwm goes to this value
        increment = true;   
      else if(pwm > 256 - int(max_adjust)){
        increment = false;
    }
    
      Serial.print("max ajustment ");
      Serial.println(max_adjust);
      if(increment == true)
        pwm += 3;
      else if(increment == false)
        pwm -= 3;

  }
  
  if(start_flag == true && mode_selector == '1') {  //looop to speedup time when the water initially travel from pump to sprinkler.
      pwm = 256;
      //Serial.print("hold time");
     // Serial.println(current_time - hold_time_prev);
      if(current_time - hold_time_prev > 2000){  //change this value to adjust the time water pump hold max power.
        start_flag = false; 
      }
  }
 
   if(mode_selector == '0'){  //turn off the mosfet->turn off waterpump
      pwm = 0;
      start_flag = true;//flag to initally start the pump with max power. 
      hold_time_prev = current_time;
  }
  
   ledcWrite(0, pwm);   //give pwm sig to mosfet.
   Serial.print("pwm sig ");
   Serial.println(pwm);
   last_button_state = current_button_state;
} 
