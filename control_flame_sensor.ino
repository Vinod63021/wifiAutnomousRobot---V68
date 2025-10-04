#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Servo.h>

#define WIFI_SSID "STARK NETWORKS"
#define WIFI_PASSWORD "Vinod630211"

ESP8266WebServer server(80);
Servo headServo;

// Motor pins
const uint8_t IN1 = D1;
const uint8_t IN2 = D2;
const uint8_t IN3 = D3;
const uint8_t IN4 = D4;

// Ultrasonic
const uint8_t TRIG = D5;
const uint8_t ECHO = D6;

// Servo pin (scan head)
const uint8_t SERVO_PIN = D7;

// Flame sensor (digital output)
const uint8_t FLAME_PIN = D0;  // DO pin → D0

// Control vars
bool autonomous = false;
unsigned long lastAutoCheck = 0;
const unsigned long AUTO_INTERVAL = 200;

// Flame debounce/confirm
unsigned long flameLastTriggered = 0;
const unsigned long FLAME_CONFIRM_MS = 400; // must sustain for this long
bool flameConfirmed = false;
unsigned long flameConfirmStart = 0;

void setup() {
  Serial.begin(115200);

  // Motors
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  stopMotors();

  // Ultrasonic
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);

  // Flame
  pinMode(FLAME_PIN, INPUT);

  // Servo
  headServo.attach(SERVO_PIN);
  headServo.write(90);

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300); Serial.print(".");
  }
  Serial.println();
  Serial.print("IP: "); Serial.println(WiFi.localIP());

  // Routes
  server.on("/", handleRoot);
  server.on("/forward", [](){ forward(); server.send(200,"text/plain","OK"); });
  server.on("/backward", [](){ backward(); server.send(200,"text/plain","OK"); });
  server.on("/left", [](){ left(); server.send(200,"text/plain","OK"); });
  server.on("/right", [](){ right(); server.send(200,"text/plain","OK"); });
  server.on("/stop", [](){ stopMotors(); server.send(200,"text/plain","OK"); });
  server.on("/autoon", [](){ autonomous=true; server.send(200,"text/plain","AUTO ON"); });
  server.on("/autooff", [](){ autonomous=false; server.send(200,"text/plain","AUTO OFF"); });
  server.on("/telemetry", handleTelemetry);

  server.begin();
}

void loop() {
  server.handleClient();
  unsigned long now = millis();
  if (autonomous && (now - lastAutoCheck >= AUTO_INTERVAL)) {
    lastAutoCheck = now;
    autonomousStep();
  }
  checkFlameSensor();
}

// ---------------- Web UI ----------------
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'><title>Robot</title></head><body>";
  html += "<h2>Robot Control</h2>";
  html += "<button onclick=\"fetch('/forward')\">Forward</button> ";
  html += "<button onclick=\"fetch('/left')\">Left</button> ";
  html += "<button onclick=\"fetch('/stop')\">Stop</button> ";
  html += "<button onclick=\"fetch('/right')\">Right</button> ";
  html += "<button onclick=\"fetch('/backward')\">Backward</button><br><br>";
  html += "<button onclick=\"fetch('/autoon')\">Auto ON</button> ";
  html += "<button onclick=\"fetch('/autooff')\">Auto OFF</button><hr>";
  html += "<h3>Telemetry</h3><div id='tele' style='padding:10px;border:1px solid #ccc;'>loading...</div>";
  html += "<div id='firewarn' style='color:white;font-weight:bold;background:red;display:none;padding:10px;margin-top:10px;'>FIRE ALERT!</div>";
  html += "<script>setInterval(async()=>{let r=await fetch('/telemetry'); let j=await r.json(); document.getElementById('tele').innerText = JSON.stringify(j); if(j.fire && j.fire==1){ document.getElementById('firewarn').style.display='block'; if(!window.fired){ window.fired=true; alert('FIRE ALERT!'); speechSynthesis.speak(new SpeechSynthesisUtterance('Warning! Fire detected.')); } } else { document.getElementById('firewarn').style.display='none'; window.fired=false; }},800);</script>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleTelemetry() {
  int dist = readDistance();
  int fire = flameConfirmed ? 1 : 0;
  String json = "{\"distance\":";
  json += String(dist);
  json += ",\"autonomous\":" + String(autonomous ? "1":"0");
  json += ",\"fire\":" + String(fire);
  json += "}";
  server.send(200, "application/json", json);
}

// ---------------- Autonomous ----------------
void autonomousStep() {
  if (flameConfirmed) {
    stopMotors();
    autonomous = false;
    return;
  }

  int dist = readDistance();
  if (dist > 0 && dist <= 20) {
    stopMotors();
    delay(200);

    // Scan left
    headServo.write(150);
    delay(350);
    int leftDist = readDistance();

    // Scan right
    headServo.write(30);
    delay(350);
    int rightDist = readDistance();

    // Center
    headServo.write(90);
    delay(200);

    // Decide
    if (leftDist > rightDist) {
      left();
      delay(400);
    } else {
      right();
      delay(400);
    }
    stopMotors();
  } else {
    forward();
  }
}

// ---------------- Flame sensor ----------------
void checkFlameSensor() {
  int reading = digitalRead(FLAME_PIN);
  // Inverted logic: LOW = fire
  bool rawDetect = (reading == LOW);

  unsigned long now = millis();
  if (rawDetect) {
    if (flameConfirmStart == 0) {
      flameConfirmStart = now;
    } else {
      if (!flameConfirmed && now - flameConfirmStart >= FLAME_CONFIRM_MS) {
        flameConfirmed = true;
        flameLastTriggered = now;
        Serial.println("🔥 FLAME CONFIRMED!");
        stopMotors();
        autonomous = false;
      }
    }
  } else {
    flameConfirmStart = 0;
    if (flameConfirmed && now - flameLastTriggered > 10000) {
      flameConfirmed = false;
      Serial.println("Flame cleared (timeout).");
    }
  }
}

// ---------------- Ultrasonic ----------------
int readDistance() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(4);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  long duration = pulseIn(ECHO, HIGH, 25000);
  if (duration == 0) return -1;
  return duration / 29 / 2;
}

// ---------------- Motor control ----------------
void forward() {
  digitalWrite(IN1,HIGH); digitalWrite(IN2,LOW);
  digitalWrite(IN3,HIGH); digitalWrite(IN4,LOW);
}
void backward() {
  digitalWrite(IN1,LOW); digitalWrite(IN2,HIGH);
  digitalWrite(IN3,LOW); digitalWrite(IN4,HIGH);
}
void left() {
  digitalWrite(IN1,LOW); digitalWrite(IN2,HIGH);
  digitalWrite(IN3,HIGH); digitalWrite(IN4,LOW);
}
void right() {
  digitalWrite(IN1,HIGH); digitalWrite(IN2,LOW);
  digitalWrite(IN3,LOW); digitalWrite(IN4,HIGH);
}
void stopMotors() {
  digitalWrite(IN1,LOW); digitalWrite(IN2,LOW);
  digitalWrite(IN3,LOW); digitalWrite(IN4,LOW);
}
