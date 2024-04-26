int range1 = 0;
int pos = 0;

void setup() {
  Serial.begin(115200);
}

void loop() {
  int sensor1 = analogRead(A0);
  int constrainedInput1 = constrain(sensor1, 350, 650);
  range1 = map(constrainedInput1, 350, 750, 0, 6);
  switch(range1) {
    case 0:
      Serial.println("Down");
      delay(1000);
      break;
    case 4:
      Serial.println("Up");
      delay(1000);
      break;
    default:
      Serial.println("0");
      break;
  }
}
