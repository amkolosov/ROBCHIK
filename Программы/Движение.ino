#include <AccelStepper.h>//указываем библиотеку

AccelStepper mL(AccelStepper::DRIVER, 5, 2);//привязываем dir и step пины левму мотору
AccelStepper mR(AccelStepper::DRIVER, 6, 3);//привязываем dir и step пины правому мотору

String command = "";//назначаем переменную command

void aruco1() {delay(2000);}//создаём функции для арукомаркеров
void aruco2() {delay(2000);}
void aruco3() {delay(2000);}
void aruco4() {delay(2000);}
void aruco5() {delay(2000);}

bool a = false;// создаём переменную а

void setup() {
  Serial.begin(9600);//настраиваем скорость сериал порта
	Serial.setTimeout(10);//настраиваем таймаут сериал порта
  mL.setMaxSpeed(75);//устанавливаем макс. скорость для моторов
  mR.setMaxSpeed(64);
  mL.setAcceleration(75);//устанавливаем ускорение для моторов
  mR.setAcceleration(64);

  while (a == false) {//пока а = false выполняем:
     
    while (command != "6") {//пока command не станет 6, обновляем command

      if (Serial.available() > 0) {command = Serial.readStringUntil('\n');command.trim();}//обновляем command

    }
    delay(2000);//задержка для включения системы
     String command = "";//назначаем переменную command
    mL.move(-935);mR.move(-915);//прямо1  //кол-во шагов для моторов
    
      if (Serial.available() > 0) {command = Serial.readStringUntil('\n');command.trim();}//обновляем command


    while (mL.distanceToGo() && mR.distanceToGo()) {//пока моторы в движении обновляем command и распознаём команды

      if (Serial.available() > 0) {command = Serial.readStringUntil('\n');command.trim();}//обновляем command

           if (command == "1") {aruco1();command = "";}//если command = 1, то выполняем aruco1 и обновляем command
      else if (command == "2") {aruco2();command = "";}//если command = 2, то выполняем aruco2 и обновляем command
      else if (command == "9") {delay(1000);command = "";}//если command = 9, то делаем задержку 1 сек. и обновляем command
      else if (command == "7") {delay(9999999);mL.stop();mR.stop();}//если command = 7, то останавливаем моторы

        
        
      

      mL.run();mR.run();//моторы делают 1 шаг
      
    }
    mL.move(-340);mR.move(295); //поворот1  //кол-во шагов для моторов
     
    while (mL.distanceToGo() && mR.distanceToGo()) {//приводим моторы в движенние

      mL.run();mR.run();//моторы делают 1 шаг
      
    }
    mL.move(-900);mR.move(-880); //прямо2 //кол-во шагов для моторов
    
    while (mL.distanceToGo() && mR.distanceToGo()) {//пока моторы в движении обновляем command и распознаём команды

     if (Serial.available() > 0) {command = Serial.readStringUntil('\n');command.trim();}//обновляем command

           if (command == "3") {aruco3();command = "";}//если command = 3, то выполняем aruco3 и обновляем command
      else if (command == "9") {delay(1000);command = "";}//если command = 9, то делаем задержку 1 сек. и обновляем command
      else if (command == "7") {delay(9999999);mL.stop();mR.stop();}//если command = 7, то останавливаем моторы

      mL.run();mR.run();//моторы делают 1 шаг
     
    }
    mL.move(-285);mR.move(285);//поворот2 //кол-во шагов для моторов
      
    while (mL.distanceToGo() && mR.distanceToGo()) {//приводим моторы в движенние

      mL.run();mR.run();//моторы делают 1 шаг
      
    }
    mL.move(-870);mR.move(-850);  //прямо3 //кол-во шагов для моторов
    
    while (mL.distanceToGo() && mR.distanceToGo()) {//пока моторы в движении обновляем command и распознаём команды

     if (Serial.available() > 0) {command = Serial.readStringUntil('\n');command.trim();}//обновляем command

            if (command == "4") {aruco4();command = "";}//если command = 4, то выполняем aruco4 и обновляем command
       else if (command == "5") {aruco5();command = "";a = true;}//если command = 5, то выполняем aruco5, обновляем command и делаем а = true
       else if (command == "9") {delay(1000);command = "";}//если command = 9, то делаем задержку 1 сек. и обновляем command
       else if (command == "7") {delay(9999999);mL.stop();mR.stop();}//если command = 7, то останавливаем моторы
      mL.run();mR.run();//моторы делают 1 шаг
      
    }
  }
}
void loop() {}

