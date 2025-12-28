#include "BluetoothSerial.h"

//PIN GPIO15: SEÑAL DE ENCENDIDO (ON/OFF) "PIN TDO"

RTC_DATA_ATTR int bootNum = 0;            //VARIABLE PARA LLEVAR EL CONTEO DE LOS BOOT´S HECHOS
RTC_DATA_ATTR boolean despierto = false;  //VARIABLE PARA EVITAR QUE EL SISTEMA SE DUERMA DE MANERA INFINITA

#define uS_TO_S_FACTOR 1000000ULL
#define TIME_TO_SLEEP 5 // segundos

void print_wakeup_reason() {

  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("desperto por tiempo");
      Serial.println("luz encendida");
      int led_rojo = 25;
      pinMode(led_rojo, OUTPUT);

      digitalWrite(led_rojo, HIGH);
      delay(300);
      digitalWrite(led_rojo, LOW);

      if (bootNum >= 60000) {
        ESP.restart();
      }

      esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
      despierto = false;
      dormir();
      break;
      //default : Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;
  }
}

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

BluetoothSerial SerialBT;

int principal = 27;  //PIN GPIO PARA EL CONTROL DEL ENCENDIDO COMPLETO DE LA MOTOCICLETA
int sirena = 4;
int direccion = 32;  //PIN GPIO PARA LA ACTIVACION DE LAS DIRECCIONALES
int led_rojo = 25;   //PIN GPIO PARA EL CONTROL DE LA LLAVE ROJA DEL TABLERO
int lm2596 = 26;

TaskHandle_t comunicacion;

long long claveCifrada = 0LL;
long claveDinamica = 0;
int claveDinamicaDescifrada[5];
int clave[4];

int estadoActual = 0;

int desplazamiento = 0;

bool conexionValida = false;
bool procesoEncender = false;
bool procesoParpadeo = true;

int ArrayA[] = { 6, 2, 5, 1, 4, 9, 8, 7, 3 };  // Desplazar X+Num de la izquierda a la derecha
int ArrayB[] = { 2, 9, 8, 4, 6, 1, 5, 3, 7 };
int ArrayC[] = { 5, 3, 6, 7, 2, 9, 4, 8, 1 };

void callback_function(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  if (event == ESP_SPP_START_EVT) {
    Serial.println("Inicializado SPP");
  } else if (event == ESP_SPP_SRV_OPEN_EVT) {
    Serial.println("Cliente conectado");
    SerialBT.println(claveCifrada);
  } else if (event == ESP_SPP_CLOSE_EVT) {
    Serial.println("Cliente desconectado");
    procesoApagado(3);
  }
}

void setup() {
  Serial.begin(115200);

  print_wakeup_reason();
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

  Serial.println();
  Serial.println("............BIENVENIDO............");
  Serial.println("CONTROL DE ENCENDIDO KTM 390");
  dormir();

  pinMode(principal, OUTPUT);
  pinMode(direccion, OUTPUT);
  pinMode(led_rojo, OUTPUT);
  pinMode(lm2596, OUTPUT);
  pinMode(sirena, OUTPUT);
  pinMode(15, INPUT);  //SEÑAL DE ARRANQUE
  digitalWrite(principal, LOW);
  digitalWrite(direccion, LOW);
  digitalWrite(led_rojo, LOW);
  digitalWrite(lm2596, LOW);
  digitalWrite(sirena, LOW);

  SerialBT.begin("ESP32");  //NOMBRE COMO SE HARA VISIBLE EL DISPOSITIVO BLUETOOTH
  Serial.println("EL DISPOSTIVO SE INICIO, YA ES POSIBLE USAR EL BLUETOOTH");


  SerialBT.register_callback(callback_function);  // Registramos la función "callback_function"

  xTaskCreatePinnedToCore(
    loop_comunicacion,
    "comunicacion",
    10000,
    NULL,
    0,
    &comunicacion,
    0);

  generarClaveInicial();
}


unsigned long T = 0;
unsigned long T_Led = 0;
unsigned long T_Direccion = 0;
unsigned long T_Sirena = 0;

bool estadoled = false;
bool estadodireccion = true;
bool estadosirena = true;

int ciclosdireccion = 0;
int ciclossirena = 0;

void loop() {

  //if (analogRead(15) < 400) {
  if (digitalRead(15) == LOW) {
    procesoApagado(1);
  }

  if (procesoParpadeo == true){
    if (millis() >= T_Led + 150) {
      if (estadoled == false) {
        digitalWrite(led_rojo, HIGH);
        estadoled = true;
      } else {
        digitalWrite(led_rojo, LOW);
        estadoled = false;
      }
      T_Led = millis();
    }
  }

  if (procesoEncender == true) {
    if (millis() >= millis() + T) {
      Serial.println("Activando Sistema");
      T = millis();
      procesoParpadeo = false;
      digitalWrite(led_rojo, LOW);
      digitalWrite(lm2596, HIGH);
      digitalWrite(principal, HIGH);
      digitalWrite(sirena, HIGH);
      digitalWrite(direccion, HIGH);
    }

    if (ciclosdireccion <= 3) {
      if (millis() >= T + T_Direccion) {
        if (estadodireccion == true) {
          digitalWrite(direccion, LOW);
          estadodireccion = false;
          ciclosdireccion += 1;
        } else {
          digitalWrite(direccion, HIGH);
          estadodireccion = true;
        }
        T_Direccion += 300;
      }
    }

    if (ciclossirena <= 2) {
      if (millis() >= T + T_Sirena) {
        if (estadosirena == true) {
          digitalWrite(sirena, LOW);
          estadosirena = false;
          ciclossirena += 1;
        } else {
          digitalWrite(sirena, HIGH);
          estadosirena = true;
        }
        T_Sirena += 200;
      }
    }
    
    if (millis() >= T + 2000) {
      Serial.println("Encendiendo led");
      digitalWrite(led_rojo, HIGH);
      procesoEncender = false;
    }
  }
  delay(10);
}

void dormir() {
  if (despierto == false) {
    bootNum++;  //INCREMENTA CADA VEZ QUE SE DESPIERTA
    Serial.println("numero de boot: " + String(bootNum));
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_15, 1);  //DETERMINA EL PIN GPIO RTC QUE DESPERTARA EL SISTEMA, 1 = High, 0 = Low
    Serial.println("..........Modo Deep Sleep..........");
    Serial.flush();
    despierto = true;
    esp_deep_sleep_start();
    Serial.println("...............EL SISTEMA NO ENTRO EN MODO DEEP SLEEP...............");  //SI SE DUERME CORRECTAMENTE ESTA LINEA NO DEBERIA IMPRIMIRSE
  }
}

void loop_comunicacion(void *pvParameters) {
  unsigned long tiempoAnterior = 0;
  unsigned long frecuencia = 1000;
  int contador = 0;
  int contadorPaquetesPerdidos = 0;

  while (1) {

    String mensajeRecibido = "";

    while (Serial.available()) {
      char c = Serial.read();

      if (c == '\n') {  // Detecta el fin de mensaje
        Serial.print("Mensaje Recibido: ");
        Serial.println(mensajeRecibido);
        SerialBT.println(mensajeRecibido);

        if (estadoActual == 0 && mensajeRecibido == "200") {
          mensajeRecibido = "";
          estadoActual = 1;
        } else if (mensajeRecibido.length() == 5) {
          if (validarClaveDinamica(claveDinamica, mensajeRecibido, clave[1])) {
            contadorPaquetesPerdidos = 0;
          }
        }
      } else {
        mensajeRecibido += c;  // Acumula carácter
      }
    }

    while (SerialBT.available()) {
      char c = SerialBT.read();

      if (c == '\n') {  // Detecta el fin de mensaje
        //Serial.print("Mensaje Recibido: ");
        //Serial.println(mensajeRecibido);

        if (estadoActual == 0 && mensajeRecibido == "200") {
          mensajeRecibido = "";
          estadoActual = 1;
        } else if (mensajeRecibido == "301") {
          procesoApagado(2);
          SerialBT.println("301Y");
        } else if (mensajeRecibido == "302") {
          alarma();
        } else if (mensajeRecibido.length() == 5) {
          if (validarClaveDinamica(claveDinamica, mensajeRecibido, clave[1])) {
            Serial.println("Validación Correcta");
            if (conexionValida == false) {
              conexionValida = true;
              estadoActual = 2;
              procesoEncender = true;
            }
            contadorPaquetesPerdidos = 0;
          } else {
            Serial.println("Validación Fallida");
          }
        }
      } else {
        mensajeRecibido += c;  // Acumula carácter
      }
    }

    if (estadoActual == 1) {
      GenerarClaveDinamica();
      estadoActual = 0;
    } else if (estadoActual == 2) {
      if (millis() - tiempoAnterior >= frecuencia) {
        tiempoAnterior = millis();
        contador++;
      }
      if (contador >= 5) {
        Serial.println("\n\n\n\n");
        Serial.print("ContadorPerdidos: ");
        Serial.println(contadorPaquetesPerdidos);
        if(contadorPaquetesPerdidos >= 3) {
          procesoApagado(3);
        }
        GenerarClaveDinamica();
        contador = 0;
        contadorPaquetesPerdidos += 1;
      }
    }
    delay(20);
  }
}

bool generarClaveInicial() {

  int digitos = 0;
  long long copiaClaveCifrada = 0LL;
  int arrayclaveCifrada[15];

  while (digitos != 15) {

    digitos = 0;
    claveCifrada = 0;

    clave[0] = 0;

    for (int i = 0; i < 15; i++) {
      int digito = random(1, 10);  // Dígito aleatorio entre 1 y 9
      claveCifrada = claveCifrada * 10 + digito;
      if (digito != 0) {
        digitos++;
      }
    }
  }

  int i = 14;
  copiaClaveCifrada = claveCifrada;
  while (copiaClaveCifrada > 0) {
    arrayclaveCifrada[i] = copiaClaveCifrada % 10;
    copiaClaveCifrada /= 10;
    i--;
  }

  if (arrayclaveCifrada[1] != 0 && digitos == 15) {
    clave[0] = ((arrayclaveCifrada[0] * 10) + arrayclaveCifrada[1]);

    if (clave[0] <= 13) {
      clave[1] = arrayclaveCifrada[clave[0] - 1];
      clave[2] = arrayclaveCifrada[clave[0]];
      clave[3] = arrayclaveCifrada[clave[0] + 1];
      clave[0] = ((clave[1] * 100) + (clave[2] * 10) + (clave[3]));
    } else if (clave[0] >= 14) {
      clave[0] = arrayclaveCifrada[1];
      clave[1] = arrayclaveCifrada[clave[0] - 1];
      clave[2] = arrayclaveCifrada[clave[0]];
      clave[3] = arrayclaveCifrada[clave[0] + 1];
      clave[0] = ((clave[1] * 100) + (clave[2] * 10) + (clave[3]));
    }

    if (clave[0] >= 100) {
      Serial.print("Mensaje generado: ");
      Serial.println(claveCifrada);
      Serial.print("Clave: ");
      Serial.println(clave[0]);
      return true;
    } else {
      digitos = 0;
    }
  } else {
    digitos = 0;
  }
}

void GenerarClaveDinamica() {

  int digitos = 0;
  claveDinamica = 0;
  long copiaClaveDinamica = 0;
  int arrayclaveDinamica[5];

  while (digitos != 5) {

    digitos = 0;
    claveDinamica = 0;

    for (int i = 0; i < 5; i++) {
      int digito = random(1, 10);  // Dígito aleatorio entre 0 y 9
      claveDinamica = claveDinamica * 10 + digito;
      if (digito != 0) {
        digitos++;
      }
    }
  }
  SerialBT.println(claveDinamica);
}

bool validarClaveDinamica(long claveDinamica, String mensajeRecibido, int claveA) {
  //Serial.println("................Validacion Claves................");
  Serial.print("Clave Dinamica: ");
  Serial.println(claveDinamica);
  Serial.print("mensaje Recibido: ");
  Serial.println(mensajeRecibido);

  //claveDinamica = 79328;
  //claveA = 7;

  int i = 4;
  long copiaclaveDinamica = claveDinamica;
  while (copiaclaveDinamica > 0) {
    claveDinamicaDescifrada[i] = copiaclaveDinamica % 10;
    copiaclaveDinamica /= 10;
    i--;
  }

  // claveDinamica  = { 7, 9, 3, 2, 8 }
  // ArrayA[] = { 6, 2, 5, 1, 4, 9, 8, 7, 3 }; // Desplazar a la derecha la cantidad de veces del numero a la izquierda + la clave
  // claveDinamicaF = { 9, 2, 8, 5, 8 }

  int arrayMensajeOriginal[5];  // Para guardar el mensaje original

  for (int i = 0; i < 5; i++) {
    arrayMensajeOriginal[i] = claveDinamicaDescifrada[i];
  }

  for (int i = 0; i <= 4; i++) {
    for (int j = 0; j <= 8; j++) {
      if (arrayMensajeOriginal[i] == ArrayA[j]) {
        if (i == 0) {
          desplazamiento = j + claveA;
        } else {
          desplazamiento = j + arrayMensajeOriginal[i - 1] + claveA;
        }
        if (desplazamiento > 8) {
          desplazamiento -= 9;
        }
        if (desplazamiento > 8) {
          desplazamiento -= 9;
        }
        claveDinamicaDescifrada[i] = ArrayA[desplazamiento];

        break;
      }
    }
  }

  for (int i = 1; i < 5; i++) {
    claveDinamicaDescifrada[0] = claveDinamicaDescifrada[0] * 10 + claveDinamicaDescifrada[i];
  }
  Serial.print("Mensaje Descifrado: ");
  Serial.println(claveDinamicaDescifrada[0]);

  if (claveDinamicaDescifrada[0] == mensajeRecibido.toInt()) return true;
  else return false;
}

void procesoApagado(int tipo) {
  if (tipo == 1) {
    Serial.println("Motivo Apagado: 1 - Switch");
    digitalWrite(principal, LOW);
    int contadorApagado = 0;
    int T_LedApagdo = 0;
    int estadoledApagado = false;
    while (contadorApagado < 3) {
      if (millis() >= T_LedApagdo + 200) {
        if (estadoledApagado == false) {
          digitalWrite(led_rojo, HIGH);
          digitalWrite(direccion, HIGH);
          if (contadorApagado < 2)
            digitalWrite(sirena, HIGH);
          estadoledApagado = true;
        } else {
          digitalWrite(led_rojo, LOW);
          digitalWrite(direccion, LOW);
          digitalWrite(sirena, LOW);
          estadoledApagado = false;
          contadorApagado += 1;
        }
        T_LedApagdo = millis();
      }
    }
    digitalWrite(lm2596, LOW);
    delay(200);
    despierto = false;
    dormir();
  } else if (tipo == 2) {
    Serial.println("Motivo Apagado: 2 - Señal de apagado remoto");
    digitalWrite(principal, LOW);
    int contadorApagado = 0;
    int T_LedApagdo = 0;
    int estadoledApagado = false;
    while (contadorApagado < 2) {
      if (millis() >= T_LedApagdo + 200) {
        if (estadoledApagado == false) {
          digitalWrite(led_rojo, HIGH);
          digitalWrite(direccion, HIGH);
          if (contadorApagado < 1)
            digitalWrite(sirena, HIGH);
          estadoledApagado = true;
        } else {
          digitalWrite(led_rojo, LOW);
          digitalWrite(direccion, LOW);
          digitalWrite(sirena, LOW);
          estadoledApagado = false;
          contadorApagado += 1;
        }
        T_LedApagdo = millis();
      }
    }
  } else if (tipo == 3) {
    Serial.println("Motivo Apagado: 3 - Desconexión, Perdida de paquetes");
    alarma();
  }
}

bool procesoAlarma = false;

void alarma() {
  Serial.println("Iniciando Proceso de Alarma");
  if (procesoAlarma == false) {
    procesoAlarma = true;
    while (1) {
      delay(15000);
      digitalWrite(principal, LOW);
      for (int i = 0; i < 40; i++) {
        digitalWrite(led_rojo, HIGH);
        if (i == 20)
          digitalWrite(sirena, HIGH);
        delay(250);
        digitalWrite(led_rojo, LOW);
        delay(250);
      }
      digitalWrite(sirena, LOW);
      ESP.restart();
    }
  }
}