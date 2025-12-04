#include <Arduino.h>
#include <AccelStepper.h>


#define PIN_STEP_X 23
#define PIN_DIR_X  25
#define PIN_STEP_Y 22
#define PIN_DIR_Y  26
#define PIN_STEP_Z 32
#define PIN_DIR_Z 27
#define fimDeCursoX 14
#define fimDeCursoY 12
#define fimDeCursoZ 13

// Constantes Físicas e de Movimento
const float passos = 80.0;
const float raio = 2.95;       // Raio do círculo
const int segmentos = 50;       // Número de segmentos para a circunferência
const int VELOCIDADE_MAX = 600; // Passos/s (Para a execução do círculo)
const int VELOCIDADE_HOME = 600; // Passos/s (Velocidade de Homing)
const int ACEL_DEFAULT = 100;

// Estrutura para o ponto de destino (coordenadas em passos)
typedef struct {
  long x_passos;
  long y_passos;
} CoordenadaDestino_t;


AccelStepper motorX(AccelStepper::DRIVER, PIN_STEP_X, PIN_DIR_X);
AccelStepper motorY(AccelStepper::DRIVER, PIN_STEP_Y, PIN_DIR_Y);
AccelStepper motorZ(AccelStepper::DRIVER, PIN_STEP_Z, PIN_DIR_Z);

// Fila para comunicar o próximo ponto de destino
QueueHandle_t xQueueDestinoX;   
QueueHandle_t xQueueDestinoY;   

SemaphoreHandle_t xSemaphoreHoming; 
SemaphoreHandle_t xSemaphoreXDone;  
SemaphoreHandle_t xSemaphoreYDone;  
SemaphoreHandle_t xSemaphoreZDone; 


CoordenadaDestino_t circulo[segmentos];

void calcularCircunferencia() {
  Serial.println("Calculando circunferência");
  for (int i = 0; i < segmentos; i++) {
    float angulo = 2 * PI * i / segmentos;
    circulo[i].x_passos = lround(raio * cos(angulo) * passos);
    circulo[i].y_passos = lround(raio * sin(angulo) * passos);
  }
}


void TaskHoming(void *pvParameters) {
  (void) pvParameters;
  Serial.println("TaskHoming começou");
  
  bool faltaX = true;
  bool faltaY = true;
  bool faltaZ = true;
  
  motorX.setSpeed(VELOCIDADE_HOME); 
  motorY.setSpeed(VELOCIDADE_HOME);
  motorZ.setSpeed(VELOCIDADE_HOME);

  // Move os motores até as chaves fim de curso serem ativadas
  while (faltaX || faltaY || faltaZ) {
    if (faltaX && digitalRead(fimDeCursoX) == HIGH) {
      motorX.runSpeed();
    } else if (faltaX) {
      Serial.println("X tocou");
      faltaX = false;
      motorX.stop();
    }

    if (faltaY && digitalRead(fimDeCursoY) == HIGH) {
      motorY.runSpeed();
    } else if (faltaY) {
      Serial.println("Y tocou");
      faltaY = false;
      motorY.stop();
    }

    if (faltaZ && digitalRead(fimDeCursoZ) == HIGH) {
      motorZ.runSpeed();
    } else if (faltaZ) {
      Serial.println("Z tocou");
      faltaZ = false;
      motorZ.stop();
    }
  }

  // Define a posição atual como 0 e executa o backoff coordenado
  motorX.setCurrentPosition(0);
  motorY.setCurrentPosition(0);
  motorZ.setCurrentPosition(0);

  Serial.println("Backoff");
  int distancia = -350; 
  
  motorX.moveTo(distancia);
  motorY.moveTo(distancia);
  motorZ.moveTo(distancia);

  while(motorX.distanceToGo() != 0 || motorY.distanceToGo() != 0 || motorZ.distanceToGo() != 0) {
      motorX.run();
      motorY.run();
      motorZ.run();
      vTaskDelay(pdMS_TO_TICKS(1)); 
  }
  
  motorX.setCurrentPosition(0);
  motorY.setCurrentPosition(0);
  motorZ.setCurrentPosition(0);
  
  Serial.println("Homing concluído.");
  xSemaphoreGive(xSemaphoreHoming);

  vTaskDelete(NULL);
}

void TaskControleMovimento(void *pvParameters) {
  (void) pvParameters;
  
  xSemaphoreTake(xSemaphoreHoming, portMAX_DELAY);
  xSemaphoreGive(xSemaphoreHoming); // Devolve para X poder pegar
  
  Serial.println("Controle iniciando após Homing.");

  int pontoAtual = 0;
  long x_destino, y_destino;

  while (1) {
    //Obter o próximo ponto de destino
    CoordenadaDestino_t proximoDestino = circulo[pontoAtual];
    x_destino = proximoDestino.x_passos;
    y_destino = proximoDestino.y_passos;

    if (xQueueSend(xQueueDestinoX, &x_destino, portMAX_DELAY) != pdPASS) {
      Serial.println(" Falha ao enviar X para a fila.");
    }
    if (xQueueSend(xQueueDestinoY, &y_destino, portMAX_DELAY) != pdPASS) {
      Serial.println("Falha ao enviar Y para a fila.");
    }
    
    Serial.printf("Controle: Enviado ponto %d: (%ld, %ld)\n", pontoAtual, x_destino, y_destino);

    //ESPERAR a conclusão de Z, que sinaliza que o segmento completo (X, Y, e Z) terminou
    xSemaphoreTake(xSemaphoreZDone, portMAX_DELAY); 
    Serial.printf("Controle: Segmento %d concluído. Enviando o próximo.\n", pontoAtual);

    pontoAtual++;
    if (pontoAtual >= segmentos) {
      pontoAtual = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(10)); 
  }
}


void TaskExecucaoMotorX(void *pvParameters) {
  (void) pvParameters;
  long destino_x;
  
  // ESPERA O HOMING ANTES DE COMEÇAR
  xSemaphoreTake(xSemaphoreHoming, portMAX_DELAY);
  
  Serial.println("TaskExecucaoMotorX iniciada.");
  motorX.setMaxSpeed(VELOCIDADE_MAX);
  motorX.setAcceleration(VELOCIDADE_MAX / 2.0);

  while (1) {
    if (xQueueReceive(xQueueDestinoX, &destino_x, portMAX_DELAY) == pdPASS) {
      

      motorX.moveTo(destino_x);

      while (motorX.distanceToGo() != 0) {
        motorX.run();
        vTaskDelay(pdMS_TO_TICKS(1)); 
      }
      
      
      xSemaphoreGive(xSemaphoreXDone); 
    }
  }
}


void TaskExecucaoMotorY(void *pvParameters) {
  (void) pvParameters;
  long destino_y;
  
  Serial.println("TaskExecucaoMotorY iniciada.");

  // Configurações da AccelStepper
  motorY.setMaxSpeed(VELOCIDADE_MAX);
  motorY.setAcceleration(VELOCIDADE_MAX / 2.0);

  while (1) {
    
    xSemaphoreTake(xSemaphoreXDone, portMAX_DELAY);
    
    // Esperar por um novo ponto de destino Y na fila
    if (xQueueReceive(xQueueDestinoY, &destino_y, portMAX_DELAY) == pdPASS) {
      
       //Definir o novo destino para o motor Y
      motorY.moveTo(destino_y);

      while (motorY.distanceToGo() != 0) {
        motorY.run();
        vTaskDelay(pdMS_TO_TICKS(1)); 
      }
      xSemaphoreGive(xSemaphoreYDone);
    }
  }
}

void TaskExecucaoMotorZ(void *pvParameters) {
  (void) pvParameters;
  
  Serial.println("TaskExecucaoMotorZ iniciada.");
  
  motorZ.setMaxSpeed(VELOCIDADE_MAX);
  motorZ.setAcceleration(VELOCIDADE_MAX / 2.0);
  int distancia_z=200; // Distância fixa para o movimento de Z
  
  while(1) {
  
    xSemaphoreTake(xSemaphoreYDone, portMAX_DELAY);
    
    motorZ.moveTo(distancia_z);
    if(motorZ.distanceToGo() != 0) {
        for(int i=0; i<100; i++) motorZ.run();
        vTaskDelay(pdMS_TO_TICKS(1)); 
    }
 
       xSemaphoreGive(xSemaphoreZDone); 
  }
}


void setup() {
  Serial.begin(115200); 
  disableCore0WDT();
   motorX.setMaxSpeed(600);
  motorY.setMaxSpeed(600);
  motorZ.setMaxSpeed(600);
  
  // Configuração dos pinos de fim de curso
  pinMode(fimDeCursoX, INPUT_PULLUP);
  pinMode(fimDeCursoY, INPUT_PULLUP);
  pinMode(fimDeCursoZ, INPUT_PULLUP);

  motorX.setAcceleration(ACEL_DEFAULT);
  motorY.setAcceleration(ACEL_DEFAULT);
  motorZ.setAcceleration(ACEL_DEFAULT);
 
  calcularCircunferencia();


  xQueueDestinoX = xQueueCreate(1, sizeof(long)); 
  xQueueDestinoY = xQueueCreate(1, sizeof(long)); 
  xSemaphoreHoming = xSemaphoreCreateBinary();
  xSemaphoreXDone = xSemaphoreCreateBinary();
  xSemaphoreYDone = xSemaphoreCreateBinary();
  xSemaphoreZDone = xSemaphoreCreateBinary();
  
 
  xTaskCreatePinnedToCore(TaskHoming, "Homing", 2048, NULL, 3, NULL, 0); 
  
  xTaskCreatePinnedToCore(TaskExecucaoMotorX,"MotorX",3072, NULL,1,NULL, 0); 
  xTaskCreatePinnedToCore(TaskExecucaoMotorY,"MotorY",3072, NULL,1,NULL, 0); 
  xTaskCreatePinnedToCore(TaskExecucaoMotorZ,"MotorZ",3072, NULL,1,NULL, 0); 

  xTaskCreatePinnedToCore(TaskControleMovimento,"ControleMovimento",2048, NULL,2,NULL, 0);

}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1));
}